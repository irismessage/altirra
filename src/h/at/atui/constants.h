//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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

#ifndef f_AT_ATUI_CONSTANTS_H
#define f_AT_ATUI_CONSTANTS_H

#include <vd2/system/vdtypes.h>

enum ATUITouchMode : uint32 {				//	flicks		press-and-hold		gestures
	kATUITouchMode_Default,					//	on			on					on
	kATUITouchMode_Immediate,				//	off			off					off
	kATUITouchMode_Direct,					//	off			on					off
	kATUITouchMode_VerticalPan,				//	off			on					vertical pan only
	kATUITouchMode_2DPan,					//	off			on					on
	kATUITouchMode_2DPanSmooth,				//	off			on					on, pan gutters disabled
	kATUITouchMode_MultiTouch,				//	off			on					off, registered as touch window
	kATUITouchMode_MultiTouchImmediate,		//	off			off					off, registered as touch window
	kATUITouchMode_Dynamic,					//	<determined by lookup>
	kATUITouchMode_MultiTouchDynamic		//	<determined by lookup>
};

enum ATUICursorImage : uint32 {
	kATUICursorImage_None,
	kATUICursorImage_Hidden,
	kATUICursorImage_Arrow,
	kATUICursorImage_IBeam,
	kATUICursorImage_Cross,
	kATUICursorImage_Query,
	kATUICursorImage_Move,
	kATUICursorImage_SizeHoriz,
	kATUICursorImage_SizeVert,
	kATUICursorImage_SizeDiagFwd,
	kATUICursorImage_SizeDiagRev,
	kATUICursorImage_Target,
	kATUICursorImage_TargetOff
};

#endif
