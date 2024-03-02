//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2017 Avery Lee
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

#ifndef f_AT_ATCORE_ENUMPARSE_H
#define f_AT_ATCORE_ENUMPARSE_H

#include <type_traits>
#include <vd2/system/vdtypes.h>

class VDStringSpanA;

struct ATEnumLookupTable;
template<typename T> const ATEnumLookupTable& ATGetEnumLookupTable() = delete;

template<typename T>
struct ATEnumParseResult {
	bool mValid;
	T mValue;
};

const char *ATEnumToString(const ATEnumLookupTable& table, uint32 value);
ATEnumParseResult<uint32> ATParseEnum(const ATEnumLookupTable& table, const VDStringSpanA& str);
ATEnumParseResult<uint32> ATParseEnum(const ATEnumLookupTable& table, const VDStringSpanW& str);

template<typename T>
ATEnumParseResult<T> ATParseEnum(const VDStringSpanA& str) {
	auto v = ATParseEnum(ATGetEnumLookupTable<T>(), str);

	return { v.mValid, (T)v.mValue };
}

template<typename T>
const char *ATEnumToString(T value) {
	return ATEnumToString(ATGetEnumLookupTable<T>(), (uint32)value);
}

template<typename T, typename U> requires std::is_enum_v<T> && (std::is_integral_v<U> || std::is_same_v<T, U>)
bool ATIsValidEnumValue(U value) {
	std::underlying_type_t<T> v2 = value;

	return (value == v2) && ATEnumToString(ATGetEnumLookupTable<T>(), (uint32)value);
}

#define AT_DECLARE_ENUM_TABLE(enumName) template<> const ATEnumLookupTable& ATGetEnumLookupTable<enumName>()


#endif
