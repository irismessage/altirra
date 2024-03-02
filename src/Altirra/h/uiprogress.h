//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2011 Avery Lee
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

#ifndef f_AT_UIPROGRESS_H
#define f_AT_UIPROGRESS_H

#include <vd2/system/VDString.h>
#include <vd2/system/win32/miniwindows.h>

VDZHWND ATUISetProgressWindowParentW32(VDZHWND hwnd);

bool ATUIBeginProgressDialog(const wchar_t *desc, const wchar_t *statusFormat, uint32 total, const VDGUIHandle *parent = 0);
void ATUIUpdateProgressDialog(uint32 count);
void ATUIEndProgressDialog();

class ATUIProgress {
	ATUIProgress(const ATUIProgress&);
	ATUIProgress& operator=(const ATUIProgress&);
public:
	ATUIProgress();
	~ATUIProgress();

	void InitF(VDGUIHandle parent, uint32 n, const wchar_t *statusFormat, const wchar_t *descFormat, ...);
	void InitF(uint32 n, const wchar_t *statusFormat, const wchar_t *descFormat, ...);

	void Shutdown();

	void Update(uint32 value) {
		ATUIUpdateProgressDialog(value);
	}

protected:
	VDStringW mDesc;
	VDStringW mStatusFormat;
	bool mbCreated;
};

#endif
