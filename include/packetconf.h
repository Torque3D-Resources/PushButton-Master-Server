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
/**
 * Define characteristics of outgoing packets.
 */
#ifndef _PACKETCONF_H_
#define _PACKETCONF_H_

/**
 * @brief Maximum size of a packet.
 *
 * DSL w/ PPPoE MTU is typically 1492, but that includes the IP headers,
 * therefore I changed this from 1500 to 1400 so we avoid fragmentation,
 * and as for as cable/FIOS is concerned they don't have as severe MTU
 * sizes as DSL w/ PPPoE has. --TRON
 */
#define UDP_HEADER_SIZE			48
#define MAX_MTU_SIZE			1492
#define MAX_PACKET_SIZE			(MAX_MTU_SIZE - UDP_HEADER_SIZE)

/**
 * @brief Maximum size of a list packet.
 *
 */
#define LIST_PACKET_SIZE		MAX_PACKET_SIZE

// packet's header size in bytes (note: this is for the worst case new packet style)
#define PACKET_HEADER_SIZE  8

/**
 * @brief Size of packet header (protocol-dependent)
 */
#define LIST_PACKET_HEADER		(PACKET_HEADER_SIZE + 10)

// space remaining after packet header
#define PACKET_PAYLOAD_SIZE	(MAX_PACKET_SIZE - PACKET_HEADER_SIZE)

#endif

