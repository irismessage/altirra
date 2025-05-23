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
#include <at/atdebugger/target.h>

const wchar_t *ATDebugGetDisasmModeDisplayName(ATDebugDisasmMode mode) {
	static constexpr const wchar_t *kModes[]={
		L"6502",
		L"65C02",
		L"65C816",
		L"Z80",
		L"8048",
		L"8051",
		L"6809",
	};

	if ((unsigned)mode < std::size(kModes))
		return kModes[mode];
	else
		return L"?";
}
