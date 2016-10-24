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
#ifndef _SESSION_HANDLER_H_
#define _SESSION_HANDLER_H_

#include <map>
#include <list>
#include <vector>
#include "commonTypes.h"
#include "packetconf.h"

// absolute maximum number of sessions per peer / remote host at the same time
#define SESSION_ABSOLUTE_MAX				10

struct ServerResultPacket
{
	U16 size;
	U8 data[LIST_PACKET_SIZE - LIST_PACKET_HEADER];

	inline U16 getNumServers() { return *((U16*)data); }
};

typedef std::vector<ServerResultPacket> tcServerAddrVector;

class Session
{
public:

	enum // Query Flags
	{
		OnlineQuery       = 0,        // Authenticated with master
		OfflineQuery      = 1,   // On our own
		NoStringCompress  = 1<<1,
		NewStyleResponse = 1<<2,   // Send new style packet response (with ipv6 & such),
		AuthenticatedSession = 1<<3, // Any dispatched tokens are authenticated
	};

	U32						authSession;	// Authenticated session key

	time_t					tsLastUsed;		// last time this session was used

	tcServerAddrVector	results;			// associated server query results

	U16						session;			// associated session identifier
	U16						total;			// total number of servers
	U16						packTotal;		// total number of packets

	U8 sessionFlags;							// flags for session transmission data

	Session(U16 session, U8 flags)
	{
//		printf("DEBUG: session object born: %X\n", this);
		
		this->session	= session;
		this->tsLastUsed	= getAbsTime();
		this->sessionFlags = flags & ~AuthenticatedSession;
		this->authSession = 0;
		this->total = 0;
		this->packTotal = 0;
	}
	~Session()
	{
//		printf("DEBUG: session object dying: %X\n", this);
		
		// do nothing
	}

	bool isAuthenticated()
	{
		return authSession != 0;
	}
};

typedef std::list<Session *> tcSessionList;


//=============================================================================
// Flood Control Manager
//=============================================================================

// TODO: this should be renamed to more like PeerControl since it is both the
// flood/spam and session manager now.

typedef struct tPeerRecord
{
	ServerAddress	peer;				// remote peer's address and port info
	tcSessionList	sessions;			// sessions list of (game client) peer
	time_t			tsCreated;			// when this record was created
	time_t			tsLastSeen;			// last time peer was seen
	time_t			tsLastReset;		// last time peer's tickets were reset
	time_t			tsBannedUntil;		// peer is banned until timestamp
	S32				tickets;			// count of violations
	U32				bans;				// count of times peer has been banned
} tPeerRecord;

typedef std::unordered_map<ServerAddress, tPeerRecord> tcPeerRecordMap;
class tMessageSession;

class FloodControl
{
private:
	tcPeerRecordMap				m_Records;
	tcPeerRecordMap::iterator	m_ProcIT;

	void GetPeerRecord(tPeerRecord **peerrec, ServerAddress &peer, bool createNoExist);
	void CheckSessions(tPeerRecord *peerrec, bool forceExpire = false);
	
public:
	FloodControl();
	~FloodControl();


	// expunge expired peer records
	void DoProcessing(U32 count = 5);

	// the following functions return true=allowed, false=banned
	bool CheckPeer(ServerAddress &peer, tPeerRecord **peerrec = NULL, bool effectRep = true);
	bool CheckPeer(tPeerRecord *peerrec, bool effectRep = false);

	// modify peer record reputation
	void RepPeer(ServerAddress &peer, S32 tickets);
	void RepPeer(tPeerRecord *peerrec, S32 tickets);

	// session management functions
	void CreateSession(tPeerRecord *peerrec, tPacketHeader *header, Session **session);
	bool GetSession(tPeerRecord *peerrec, tPacketHeader *header, Session **session);

	void SendAuthenticationChallenge(tMessageSession &msg);
	bool GetAuthenticatedSession(tPeerRecord *peerrec, tPacketHeader *header, Session **session, bool ignoreNoSession);
};

//extern SessionHandler	*gm_pSessions;
extern FloodControl		*gm_pFloodControl;

#endif
