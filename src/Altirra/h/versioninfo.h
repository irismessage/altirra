//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_VERSIONINFO_H
#define f_AT_VERSIONINFO_H

#include "version.h"

#define AT_WIDESTR1(x) L##x
#define AT_WIDESTR(x) AT_WIDESTR1(x)

#ifdef _DEBUG
	#define AT_VERSION_DEBUG_STR L"-debug"
#elif defined(ATNRELEASE)
	#define AT_VERSION_DEBUG_STR L"-profile"
#else
	#define AT_VERSION_DEBUG_STR L""
#endif

#define AT_VERSION_STR AT_WIDESTR(AT_VERSION)

#define AT_PROGRAM_NAME_STR_ATASCII		"Altirra"
#define AT_PROGRAM_NAME_STR				L"Altirra"

#if defined(VD_CPU_ARM64)
	#define AT_PROGRAM_PLATFORM_STR L"/ARM64"
#elif defined(VD_CPU_AMD64)
	#define AT_PROGRAM_PLATFORM_STR L"/x64"
#else
	#define AT_PROGRAM_PLATFORM_STR L""
#endif

#if AT_VERSION_PRERELEASE
	#define	AT_VERSION_PRERELEASE_STR L" [prerelease]"
#else
	#define	AT_VERSION_PRERELEASE_STR
#endif

#define AT_FULL_VERSION_STR AT_PROGRAM_NAME_STR AT_PROGRAM_PLATFORM_STR L" " AT_VERSION_STR AT_VERSION_DEBUG_STR AT_VERSION_PRERELEASE_STR

#define AT_PRIMARY_URL		L"https://www.virtualdub.org/altirra.html"
#define AT_DOWNLOAD_URL		L"https://www.virtualdub.org/altirra.html"

#define AT_HTTP_USER_AGENT	(L"Altirra/" AT_VERSION_STR)

#define AT_UPDATE_CHECK_URL_LOCAL_TEST		L"http://127.0.0.1:8000/altirra-update-dev.xml"
#define AT_UPDATE_CHECK_URL_LOCAL_REL		L"http://127.0.0.1:8000/altirra-update-release.xml"
#define AT_UPDATE_CHECK_URL_TEST	L"https://www.virtualdub.org/feeds/altirra-update-dev.xml"
#define AT_UPDATE_CHECK_URL_REL		L"https://www.virtualdub.org/feeds/altirra-update-release.xml"

#if defined(AT_VERSION_DEV) && (defined(_DEBUG) || defined(ATNRELEASE))
	#define AT_UPDATE_CHECK_URL		    AT_UPDATE_CHECK_URL_TEST
	#define AT_UPDATE_CHECK_URL_LOCAL	AT_UPDATE_CHECK_URL_LOCAL_TEST
#elif defined(AT_VERSION_PRERELEASE)
	#define AT_UPDATE_CHECK_URL	    	AT_UPDATE_CHECK_URL_TEST
	#define AT_UPDATE_CHECK_URL_LOCAL	AT_UPDATE_CHECK_URL_LOCAL_TEST
#else
	#define AT_UPDATE_CHECK_URL	    	AT_UPDATE_CHECK_URL_REL
	#define AT_UPDATE_CHECK_URL_LOCAL	AT_UPDATE_CHECK_URL_LOCAL_REL
#endif

#endif
