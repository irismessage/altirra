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

#ifndef f_AT_ATNATIVEUI_CONTROLSTYLES_H
#define f_AT_ATNATIVEUI_CONTROLSTYLES_H

#include <windows.h>
#include <vd2/system/refcount.h>

class IATUICheckboxStyleW32 : public IVDRefCount {
public:
	virtual bool IsMatch(HDC, int w, int h) const = 0;
	virtual void Draw(HDC hdc, int dx, int dy, bool radio, bool disabled, bool pushed, bool highlighted, bool checked, bool indeterminate) = 0;
};

vdrefptr<IATUICheckboxStyleW32> ATUIGetCheckboxStyleW32(HDC hdc, int w, int h);

void ATUIInitControlStylesW32();
void ATUIShutdownControlStylesW32();

#endif
