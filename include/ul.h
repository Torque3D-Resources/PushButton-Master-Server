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

     $Id: ul.h,v 1.69 2004/06/24 16:30:55 sjbaker Exp $
*/

//
//  UL - utility library
//
//  Contains:
//  - necessary system includes
//  - basic types
//  - error message routines
//  - high performance clocks
//  - ulList
//  - ulLinkedList
//  - more to come (endian support, version ID)
//

#ifndef _INCLUDED_UL_H_
#define _INCLUDED_UL_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

/**********************\
*                      *
*  Determine OS type   *
*                      *
\**********************/

#if defined(__CYGWIN__)

#define UL_WIN32     1
#define UL_CYGWIN    1    /* Windoze AND Cygwin. */

#elif defined(_WIN32) || defined(__WIN32__) || defined(_MSC_VER)

#define UL_WIN32     1
#define UL_MSVC      1    /* Windoze AND MSVC. */

#elif defined(__BEOS__)

#define UL_BEOS      1

#elif defined( macintosh )

#define UL_MACINTOSH 1

#elif defined(__APPLE__)

#define UL_MAC_OSX   1 

#elif defined(__linux__)

#define UL_LINUX     1

#elif defined(__sgi)

#define UL_IRIX      1

#elif defined(_AIX)

#define UL_AIX       1

#elif defined(SOLARIS) || defined(sun)

#define UL_SOLARIS   1

#elif defined(hpux)

#define UL_HPUX      1

#elif (defined(__unix__) || defined(unix)) && !defined(USG)

#define UL_BSD       1

#endif


#if defined(BORLANDBUILDER)

#define UL_BB     1

#endif

/*
  Add specialised includes/defines...
*/

#ifdef UL_WIN32
#include <windows.h>
#include <mmsystem.h>
#include <regstr.h>
#define  UL_WGL     1
#endif

#ifdef UL_CYGWIN
#include <unistd.h>
#define  UL_WGL     1
#endif

#ifdef UL_BEOS
#include <be/kernel/image.h>
#define  UL_GLX     1
#endif

#ifdef UL_MACINTOSH
#include <CodeFragments.h>
#include <unistd.h>
#define  UL_AGL     1
#endif

#ifdef UL_MAC_OSX
#include <unistd.h>
#define  UL_CGL     1
#endif

#if defined(UL_LINUX) || defined(UL_BSD) || defined(UL_IRIX) || defined(UL_SOLARIS) || defined(UL_AIX)
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#define  UL_GLX     1
#endif

#if defined(UL_BSD)
#include <sys/param.h>
#define  UL_GLX     1
#endif

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <float.h>
#include <errno.h>

/* PLIB version macros */

#define PLIB_MAJOR_VERSION 1
#define PLIB_MINOR_VERSION 8
#define PLIB_TINY_VERSION  4

#define PLIB_VERSION (PLIB_MAJOR_VERSION*100 \
                     +PLIB_MINOR_VERSION*10 \
                     +PLIB_TINY_VERSION)

/* SGI machines seem to suffer from a lack of FLT_EPSILON so... */

#ifndef FLT_EPSILON
#define FLT_EPSILON 1.19209290e-07f        
#endif

#ifndef DBL_EPSILON
#define DBL_EPSILON 1.19209290e-07f
#endif

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

/* SUNWspro 4.2 and earlier need bool to be defined */

#if defined(__SUNPRO_CC) && __SUNPRO_CC < 0x500
typedef int bool ;
const   int true  = 1 ;
const   int false = 0 ;
#endif


/*
  Basic Types
*/


inline void ignore_result_helper(int __attribute__((unused)) dummy, ...)
{
}
#define IGNORE_RESULT(X) ignore_result_helper(0, (X))

/*
  This is extern C to enable 'configure.in' to
  find it with a C-coded probe.
*/

extern "C" void ulInit () ;

/*
  Error handler.
*/

enum ulSeverity
{
  UL_DEBUG,    // Messages that can safely be ignored.
  UL_WARNING,  // Messages that are important.
  UL_FATAL,    // Errors that we cannot recover from.
  UL_MAX_SEVERITY
} ;


typedef void (*ulErrorCallback) ( enum ulSeverity severity, char* msg ) ;

void            ulSetError         ( enum ulSeverity severity, const char *fmt, ... ) ;
char*           ulGetError         ( void ) ;
void            ulClearError       ( void ) ;
ulErrorCallback ulGetErrorCallback ( void ) ;
void            ulSetErrorCallback ( ulErrorCallback cb ) ;

//lint -restore

#endif

