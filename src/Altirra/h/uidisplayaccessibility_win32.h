//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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

#ifndef f_AT_UIDISPLAYACCESSIBILITY_WIN32_H
#define f_AT_UIDISPLAYACCESSIBILITY_WIN32_H

#include <vd2/system/vectors.h>

struct IRawElementProviderSimple;
struct ATUIDisplayAccessibilityLineInfo;
struct ATUIDisplayAccessibilityScreen;

class IATUIDisplayAccessibilityProviderW32 : public IVDRefCount {
public:
	virtual IRawElementProviderSimple *AsRawElementProviderSimple() = 0;

	virtual void Detach() = 0;

	virtual void SetScreen(ATUIDisplayAccessibilityScreen *screenInfo) = 0;

	virtual void OnGainedFocus() = 0;
};

class IATUIDisplayAccessibilityCallbacksW32 {
public:
	virtual vdpoint32 GetNearestBeamPositionForScreenPoint(sint32 x, sint32 y) const = 0;
	virtual vdrect32 GetTextSpanBoundingBox(int ypos, int xc1, int xc2) const = 0;
};

void ATUICreateDisplayAccessibilityProviderW32(HWND hwnd, IATUIDisplayAccessibilityCallbacksW32& callbacks, IATUIDisplayAccessibilityProviderW32 **provider);

#endif
