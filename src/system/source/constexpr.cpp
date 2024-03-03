//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2023 Avery Lee, All Rights Reserved.
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
#include <vd2/system/constexpr.h>

// exactly defined tests
static_assert(VDCxSinPi(-4.0f) ==  0.0f);
static_assert(VDCxSinPi(-3.5f) ==  1.0f);
static_assert(VDCxSinPi(-3.0f) ==  0.0f);
static_assert(VDCxSinPi(-2.5f) == -1.0f);
static_assert(VDCxSinPi(-2.0f) ==  0.0f);
static_assert(VDCxSinPi(-1.5f) ==  1.0f);
static_assert(VDCxSinPi(-1.0f) ==  0.0f);
static_assert(VDCxSinPi(-0.5f) == -1.0f);
static_assert(VDCxSinPi( 0.0f) ==  0.0f);
static_assert(VDCxSinPi( 0.5f) ==  1.0f);
static_assert(VDCxSinPi( 1.0f) ==  0.0f);
static_assert(VDCxSinPi( 1.5f) == -1.0f);
static_assert(VDCxSinPi( 2.0f) ==  0.0f);
static_assert(VDCxSinPi( 2.5f) ==  1.0f);
static_assert(VDCxSinPi( 3.0f) ==  0.0f);
static_assert(VDCxSinPi( 3.5f) == -1.0f);

static_assert(VDCxCosPi(-4.0f) ==  1.0f);
static_assert(VDCxCosPi(-3.5f) ==  0.0f);
static_assert(VDCxCosPi(-3.0f) == -1.0f);
static_assert(VDCxCosPi(-2.5f) ==  0.0f);
static_assert(VDCxCosPi(-2.0f) ==  1.0f);
static_assert(VDCxCosPi(-1.5f) ==  0.0f);
static_assert(VDCxCosPi(-1.0f) == -1.0f);
static_assert(VDCxCosPi(-0.5f) ==  0.0f);
static_assert(VDCxCosPi( 0.0f) ==  1.0f);
static_assert(VDCxCosPi( 0.5f) ==  0.0f);
static_assert(VDCxCosPi( 1.0f) == -1.0f);
static_assert(VDCxCosPi( 1.5f) ==  0.0f);
static_assert(VDCxCosPi( 2.0f) ==  1.0f);
static_assert(VDCxCosPi( 2.5f) ==  0.0f);
static_assert(VDCxCosPi( 3.0f) == -1.0f);
static_assert(VDCxCosPi( 3.5f) ==  0.0f);

// inexact tests (but which we should hit within machine precision)
static_assert(VDCxSinPi(-0.75f) == -0xB504F3p-24f);
static_assert(VDCxSinPi(-0.25f) == -0xB504F3p-24f);
static_assert(VDCxSinPi( 0.25f) ==  0xB504F3p-24f);	// sqrt(2)/2
static_assert(VDCxSinPi( 0.75f) ==  0xB504F3p-24f);

static_assert(VDCxCosPi(-0.75f) == -0xB504F3p-24f);
static_assert(VDCxCosPi(-0.25f) ==  0xB504F3p-24f);
static_assert(VDCxCosPi( 0.25f) ==  0xB504F3p-24f);
static_assert(VDCxCosPi( 0.75f) == -0xB504F3p-24f);
