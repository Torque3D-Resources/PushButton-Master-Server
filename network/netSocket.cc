/*
     PLIB - A Suite of Portable Game Libraries
     Copyright (C) 1998,2002  Steve Baker
 
     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Library General Public
     License as published by the Free Software Foundation; either
     version 2 of the License, or (at your option) any later version.
 
     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Library General Public License for more details.
 
     You should have received a copy of the GNU Library General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 
     For further information visit http://plib.sourceforge.net

     $Id: netSocket.cxx,v 1.30 2004/04/06 12:42:40 sjbaker Exp $
*/

#include "netSocket.h"
#include "ServerStore.h"

netAddress::netAddress ( const sockaddr_storage *inData )
{
  memcpy(&data, inData, sizeof(sockaddr_storage));
}

netAddress::netAddress ( const ServerAddress *inData )
{
  set(inData);
}
netAddress::netAddress ( int address, int port )
{
  set ( address, port ) ;
}


void netAddress::set ( const ServerAddress *info )
{
  memset(this, 0, sizeof(netAddress));

  if (info->type == ServerAddress::IPAddress)
  {
    sockaddr_in *addr = (sockaddr_in*)&data;
    addr->sin_family = AF_INET;
    addr->sin_port = htons (info->port);

    uint8_t *addrNum = (uint8_t*)&addr->sin_addr;
    addrNum[0] = info->address.ipv4.netNum[0];
    addrNum[1] = info->address.ipv4.netNum[1];
    addrNum[2] = info->address.ipv4.netNum[2];
    addrNum[4] = info->address.ipv4.netNum[3];
  }
  else if (info->type == ServerAddress::IPV6Address)
  {
    sockaddr_in6 *addr = (sockaddr_in6*)&data;
    addr->sin6_family = AF_INET6;
    addr->sin6_port = htons (info->port);
    addr->sin6_flowinfo = info->address.ipv6.netFlow;
    addr->sin6_scope_id = info->address.ipv6.netScope;
    memcpy(&addr->sin6_addr, info->address.ipv6.netNum, sizeof(info->address.ipv6.netNum));
  }
}

void netAddress::set ( const char *inStr, bool hostLookup )
{
  struct addrinfo hint, *res = NULL;
  memset(this, 0, sizeof(netAddress));
  memset(&hint, 0, sizeof(hint));

  hint.ai_family = AF_UNSPEC;
  hint.ai_flags = hostLookup ? 0 : AI_NUMERICHOST;

  if (getaddrinfo(inStr, NULL, &hint, &res) == 0)
  {
    if (res->ai_family == AF_INET)
      memcpy(&data, res->ai_addr, sizeof(sockaddr_in));
    else if (res->ai_family == AF_INET6)
      memcpy(&data, res->ai_addr, sizeof(sockaddr_in6));
  }
}

void netAddress::setPort ( int port )
{
  sockaddr_in *socketAddr = (sockaddr_in*)&data;
  sockaddr_in6 *socketAddr6 = (sockaddr_in6*)&data;

  if (socketAddr->sin_family == AF_INET)
    socketAddr->sin_port = htons(port);
  else if (socketAddr->sin_family == AF_INET6)
    socketAddr6->sin6_port = htons(port);
}

/* Create a string object representing an IP address.
   This is always a string of the form 'dd.dd.dd.dd' (with variable
   size numbers). */

void netAddress::getHost (char outStr[256]) const
{
  outStr[0] = '\0';
  inet_ntop(data.ss_family, &data, outStr, 256);
}


int netAddress::getAddress() const
{
  return sin_addr; //ntohl(sin_addr);
}

int netAddress::getPort() const
{
  const sockaddr_in *socketAddr = (const sockaddr_in*)&data;
  const sockaddr_in6 *socketAddr6 = (const sockaddr_in6*)&data;

  if (socketAddr->sin_family == AF_INET)
    return ntohs(socketAddr->sin_port);
  else if (socketAddr->sin_family == AF_INET6)
    return ntohs(socketAddr6->sin6_port);
  else
    return 0;
}

bool netAddress::getLocalHost (int family, netAddress *out)
{
  // TODO: getaddrinfo
  return false;
}

bool netAddress::getBroadcast () const
{
  const sockaddr_in *socketAddr = (const sockaddr_in*)&data;
  const uint32_t broadcastIP = INADDR_BROADCAST;
  return socketAddr->sin_family == AF_INET && memcmp(&socketAddr->sin_addr, &broadcastIP, sizeof(broadcastIP)) == 0;
}

int netAddress::getFamily () const
{
  const sockaddr_in *socketAddr = (const sockaddr_in*)&data;
  return socketAddr->sin_family;
}

int netAddress::getDataSize() const
{
  return data.ss_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
}

const void* netAddress::getData() const
{
  return &data;
}

void netAddress::toString(char outStr[256]) const
{
  if (data.ss_family == AF_INET)
  {
    const sockaddr_in *socketAddr = (const sockaddr_in*)&data;
    char buffer[128];
    inet_ntop(AF_INET, (sockaddr*)(&data), buffer, sizeof(buffer));

    if (socketAddr->sin_port == 0)
      snprintf(outStr, 256, "%s", buffer);
    else
      snprintf(outStr, 256, "%s:%i", buffer, ntohs(socketAddr->sin_port));
  }
  else if (data.ss_family == AF_INET6)
  {
    const sockaddr_in6 *socketAddr6 = (const sockaddr_in6*)&data;
    char buffer[128];
    inet_ntop(AF_INET6, (sockaddr*)(&data), buffer, sizeof(buffer));

    if (socketAddr6->sin6_port == 0)
      snprintf(outStr, 256, "%s", buffer);
    else
      snprintf(outStr, 256, "[%s]:%i", buffer, ntohs(socketAddr6->sin6_port));
  }
  else
  {
    outStr[0] = '\0';
  }
}

netSocket::netSocket ()
{
  handle = -1 ;
}


netSocket::~netSocket ()
{
  close () ;
}


void netSocket::setHandle (int _handle)
{
  close () ;
  handle = _handle ;
}


bool netSocket::open ( bool stream )
{
  close () ;
  handle = ::socket ( AF_INET, (stream? SOCK_STREAM: SOCK_DGRAM), 0 ) ;
  return (handle != -1);
}


void netSocket::setBlocking ( bool blocking )
{
  assert ( handle != -1 ) ;

#if defined(UL_CYGWIN) || !defined (UL_WIN32)

  int delay_flag = ::fcntl (handle, F_GETFL, 0);

  if (blocking)
    delay_flag &= (~O_NDELAY);
  else
    delay_flag |= O_NDELAY;

  ::fcntl (handle, F_SETFL, delay_flag);

#else

  u_long nblocking = blocking? 0: 1;
  ::ioctlsocket(handle, FIONBIO, &nblocking);

#endif
}


void netSocket::setBroadcast ( bool broadcast )
{
  assert ( handle != -1 ) ;
  int result;
  if ( broadcast ) {
      int one = 1;
#ifdef UL_WIN32
      result = ::setsockopt( handle, SOL_SOCKET, SO_BROADCAST, (char*)&one, sizeof(one) );
#else
      result = ::setsockopt( handle, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one) );
#endif
  } else {
      result = ::setsockopt( handle, SOL_SOCKET, SO_BROADCAST, NULL, 0 );
  }
  if ( result < 0 ) {
      perror("set broadcast:");
  }
  assert ( result != -1 );
}


int netSocket::bind ( const netAddress* bindAddress )
{
  assert ( handle != -1 ) ;
  return ::bind(handle,(const sockaddr*)bindAddress,sizeof(netAddress));
}


int netSocket::listen ( int backlog )
{
  assert ( handle != -1 ) ;
  return ::listen(handle,backlog);
}


int netSocket::accept ( const netAddress* addr )
{
  assert ( handle != -1 ) ;

  if ( addr == NULL )
  {
    return ::accept(handle,NULL,NULL);
  }
  else
  {
    socklen_t addr_len = (socklen_t) sizeof(netAddress) ;
    return ::accept(handle,(sockaddr*)addr,&addr_len);
  }
}


int netSocket::connect ( const netAddress *addr )
{
  assert ( handle != -1 ) ;
  if ( addr->getBroadcast() ) {
      setBroadcast( true );
  }
  return ::connect(handle,(const sockaddr*)(addr->getData()),addr->getDataSize());
}


int netSocket::send (const void * buffer, int size, int flags)
{
  assert ( handle != -1 ) ;
  return ::send (handle, (const char*)buffer, size, flags);
}


int netSocket::sendto ( const void * buffer, int size,
                        int flags, const netAddress* to )
{
  assert ( handle != -1 ) ;
  return ::sendto(handle,(const char*)buffer,size,flags,
                         (const sockaddr*)to,sizeof(netAddress));
}


int netSocket::recv (void * buffer, int size, int flags)
{
  assert ( handle != -1 ) ;
  return ::recv (handle, (char*)buffer, size, flags);
}


int netSocket::recvfrom ( void * buffer, int size,
                          int flags, netAddress* from )
{
  assert ( handle != -1 ) ;
  socklen_t fromlen = (socklen_t) sizeof(netAddress) ;
  return ::recvfrom(handle,(char*)buffer,size,flags,(sockaddr*)from,&fromlen);
}


void netSocket::close (void)
{
  if ( handle != -1 )
  {
#if defined(UL_CYGWIN) || !defined (UL_WIN32)
    ::close( handle );
#else
    ::closesocket( handle );
#endif
    handle = -1 ;
  }
}


bool netSocket::isNonBlockingError ()
{
#if defined(UL_CYGWIN) || !defined (UL_WIN32)
  switch (errno) {
  case EWOULDBLOCK: // always == NET_EAGAIN?
  case EALREADY:
  case EINPROGRESS:
    return true;
  }
  return false;
#else
  int wsa_errno = WSAGetLastError();
  if ( wsa_errno != 0 )
  {
    WSASetLastError(0);
    ulSetError(UL_WARNING,"WSAGetLastError() => %d",wsa_errno);
    switch (wsa_errno) {
    case WSAEWOULDBLOCK: // always == NET_EAGAIN?
    case WSAEALREADY:
    case WSAEINPROGRESS:
      return true;
    }
  }
  return false;
#endif
}


int netSocket::select ( netSocket** reads, netSocket** writes, int timeout )
{
  fd_set r,w;
  
  FD_ZERO (&r);
  FD_ZERO (&w);

  int i, k ;
  int num = 0 ;

  for ( i=0; reads[i]; i++ )
  {
    int fd = reads[i]->getHandle();
    FD_SET (fd, &r);
    num++;
  }

  for ( i=0; writes[i]; i++ )
  {
    int fd = writes[i]->getHandle();
    FD_SET (fd, &w);
    num++;
  }

  if (!num)
    return num ;

  /* Set up the timeout */
  struct timeval tv ;
  tv.tv_sec = timeout/1000;
  tv.tv_usec = (timeout%1000)*1000;

  // It bothers me that select()'s first argument does not appear to
  // work as advertised... [it hangs like this if called with
  // anything less than FD_SETSIZE, which seems wasteful?]
  
  // Note: we ignore the 'exception' fd_set - I have never had a
  // need to use it.  The name is somewhat misleading - the only
  // thing I have ever seen it used for is to detect urgent data -
  // which is an unportable feature anyway.

  ::select (FD_SETSIZE, &r, &w, 0, &tv);

  //remove sockets that had no activity

  num = 0 ;

  for ( k=i=0; reads[i]; i++ )
  {
    int fd = reads[i]->getHandle();
    if (FD_ISSET (fd, &r)) {
      reads[k++] = reads[i];
      num++;
    }
  }
  reads[k] = NULL ;

  for ( k=i=0; writes[i]; i++ )
  {
    int fd = writes[i]->getHandle();
    if (FD_ISSET (fd, &w)) {
      writes[k++] = writes[i];
      num++;
    }
  }
  writes[k] = NULL ;

  return num ;
}


/* Init/Exit functions */

static void netExit ( void )
{
#if defined(UL_CYGWIN) || !defined (UL_WIN32)
#else
	/* Clean up windows networking */
	if ( WSACleanup() == SOCKET_ERROR ) {
		if ( WSAGetLastError() == WSAEINPROGRESS ) {
			WSACancelBlockingCall();
			WSACleanup();
		}
	}
#endif
}


int netInit ( int* argc, char** argv )
{
  /* Legacy */

  return netInit () ;
}


int netInit ()
{
  assert ( sizeof(sockaddr_in) == sizeof(netAddress) ) ;

#if defined(UL_CYGWIN) || !defined (UL_WIN32)
#else
	/* Start up the windows networking */
	WORD version_wanted = MAKEWORD(1,1);
	WSADATA wsaData;

	if ( WSAStartup(version_wanted, &wsaData) != 0 ) {
		ulSetError(UL_WARNING,"Couldn't initialize Winsock 1.1");
		return(-1);
	}
#endif

  atexit( netExit ) ;
	return(0);
}


const char* netFormat ( const char* format, ... )
{
  static char buffer[ 256 ];
  va_list argptr;
  va_start(argptr, format);
  vsprintf( buffer, format, argptr );
  va_end(argptr);
  return( buffer );
}


