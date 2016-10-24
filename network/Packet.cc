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
#include "Packet.h"
#include "masterd.h"

/**
 * @brief Writable packet constructor
 *
 * @param	size	Size of the packet. (in bytes)
 */
Packet::Packet(size_t size)
{
	// Make a new packet with size bytes allocated
	readOnly = false;
	statusOK = true;
	buff = new char[size];
	ptr = buff;

	this->size = size;
}

/**
 * @brief Read-only packet constructor
 *
 * @param	len		Length of the packet.
 * @param	aBuff	Character buffer holding packet data.
 * @todo Make sure that we check bounds?
 */
Packet::Packet( char * aBuff, size_t length)
{
	// Prepare to parse a received packet
	buff = new char[length];
	memcpy(buff, aBuff, length);
	ptr = buff;
	readOnly = true;
	statusOK = true;
	size = length;
}

Packet::~Packet()
{
	// Clean up.
	delete[] buff;
}


bool Packet::writeBytes(const void *data, size_t length)
{
	if(readOnly)
	{
		printf("Attempted to write to a readonly packet!\n");
		return false;
	}

	if(!length)
		return true;
	
	// verify we have enough buffer space to write to
	if((getLength() + length) > size)
	{
		// not enough bytes remain, abort
		statusOK = false;
		return false;
	}

	// write out data
	memcpy(ptr, data, length);

	// update write pointer
	ptr += length;

	// done
	return true;
}

bool Packet::readBytes(void *data, size_t length)
{
	if(!readOnly)
	{
		printf("Attempted to read from a writeonly packet!\n");
		return false;
	}

	if(!length)
		return true;
	
	// verify we have enough data to read
	if((getLength() + length) > size)
	{
		// not enough bytes remain, abort
		statusOK = false;
		return false;
	}

	// read in data
	memcpy(data, ptr, length);

	// update read pointer
	ptr += length;

	// done
	return true;
}


void Packet::writeU8(U8 b)
{
//	printf("\t\tWrote a %d...\n", b);
	if(readOnly)
	{
		printf("Attempted to write to a readonly packet!\n");
		return;
	}

	// verify we have room for another byte
	if(getLength() < size)
		*(ptr++) = b;
	else
		statusOK = false;
}

U8 Packet::readU8()
{
//	printf("\t\tRead a %d...\n", *ptr);

	if(!readOnly)
	{
		printf("Attempted to read from a writeonly packet!\n");
		return 0;
	}

	// verify we have another byte to read
	if(getLength() < size)
		return *(ptr++);

	statusOK = false;
	return 0;
}

char* Packet::readCString()
{
	size_t length;
	char *str;

	// read string length
	length = readU8();

	// allocate a new string plus a NULL char
	str = new char[length +1];
	str[0] = 0;
	str[length] = 0;

	// read string into our new string
	readBytes(str, length);

	// return new string
	return str;
}

void Packet::writeCString(const char *str, size_t length)
{
	if(length > 0xFF)
	{
		// We can't write more than 255 bytes of characters in a string to the
		// packet due to the 8bit character array length field size.
		debugPrintf(DPRINT_WARN, "Truncated write of a string longer than 255 to a packet\n");
		length = 0xFF;
	}

	writeU8(length);			// string length, excluding NULL
	writeBytes(str, length);	// string characters, excluding NULL	
}

/**
 * @brief Write standard protocol header to the packet.
 */
void Packet::writeHeader(U8 type, U8 flags, U32 session, U16 key)
{
	writeU8( type);		// packet type
	writeU8( flags);	// flags

	if (flags & Session::AuthenticatedSession)
	{
		//printf("WROTE AUTHENTICATED HEADER %u\n", flags);
		writeU32(session);
	}
	else
	{
		//printf("WROTE AUTHENTICATED HEADER %u\n", flags);
		writeU16(session);	// session
		writeU16(key);		// key
	}
}

/**
 * @brief Read standard protocol header to the packet.
 */
void Packet::readHeader(U8 &type, U8 &flags, U32 &session, U16 &key)
{
	type	= readU8();		// packet type
	flags	= readU8();		// flags

	if (flags & Session::AuthenticatedSession)	// session
	{
		session = readU32();
		key = 0;
	}
	else
	{
		session = readU16();
		key	  = readU16();	// key
	}
}


/**
 * @brief Return pointer to copy of packet's internal buffer.
 *
 * This is used to get the data out of the packet so that we can
 * actually transmit it somewhere, for instance.
 *
 * It returns a copy of the data, so be sure to free it when you're
 * done.
 *
 * @return pointer to copy of packet's internal buffer. (a char*)
 */
char * Packet::getBufferCopy()
{
	// Copy buffer and return
	char * buff_copy = new char[getLength()];
	memcpy(buff_copy, this->buff, getLength());
	return buff_copy;
}

