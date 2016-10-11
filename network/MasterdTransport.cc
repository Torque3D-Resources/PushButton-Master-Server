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
#include "internal.h"
#include "MasterdTransport.h"
#include "masterd.h"
#include "netSocket.h"


/**
 * @brief Constructor for the transport.
 *
 * Initializes the transport on the given host/port.
 *
 * @param listenAddresses	A list of addresses to bind to.
 */
MasterdTransport::MasterdTransport( std::vector<netAddress> &listenAddresses )
{
	int result;
	char buffer[256];
	
	
	pfdCount = 0;
	sockOK   = false;

	memset(sock, '\0', sizeof(sock));
	memset(pfdArray, '\0', sizeof(pfdArray));
	
	for (int i=0; i<listenAddresses.size(); i++)
	{
		if (pfdCount >= MAX_LISTEN_SOCKETS)
		{
			debugPrintf(DPRINT_ERROR, " - Can't bind to %s, ran out of sockets\n", listenAddresses[i].toString(buffer));
			break;
		}

		netSocket *socket = new netSocket();
		socket->open(listenAddresses[i].getFamily() == AF_INET6, false);
		result = socket->bind(&listenAddresses[i]);
		if(result < 0)
		{
			// we failed to bind, usual causes are address and/or port already in use
			debugPrintf(DPRINT_ERROR, " - Failed to bind to %s, error: [%d] %s\n",
						listenAddresses[i].toString(buffer), errno, strerror(errno));
			delete socket;
			sock[pfdCount] = NULL;
			continue;
		}

		debugPrintf(DPRINT_INFO, " - Binding master server to %s\n", listenAddresses[i].toString(buffer));
		sock[pfdCount] = socket;

		// prepare the poll file descriptor test array
		pfdArray[pfdCount].fd     = socket->getHandle();	// assign our socket to polling array entity
		pfdArray[pfdCount].events = POLLIN;						// we want only the recvfrom() ready event
		
		pfdCount++;
	}

	// we're done and ready for use
	sockOK = pfdCount > 0;
}


/**
 * @brief Shut things down.
 *
 * Closes socket, cleans up any other resources.
 */
MasterdTransport::~MasterdTransport()
{
	for (int i=0; i<MAX_LISTEN_SOCKETS; i++)
	{
		if (sock[i])
			delete sock[i];
	}
}

bool MasterdTransport::checkSockets(Packet **data, ServerAddress **from)
{
	char buff[2500];
	netAddress from_x;
	int len;

	for (int i=0; i<pfdCount; i++)
	{
		if(pfdArray[i].revents & POLLIN)
		{
			
			// Read in a packet.... if we have one.
			if(
				(len=sock[i]->recvfrom(buff, 2500, 0, &from_x))
				> 0)
			{
				*from = new ServerAddress(&from_x, i);
				*data = new Packet(buff, len);

				pfdArray[i].revents = 0;

				return true;
			}
		}
	}

	return false;
}

/**
 * @brief Poll for packets.
 *
 * This is the heart of the transport, as it allows you to fetch
 * packets from the incoming queue. The recommended use is to
 * spin on a bit, and sleep  or do other things for a few milliseconds
 * if you don't have any packets pending.
 *
 * This assumes packets are less than 2500 bytes. Beware!
 *
 * @param	data	Pointer to a packet pointer; makes pointer store a
  					new packet (which you will need to clean up). Sets
  					this to null if we didn't have anything.
 * @param	from	Pointer to a ServerAddress pointer, makes pointer
					store a new server address (which you will also need
 					to clean up). Sets this to null if we didn't have
 					anything.
 * @return	true if it returned packet data, otherwise false.
 */
bool MasterdTransport::poll(Packet **data, ServerAddress **from, int timeout)
{
	// Check for packets
	int result;

	// since function won't return false because of recvfrom() this
	// next section of code will use UNIX poll() to know when the
	// socket actually has anything. --TRON

	if (checkSockets(data, from))
		return true;

	// perform the actual polling with a blocking timeout
	result = ::poll(&pfdArray[0], pfdCount, timeout);
	if (result > 0)
	{
		if (checkSockets(data, from))
			return true;
	}

	*from = NULL;
	*data = NULL;

	// Nada.
	return false;
}

/**
 * @brief Send a packet through this transport.
 *
 * @param	data	Packet containing data to send.
 * @param	to		Address to which to send this data.
 */
void MasterdTransport::sendPacket(Packet *data, ServerAddress *to)
{
	char buffer[256];
	netAddress naddr(to);
	assert(to->socket >= 0 && to->socket < MAX_LISTEN_SOCKETS);
	//printf("sendPacket to socket %i (%s)\n", to->socket, naddr.toString(buffer));
	sock[to->socket]->sendto(data->getBufferPtr(), (int)data->getLength(), 0, &naddr);
}

