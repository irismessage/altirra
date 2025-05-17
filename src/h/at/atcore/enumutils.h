//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_ENUMUTILS_H
#define f_AT_ATCORE_ENUMUTILS_H

#include <type_traits>

#define AT_IMPLEMENT_ENUM_FLAGS(T) AT_IMPLEMENT_ENUM_FLAGS2(T,inline)
#define AT_IMPLEMENT_ENUM_FLAGS_FRIEND_STATIC(T) AT_IMPLEMENT_ENUM_FLAGS2(T,inline friend)
#define AT_IMPLEMENT_ENUM_FLAGS2(T,mode) \
	mode bool operator!(T x) { return x == T{}; } \
	mode auto operator+(T x) { return std::to_underlying(x); } \
	mode T operator~(T x) { return T(~std::to_underlying(x)); } \
	mode T operator&(T x, T y) { return T(std::to_underlying(x) & std::to_underlying(y)); } \
	mode T operator|(T x, T y) { return T(std::to_underlying(x) | std::to_underlying(y)); } \
	mode T& operator&=(T& x, T y) { x = T(std::to_underlying(x) & std::to_underlying(y)); return x; } \
	mode T& operator|=(T& x, T y) { x = T(std::to_underlying(x) | std::to_underlying(y)); return x; }

#endif
