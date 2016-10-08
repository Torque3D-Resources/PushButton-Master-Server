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
#include <stdlib.h>
#include <assert.h>


// Trick to allow conditional compilation of sqlite.
#ifndef HAVE_SQLITE
#	define _SERVERSTORESQLITE_CPP_
#else
#	pragma	message( "	- Compiling RAM support.")
#endif

#ifndef	_SERVERSTORERAM_CPP_
#define _SERVERSTORERAM_CPP_

#include "ServerStoreRAM.h"


// Enables extensive debugging of query filtering of game servers in ServerStoreRAM::QueryServers().
// Should not be used for production, to be sure we also check for NDEBUG to avoid enabling on release builds.
#ifndef NDEBUG
#	define DEBUG_FILTER
#endif // !NDEBUG


// Simple builder helper for packet list
struct ListPacketBuilder
{
	static const U32 ListSize = LIST_PACKET_SIZE - LIST_PACKET_HEADER;
	U8 data[ListSize];
	U8 *ptr;
	U16 numServers;
	U8 responseType;

	static const U32 IPAddressSize = 7;
	static const U32 IP6AddressSize = 18;

	inline void reset(U8 newResponseType)
	{
		U16 *szPtr = (U16*)data;
		*szPtr++ = 0;
		ptr = (U8*)szPtr;
		numServers = 0;
		responseType = newResponseType;
	}

	inline void setNumServers(U16 count)
	{
		U16 *szPtr = (U16*)data;
		*szPtr = count;
	}

	inline void putU8(U8 value)
	{
		*ptr++ = value;
	}

	inline void putU16(U16 value)
	{
		U16 *vp = (U16*)ptr;
		*vp++ = value;
		ptr = (U8*)vp;
	}

	inline void putU32(U32 value)
	{
		U32 *vp = (U32*)ptr;
		*vp++ = value;
		ptr = (U8*)vp;
	}

	inline bool addIPV4Address(ServerAddress *addr)
	{
		if (bytesLeft() < IPAddressSize)
			return false;

		if (responseType == 1)
		{
			putU8((U8)ServerAddress::IPAddress);
		}
		putU32(addr->address.ipv4.address);
		putU16(addr->port);
		numServers++;
		return true;
	}

	inline bool addIPV6Address(ServerAddress *addr)
	{
		if (bytesLeft() < IP6AddressSize)
			return false;

		assert(responseType > 0); // not supported with old packets

		putU8((U8)ServerAddress::IPV6Address);
		memcpy(ptr, addr->address.ipv6.netNum, 16);
		ptr += 16;
		putU16(addr->port);
		numServers++;
		return true;
	}

	inline bool addServerAddress(ServerAddress *addr)
	{
		if (addr->type == ServerAddress::IPAddress)
		{
			if (!addIPV4Address(addr))
				return false;
		}
		else if (addr->type == ServerAddress::IPV6Address)
		{
			if (!addIPV6Address(addr))
				return false;
		}
		return true;
	}

	inline U32 bytesLeft() { return ListSize - (ptr - data);}
	
	void dumpFinalData(tcServerAddrVector &list)
	{
		ServerResultPacket packet;
		packet.size = ptr - data;
		setNumServers(numServers);
		memcpy(packet.data, data, packet.size);
		list.push_back(packet);
	}
};

/**
 * @brief Initialize server db to use filename.
 *
 * @param	filename	File to store database in.
 */
ServerStoreRAM::ServerStoreRAM()
{
	m_ProcIT = m_Servers.begin();
	
}
ServerStoreRAM::~ServerStoreRAM()
{
	
}


//==============================================================================
// Server Store in RAM rewrite
//==============================================================================

bool ServerStoreRAM::FindServer(ServerAddress *addr, tcServerMap::iterator &it)
{
	// abort on NULL
	if(!addr)
		return false;

	// find server entry
	it = m_Servers.find(*addr);
	if(it != m_Servers.end())
		return true;

	// done
	return false;
}

bool ServerStoreRAM::FindServer(ServerAddress *addr, ServerInfo **serv)
{
	tcServerMap::iterator	it;


	if(FindServer(addr, it))
		*serv = &it->second;
	else
		*serv = NULL;

	// done
	return (*serv != NULL);
}

void ServerStoreRAM::AddServer(ServerAddress *addr, ServerInfo *info)
{
	char		*oldGame, *oldMission;
	char buffer[256];
	bool oldOwnsStrings;


	// abort on NULL
	if(!addr || !info)
		return;

	
	// add missing information
	info->last_info		= getAbsTime();
	info->addr			= *addr;

	// notify game and mission types manager
	info->gameType		= m_GameTypes.Push(   oldGame    = info->gameType);
	info->missionType	= m_MissionTypes.Push(oldMission = info->missionType);

	oldOwnsStrings = info->ownsStrings;
	info->ownsStrings = false;

	// Make sure the ipv4/ipv6 bit is correctly set
	info->regions &= ~ServerFilter::RegionAddressMask;

	if (addr->type == ServerAddress::IPAddress)
	{
		info->regions |= ServerFilter::RegionIsIPV4Address;
	}
	else if (addr->type == ServerAddress::IPV6Address)
	{
		info->regions |= ServerFilter::RegionIsIPV6Address;
	}

	// insert new server record
	m_Servers[*addr]		= *info;

	if(shouldDebugPrintf(DPRINT_VERBOSE))
	{
		debugPrintf(DPRINT_VERBOSE, "New Server [%s] Game:\"%s\", Mission:\"%s\"\n",
					addr->toString(buffer), info->gameType, info->missionType);
	}

	debugPrintf(DPRINT_DEBUG, " Players(bots)/Max %hhu(%hhu)/%hhu, CPU %hu MHz, version %u, regions 0x%08x\n",
				info->playerCount, info->numBots, info->maxPlayers, info->CPUSpeed, info->version, info->regions);
	
	// give back the old string references to the stack serverinfo
	info->gameType		= oldGame;
	info->missionType	= oldMission;
	info->ownsStrings	= oldOwnsStrings;

	// don't destroy player list, we're using it
	info->setToDestroy(false);
	
	// done
}

void ServerStoreRAM::RemoveServer(tcServerMap::iterator &it)
{
	ServerInfo *info;
	info = &it->second;
	char buffer[256];

	if(shouldDebugPrintf(DPRINT_VERBOSE))
	{
		debugPrintf(DPRINT_VERBOSE, "Remove Server [%s] Game:\"%s\", Mission:\"%s\"\n",
					info->addr.toString(buffer), info->gameType, info->missionType);
	}
	debugPrintf(DPRINT_DEBUG, " Players(bots)/Max %hhu(%hhu)/%hhu, CPU %hu MHz, version %u, regions 0x%08x\n",
				info->playerCount, info->numBots, info->maxPlayers, info->CPUSpeed, info->version,info->regions);
	
	// notify game and mission types manager
	m_GameTypes.PopRef(info->gameType);
	m_MissionTypes.PopRef(info->missionType);

	// invalid the type pointers
	info->gameType		= NULL;
	info->missionType	= NULL;

	// delete the record
	m_Servers.erase(it);

	// done
}

void ServerStoreRAM::RemoveServer(ServerAddress *addr)
{
	tcServerMap::iterator	it;

	// find and then delete server record
	if(FindServer(addr, it))
		RemoveServer(it);

	// done
}



void ServerStoreRAM::DoProcessing(int count)
{
	tcServerMap::iterator next;
	ServerInfo *rec;
	bool testMode = gm_pConfig->testingMode;


//CheckMoreServers:
	// check for out of date records
	for(; m_ProcIT != m_Servers.end() && --count;)
	{
		rec = &m_ProcIT->second;

		// has server record expired?
		if(rec->last_info + (int)gm_pConfig->heartbeat > getAbsTime() || (testMode && rec->testServer))
		{
			// nope, next...
			m_ProcIT++;
			continue;
		}

		next = m_ProcIT;
		next++;

		// server record has expired, remove it
		RemoveServer(m_ProcIT);

		// iterator is now invalid, use next
		m_ProcIT = next;
	}

	// start from the beginning if we hit the end
	if(m_ProcIT == m_Servers.end())
		m_ProcIT = m_Servers.begin();

	// check on more records if we reached the end too early
//	if(count && (count < m_Servers.size()))
//		goto CheckMoreServers;

	// done
}

void ServerStoreRAM::HeartbeatServer(ServerAddress *addr, U16 *session, U16 *key)
{
	// generate a random session and key to use to send an info request from the
	// server that just hearbeat us. We really don't care about the session and
	// key details so we don't save them because we track servers based on the
	// IP address and port anyway. Other than that this function does nothing
	// special.

	// seed the random generator
	srand(getAbsTime() + *((uint32_t*)addr->address.ipv4.netNum) + addr->port);
	
	if(session)	*session	= (U16)rand();
	if(key)		*key		= (U16)rand();

	// done
}

void ServerStoreRAM::UpdateServer(ServerAddress *addr, ServerInfo *info)
{
	ServerInfo	*rec;
	char		*oldGame, *oldMission;
	char buffer[256];


	// find the existing server record
	if(!FindServer(addr, &rec))
	{
		// not found, add server to our list and abort
		AddServer(addr, info);
		return;
	}

	// update an existing server record
	rec->addr			= *addr;
	rec->playerCount	= info->playerCount;
	rec->maxPlayers 	= info->maxPlayers;
	rec->regions		= info->regions;
	rec->version		= info->version;
	rec->CPUSpeed		= info->CPUSpeed;
	rec->infoFlags		= info->infoFlags;
	rec->numBots		= info->numBots;
	rec->testServer		= info->testServer;

	// special handling of game and mission type,
	// remember current type references.
	oldGame		= rec->gameType;
	oldMission	= rec->missionType;

	// use game and mission type managers to see if these types are unique 
	rec->gameType		= m_GameTypes.Push(   info->gameType,    false);
	rec->missionType	= m_MissionTypes.Push(info->missionType, false);

	// notify game and mission type managers that type isn't in use anymore by server
	if(oldGame    != rec->gameType)		m_GameTypes.PopRef(oldGame);
	if(oldMission != rec->missionType)	m_MissionTypes.PopRef(oldMission);


	// destroy existing player GUID list and point to the new one
	if(rec->playerList)
		delete[] rec->playerList;

	rec->playerList = info->playerList;
	info->setToDestroy(false);	// don't destroy player list, we're using it

	// update last information update time
	rec->last_info = getAbsTime();

	if(shouldDebugPrintf(DPRINT_VERBOSE))
	{
		debugPrintf(DPRINT_VERBOSE, "Updated Server [%s] Game:\"%s\", Mission:\"%s\"\n",
					addr->toString(buffer), rec->gameType, rec->missionType);
	}
	debugPrintf(DPRINT_DEBUG, " Players(bots)/Max %hhu(%hhu)/%hhu, CPU %hu MHz, version %u, regions 0x%08x\n",
				rec->playerCount, rec->numBots, rec->maxPlayers, rec->CPUSpeed, rec->version, rec->regions);
	
	// done
}

void ServerStoreRAM::QueryServers(Session *session, ServerFilter *filter)
{
	tcServerMap::iterator	it;
	ServerInfo				*info;
	char					*game = NULL, *mission = NULL;
	char buffer[256];
	bool					buddyFound;
	U32						i, n;
#ifdef DEBUG_FILTER
	size_t					current = 0;
#endif // DEBUG_FILTER

	std::vector<ServerAddress*> totalResults;
	ListPacketBuilder packetBuilder;

	debugPrintf(DPRINT_VERBOSE, "Query for Game:\"%s\", Mission:\"%s\"\n",
				filter->gameType, filter->missionType);
	debugPrintf(DPRINT_DEBUG, " Players Min/Max(Bots) %hhu/%hhu(%hhu), CPU >= %hu MHz, Version >= %u, Regions 0x%08x, Flags 0x%08x\n",
				filter->minPlayers, filter->maxPlayers, filter->maxBots, filter->minCPUSpeed, filter->version, filter->regions, filter->filterFlags);

	// special handling of game and mission types
	if(filter->gameType && strlen(filter->gameType))
	{
		// see if game type is a wildcard 'any' keyword
		if(stricmp(filter->gameType, "any"))
		{
			// nope, now try finding this type in our unique game type manager
			game = m_GameTypes.GetRef(filter->gameType);

			// was game type found?
			if(!game)
			{
				debugPrintf(DPRINT_DEBUG, " Aborting query, no matching servers for Game filter.\n");
				goto SkipFilterTests; // no match found, no servers will satify filter
			}
		}
	}
	if(filter->missionType && strlen(filter->missionType))
	{
		// see if mission type is a wildcard 'any' keyword
		if(stricmp(filter->missionType, "any"))
		{
			// nope, now try finding this type in our unique mission type manager
			mission = m_MissionTypes.GetRef(filter->missionType);

			// was mission type found?
			if(!mission)
			{
				debugPrintf(DPRINT_DEBUG, " Aborting query, no matching servers for Mission filter.\n");
				goto SkipFilterTests; // no match found, no servers will satify filter
			}
		}
	}
	
	// build server list matching the query filter
	for(it = m_Servers.begin(); it != m_Servers.end(); it++)
	{
		// get server record
		info = &it->second;

#ifdef DEBUG_FILTER
		if(shouldDebugPrintf(DPRINT_DEBUG))
		{
			debugPrintf(DPRINT_DEBUG, " FilterCheck(%zu/%zu) Server [%s] Game:\"%s\", Mission:\"%s\"\n",
						++current, m_Servers.size(), info->addr.toString(buffer), info->gameType, info->missionType);
		}
#endif // DEBUG_FILTER

		// make sure we only emit ipv4 on old responses
		if ((!(session->sessionFlags & Session::NewStyleResponse)))
		{
			if (info->addr.type != ServerAddress::IPAddress)
			{
#ifdef DEBUG_FILTER
			debugPrintf(DPRINT_DEBUG, "  Filter fault: No ipv4.\n");
#endif // DEBUG_FILTER
				continue;
			}
		}
		
		// check the game type
		if(game && (game != info->gameType))
		{
#ifdef DEBUG_FILTER
			debugPrintf(DPRINT_DEBUG, "  Filter fault: Game mismatch.\n");
#endif // DEBUG_FILTER
			continue; // skip
		}

		// check the mission type
		if(mission && (mission != info->missionType))
		{
#ifdef DEBUG_FILTER
			debugPrintf(DPRINT_DEBUG, "  Filter fault: Mission mismatch.\n");
#endif // DEBUG_FILTER
			continue; // skip
		}

		// check minimum player count
		if(filter->minPlayers && (info->playerCount < filter->minPlayers))
		{
#ifdef DEBUG_FILTER
			debugPrintf(DPRINT_DEBUG, "  Filter fault: Min players, %hhu < %hhu.\n", info->playerCount, filter->minPlayers);
#endif // DEBUG_FILTER
			continue; // skip
		}

		// check maximum player count
		if(filter->maxPlayers && (info->playerCount > filter->maxPlayers))
		{
#ifdef DEBUG_FILTER
			debugPrintf(DPRINT_DEBUG, "  Filter fault: Max players, %hhu > %hhu.\n", info->playerCount, filter->maxPlayers);
#endif // DEBUG_FILTER
			continue; // skip
		}

		// check regions mask
		if(filter->regions && !(info->regions & filter->regions))
		{
#ifdef DEBUG_FILTER
			debugPrintf(DPRINT_DEBUG, "  Filter fault: Regions mask, 0x%08x & 0x%08x = 0x%08x.\n",
						info->regions, filter->regions, info->regions & filter->regions);
#endif // DEBUG_FILTER
			continue; // skip
		}

		// check minimum version
		if(filter->version && (info->version < filter->version))
		{
#ifdef DEBUG_FILTER
			debugPrintf(DPRINT_DEBUG, "  Filter fault: Min version, %hhu < %hhu.\n", info->version, filter->version);
#endif // DEBUG_FILTER
			continue; // skip
		}

		// check information bit flag mask
		if(filter->filterFlags && !(info->infoFlags & filter->filterFlags))
		{
#ifdef DEBUG_FILTER
			debugPrintf(DPRINT_DEBUG, "  Filter fault: Flags mask, 0x%08x & 0x%08x = 0x%08x.\n",
						info->infoFlags, filter->filterFlags, info->infoFlags & filter->filterFlags);
#endif // DEBUG_FILTER
			continue; // skip
		}

		// check maximum bot count
		if(filter->maxBots && (info->numBots > filter->maxBots))
		{
#ifdef DEBUG_FILTER
			debugPrintf(DPRINT_DEBUG, "  Filter fault: Max bots, %hhu > %hhu.\n", info->numBots, filter->maxBots);
#endif // DEBUG_FILTER
			continue; // skip
		}

		// check minimum processor speed
		if(filter->minCPUSpeed && (info->CPUSpeed < filter->minCPUSpeed))
		{
#ifdef DEBUG_FILTER
			debugPrintf(DPRINT_DEBUG, "  Filter fault: Min CPU, %hu < %hu.\n", info->CPUSpeed, filter->minCPUSpeed);
#endif // DEBUG_FILTER
			continue; // skip
		}

		// this part we check on our buddies, but I don't know if we're suppose
		// to exclude servers of which client's buddies aren't on them just like
		// an explicit filter, or any servers that have our buddies on them will
		// be an exception to the filters above, but for now we're doing the former.
		// --TRON

		// check buddies list
		if(filter->buddyCount && info->playerList)
		{
			buddyFound = false;
			
			// see if any of our buddies are on this server
			for(i=0; (i < info->playerCount) && !buddyFound; i++)
			{
				for(n=0; n < filter->buddyCount; n++)
				{
					// does buddy match?
					if(filter->buddyList[n] == info->playerList[i])
					{
						// yep, server is acceptable
						buddyFound = true;
						break;
					}
				}
			}

			// was any of the client's buddies on this server
			if(!buddyFound)
			{
#ifdef DEBUG_FILTER
				debugPrintf(DPRINT_DEBUG, "  Filter fault: no buddies match.\n");
#endif // DEBUG_FILTER
				continue; // nope, skip
			}
		}


		// server passed the filter test, add it to the list
		totalResults.push_back(&info->addr);
	}

SkipFilterTests:
	// now we have our server list result and need to figure out how many
	// packets are required.

	U8 responseType = session->sessionFlags & Session::NewStyleResponse ? 1 : 0;
	packetBuilder.reset(responseType);

	for (S32 i=0; i<totalResults.size(); i++)
	{
		ServerAddress *addAddress = totalResults[i];
		if (!packetBuilder.addServerAddress(addAddress))
		{
			// Limit results index to 255, as its impossible to return more packets than this (as packetIndex 255 == reset)
			if (session->results.size() >= 254)
			{
				debugPrintf(DPRINT_DEBUG, "  Filter warning: too many results, clipping packets.\n");
				break;
			}

			packetBuilder.dumpFinalData(session->results);
			packetBuilder.reset(responseType);
			i--;
		}
	}

	packetBuilder.dumpFinalData(session->results);

	session->total		= totalResults.size();
	session->packTotal	= session->results.size();

	// done
}



#endif // _SERVERSTORERAM_CPP_

