/*
	(c) Ben Garney <bgarney@pblabs.com> 2002-2003

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
#include "internal.h"
#include "ServerAddress.h"
#include "masterd.h"

/**
 * @brief Blank constructor.
 */
ServerAddress::ServerAddress()
{
	this->port		= 0;
	memset(&address, 0, sizeof(address));
}

/**
 * Copy constructor.
 *
 * @param	a	ServerAddress to construct from.
 */
ServerAddress::ServerAddress( const ServerAddress *a )
{
	this->port		= a->port;
	this->address	= a->address;
}

/**
 * @brief Create ServerAddress from a netAddress.
 *
 * This is a convenience function for use by plib
 * aware portions of the network library. Replace
 * or extend as needed to support other networking
 * systems.
 */
ServerAddress::ServerAddress( const netAddress * a ) {
	getFrom(a);
}

/**
 * @brief Set the host/port of the address.
 *
 * @param	aHost	String specifying host (in a.b.c.d format)
 * @param	aPort	Port number.
 */
void ServerAddress::set( const netAddress *a )
{
	const sockaddr_storage *addr = (sockaddr_storage*)a->getData();

	type = 0;
	this->port		= 0;
	memset(&address, 0, sizeof(address));

	if (addr->ss_family == AF_INET)
	{
		const sockaddr_in *sockAddr = (const sockaddr_in*)addr;
		type = ServerAddress::IPAddress;
		port = ntohs(sockAddr->sin_port);

		uint8_t *addrNum = (uint8_t*)&sockAddr->sin_addr;
		address.ipv4.netNum[0] = addrNum[0];
		address.ipv4.netNum[1] = addrNum[1];
		address.ipv4.netNum[2] = addrNum[2];
		address.ipv4.netNum[3] = addrNum[3];
	}
	else if (addr->ss_family == AF_INET6)
	{

		const sockaddr_in6 *sockAddr = (const sockaddr_in6*)addr;
		type = ServerAddress::IPV6Address;
		port = ntohs(sockAddr->sin6_port);
		address.ipv6.netFlow = sockAddr->sin6_flowinfo ;
		address.ipv6.netScope = sockAddr->sin6_scope_id;
		memcpy(&address.ipv6.netNum, &sockAddr->sin6_addr, sizeof(address.ipv6.netNum));
	}
}

/**
 * @brief Get a string representing the IPv4 address.
 *
 * Format is "a.b.c.d".
 *
 * @param[out] buff String buffer with at least 16 bytes capacity.
 * @return provided string buffer containing the address.
 */
void ServerAddress::toString( char outStr[256] ) const
{
	netAddress addr(this);
	addr.toString(outStr);
}

/**
 * @brief Put the address information into a netAddress
 *
 * @param	a	A netAddress to store into.
 */
void ServerAddress::putInto( netAddress * a )
{
	a->set(this);
}

/**
 * @brief Load addy info from a netAddress
 *
 * @param	a	A netAddress to get info from.
 */
void ServerAddress::getFrom( const netAddress * a )
{
	set(a);
}


/**
 * @brief Check if this ServerAddress equals another.
 *
 * @return True on equality.
 * @param	a	ServerAddress to compare with.
 */
bool ServerAddress::equals(const ServerAddress * a)
{
	if (type != a->type)
		return false;

	switch (type)
	{
		case ServerAddress::IPAddress:
			return port == a->port && (memcmp(a->address.ipv4.netNum, address.ipv4.netNum, 4) == 0);
		break;
		case ServerAddress::IPV6Address:
			return port == a->port && (memcmp(a->address.ipv6.netNum, address.ipv6.netNum, 16) == 0);
		break;
	}

	return false;
}

