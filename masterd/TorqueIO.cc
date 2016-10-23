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


bool isPrintableString(const char *str)
{
	// iterate through the string to ensure only printable characters
	// make up the entire string.
	while(str && *str)
	{
		if(*str < 0x20 || *str > 0x7E)
			return false; // detected non-printable character
		
		str++;
	}

	// all characters are printable
	return true;
}

/**
 * @brief Handles session auth for packets which require sessions
 */
bool handlePacketAuthentication(tMessageSession &msg, Session **outPs, bool ignoreNoSession)
{
	Session			*ps = NULL;

	if (gm_pConfig->challengeMode)
	{
		gm_pFloodControl->GetAuthenticatedSession(msg.peerrec, msg.header, &ps, ignoreNoSession);
		if (!ps)
		{
			debugPrintf(DPRINT_ERROR, " - Failed to create authenticated session!\n");
			return false;
		}
		else if (!ps->isAuthenticated())
		{
			msg.session = ps;
			debugPrintf(DPRINT_VERBOSE, " - Sending client authentication challenge\n");
			gm_pFloodControl->SendAuthenticationChallenge(msg);
			return false;
		}

		msg.session = ps;
	}
	else if (!ignoreNoSession)
	{
		// original method, create session if it doesn't exist
		gm_pFloodControl->CreateSession(msg.peerrec, msg.header, &ps);
		msg.session = ps;
	}
	else
	{
		// original method, sans challenge
		gm_pFloodControl->GetSession(msg.peerrec, msg.header, &ps);
		msg.session = ps;
	}

	*outPs = ps;
	return true;
}

bool handleChallengePacket(tMessageSession &msg)
{
	Session *ps = NULL;
	if (gm_pConfig->challengeMode)
	{
		handlePacketAuthentication(msg, &ps, false);
	}
	
	return true;
}

/**
 * @brief Parse a list request packet and reply.
 */
bool handleListRequest(tMessageSession &msg)
{
	ServerFilter	filter;
	Session			*ps;
	U16				index;
	U8				responseType;
	int				i;
	char			buffer[256];

	
	/*

	Format of request after header:

	U8		packetIndex;
	U8		gameTypeSize;
	char	gameType[gameTypeSize];
	U8		missionTypeSize;
	char	missionType[missionTypeSize];
	U8		minPlayers;
	U8		maxPlayers;
	U32		regionMask;
	U32		version;
	U8		filterFlags;
	U8		maxBots;
	U16		minCPU;
	U8		buddyListSize;
	U32		buddyList[buddyListSize];
	
	*/

	/***********************************
	 List Query packet
	***********************************/
	index = msg.pack->readU8();

	if(checkLogLevel(DPRINT_VERBOSE))
	{
		msg.addr->toString(buffer);
		if(index == 0xFF)
			printf("Received list query request from %s\n", buffer);
		else
			printf("Received list resend request from %s\n", buffer);
		printf(" [F: %X, S: %X, K: %u, I: %X]\n", msg.header->flags, msg.header->session, msg.header->key, index);
	}

//	printf(" dumping request packet..\n");
//	debugPrintHexDump(msg.pack->getBufferPtr(), msg.pack->getBufferSize());

	// don't waste our time parsing the rest of this resend request packet,
	// go ahead and resend the specific packet now
	if(index != 0xFF)
	{
		// get associated session

		if (!handlePacketAuthentication(msg, &ps, true))
		{
			printf(" Authentication failed for some reason\n");

			if(checkLogLevel(DPRINT_DEBUG))
			{
				printf(" No such session exists, ignoring resend request.\n");
			}
			
			return true;
		}
		
		if(ps)
		{
			// resend the requested list packet
			msg.session = ps;
			sendListResponse(msg, index);
		}

		// received packet OK
		return true;
	}

	/***********************************
	 Read gameType/missionType
	***********************************/
	filter.gameType		= msg.pack->readCString();
	filter.missionType	= msg.pack->readCString();

	// go ahead and make sure the strings aren't garbage
	if(!isPrintableString(filter.gameType) || !isPrintableString(filter.missionType))
	{
		if(checkLogLevel(DPRINT_DEBUG))
		{
			debugPrintf(DPRINT_DEBUG, " Invalid query strings issued, ignoring query request.\n");
			if(isPrintableString(filter.gameType))
			{
				debugPrintf(DPRINT_DEBUG, " gameType is invalid..\n");
				debugPrintHexDump(filter.gameType, strlen(filter.gameType));
			}
			if(isPrintableString(filter.missionType))
			{
				debugPrintf(DPRINT_DEBUG, " missionType is invalid..\n");
				debugPrintHexDump(filter.gameType, strlen(filter.missionType));
			}
		}
		return false; // strings are garbage and packet is malformed
	}

	/***********************************
	 Read miscellaneous properties
	***********************************/
	filter.minPlayers	= msg.pack->readU8();
	filter.maxPlayers	= msg.pack->readU8();
	filter.regions		= msg.pack->readU32();
	filter.version		= msg.pack->readU32();
	filter.filterFlags	= msg.pack->readU8();
	filter.maxBots		= msg.pack->readU8();
	filter.minCPUSpeed	= msg.pack->readU16();
	
	/************************************
	 Read in the buddy list
	************************************/
	filter.buddyCount	= msg.pack->readU8();
	filter.buddyList	= NULL;

	if(filter.buddyCount)
	{
		filter.buddyList = new U32[filter.buddyCount];
		msg.pack->readBytes(filter.buddyList, filter.buddyCount * sizeof(U32));
	}

	// check packet parser status
	if(!msg.pack->getStatus())
		return false; // packet was malformed

	// sanity check of filter properties
	if(filter.maxPlayers  < filter.minPlayers)
		filter.maxPlayers = filter.minPlayers;

	// make sure we have a valid authenticated session
	if (!handlePacketAuthentication(msg, &ps, false))
	{
		debugPrintf(DPRINT_ERROR, " - Packet authentication failed!\n");
		return true;
	}

	// search for servers matching query filter
	msg.session = ps;

	// We need to force the default region filter mask based on the request
	responseType = ps->sessionFlags & Session::NewStyleResponse ? 1 : 0;
	if (responseType == 0)
	{
		if ((filter.regions & ServerFilter::RegionAddressMask) == 0)
		{
			filter.regions |= ServerFilter::RegionIsIPV4Address;
		}

		// this should be unset
		filter.regions &= ~ServerFilter::RegionIsIPV6Address;
	}
	else if (responseType == 1)
	{
		if ((filter.regions & ServerFilter::RegionAddressMask) == 0)
		{
			filter.regions |= ServerFilter::RegionIsIPV4Address;
			filter.regions |= ServerFilter::RegionIsIPV6Address;
		}
	}

	// NOW we can search
	gm_pStore->QueryServers(ps, &filter);
	
	debugPrintf(DPRINT_VERBOSE, "Got %hu results from queryServers.\n", ps->total);

	// send the results
	for(i=0; i<ps->packTotal; i++)
		sendListResponse(msg, i);

	// received packet OK
	return true;
}

/**
 * @brief Handle a TypesRequest packet.
 */
bool handleTypesRequest(tMessageSession &msg)
{
	///	No Format of request after header.

	// Send the types response.
	sendTypesResponse(msg);

	// received packet OK
	return true;
}

bool handleInfoRequest(tMessageSession &msg)
{
	///	No Format of request after header.
	
	// Send the types response.
	sendInfoResponse(msg);

	// received packet OK
	return true;
}

/**
 * @brief Handle an InfoResponse packet.
 */
bool handleInfoResponse(tMessageSession &msg)
{
	ServerInfo info;

	/*

	Format of request after header:

	U8		gameTypeSize;
	char	gameType[gameTypeSize];
	U8		missionTypeSize;
	char	missionType[missionTypeSize];
	U8		maxPlayers;
	U32		regionMask;
	U32		version;
	U8		infoFlags;
	U8		numBots;
	U16		CPUSpeed;
	U8		numPlayers;
	U32		playersGuiList[numPlayers];

	*/
	
	info.addr			= *msg.addr;
	info.session		= msg.header->session;
	info.key			= msg.header->key;

	info.gameType		= msg.pack->readCString();
	info.missionType	= msg.pack->readCString();
	info.maxPlayers		= msg.pack->readU8();
	info.regions		= msg.pack->readU32() & ~ServerFilter::RegionAddressMask;
	info.version		= msg.pack->readU32();
	info.infoFlags		= msg.pack->readU8();
	info.numBots		= msg.pack->readU8();
	info.CPUSpeed		= msg.pack->readU32();
	info.playerCount	= msg.pack->readU8();
	info.playerList		= NULL;

	// go ahead and make sure the strings aren't garbage
	if(!isPrintableString(info.gameType) || !isPrintableString(info.missionType))
	{
		if(checkLogLevel(DPRINT_DEBUG))
		{
			debugPrintf(DPRINT_DEBUG, " Invalid response strings issued, ignoring info response.\n");
			if(isPrintableString(info.gameType))
			{
				debugPrintf(DPRINT_DEBUG, " gameType is invalid..\n");
				debugPrintHexDump(info.gameType, strlen(info.gameType));
			}
			if(isPrintableString(info.missionType))
			{
				debugPrintf(DPRINT_DEBUG, " missionType is invalid..\n");
				debugPrintHexDump(info.gameType, strlen(info.missionType));
			}
		}
		return false; // strings are garbage and packet is malformed
	}

	// check packet parser status
	if(!msg.pack->getStatus())
		return false; // packet was malformed

	// Torque doesn't normally send player GUIDs, while Tribes 2 did, so account for it
	if(info.playerCount && ((msg.pack->getLength() / sizeof(U32)) >= info.playerCount))
	{
		info.playerList = new U32[info.playerCount];
		msg.pack->readBytes(info.playerList, info.playerCount * sizeof(U32));
	}

//	// check packet parser status
//	if(!msg.pack->getStatus())
//		return false; // packet was malformed

	// Ok, all done! - store
	gm_pStore->UpdateServer(msg.addr, &info);

	// received packet OK
	return true;
}

/**
 * @brief Deal with heartbeat packets.
 */
bool handleHeartbeat(tMessageSession &msg)
{
	U32 session;
	U16 key;
	
	///	No Format of request after header.
	
	// get session and key from server store manager
	gm_pStore->HeartbeatServer(msg.addr, &session, &key);

	// The response to a heartbeat (in addition) is to request info from the server.
	Packet *reply  = new Packet(64); // Is 64 a good size?

	// Prep and send packet
	reply->writeHeader(GameMasterInfoRequest, 0, session, key);
	gm_pTransport->sendPacket(reply, msg.addr);

	delete reply;

	// received packet OK
	return true;
}


/**
 * @brief Send a response to a TypesRequest.
 */
void sendTypesResponse(tMessageSession &msg)
{
	U32 i, num, limit, gameCount, missionCount;
	tcUniqueString::iterator it;


	// create the reply packet
	Packet *reply = new Packet(MAX_PACKET_SIZE);
	reply->writeHeader(MasterServerGameTypesResponse, 0, msg.header->session, msg.header->key);

	/*
	
	This is a leftover and forgotten feature of Tribes 2 where the master server
	would send the client the unique list of game types and mission types that
	were available for filtering.

	Format of response after header:

	U8		gameTypesCount;
	char	gameTypes[gameTypesCount];

	U8		missionTypesCount;
	char	missionTypes[missionTypesCount];

	*/

	// TODO: this all really should belong in the ServerStore handler and when
	// collective types size exceeds allowed packet size then the results should
	// then be cached until either of the types' entries are changed. --TRON

	/// figure out the best way to send the types

	// test to see if all both game and mission types will fit into a single packet
	gameCount		= msg.store->m_GameTypes.Count();
	missionCount	= msg.store->m_MissionTypes.Count();
	
	num  = msg.store->m_GameTypes.TotalSize()    + gameCount;
	num += msg.store->m_MissionTypes.TotalSize() + missionCount;
	num += 2; // one byte per type count

	if((num > PACKET_PAYLOAD_SIZE) || (gameCount > 0xFF) || (missionCount > 0xFF))
	{
		// can't send all game and mission types, will need to divide space into
		// two. Iterate through the entire types and stop once reaching the limit.

		// set byte limit per type list
		limit = PACKET_PAYLOAD_SIZE / 2;

		// zero the type counts
		gameCount		= 0;
		missionCount	= 0;

		// figure out the count for game type
		for(num = 0,
			it  = msg.store->m_GameTypes.m_List.begin();
			it != msg.store->m_GameTypes.m_List.end(); it++)
		{
			// stop when limit reached
			if(num + it->length + 1 > limit)
				break;

			// update working size
			num += it->length + 1;
			gameCount++;
		}

		// figure out the count for mission type
		for(num = 0,
			it  = msg.store->m_MissionTypes.m_List.begin();
			it != msg.store->m_MissionTypes.m_List.end(); it++)
		{
			// stop when limit reached
			if(num + it->length + 1 > limit)
				break;

			// update working size
			num += it->length + 1;
			missionCount++;
		}
	}
	
	/// send game types
	reply->writeU8(gameCount);
	for(i=0,
		it  = msg.store->m_GameTypes.m_List.begin();
		it != msg.store->m_GameTypes.m_List.end() && (i < gameCount);
		it++, i++)
	{
		reply->writeCString(it->str);
	}

	/// send mission types
	reply->writeU8(missionCount);
	for(i=0,
		it  = msg.store->m_MissionTypes.m_List.begin();
		it != msg.store->m_MissionTypes.m_List.end() && (i < missionCount);
		it++, i++)
	{
		reply->writeCString(it->str);
	}

	// send response
	gm_pTransport->sendPacket(reply, msg.addr);
	delete reply;
}

/**
 * @brief Send a response to a InfoRequest.
 */
void sendInfoResponse(tMessageSession &msg)
{
	Packet *reply = new Packet(MAX_PACKET_SIZE);

	/// Custom message identifier by MikeK, it has no standard format.
	
	reply->writeHeader(MasterServerInfoResponse, 0, msg.header->session, msg.header->key);

	// Send MServer Name
	reply->writeCString(gm_pConfig->name, strlen(gm_pConfig->name));

	// Send MServer Region
	reply->writeCString(gm_pConfig->region, strlen(gm_pConfig->region));

	// Send Number Servers
	reply->writeU16(msg.store->getCount());

	// Send that, too
	gm_pTransport->sendPacket(reply, msg.addr);

	delete reply;
}


/**
 *
 * On the sending of list packets:
 *
 * <ul>
 *	<li> We have 1500 bytes usable for the packet. (XXX: Are 1500b packets really reliable?)
 *	<li>We budget the first 12 bytes for header, leaving us 1488. It takes us 6 bytes/server.
 *	<li> We can send 248 servers/packet, then.
 * </ul>
 *
 * 	We never want to send more than 254 packets
 *  Because 255 == FF == the initial request indicator
 *
 *	So we can never give more than 62992 servers as a result. Note that this is quite
 *	a lot of servers; if we plan on more, then we can just make the packet index a
 *	U16 and issue an upgrade.
 *
 *	We customize all this behaviour with defines.
 */
void sendListResponse(tMessageSession &msg, U16 index)
{
	Packet			*reply = new Packet(LIST_PACKET_SIZE);

	/*
	
	Format of response after header:

	U8			packetIndex;
	U8			packetTotal;
	U16			serverCount;
	struct MasterServerListResponse { // old style
		struct {
			U32		address;
			U16		port;
		}			servers[serverCount];
	}
	struct MasterServerExtendedListResponse { // new style
		U8 addressType;
		union
		{
			struct ipv4 // if addressType == 0
			{
				U32		address;
				U16		port;
			}
			struct ipv6 // if addressType == 1
			{
				U8		address[16];
				U16		port;
			}
		}
	}			servers[serverCount];
	

	*/

	// first thing is to make sure requested index is within server results range
	if(index >= msg.session->packTotal)
		return; // abort, invalid packet index

	ServerResultPacket &resultPacket = msg.session->results[index];

	// write packet header and the list details
	U8 responseType = msg.session->sessionFlags & Session::NewStyleResponse ? 1 : 0;
	if (responseType == 0)
	{
		//printf(" Sending normal response (%i packets, %i total results).\n", msg.session->packTotal, msg.session->total);
		reply->writeHeader(MasterServerListResponse, msg.session->sessionFlags, msg.header->session, msg.header->key);
		reply->writeU8(index);						// packet index
		reply->writeU8(msg.session->packTotal);		// total packets
		reply->writeBytes(resultPacket.data, resultPacket.size);
	}
	else if (responseType == 1)
	{
		//printf(" Sending new response (%i packets, %i total results, %i size).\n", msg.session->packTotal, msg.session->total, resultPacket.size);
		reply->writeHeader(MasterServerExtendedListResponse, msg.session->sessionFlags, msg.header->session, msg.header->key);
		reply->writeU8(index);						// packet index
		reply->writeU8(msg.session->packTotal);		// total packets
		reply->writeBytes(resultPacket.data, resultPacket.size);
	}

	// All done, send.
	gm_pTransport->sendPacket(reply, msg.addr);
	delete reply;

	// done
}

