//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2011 Avery Lee, All Rights Reserved.
//
//	Beginning with 1.6.0, the VirtualDub system library is licensed
//	differently than the remainder of VirtualDub.  This particular file is
//	thus licensed as follows (the "zlib" license):
//
//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.

#include <stdafx.h>
#include <vd2/system/date.h>
#include <vd2/system/w32assist.h>
#include <windows.h>

static_assert(+VDDateInterval{0} == VDDateInterval{0});
static_assert(+VDDateInterval{1} == VDDateInterval{1});
static_assert(-VDDateInterval{0} == VDDateInterval{0});
static_assert(-VDDateInterval{1} == VDDateInterval{-1});

static_assert(VDDateInterval{0}.Abs().mDeltaTicks == 0);
static_assert(VDDateInterval{1}.Abs().mDeltaTicks == 1);
static_assert(VDDateInterval{-1}.Abs().mDeltaTicks == 1);

static_assert(VDDateInterval{ 1} != VDDateInterval{0});
static_assert(VDDateInterval{ 0} == VDDateInterval{0});
static_assert(VDDateInterval{ 0} >= VDDateInterval{0});
static_assert(VDDateInterval{ 1} >= VDDateInterval{0});
static_assert(VDDateInterval{ 1} >  VDDateInterval{0});
static_assert(VDDateInterval{ 0} <= VDDateInterval{0});
static_assert(VDDateInterval{-1} <= VDDateInterval{0});
static_assert(VDDateInterval{-1} <  VDDateInterval{0});

static_assert(!(VDDateInterval{ 0} != VDDateInterval{0}));
static_assert(!(VDDateInterval{ 1} == VDDateInterval{0}));
static_assert(!(VDDateInterval{-1} >= VDDateInterval{0}));
static_assert(!(VDDateInterval{ 0} >  VDDateInterval{0}));
static_assert(!(VDDateInterval{ 1} <= VDDateInterval{0}));
static_assert(!(VDDateInterval{ 0} <  VDDateInterval{0}));

static_assert(VDDateInterval{0}.ToSeconds() == 0.0f);
static_assert(VDDateInterval{10000000}.ToSeconds() == 1.0f);
static_assert(VDDateInterval{-10000000}.ToSeconds() == -1.0f);
static_assert(VDDateInterval::FromSeconds(0).mDeltaTicks == 0);
static_assert(VDDateInterval::FromSeconds(1.0f).mDeltaTicks == 10000000);
static_assert(VDDateInterval::FromSeconds(-1.0f).mDeltaTicks == -10000000);

static_assert(VDDate{0} - VDDate{0} == VDDateInterval{0});
static_assert(VDDate{0} - VDDate{1} == VDDateInterval{-1});
static_assert(VDDate{1} - VDDate{0} == VDDateInterval{1});
static_assert(VDDate{1000} + VDDateInterval{1} == VDDate{1001});
static_assert(VDDateInterval{1} + VDDate{1000} == VDDate{1001});
static_assert(VDDate{1000} - VDDateInterval{1} == VDDate{999});

VDDate VDGetCurrentDate() {
	FILETIME ft;
	::GetSystemTimeAsFileTime(&ft);

	VDDate r;
	r.mTicks = ft.dwLowDateTime + ((uint64)ft.dwHighDateTime << 32);

	return r;
}

sint64 VDGetDateAsTimeT(const VDDate& date) {
	return ((sint64)date.mTicks - 116444736000000000) / 10000000;
}

VDExpandedDate VDGetLocalDate(const VDDate& date) {
	VDExpandedDate r = {0};

	FILETIME ft;
	ft.dwLowDateTime = (uint32)date.mTicks;
	ft.dwHighDateTime = (uint32)(date.mTicks >> 32);

	SYSTEMTIME st;
	SYSTEMTIME lt;
	if (::FileTimeToSystemTime(&ft, &st) &&
		::SystemTimeToTzSpecificLocalTime(NULL, &st, &lt))
	{
		r.mYear = lt.wYear; 
		r.mMonth = (uint8)lt.wMonth; 
		r.mDayOfWeek = (uint8)lt.wDayOfWeek;
		r.mDay = (uint8)lt.wDay;
		r.mHour = (uint8)lt.wHour;
		r.mMinute = (uint8)lt.wMinute;
		r.mSecond = (uint8)lt.wSecond;
		r.mMilliseconds = (uint16)lt.wMilliseconds;
	}

	return r;
}

void VDConvertExpandedDateToNativeW32(SYSTEMTIME& dst, const VDExpandedDate& src) {
	dst.wYear = src.mYear; 
	dst.wMonth = src.mMonth; 
	dst.wDayOfWeek = src.mDayOfWeek;
	dst.wDay = src.mDay;
	dst.wHour = src.mHour;
	dst.wMinute = src.mMinute;
	dst.wSecond = src.mSecond;
	dst.wMilliseconds = src.mMilliseconds;
}

VDDate VDDateFromLocalDate(const VDExpandedDate& date) {
	SYSTEMTIME st;
	VDConvertExpandedDateToNativeW32(st, date);

	SYSTEMTIME ust;
	if (!TzSpecificLocalTimeToSystemTime(NULL, &st, &ust))
		return {};

	FILETIME ft;
	if (!SystemTimeToFileTime(&ust, &ft))
		return {};

	return VDDate { ft.dwLowDateTime + ((uint64)ft.dwHighDateTime << 32) };
}

void VDAppendLocalDateString(VDStringW& dst, const VDExpandedDate& ed) {
	SYSTEMTIME st;

	VDConvertExpandedDateToNativeW32(st, ed);

	int len = ::GetDateFormatW(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP, &st, NULL, NULL, 0);

	if (len > 0) {
		vdfastvector<WCHAR> buf;
		buf.resize(len, 0);

		if (::GetDateFormatW(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP | DATE_SHORTDATE, &st, NULL, buf.data(), len))
			dst += buf.data();
	}
}

void VDAppendLocalTimeString(VDStringW& dst, const VDExpandedDate& ed) {
	SYSTEMTIME st;

	VDConvertExpandedDateToNativeW32(st, ed);

	int len = ::GetTimeFormatW(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP | TIME_NOSECONDS, &st, NULL, NULL, 0);

	if (len > 0) {
		vdfastvector<WCHAR> buf;
		buf.resize(len, 0);

		if (::GetTimeFormatW(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP | TIME_NOSECONDS, &st, NULL, buf.data(), len))
			dst += buf.data();
	}
}
