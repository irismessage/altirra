//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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

#include <stdafx.h>
#include "errordecode.h"

vdfastvector<ATDecodedError> ATDecodeError(uint8 code) {
	enum class ErrClass : uint8 {
		BASIC,
		CIO,
		SIO,
		DOS,
		DOS3,
		SDX
	};

	static constexpr const wchar_t *kErrClasses[] {
		L"Atari BASIC",
		L"CIO",
		L"CIO/SIO",
		L"DOS",
		L"DOS 3",
		L"SDX",
	};

	struct ErrInfo {
		uint8 mCode;
		ErrClass mClass;
		const wchar_t *mpMsg;

		bool operator<(const ErrInfo& a) const {
			return mCode < a.mCode;
		}
	};

	static constexpr ErrInfo kErrInfo[] {
		{   2, ErrClass::BASIC,	L"Out of memory" },
		{   3, ErrClass::BASIC,	L"Value error" },
		{   4, ErrClass::BASIC,	L"Too many variables" },
		{   5, ErrClass::BASIC,	L"String length error" },
		{   6, ErrClass::BASIC,	L"Out of data" },
		{   7, ErrClass::BASIC,	L"Number &gt;32767" },
		{   8, ErrClass::BASIC,	L"Input statement error" },
		{   9, ErrClass::BASIC,	L"DIM error" },
		{  10, ErrClass::BASIC,	L"Argument stack overflow" },
		{  11, ErrClass::BASIC,	L"Floating point overflow/underflow" },
		{  12, ErrClass::BASIC,	L"Line not found" },
		{  13, ErrClass::BASIC,	L"No matching FOR statement" },
		{  14, ErrClass::BASIC,	L"Line too long" },
		{  15, ErrClass::BASIC,	L"GOSUB or FOR line deleted" },
		{  16, ErrClass::BASIC,	L"RETURN error" },
		{  17, ErrClass::BASIC,	L"Garbage error" },
		{  18, ErrClass::BASIC,	L"Invalid string character" },
		{  19, ErrClass::BASIC,	L"LOAD program too long" },
		{  20, ErrClass::BASIC,	L"Device number error" },
		{  21, ErrClass::BASIC,	L"LOAD file error" },

		{ 128, ErrClass::CIO,	L"User break abort" },
		{ 129, ErrClass::CIO,	L"IOCB in use" },
		{ 130, ErrClass::CIO,	L"Unknown device" },
		{ 131, ErrClass::CIO,	L"IOCB write only" },
		{ 132, ErrClass::CIO,	L"Invalid command" },
		{ 133, ErrClass::CIO,	L"IOCB not open" },
		{ 134, ErrClass::CIO,	L"Invalid IOCB" },
		{ 135, ErrClass::CIO,	L"IOCB read only" },
		{ 136, ErrClass::CIO,	L"End of file" },
		{ 137, ErrClass::CIO,	L"Truncated record" },
		{ 138, ErrClass::SIO,	L"Timeout" },
		{ 139, ErrClass::SIO,	L"Device NAK" },
		{ 140, ErrClass::SIO,	L"Bad frame" },
		{ 142, ErrClass::SIO,	L"Serial input overrun" },
		{ 143, ErrClass::SIO,	L"Checksum error" },
		{ 144, ErrClass::SIO,	L"Device error or write protected disk" },
		{ 145, ErrClass::CIO,	L"Bad screen mode" },
		{ 146, ErrClass::CIO,	L"Not supported" },
		{ 147, ErrClass::CIO,	L"Out of memory" },

		{ 160, ErrClass::DOS,	L"Invalid drive number" },
		{ 161, ErrClass::DOS,	L"Too many open files" },
		{ 162, ErrClass::DOS,	L"Disk full" },
		{ 163, ErrClass::DOS,	L"Fatal disk I/O error" },
		{ 164, ErrClass::DOS,	L"File number mismatch" },
		{ 165, ErrClass::DOS,	L"File name error" },
		{ 166, ErrClass::DOS,	L"POINT data length error" },
		{ 167, ErrClass::DOS,	L"File locked" },
		{ 168, ErrClass::DOS,	L"Command invalid" },
		{ 169, ErrClass::DOS,	L"Directory full" },
		{ 170, ErrClass::DOS,	L"File not found" },
		{ 171, ErrClass::DOS,	L"Invalid POINT" },

		{ 173, ErrClass::DOS3,	L"Bad sectors at format time" },
		{ 174, ErrClass::DOS3,	L"Duplicate filename" },
		{ 175, ErrClass::DOS3,	L"Bad load file" },
		{ 176, ErrClass::SDX,	L"Access denied\n<b>DOS 3:</b> Incompatible format" },
		{ 177, ErrClass::DOS3,	L"Disk structure damaged" },
		{ 182, ErrClass::SDX,	L"Path too long" },
		{ 255, ErrClass::SDX,	L"System error" },
	};

	auto r = std::equal_range(std::begin(kErrInfo), std::end(kErrInfo), ErrInfo{code});

	vdfastvector<ATDecodedError> result;
	for(auto it = r.first; it != r.second; ++it) {
		result.emplace_back(kErrClasses[(int)it->mClass], it->mpMsg);
	}

	return result;
}
