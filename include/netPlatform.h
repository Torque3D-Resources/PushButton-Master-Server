#ifndef _NETPLATFORM_H_
#define _NETPLATFORM_H_

#if defined(UL_CYGWIN) || !defined (UL_WIN32)

#if defined(UL_MAC_OSX)
#  include <netinet/in.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>    /* Need both for Mandrake 8.0!! */
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>

#else

#include <ws2tcpip.h>
#include <stdarg.h>

#endif

#if defined(UL_MSVC) && !defined(socklen_t)
#define socklen_t int
#endif

/* Paul Wiltsey says we need this for Solaris 2.8 */
 
#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned long)-1)
#endif

#include <errno.h>

#endif