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
#ifndef _SERVERSTORE_H_
#define _SERVERSTORE_H_

#include "masterd.h"
#include "packetconf.h"
#include "SessionHandler.h"
#include <deque>
#include <string.h>

#if !defined(WIN32) && defined(__GNUC__)
	#define stricmp strcasecmp
	#define strnicmp strncasecmp
#endif


//=============================================================================
// Unique String Class
//=============================================================================
typedef struct tUniqueString
{
	char	*str;		// pointer to string storage
	U32		length;		// length of the string
	U32		refCount;	// reference counter
} tUniqueString;

typedef std::deque<tUniqueString> tcUniqueString;

class UniqueStringList
{
public:
	tcUniqueString	m_List;
	U32				m_TotalSize;	// combined total size in bytes of all strings together
	
	UniqueStringList()
	{
		m_TotalSize = 0;
	}
	~UniqueStringList()
	{
		tcUniqueString::iterator	it;
		
		// destroy all strings
		for(it = m_List.begin(); it != m_List.end(); it++)
		{
			if(it->str)
				delete[] it->str;
		}
	}

	U32 TotalSize()	{ return m_TotalSize;   }
	U32 Count()		{ return m_List.size(); }


	tUniqueString* GetMatch(const char *str)
	{
		tcUniqueString::iterator	it;
		U32							len;
//		U32							i=0;


		if(!str)
			return NULL;

		// get length of the string
		len = strlen(str);

		// find out if this string already exists in the list
		for(it = m_List.begin(); it != m_List.end(); it++)
		{
			if(len != it->length || stricmp(it->str, str))
			{
//				printf("GetMatch(%lu) [%lu]\"%s\" != [%lu]\"%s\"\n",
//				i++, it->length, it->str, len, str);
				continue;
			}

			// match found, increment reference counter
			return &*it;
		}

		// no match found
		return NULL;
	}

	char* Push(const char *str, bool effectRef = true)
	{
		tUniqueString	record, *pRec;
		U32				len;


		if(!str)
			return NULL;
		
		// get length of the string
		len = strlen(str);
		
		// find out if this string already exists in the list
		pRec = GetMatch(str);
		if(pRec)
		{
			// match found, increment reference counter
			if(effectRef)
				pRec->refCount++;

			// give caller the pointer to our copy of the string, done
			return pRec->str;
		}

		// we've reached here because we don't have the string in our list,
		// allocate an array to store the string into and then store it.
		record.str		= new char[len +1];
		record.length	= len;
		record.refCount	= 1;

		// copy string
		strcpy(record.str, str);

		// store the unique string
		m_List.push_back(record);
		m_TotalSize += record.length;

		// done
		return record.str;
	}
	
	void PopRef(const char *str)
	{
		tcUniqueString::iterator	it;
		tUniqueString				*pRec;

		if(!str)
			return;
		
		// pop the referenced string
		for(it = m_List.begin(); it != m_List.end(); it++)
		{
			pRec = &*it;
			
			if(str != pRec->str)
				continue;

			// decrement reference counter
			if(!--(pRec->refCount))
			{
				// drop the string entirely
				m_TotalSize -= pRec->length;
				delete[] pRec->str;
				m_List.erase(it);
			}

			// done
			break;
		}
	}

	char* GetRef(const char *str)
	{
		tUniqueString	*pRec;

		
		pRec = GetMatch(str);
		if(pRec)
			return pRec->str;

		return NULL;
	}
	
};


/**
 * @brief Filter structure
 *
 * In general, if something is null, it is a *
 */
class ServerFilter
{
public:


	enum // Filter flags
	{
		Dedicated = 0x1,
		NotPassworded = 0x2,
		Linux = 0x2,

		CurrentVersion = 0x80
	};

	enum // Region mask address flags
	{
		// The following are fixed by the master server regardless of what the server sends. If neither are set, the server will force IsIPV4 to be true, and IsIPV6 also if this is a new-style response.
		RegionIsIPV4Address = 0x40000000, // bit 31
		RegionIsIPV6Address = 0x80000000,  // bit 32

		RegionAddressMask = RegionIsIPV4Address | RegionIsIPV6Address
	};

	char	*gameType;			// game type string
	char	*missionType;		// mission type string
	U32		*buddyList;			// pointer to buddy array
	U32		regions;			// regions bitmask
	U32		version;			// minimum version
	U16		minCPUSpeed;		// minimum processor speed
	U8		minPlayers;			// minimum player count
	U8		maxPlayers;			// maximum player count
	U8		filterFlags;		// info flag filter
	U8		maxBots;			// maximum bots
	U8		buddyCount;			// number of buddies in array

	ServerFilter()
	{
		missionType		= NULL;
		gameType		= NULL;
		buddyList		= NULL;
	}

	~ServerFilter()
	{
		if(missionType)	delete[] missionType;
		if(gameType)	delete[] gameType;
		if(buddyList)	delete[] buddyList;
	}
};

/**
 * @brief Where we store actual data on a server.
 *
 * Even though it really gets stuffed into sqlite.
 */
class ServerInfo
{
public:
	ServerAddress	addr;
	U32				session;
	U16				key;

	char	*gameType;
	char	*missionType;
	U32		*playerList;	// players GUID array, may be NULL!
	U32		regions;
	U32		version;
	U16		CPUSpeed;
	U8		playerCount;
	U8		maxPlayers;
	U8		infoFlags;
	U8		numBots;

	// Bookkeeping information
	time_t  tsLastHeart;	// Last time we got a heart beat
	time_t  tsLastInfo;	// Last time we got info from them
	bool m_DestroyPlayers;
	bool testServer;
	bool ownsStrings;

	ServerInfo(bool destroyPlayers = true)
	{
		gameType	= NULL;
		missionType	= NULL;
		playerList	= NULL;
		
		maxPlayers	= 0;
		regions		= 0;
		version		= 0;
		infoFlags	= 0;
		numBots		= 0;
		CPUSpeed	= 0;
		playerCount	= 0;
		
		tsLastHeart	= 0;
		tsLastInfo	= 0;
		
		m_DestroyPlayers = destroyPlayers;
		testServer = false;
		ownsStrings = true;
	}

	~ServerInfo()
	{
		if (ownsStrings)
		{
			if(missionType)	delete[] missionType;
			if(gameType)	delete[] gameType;
		}

		// only destroy players GUID array if requested to
		if(m_DestroyPlayers && playerList)
			delete[] playerList;
	}

	void setToDestroy(bool players)
	{
		m_DestroyPlayers = players;
	}
};

class ServerStore
{
public:
	UniqueStringList	m_GameTypes;
	UniqueStringList	m_MissionTypes;

	virtual ~ServerStore() {;}
	
	// Work functions
	virtual void DoProcessing(int count = 5) = 0;
	virtual void HeartbeatServer(ServerAddress *addr, U32 *session, U16 *key) = 0;
	virtual void UpdateServer(ServerAddress *addr, ServerInfo *info) = 0;

	virtual void QueryServers(Session *session, ServerFilter *filter) = 0;

	virtual U32 getCount() = 0;
};



#endif

