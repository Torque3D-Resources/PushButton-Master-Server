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
#include "masterd.h"
#include "SessionHandler.h"
#include "TorqueIO.h"

/**
 * @brief We store the SessionHandler global here.
 */
FloodControl	*gm_pFloodControl;



/**
 *
 * What flood control is used for:
 *
 * Flood control is a flood protection class that aims to suppress or even
 * prevent a Denial of Service attack. DoS attacks are either done by wasting
 * the master server's time processing fake queries or server heartbeats,
 * or simply by packet flooding the master server host to the point of the
 * network connection is completely saturated. This class aims to prevent the
 * first kind of DoS attack from occurring, while the second technique is far
 * beyond the scope or ability of this class.
 *
 * Therefore to put it simply, this class is designed to prevent the master
 * server core from wasting its time so that it can be responsive to legitimate
 * requests.
 *
 * UPDATED: This class is now the session manager too, it just made sense to
 * to not have separate classes practically doing the same thing.
 *
 *
 * Things to consider when designing this flood control class:
 *
 * 
 * 1) There can be multiple clients(players) querying the master server from a
 *    single IP address, such as a school or after school club, so don't assume
 *    it is a flood attack if multiple queries start coming in from a single IP.
 * 
 *    Yet, downside to not assuming it is an attack could be a hole in our flood
 *    protection, therefore an easy vector to cause a DoS from a single machine.
 * 
 *    One way to avoid this problem is to figure out how many tickets a single
 *    request to full server listing would be multiply by reasonable number of
 *    clients could be behind NAT and then use a number above that for max tickets.
 * 
 * 
 * 2) Invalid packets should be considered bad reputation, of which is currently
 *    done in masterd/core.cc MasterdCore::ProcMessage(), that will eventually
 *    cause the offending host to be banned for a time. Increasing bad reputation
 *    to a host can be done by calling FloodControl::RepPeer().
 * 
 */

//=============================================================================
// Flood Control Manager
//=============================================================================

FloodControl::FloodControl()
{
	m_ProcIT = m_Records.begin();
}

FloodControl::~FloodControl()
{
	// no cleanup necessary
}


void FloodControl::GetPeerRecord(tPeerRecord **peerrec, ServerAddress &peer, bool createNoExist)
{
	tcPeerRecordMap::iterator	it;
	tPeerRecord					*pr;
	char buffer[256];


	// abort on NULL
	if(!peerrec)
		return;

	// default pointer to no record
	*peerrec = NULL;

	// locate the peer record
	it = m_Records.find(peer);

	// handle situtation when record doesn't exist
	if(it == m_Records.end())
	{
		// create record if allowed to create non-existant records
		if(createNoExist)
		{
			*peerrec = &m_Records[peer];
			pr		 = new tPeerRecord();

			// set peer address, creation and last seen time
			pr->peer			= peer;
			pr->tsCreated		= getAbsTime();
			pr->tsLastSeen		= (*peerrec)->tsCreated;
			pr->tsLastReset		= (*peerrec)->tsCreated;
			pr->tsBannedUntil	= 0;	// not banned
			pr->tickets			= 0;	// no tickets yet
			pr->bans			= 0;	// no previous bans

			**peerrec = *pr;

			// report record creation
			if(shouldDebugPrintf(DPRINT_VERBOSE))
			{
				debugPrintf(DPRINT_VERBOSE, "FloodControl: Record created for %s\n", (*peerrec)->peer.toString(buffer));
			}
		}
	} else
	{
		// get pointer to the already existant record
		*peerrec = &it->second;
	}
	
}

void FloodControl::CheckSessions(tPeerRecord *peerrec, bool forceExpire)
{
	tcSessionList::iterator	it, next;
	Session					*ps;
	time_t					ts;
	U32						sessionExpireTime = gm_pConfig->sessionTimeoutSeconds;

	// get current timestamp
	ts = getAbsTime();

	// iterate through peer's game client query sessions
	for(it = peerrec->sessions.begin(); it != peerrec->sessions.end();)
	{
		// get session
		ps = *it;
		
		if(forceExpire)
		{
			// we are to destry all existing sessions, force it to be expired
			ps->tsLastUsed = 0;
		}

		// move on to next session if current one hasn't expired yet
		if(ps->tsLastUsed + sessionExpireTime > ts)
		{
			it++;
			continue;
		}

		next = it;
		next++;

		// destroy expired session
		delete ps;
		peerrec->sessions.erase(it);

		// iterator is now invalid, figure out how to move on to the next
		it = next;
	}
}


//-----------------------------------------------------------------------------
// Cleanup dead records
//-----------------------------------------------------------------------------
void FloodControl::DoProcessing(U32 count)
{
	tcPeerRecordMap::iterator next;
	tPeerRecord *peerrec;
	char buffer[256];


//CheckMoreRecords:
	// iterate through existing peer records to check for expiration
	for(; m_ProcIT != m_Records.end() && --count;)
	{
		// get peer record
		peerrec = &m_ProcIT->second;

		// don't expire banned peers, else check expiration
		if(	(peerrec->tsBannedUntil) ||
			((peerrec->tsLastSeen + (S32)gm_pConfig->floodForgetTime) > getAbsTime()))
		{
			// peer record doesn't expire, check it for everything else
			CheckPeer(peerrec, false);
			CheckSessions(peerrec);

			// move on to next peer record
			m_ProcIT++;
			continue;
		}

		// peer is to be forgotten, last seen time has expired

		// report peer record expired
		if(shouldDebugPrintf(DPRINT_VERBOSE))
		{
			debugPrintf(DPRINT_VERBOSE, "FloodControl: Record expired for %s\n",
						peerrec->peer.toString(buffer));
		}

		next = m_ProcIT;
		next++;

		// destroy any sessions in the peer record
		CheckSessions(peerrec, true);

		// destroy peer record
		m_Records.erase(m_ProcIT);

		// iterator is now invalid, use next
		m_ProcIT = next;
	}

	// start from the beginning if we hit the end
	if(m_ProcIT == m_Records.end())
		m_ProcIT = m_Records.begin();

	// check on more records if we reached the end too early
//	if(count && (count < m_Records.size()))
//		goto CheckMoreRecords;

}


//-----------------------------------------------------------------------------
// Check reputation status of peer
//-----------------------------------------------------------------------------
bool FloodControl::CheckPeer(ServerAddress &peer, tPeerRecord **peerrec, bool effectRep)
{
	tPeerRecord *rec;


	// get the peer record based on peer, else create it
	GetPeerRecord(&rec, peer, true);

	// update caller's peer record pointer if caller wants it
	if(peerrec)
		*peerrec = rec;

	// call our associated overloaded function to handle the rest
	return CheckPeer(rec, effectRep);
}

bool FloodControl::CheckPeer(tPeerRecord *peerrec, bool effectRep)
{
	time_t ts;
	char buffer[256];

	
	// abort on NULL
	if(!peerrec)
		return true; // say allowed since no record provided

	// effect reputation if requested
	if(effectRep)
		RepPeer(peerrec, 1);

	// get current timestamp
	ts = getAbsTime();

	// reset peer record ticket count if times up
	if((peerrec->tsLastReset + (S32)gm_pConfig->floodResetTime) <= ts)
	{
		peerrec->tsLastReset	= ts;	// update last time reset occurred
		peerrec->tickets		= 0;	// reset ticket count
	}

	// see if peer is banned and if ban has expired then unban them
	if(peerrec->tsBannedUntil && peerrec->tsBannedUntil <= ts)
	{
		// clear the ban
		peerrec->tsBannedUntil	= 0;

		// update last seen even though we haven't heard from peer in order to
		// prevent record from expiring and forgetting about them so soon.
		peerrec->tsLastSeen		= ts;

		// report unban
		if(shouldDebugPrintf(DPRINT_VERBOSE))
		{
			debugPrintf(DPRINT_VERBOSE, "FloodControl: Unbanned %s [banned %u times]\n",
						peerrec->peer.toString(buffer), peerrec->bans);
		}
	}

	// now check to see if peer is still banned based on their record
	if(peerrec->tsBannedUntil)
		return false; // peer is denied, bad reputation caused the ban

	// peer isn't banned, therefore allowed
	return true;
}


//-----------------------------------------------------------------------------
// Check reputation status of peer
//-----------------------------------------------------------------------------
void FloodControl::RepPeer(ServerAddress &peer, S32 tickets)
{
	tPeerRecord *rec;


	// get the peer record based on peer, don't create non-existant record
	GetPeerRecord(&rec, peer, false);

	// abort on non-existant record
	if(!rec)
		return;

	// call our associated overloaded function to handle the rest
	RepPeer(rec, tickets);
}

void FloodControl::RepPeer(tPeerRecord *peerrec, S32 tickets)
{
	char buffer[256];

	
	// abort on NULL
	if(!peerrec)
		return;

	// add to reputation and update last seen
	peerrec->tickets	+= tickets;
	peerrec->tsLastSeen	 = getAbsTime();

	// check tickets
	if(peerrec->tickets < (S32)gm_pConfig->floodMaxTickets)
		return; // peer ticket count is fine

	// set or increase ban time
	peerrec->tsBannedUntil	+= getAbsTime() + gm_pConfig->floodBanTime;
	peerrec->tickets		= 0;	// reset ticket count
	peerrec->bans++;				// keep track number of bans for peer

	// destroy any sessions in the peer record
	CheckSessions(peerrec, true);

	// report ban
	if(shouldDebugPrintf(DPRINT_INFO))
	{
		debugPrintf(DPRINT_INFO, "FloodControl: Banned %s [banned %u times]\n",
					peerrec->peer.toString(buffer), peerrec->bans);
	}
}


//-----------------------------------------------------------------------------
// Session management of peer (for game client queries)
//-----------------------------------------------------------------------------
void FloodControl::CreateSession(tPeerRecord *peerrec, tPacketHeader *header, Session **session)
{	
	// don't allow more than SESSION_MAX sessions at the same time
	if(peerrec->sessions.size() >= gm_pConfig->maxSessionsPerPeer)
	{
		debugPrintf(DPRINT_DEBUG, "SESSION MAX EXCEEDED!!!!!!!!!!\n");
		*session = NULL;
		return; // peer has reached session limit
	}

	// create new session
	*session = new Session(header->session, header->flags);

	// keep track of session
	peerrec->sessions.push_back(*session);

	// done
}

bool FloodControl::GetSession(tPeerRecord *peerrec, tPacketHeader *header, Session **session)
{
	tcSessionList::iterator it;
	Session					*ps;


	// default to session not found
	*session = NULL;
	
	// find the requested session
	for(it = peerrec->sessions.begin(); it != peerrec->sessions.end(); it++)
	{
		// get pointer to session
		ps = *it;

		// is this the session we're looking for?
		if(ps->session == header->session)
		{
			// found it
			ps->tsLastUsed	= getAbsTime();
			*session		= ps;
			break;
		}
	}

	// done
	return (*session == NULL);
}

bool FloodControl::GetAuthenticatedSession(tPeerRecord *peerrec, tPacketHeader *header, Session **session, bool ignoreNoSession)
{
	tcSessionList::iterator it;
	Session					*ps;


	// default to session not found
	*session = NULL;
	
	// find the requested session
	for(it = peerrec->sessions.begin(); it != peerrec->sessions.end(); it++)
	{
		// get pointer to session
		ps = *it;

		// is this the session we're looking for?
		if(ps->authSession != 0 && ps->authSession == header->session)
		{
			// found it
			ps->tsLastUsed	= getAbsTime();	
			*session		= ps;
			if(shouldDebugPrintf(DPRINT_VERBOSE))
			{
				debugPrintf(DPRINT_VERBOSE, " - Found session %u with auth key %u\n", ps->session, header->session);
			}
			break;
		}
	}

	if (!ignoreNoSession && *session == NULL)
	{
		if (shouldDebugPrintf(DPRINT_VERBOSE))
		{
			debugPrintf(DPRINT_VERBOSE, "FloodControl: Creating authenticated session from session key %u\n", header->session);
		}
		gm_pFloodControl->CreateSession(peerrec, header, session);
	}

	// done
	return (*session == NULL);
}

void FloodControl::SendAuthenticationChallenge(tMessageSession &msg)
{
	Packet *reply  = new Packet(PACKET_HEADER_SIZE + 4);
	Session *sessionInstance = msg.session;
	tPeerRecord *peerrec = msg.peerrec;

	sessionInstance->sessionFlags |= Session::AuthenticatedSession | Session::NewStyleResponse;

	U32 sessionKeys[SESSION_ABSOLUTE_MAX];
	U32 numSessions = 0;
	for (tcSessionList::iterator itr = peerrec->sessions.begin(), end = peerrec->sessions.end(); itr != end; itr++)
	{
		sessionKeys[numSessions++] = (*itr)->authSession;
		if (numSessions >= SESSION_ABSOLUTE_MAX)
			break;
	}

	U32 authSession;
	bool used = true;
	while (used)
	{
		used = false;
		authSession = rand();
		if (authSession == 0)
			continue;

		for (U32 i=0; i<numSessions; i++)
		{
			if (sessionKeys[i] == authSession)
			{
				used = true;
				break;
			}
		}

		if (numSessions == 0)
		{
			break;
		}
	}

	sessionInstance->authSession = authSession;
	if (shouldDebugPrintf(DPRINT_VERBOSE))
	{
		debugPrintf(DPRINT_VERBOSE, "FloodControl: generated auth key %u, sessionFlags %u\n", authSession, sessionInstance->sessionFlags);
	}

	// Prep and send packet
	reply->writeHeader(MasterServerChallenge, sessionInstance->sessionFlags, sessionInstance->authSession, msg.header->key);
	
	if (msg.header->flags & Session::AuthenticatedSession)
	{
		// Send back the whole bad key we sent rather than the first 16 bits
		reply->writeU32(msg.header->session);
	}
	else
	{
		// For unauthenticated requests, just send back the 16-bit session & key
		reply->writeU16(sessionInstance->session);
		reply->writeU16(msg.header->key);
	}

	gm_pTransport->sendPacket(reply, &peerrec->peer);
	delete reply;
}

