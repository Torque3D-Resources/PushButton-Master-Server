/*
(c) Ben Garney <bgarney@pblabs.com> 2002-2003
(c) Mike Kuklinski <admin@kuattech.com> 2005
(c) Nathan Martin <nmartin@gmail.com> 2011

This file is part of the Pushbutton Master Server.

PMS is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

PMS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the PMS; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include "masterd.h"
#include "TorqueIO.h"
#include "SessionHandler.h"
#include "netSocket.h"
#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
using namespace std;

#if !defined(WIN32) && defined(__GNUC__)
	#include <signal.h>
	#include <sys/types.h>
	#include <unistd.h>
	#define stricmp strcasecmp
	#define strnicmp strncasecmp
#endif


ServerStore			*gm_pStore		= NULL;
MasterdTransport	*gm_pTransport	= NULL;
tDaemonConfig		*gm_pConfig		= NULL;
MasterdCore			*coreMan		= NULL;		// master daemon core manager

// local function prototypes
void sigproc(int sig);
void setpid(void);
void clearpid(void);


//-----------------------------------------------------------------------------
// main execution -- revised for MasterdCore
//-----------------------------------------------------------------------------
int main(int argc, char**argv)
{
	int sigs[] = { SIGHUP, SIGINT, SIGTERM };	// signal types array
	int i, count = sizeof(sigs) / sizeof(int);

	
	// install our signal handler
	for(i=0; i<count; i++)
		signal(sigs[i], sigproc);

	// if we ever spawn child processes then we don't care if they die
	signal(SIGCHLD, SIG_IGN);
	
	// TODO: process commandline arguments for options such as alternative
	// preferences file, etc..

	// spawn the master daemon core and then run its main thread
	coreMan = new MasterdCore();
	coreMan->RunThread();

	// we've returned from master daemon core, destroy it
	delete coreMan;
	coreMan = NULL;

	// done, let us die
	return 0;
}

void sigproc(int sig)
{
	switch (sig)
	{
		case SIGHUP:
		{
			// in the future we'll use this signal to reload our preferences
			debugPrintf(DPRINT_INFO, " - SIGHUP received and ignored.\n");
			break;
		}
		case SIGINT:
		{
			debugPrintf(DPRINT_INFO, " - SIGINT received, shutting down...\n");
			if(coreMan)
				coreMan->StopThread();
			break;
		}
		case SIGTERM:
		{
			debugPrintf(DPRINT_INFO, " - SIGTERM received, shutting down...\n");
			if(coreMan)
				coreMan->StopThread();
			break;
		}
	}
	
}


//-----------------------------------------------------------------------------
// utility functions
//-----------------------------------------------------------------------------

bool shouldDebugPrintf(const int level)
{
	if(gm_pConfig)
	{
		// yes, now filter for verbosity level
		if(level > (int)gm_pConfig->verbosity)
			return false; // message level too high, abort
	}

	return true;
}

void debugPrintf(const int level, const char *format, ...)
{
	va_list args;
	static bool skipTS = false;

	
	// is global configuration pointer set?
	if(gm_pConfig)
	{
		// yes, now filter for verbosity level
		if(level > (int)gm_pConfig->verbosity)
			return; // message level too high, abort

		if(gm_pConfig->timestamp && !skipTS)
		{
			struct tm * ts;
			time_t now;
			size_t len;
			
			time(&now);
			ts = localtime(&now);
			
			// [YYYY-MM-DD hh:mm:ss]
			printf("[%04d-%02d-%02d %02d:%02d:%02d] ",
				ts->tm_year + 1900, ts->tm_mon +1, ts->tm_mday,
				ts->tm_hour, ts->tm_min, ts->tm_sec);
			
			// cheap hack to avoid printing a timestamp between same-line messages
			if((len = strlen(format)) && (format[len -1] != '\n'))
				skipTS = true;
		} else
			skipTS = false;
	}

	// allow the print output to go through
	va_start(args, format);
	vprintf(format, args);
	va_end(args);

	// explicit flush, for stdout redirection cases
	fflush(stdout);
}

void debugPrintHexDump(const void *ptr, size_t size)
{
	const U8 *data = (const U8 *)ptr;
	size_t i, k;

	/*
	 * going for hex dump output to fit within 80 char width terminal:
	 * AAAA is 16bit address, hb is hex byte, and c is respective character.
	 *
	 * AAAA  hb hb hb hb hb hb hb hb hb hb hb hb hb hb hb hb  cccccccccccccccc
	 */
	
	printf("Hex dump of %zu bytes:\n", size);
	for(i=0; i<size;)
	{
		printf("%04zX  ", i);

		for(k=0; k<16; k++)
		{
			if((i+k)<size)
				printf("%02hhx ", data[k]);
			else
				printf("   ");
		}

		printf(" ");
		
		for(k=0; k<16; k++)
		{
			if(((i+k)<size) && (data[k] >= 0x20))
				printf("%c", data[k]);
			else
				printf(" ");
		}

		// increment by 16 bytes
		data += 16;
		i += 16;

		printf("\n");
	}
	
	printf("\n");
}

char* strtrim(char *str)
{
	// trim both left and right of all whitespace characters
	size_t len;

	
	// abort on NULL
	if(!str)
		return NULL;

	// get string length
	len = strlen(str);

	// abort on no length
	if(!len)
		return str;

	// trim back
	while(--len && (str[len] == ' ' || str[len] == '\t'))
	{
		str[len] = 0;
		len--;
	}

	// trim front
	while(*str && (*str == ' ' || *str == '\t' ))
		str++;

	return str;
}

char* strnextfield(char *str, size_t *clen)
{
	// find the next field from current one
	const char	*whitespaces = " \t";
	char		*p;
	size_t		len, num;


	// abort on NULL
	if(!str)
		return NULL;

	// get string length
	len = strlen(str);

	// abort on no length
	if(!len)
		return str;

	// find whitespaces
	p = strpbrk(str, whitespaces);
	if(!p)
		return NULL; // no trailing field detected

	// update previous field's length to caller
	if(clen)
		*clen = (size_t)p - (size_t)str;

	// find the field after whitespaces
	num = strspn(p, whitespaces);

	// set NULL at the end of current field
	*p = 0;

	// move pointer up and we're done
	p += num;

	return p;
}

void strprocfieldvalue(char *str)
{
	char *pOut = str;
	// process and clean the string of quotes,
	// leading and trailing whitespaces, etc...


	// abort on NULL
	if(!str)
		return;
	
	while(str && *str)
	{
		switch (*str)
		{
			case '"':
				break;
				
			default:
			{
				*pOut = *str;
				pOut++;
				break;	
			}
		}

		str++;
	}

	*pOut = 0;
}



//=============================================================================
// MasterdCore Class Implementation
//=============================================================================

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
MasterdCore::MasterdCore()
{
	m_RunThread = false;
	
	// initialize configuration entities array
	InitPrefs();
}


//-----------------------------------------------------------------------------
// Destructor
//-----------------------------------------------------------------------------
MasterdCore::~MasterdCore()
{
	
}


//-----------------------------------------------------------------------------
// Core Thread
//-----------------------------------------------------------------------------
void MasterdCore::RunThread(void)
{
	ServerAddress *addr;
	Packet *data;
	tPeerRecord *peerrec;
	char buffer[256];
	
	
	// print welcome message
	debugPrintf(DPRINT_INFO, " - Welcome to the Push Button Master Server 0.96\n");

	
	LoadPrefs();			// load preferences
	SetPid();				// create pid file
	m_RunThread = true;		// flag our thread for running mode

	// report name and region of this master server
	debugPrintf(DPRINT_INFO, " - Server Name: %s.\n", m_Prefs.name);
	debugPrintf(DPRINT_INFO, " - Server Region: %s.\n", m_Prefs.region);

	// setup networking
	debugPrintf(DPRINT_INFO, " - Initializing networking.\n");
	initNetworkLib();

	// create and bind to socket
	netAddress bindAddress;
	std::vector<netAddress> bindAddresses;
	for (int i=0; i<m_Prefs.address.size(); i++)
	{
		bindAddress.set(m_Prefs.address[i], false);
		if (bindAddress.getPort() == 0) bindAddress.setPort(m_Prefs.port);
		bindAddresses.push_back(bindAddress);
	}

	gm_pTransport = new MasterdTransport(bindAddresses);

	if(!gm_pTransport->GetStatus())
	{
		debugPrintf(DPRINT_ERROR, " - Bind failed, aborting!\n");
		goto ShutDown;
	}

	// ready the server database
	debugPrintf(DPRINT_INFO, " - Loading server database.\n");
	gm_pStore = new ServerStoreRAM();

	// setup session tracking
	debugPrintf(DPRINT_INFO, " - Initializing session handler.\n");
	gm_pFloodControl = new FloodControl();	// FloodControl is now also the session manager

	// report we're starting the core loop
	debugPrintf(DPRINT_INFO, " - Entering core loop.\n");

	if (m_Prefs.testingMode)
	{
		const U32 dummyIpv4Count = 255 * 128;
		const U32 dummyIpv6Count = 255 * 128;
		const U32 dummyServerCount = dummyIpv4Count + dummyIpv6Count;

		debugPrintf(DPRINT_INFO, " - Adding %i dummy servers.\n", dummyServerCount);

		ServerInfo info;
		ServerAddress serverAddr;

		const char *gameType = "TEST";
		const char *missionType = "NORMAL";
		info.gameType = new char[strlen(gameType)+1];
		info.missionType = new char[strlen(missionType)+1];

		strcpy(info.gameType, gameType);
		strcpy(info.missionType, missionType);

		info.testServer = true;

		// Add IPV4
		serverAddr.type = ServerAddress::IPAddress;
		serverAddr.port = 28022;
		serverAddr.address.ipv4.netNum[0] = 192;
		serverAddr.address.ipv4.netNum[1] = 168;
		serverAddr.address.ipv4.netNum[2] = 80;
		serverAddr.address.ipv4.netNum[3] = 0;

		for (int i=0; i<dummyIpv4Count; i++)
		{
			gm_pStore->UpdateServer(&serverAddr, &info);

			serverAddr.port += 1;
			if (serverAddr.port > 28022+255)
			{
				serverAddr.port = 28022;
				serverAddr.address.ipv4.netNum[3]++;
			}

			if (serverAddr.address.ipv4.netNum[3] == 255)
			{
				serverAddr.address.ipv4.netNum[3] = 0;
				serverAddr.address.ipv4.netNum[2]++;
			}
		}

		// Add IPV6
		serverAddr.type = ServerAddress::IPV6Address;
		serverAddr.port = 28022;
		memset(serverAddr.address.ipv6.netNum, '\0', sizeof(serverAddr.address.ipv6.netNum));
		serverAddr.address.ipv6.netNum[0] = 0x20;
		serverAddr.address.ipv6.netNum[1] = 0x1;
		serverAddr.address.ipv6.netNum[2] = 0x0D;
		serverAddr.address.ipv6.netNum[3] = 0xB8;
		serverAddr.address.ipv6.netNum[15] = 0x1;
		for (int i=0; i<dummyIpv6Count; i++)
		{
			gm_pStore->UpdateServer(&serverAddr, &info);

			serverAddr.port += 1;
			if (serverAddr.port > 28022+255)
			{
				serverAddr.port = 28022;
				serverAddr.address.ipv6.netNum[15]++;
			}

			if (serverAddr.address.ipv6.netNum[15] == 255)
			{
				serverAddr.address.ipv6.netNum[15] = 0;
				serverAddr.address.ipv6.netNum[14]++;
			}
		}
	}

	// socket message handling, loop until thread is stop flagged
	while(m_RunThread)
	{
		// expire old sessions
		gm_pFloodControl->DoProcessing();
		gm_pStore->DoProcessing();

		// check for messages, don't stop until there are none left, and block
		// for up to 10 milliseconds when no messages (same as millisleep()).
		while(gm_pTransport->poll(&data, &addr, 10))
		{
			// check on reputation of peer
			if(!gm_pFloodControl->CheckPeer(*addr, &peerrec, true))
			{
				// bad reputation, ignore peer
				debugPrintf(DPRINT_DEBUG, "Dropped packet from banned host %s\n", addr->toString(buffer));
				goto SkipPeerMsg;
			}
			
			// process received message
			ProcMessage(addr, data, peerrec);

SkipPeerMsg:
			// destroy temporary instances
			delete data;
			delete addr;
		}
	}

ShutDown:
	debugPrintf(DPRINT_INFO, " - Shutting down...\n");

	// shut it all down
	if(gm_pFloodControl)	delete gm_pFloodControl;
	if(gm_pStore)			delete gm_pStore;
	if(gm_pTransport)		delete gm_pTransport;

	// destroy pid file
	ClearPid();

	debugPrintf(DPRINT_INFO, " - Shutdown complete.\n");
}

void MasterdCore::StopThread(void)
{
	m_RunThread = false;
}


//-----------------------------------------------------------------------------
// Message Processing
//-----------------------------------------------------------------------------
void MasterdCore::ProcMessage(ServerAddress *addr, Packet *data, tPeerRecord *peerrec)
{
	tMessageSession	message;
	tPacketHeader	header;
	bool result;
	char buffer[256];

	
	// setup message structure, we use this instead of passing along all these
	// class and data pointers thats wasting space filling up the stack. And
	// this is a lot simpler in the long run if we need to pass along additional
	// information later on.

	message.addr	= addr;
	message.pack	= data;
	message.header	= &header;
	message.peerrec	= peerrec;
	message.session	= NULL;
	message.store	= gm_pStore;


	// get packet header
	data->readHeader(header);
	if(!data->getStatus())
	{
BadRepPeer:

		// report bad message header from peer
		if(shouldDebugPrintf(DPRINT_VERBOSE))
		{
			debugPrintf(DPRINT_VERBOSE, "Received bad packet from %s\n", addr->toString(buffer));
		}

		// increase bad reputation for peer
		gm_pFloodControl->RepPeer(peerrec, m_Prefs.floodBadMsgTicket);
		return;
	}

	// is global configuration pointer set?
	if(shouldDebugPrintf(DPRINT_VERBOSE))
	{
		debugPrintf(DPRINT_VERBOSE, "[%s]: ", addr->toString(buffer));
	}

	// handle the specific message type
	switch(header.type)
	{
		case MasterServerGameTypesRequest:
		{
			debugPrintf(DPRINT_VERBOSE, "Received MasterServerGameTypesRequest\n");
			result = handleTypesRequest(message);
			break;
		}

		case MasterServerListRequest:
		case MasterServerExtendedListRequest:
		{
			debugPrintf(DPRINT_VERBOSE, "Received MasterServerListRequest\n");
			result = handleListRequest(message);
			break;
		}

		case GameMasterInfoResponse:
		{
			debugPrintf(DPRINT_VERBOSE, "Received GameMasterInfoResponse\n");
			result = handleInfoResponse(message);
			break;
		}

		case GameHeartbeat:
		{
			debugPrintf(DPRINT_VERBOSE, "Received GameHeartbeat\n");
			result = handleHeartbeat(message);
			break;
		}

		case MasterServerInfoRequest:
		{
			debugPrintf(DPRINT_VERBOSE, "Received MasterServerInfoRequest\n");
			result = handleInfoRequest(message);
			break;
		}

		case MasterServerChallenge:
		{
			debugPrintf(DPRINT_VERBOSE, "Received MasterServerChallenge\n");
			result = handleChallengePacket(message);
			break;
		}

		default:
		{
			debugPrintf(DPRINT_VERBOSE, "Unknown Packet Type %hhu\n", header.type);
			result = false;
		}
	}

	// check on result of handler call, if false then the packet was malformed
	if(!result)
		goto BadRepPeer;
	
}


//-----------------------------------------------------------------------------
// Initialize Preferences Handler
//-----------------------------------------------------------------------------
#define CONFIG_LINE_VARSTART	'$'
#define CONFIG_LINE_COMMENT		"# "
#define CONFIG_LINE_COMMENTCHAR	'#'

void MasterdCore::InitPrefs(void)
{
	static tConfigEntity entities[] =
	{
		{
			CONFIG_SECTION,		NULL,	NULL,
			"General Server Settings"
		},
		{	CONFIG_TYPE_STR,	&m_Prefs.name,		"name",
			"Name of the Master Server. Default: \"PBMS\""
		},
		{	CONFIG_TYPE_STR,	&m_Prefs.region,	"region",
			"Region the Master Server is in. Default: \"Earth\""
		},
		{	CONFIG_TYPE_STR_VECTOR,	&m_Prefs.address,	"address",
			"IPv4 address that the Daemon listens and sends on. Default: \"0.0.0.0\" for All\n"
			"NOTE: IPv6 currently not supported."
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.port,		"port",
			"Port number that the Daemon listens and sends on. Default: 28002"
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.heartbeat,	"heartbeat",
			"How long since the last heartbeat from a server before it is deleted.\n"
			"Default: 180 (3min)"
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.verbosity,	"verbosity",
			"Verbosity of log output. Default: 4\n"
			"   0 - No Messages (except initial startup messages)\n"
			"   1 - Error Messages\n"
			"   2 - Warning Messages\n"
			"   3 - Informative Messages\n"
			"   4 - Verbose [miscellaneous] Messages\n"
			"   5 - Debug Messages (warning: message flood) \n\n"
			" The chosen message output level will include all those above it.\n"
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.timestamp,	"timestamp",
			"Enable prefixing timestamps to messages. Set to 0 (zero) to not timestamp.\n"
			"Default: 1 (Enable)"
		},

		{	CONFIG_SECTION,		NULL,	NULL,
			"Flood Control Settings\n\n"
			"Flood control uses a ticket based approach to deal with remote hosts\n"
			"that are either game clients continously querying us or malicious parties\n"
			"attempting cause an Denial of Service attack by keeping the master busy.\n\n"
			
			"A remote host will be ticketed for every packet of data it sends to this\n"
			"master server, so keep that in mind when setting the maximum of tickets.\n"
			"There's even a setting to incor a severe penalty for sending bad/unknown\n"
			"packets to us of which helps ban the offending remote host a lot sooner.\n\n"
			
			"Once maximum tickets has reached for a remote host they are then temporarily\n"
			"banned. When a remote host is banned they are ignored until the ban time has\n"
			"expired. A remote host is forgotten about after not receiving a packet from\n"
			"them after a time of seconds ($flood::ForgetTime), except when they are\n"
			"currently banned. Once unbanned then the forget time is reset so they are\n"
			"not forgotten too soon."
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.floodMaxTickets,	"flood::MaxTickets",
			"Maximum number of tickets before a remote host is banned.\n"
			"Default: 300"
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.floodResetTime,	"flood::TicketsResetTime",
			"Time in seconds until the tickets placed on a remote host is reset.\n"
			"Default: 60  (1 minute)"
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.floodBanTime,		"flood::BanTime",
			"Time in seconds for how long a remote host is banned once reaching max tickets.\n"
			"Default: 300 (5 minutes)"
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.floodForgetTime,	"flood::ForgetTime",
			"Time in seconds after last hearing from a remote before it's forgotten about.\n"
			"Default: 900 (15 minutes)"
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.floodBadMsgTicket,	"flood::TicksOnBadMessage",
			"Number of tickets placed against remote host for sending bad or\n"
			"unknown formatted packets.\n"
			"Default: 50"
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.testingMode,		"testingMode",
			"Enable testing mode\n"
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.challengeMode,		"challengeMode",
			"Enable challenges for user sessions\n"
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.maxServersInResponse,		"maxServersInResponse",
			"Set max servers in response (will be additionally restricted by packet limit)\n"
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.maxPacketsInResponse,		"maxServersInResponse",
			"Set max packets in response\n"
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.maxSessionsPerPeer,		"maxSessionsPerPeer",
			"Set max sessions per peer\n"
		},
		{	CONFIG_TYPE_U32,	&m_Prefs.sessionTimeoutSeconds,		"sessionTimeoutSeconds",
			"Set seconds required for a session to expire\n"
		},

		{ CONFIG_TYPE_NOTSET, NULL, NULL } // End of entities
	};
	
	// set instance entities location
	m_ConfigEntities = entities;

	// clean preferences structure
	m_Prefs.reset();
	
	// set preference variables' default values
	strcpy(m_Prefs.file,	"./masterd.prf");	// set preferences file path and name
	strcpy(m_Prefs.pidfile,	"./masterd.pid");	// set process id file path and name
	strcpy(m_Prefs.name,	"PBMS");			// set name
	strcpy(m_Prefs.region,	"Earth");			// set region
	m_Prefs.address.push_back(strdup("0.0.0.0"));
	m_Prefs.port				= 28002;		// set bind UDP port to standard
	m_Prefs.heartbeat			= 180;			// set heartbeat to 3 minutes
	m_Prefs.verbosity			= 4;			// set verbosity to All Messages
	m_Prefs.timestamp			= 1;			// enable timestamped messages
	m_Prefs.floodResetTime		= 60;			// reset peer ticket count every 60 seconds
	m_Prefs.floodForgetTime		= 900;			// forget/delete peer record after 15 minutes
	m_Prefs.floodBanTime		= 600;			// peer is banned for 10 minutes once reaching max tickets
	m_Prefs.floodMaxTickets		= 300;			// ban peer once reaching 300 tickets
	m_Prefs.floodBadMsgTicket	= 50;			// 50 tickets on peer per bad message sent from the peer

	// set the global daemon configuration pointer to ours
	gm_pConfig = &m_Prefs;
	
	// done
}


//-----------------------------------------------------------------------------
// Load Preferences File
//-----------------------------------------------------------------------------
void MasterdCore::LoadPrefs(void)
{
	tConfigEntity	*pfe;
	ifstream		fin;
	char			line[1024], *pLine, *pValue;
	size_t			len;
	string			str;
	bool			isFound;


	// report our intentions
	debugPrintf(DPRINT_INFO, " - Checking for preferences file.\n");

	// try opening the preferences file
	fin.open(m_Prefs.file);
	if(!fin.good())
	{
		// see why we failed
		if(errno == ENOENT)
		{
			// it doesn't exist, so create the preferences file
			CreatePrefs();
		} else
		{
			// most likely permission error or other unforseen problem,
			// report the problem and then abort out.
			debugPrintf(DPRINT_ERROR, " - Failed opening preference file: %s  (Reason: [%d] %s)\n", m_Prefs.file, errno, strerror(errno));
			debugPrintf(DPRINT_WARN,  " - Falling back to using compiled in defaults.\n");
		}

		// now abort out
		return;
	}

	// success on opening preferences file, now load it
	debugPrintf(DPRINT_INFO, " - Loading preference file.\n");
	m_Prefs.address.clear();

	// process the preferences file
	while(fin.good())
	{
		// read in a line and get its length
		fin.getline(line, sizeof(line));
		len = strlen(line);

		// reset failbit of line length max reached
		if(len == (sizeof(line) -1))
			fin.clear();

		// trim the line of white spaces
		pLine = strtrim(line);

		// check for config variable start char
		if(pLine[0] != CONFIG_LINE_VARSTART)
			continue; // nope, skip the line

		// get the trailing config variable value
		pValue = strnextfield(pLine, NULL);

		// clean the value
		strprocfieldvalue(pValue);

//		debugPrintf(DPRINT_DEBUG, " Prefs Parser: Processing pair \"%s\" -> \"%s\"\n", pLine, pValue);

		// locate matching variable name to registered config entity
		pfe     = m_ConfigEntities;
		isFound = false;
		
		while(pfe && pfe->type != CONFIG_TYPE_NOTSET)
		{
			if(pfe->type == CONFIG_SECTION)
				goto SkipToNext;
			
			// check for name comparision for a match
			if(stricmp(pLine +1, pfe->pName) != 0)
			{
SkipToNext:
				// nope, next
				pfe++;
				continue;
			}
			
			isFound = true;
			
			// store variable value
			switch (pfe->type)
			{
				case CONFIG_TYPE_STR:
				{
					// ensure string to be stored isn't larger than our storage
					len = strlen(pValue);
					if(len > 255)
					{
						len = 255;
						debugPrintf(DPRINT_WARN, " - Warning: config variable %s value has been truncated for being too large.\n",
								pLine);
					}

					// store string
					memcpy(pfe->pData, pValue, len);
					((char *)pfe->pData)[len] = 0;
					
					break;
				}
				case CONFIG_TYPE_STR_VECTOR:
				{
					// ensure string to be stored isn't larger than our storage
					len = strlen(pValue);
					if(len > 255)
					{
						len = 255;
						debugPrintf(DPRINT_WARN, " - Warning: config variable %s value has been truncated for being too large.\n",
								pLine);
					}

					// store string
					std::vector<char*> *vec = (std::vector<char*>*)pfe->pData;
					char buf[256];
					memcpy(buf, pValue, len);
					buf[len] = 0;
					vec->push_back(strdup(buf));
					
					break;
				}
				case CONFIG_TYPE_S32: *((S32 *)pfe->pData) = strtol( pValue, NULL, 10); break;
				case CONFIG_TYPE_U32: *((U32 *)pfe->pData) = strtoul(pValue, NULL, 10); break;

				default:
					break;
			}
			
			// done with this setting
			break;
		}

		// report about unknown variables
		if(!isFound)
		{
			debugPrintf(DPRINT_WARN, " - Warning: config variable %s is unknown to me.\n",
					pLine);
		}
	}


	// done loading preferences, now enforce range limits on settings

	if(m_Prefs.port > 65535)		// port must stay in valid IP port range
		m_Prefs.port = 28002;
	if(m_Prefs.heartbeat > 3600)	// hearbeat timeout should stay under an hour
		m_Prefs.heartbeat = 3600;
	if(m_Prefs.verbosity >= DPRINT__COUNT) // we only have so many verbosity levels
		m_Prefs.verbosity = DPRINT__COUNT -1;
	if (m_Prefs.maxSessionsPerPeer > SESSION_ABSOLUTE_MAX)
		m_Prefs.maxSessionsPerPeer = SESSION_ABSOLUTE_MAX;

	// report success
	debugPrintf(DPRINT_INFO, " - Preference file loaded.\n");

	// done loading
}


//-----------------------------------------------------------------------------
// Create Preferences File
//-----------------------------------------------------------------------------
void MasterdCore::CreatePrefs(void)
{
	tConfigEntity	*pfe = m_ConfigEntities;
	ofstream		fout;
	const char		*str, *str2, *strSection;
	size_t			len;

	strSection = "-----------------------------------------------------------------------------";

	// report our attempt
	debugPrintf(DPRINT_INFO, " - Attempting to write new preference file with defaults.\n");

	// open output stream to create the preferences file
	fout.open(m_Prefs.file);
	if(!fout.good()) {
		debugPrintf(DPRINT_ERROR, " - Failed creating new preference file: %s  (Reason: [%d] %s)\n", m_Prefs.file, errno, strerror(errno));
		debugPrintf(DPRINT_WARN,  " - Falling back to using compiled in defaults.\n");
		return; // abort
	}

	// write preferences file header
	fout << "#=============================================================================" << endl;
	fout << "# Pushbutton Labs' Torque Master Server Preferences/Configuration File" << endl;
	fout << "#=============================================================================" << endl;
	fout << endl;

	// iterate through registered configuration entities
	while(pfe && pfe->type != CONFIG_TYPE_NOTSET)
	{
		/*
			Output format:
			# <DescriptionLines[]>\n
			$<Name> <Default>\n\n
		*/

		if(pfe->type == CONFIG_SECTION)
			fout << endl << CONFIG_LINE_COMMENTCHAR << strSection << endl;

		// write description line(s)
		str = pfe->pDesc;
		while(true)
		{
			// search for newline character
			str2 = strchr(str, '\n');
			if(!str2)
			{
				// not found, print remaining string and stop
				fout << CONFIG_LINE_COMMENT << str << endl;
				break;
			}
			
			// figure out string length before newline
			len = (size_t)str2 - (size_t)str;
			
			// write out string just before newline
			fout << CONFIG_LINE_COMMENT;
			fout.write(str, len);
			fout << endl;
			
			// move on to next line
			str = str2;
			str++;
		}

		if(pfe->type == CONFIG_SECTION)
		{
			fout << CONFIG_LINE_COMMENTCHAR << strSection;
		}
		else if (pfe->type == CONFIG_TYPE_STR_VECTOR)
		{
			std::vector<char*> &list  = *((std::vector<char*> *)pfe->pData);
			for (int i=0; i<list.size(); i++)
			{
				fout << CONFIG_LINE_VARSTART << pfe->pName << " ";
				fout << "\"" << list[i] << "\"";
				fout << endl;
			}
		}
		else
		{
			// write variable name
			fout << CONFIG_LINE_VARSTART << pfe->pName << " ";

			// write default value
			switch (pfe->type)
			{
				case CONFIG_TYPE_STR: fout << "\"" << ((char *)pfe->pData) << "\""; break;
				case CONFIG_TYPE_S32: fout << *((S32 *)pfe->pData); break;
				case CONFIG_TYPE_U32: fout << *((U32 *)pfe->pData); break;

				default:
					break;
			}
		}

		// write additional newlines
		fout << endl << endl;

		// move on to next config entity
		pfe++;
	}

	// report success and close out the opened stream
	debugPrintf(DPRINT_INFO, " - Wrote new preference file successfully: %s\n", m_Prefs.file);
	fout.close();
}


//-----------------------------------------------------------------------------
// Create Process Identifier File
//-----------------------------------------------------------------------------
void MasterdCore::SetPid(void)
{
	ofstream	fout;
	pid_t		pid;


	// get our process identifier
	pid = getpid();

	// open output stream to create the pid file
	fout.open(m_Prefs.pidfile);
	if(!fout.good()) {
		debugPrintf(DPRINT_ERROR, " - Failed writing pid file: %s  (Reason: [%d] %s)\n", m_Prefs.pidfile, errno, strerror(errno));
		return;
	}

	// write out the pid
	fout << pid << endl;

	// report success and close out the output stream
	debugPrintf(DPRINT_INFO, " - Created pid file [PID %ld] %s\n", pid, m_Prefs.pidfile);
	fout.close();
}


//-----------------------------------------------------------------------------
// Destroy Process Identifier File
//-----------------------------------------------------------------------------
void MasterdCore::ClearPid(void)
{
	// just attempt to delete the pid file, we don't care if it fails or not
	remove(m_Prefs.pidfile);
}

