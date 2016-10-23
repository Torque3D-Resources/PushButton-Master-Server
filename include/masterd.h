/*
	(c) Ben Garney <bgarney@pblabs.com> 2002-2003
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
#ifndef _MASTERD_H_
#define _MASTERD_H_

#include "commonTypes.h"

class ServerStore;

// Some configuration
#define HAVE_SQLITE

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "network.h"
#include "ServerStore.h"
#ifdef SERVERSTORERAM
	#include "ServerStoreRAM.h"
#endif
#include "SessionHandler.h"


/**
 * @mainpage Pushbutton Master Server
 * @section b Some Architectural Notes

At this point in time, I don't have access to a cross-platform threading library.

So instead I'll go for a task-scheduler approach. This should give a good level of
performance, provided that we can send UDP packets out fast enough. When/if threading
becomes desirable, then the architecture can simply split out multiple task queues,
one for each thread/subprocess.

SQLite will be used to keep track of the known hosts. This necessitates a certain
amount of parsing of the addresses but overall is quite effective. If address-parsing
becomes a significant bottleneck, then we can have lookups to a pre-stored array of
addresses (reducing 5 parses to one).

@section c General Notes
@verbatim
In general, the master server behaves thusly:
	When a server sends us a heartbeat, we add that ip/port to the database, if not present
	We request info from all the servers in our list,
		not hitting a server more than once every 5 minutes
		wrapping through all servers

When we receive a query from a client,
	we first add their query to a list (tracking session and query parameters)
	we perform the necessary SQL to populate a results buffer
	then we cycle through the results buffer, sending the packets sequentially
	after N seconds of hearing nothing, blow away the list entry

We can optimize this by caching query results and ref counting them.
We can optimize the get-all special case by keeping a global list.
We can optimize the database queries by putting them in a different thread

Given a system to do WON-style authentication, on a UID,
	We can start indexing server entries on UID
		(prevent DoS/spoofing by keeping track of UID/address mappings)
		This way, every server requires a seperate license
		Requires adding a UID to the heartbeat packet
	We can do the same for master-server access from clients
		Requires adding UID to the info request packet(s)

Note: On the meaning of server filters.
	if the buddycount > 0:
		then it's a buddy search, we're looking for said buddies
@endverbatim
*****/

// The globals
extern ServerStore		*gm_pStore;
extern MasterdTransport *gm_pTransport;

enum
{
	DPRINT_NONE = 0,
	DPRINT_ERROR,			// Level 1
	DPRINT_WARN,			// Level 2
	DPRINT_INFO,			// Level 3
	DPRINT_VERBOSE,			// Level 4
	DPRINT_DEBUG,			// Level 5

	DPRINT__COUNT			// number of valid print levels, not a valid option
};

struct tDaemonConfig
{
	char	file[256];			// path and name of preferences file
	char	pidfile[256];		// path and name of process id file
	char	name[256];			// name of master server instance
	char	region[256];		// region of where master server resides
	std::vector<char*> address; // local IDP addresses to bind to
	U32		port;				// local UDP listening port number to bind to
	U32		heartbeat;			// amount of time without heartbeat response before server is delisted
	U32		verbosity;			// verbosity logging level
	U32		timestamp;			// prefix timestamp to messages when not zero

	// flood control settings
	U32		floodResetTime;		// reset ticket count every X seconds
	U32		floodForgetTime;	// forget/delete peer record after X seconds last time seen
	U32		floodBanTime;		// peer is banned for X seconds once reaching max tickets
	U32		floodMaxTickets;	// ban peer once reaching X tickets
	U32		floodBadMsgTicket;	// number of X tickets for receiving bad messages from peer

	U32		testingMode;
	U32		challengeMode;	// Enable session challenges

	void reset()
	{
		file[0] = '\0';
		pidfile[0] = '\0';
		name[0] = '\0';
		region[0] = '\0';

		for (int i=address.size()-1; i >= 0; i--)
		{
			delete address[i];
		}
		address.clear();

		port = 0;
		heartbeat = 0;
		verbosity = 0;
		timestamp = 0;
		floodResetTime = 0;
		floodForgetTime = 0;
		floodBanTime = 0;
		floodMaxTickets = 0;
		floodBadMsgTicket = 0;

		testingMode = 0;
		challengeMode = 0;
	}

	~tDaemonConfig()
	{
		reset();
	}
};

//=============================================================================
// Revised Masterd Core --TRON
//=============================================================================
enum eConfigEntityType
{
	CONFIG_TYPE_NOTSET = 0,		// Not Set
	CONFIG_TYPE_STR,			// String -- char array
	CONFIG_TYPE_S32,			// Signed 32bit integer
	CONFIG_TYPE_U32,			// Unsigned 32bit integer

	CONFIG_TYPE_STR_VECTOR,			// String -- vector of char array

	CONFIG_SECTION = 100		// config document section
};

struct tConfigEntity
{
	enum eConfigEntityType		type;	// entity storage type
	void						*pData;	// pointer to storage
	const char					*pName;	// entity name
	const char					*pDesc;	// pointer to description
};

class MasterdCore
{
private:
	tDaemonConfig	m_Prefs;
	tConfigEntity	*m_ConfigEntities;
	bool			m_RunThread;


public:
	MasterdCore();
	~MasterdCore();

	// where work is actually performed in
	void RunThread(void);
	void StopThread(void);

	// message handler
	void ProcMessage(ServerAddress *addr, Packet *data, tPeerRecord *peerrec);

	// preferences management
	void InitPrefs(void);
	void LoadPrefs(void);
	void CreatePrefs(void);

	// pid file management
	void SetPid(void);
	void ClearPid(void);
};


// global source member, daemon configuration structure
extern tDaemonConfig	*gm_pConfig;

extern bool shouldDebugPrintf(const int level);
extern void debugPrintf(int level, const char *format, ...);
static inline bool checkLogLevel(int level)
{
	if(gm_pConfig && (level > (int)gm_pConfig->verbosity))
		return false; // message level too high

	// message level is OK to log
	return true;
}
extern void debugPrintHexDump(const void *ptr, size_t size);


#endif // _MASTERD_H_

