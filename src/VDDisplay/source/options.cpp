//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
#include <vd2/Tessa/options.h>
#include <vd2/VDDisplay/internal/options.h>

bool VDDInternalOptions::sbD3D9LimitPS1_1 = false;
bool VDDInternalOptions::sbD3D9LimitPS2_0 = false;

void VDVideoDisplaySetDXFlipModeEnabled(bool enable) {
	VDTInternalOptions::sbEnableDXFlipMode = enable;
}

void VDVideoDisplaySetDXFlipDiscardEnabled(bool enable) {
	VDTInternalOptions::sbEnableDXFlipDiscard = enable;
}

void VDVideoDisplaySetDXWaitableObjectEnabled(bool enable) {
	VDTInternalOptions::sbEnableDXWaitableObject = enable;
}

void VDVideoDisplaySetDXDoNotWaitEnabled(bool enable) {
	VDTInternalOptions::sbEnableDXDoNotWait = enable;
}

void VDVideoDisplaySetD3D9LimitPS1_1(bool enable) {
	VDDInternalOptions::sbD3D9LimitPS1_1 = enable;
}

void VDVideoDisplaySetD3D9LimitPS2_0(bool enable) {
	VDDInternalOptions::sbD3D9LimitPS2_0 = enable;
}

void VDVideoDisplaySetD3D11Force9_1(bool enable) {
	VDTInternalOptions::sbD3D11Force9_1 = enable;
}

void VDVideoDisplaySetD3D11Force9_3(bool enable) {
	VDTInternalOptions::sbD3D11Force9_3 = enable;
}

void VDVideoDisplaySetD3D11Force10_0(bool enable) {
	VDTInternalOptions::sbD3D11Force10_0 = enable;
}
