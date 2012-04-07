#pragma once
/*
 *      Copyright (C) 2005-2012 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef PVRCLIENT_FORTHERECORD_OS_POSIX_H
#define PVRCLIENT_FORTHERECORD_OS_POSIX_H

// Success codes
#define S_OK                             0L
#define S_FALSE                          1L
#define _FILE_OFFSET_BITS 64

#include "PlatformInclude.h"
#include "limits.h"
#include "File.h"
#include <sys/time.h>

//
// Error codes
#define ERROR_FILENAME_EXCED_RANGE       206L
//#define E_OUTOFMEMORY                    0x8007000EL
//#define E_FAIL                           0x8004005EL
#define ERROR_INVALID_NAME               123L

#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)

#ifdef TARGET_LINUX
#include <limits.h>
#define MAX_PATH PATH_MAX
#else
#define MAX_PATH 256
#endif

/* Platform dependent path separator */
#define PATH_SEPARATOR_CHAR '/'
#define PATH_SEPARATOR_STRING "/"

#include <string.h>
//#define strnicmp(X,Y,N) strncasecmp(X,Y,N)
#define _strcmpi strcasecmp
using namespace XFILE;

inline size_t WcsLen(const wchar_t *str)
{
  const unsigned short *eos = (const unsigned short*)str;
  while( *eos++ ) ;
  return( (size_t)(eos - (const unsigned short*)str) -1);
};

inline size_t WcsToMbs(char *s, const wchar_t *w, size_t n)
{
  size_t i = 0;
  const unsigned short *wc = (const unsigned short*) w;
  while(wc[i] && (i < n))
  {
    s[i] = wc[i];
    ++i;
  }
  if (i < n) s[i] = '\0';

  return (i);
};

#endif
