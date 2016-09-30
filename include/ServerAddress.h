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
#ifndef _SERVERADDRESS_H_
#define _SERVERADDRESS_H_

class netAddress;

/**
 * @brief Simple address representation.
 *
 * This class exists to abstract from the netAddress class
 * in plib. plib's class has a few shortcomings, primarily
 * that getting the address in non-string form is an expensive
 * operation. (You have to have netAddress sprintf it, then you
 * have to sscanf that string.)
 *
 * ServerAddress is a simple solution to this problem.
 */
class ServerAddress
{
public:
	ServerAddress();
	ServerAddress(const ServerAddress *addr);
	ServerAddress(const netAddress *addr);

	void toString( char outstr[256] ) const;
	void set(const netAddress *addr);


	void putInto(netAddress *a);
	void getFrom(const netAddress *a);

	bool equals(const ServerAddress *a);

	enum Type
	{
		IPAddress,
		IPV6Address
	};

	S32 type;
/* for ref

	union
	{
		U32	address;
		U8	addy[4];
	};
	*/

	/**
	 * @brief Quads for addy.
	 */
	 union
	 {
	 	struct {
	 		union
	 		{
	 			U32 address;
	 			U8 netNum[4];
	 		};
	 	} ipv4;

	 	struct {
	 		U8 netNum[16];
	 		U32 netFlow;
	 		U32 netScope;
	 	} ipv6;

	 	struct {
	 		U8 netNum[16];
	 		U8 netFlow[4];
	 		U8 netScope[4];
	 	} ipv6_raw;

	 } address;

	/**
	 * @brief Port for addy.
	 */
	U16	port;
   
    // @note: comparison operator doesn't check netFlow or netScope
    bool operator==(const ServerAddress &other) const
    {
        if(other.type != type)
            return false; // incompatible address types
           
        if(other.port != port)
            return false; // ports don't match
 
        if(type == IPAddress)
        {
            // compare IPv4 address
            return (other.address.ipv4.address == address.ipv4.address);
        }
        else if(type == IPV6Address)
        {
            // compare IPv6 address
            return (memcmp(other.address.ipv6.netNum, address.ipv6.netNum,
                        sizeof(address.ipv6.netNum)) == 0);
        }
       
        // invalid address type
        return false;
    }
};

#include <unordered_map>

namespace std
{
    template<>
    struct hash<ServerAddress>
    {
        std::size_t operator()(const ServerAddress &sa) const
        {
#ifdef __LP64__
            // 64bit platform
           
            if(sa.type == ServerAddress::IPAddress)
            {
                // address + port
                return (std::size_t)sa.address.ipv4.address | ((std::size_t)sa.port << 32);
            }
            else if(sa.type == ServerAddress::IPV6Address)
            {
                // hash address
                const uint64_t *addr = (const uint64_t *)(sa.address.ipv6.netNum);
                return
                    (hash<uint64_t>()(addr[0])
                    ^ (hash<uint64_t>()(addr[1]) << 1) >> 1);
            }
#else // !__LP64__
            // assuming 32bit platform
 
            if(type == ServerAddress::IPAddress)
            {
                // just the address should be good enough
                return sa.address.ipv4.address;
            }
            else if(sa.type == ServerAddress::IPV6Address)
            {
                // attempt at hashing the address
                const uint32_t *addr = static_cast<const uint32_t *>(sa.address.ipv6.netNum);
                return
                    (((hash<uint32_t>()(addr[0])
                    ^ (hash<uint32_t>()(addr[1]) << 1)) >> 1)
                    ^ (hash<uint32_t>()(addr[2]) << 1)  >> 1)
                    ^ (hash<uint32_t>()(addr[3]) << 1);
            }
#endif // !__LP64__
           
            // invalid address type
            return ~(std::size_t)0;
        }
    };
}; // namespace std

#endif

