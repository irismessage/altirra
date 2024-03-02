//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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

#ifndef f_AT_UIMENU_H
#define f_AT_UIMENU_H

#pragma once

class ATUIMenu;

class IATUIDynamicMenuProvider {
public:
	virtual bool IsRebuildNeeded() const = 0;
	virtual void RebuildMenu(ATUIMenu& menu, uint32 pos, uint32 idbase) = 0;
	virtual void UpdateMenu(ATUIMenu& menu, uint32 firstIndex, uint32 n) = 0;
	virtual void HandleMenuCommand(uint32 index) = 0;
};

enum ATUIDynamicMenu {
	kATUIDynamicMenu_OS,
	kATUIDynamicMenu_BASIC,
	kATUIDynamicMenu_Profile,
	kATUIDynamicMenu_VideoOutput,
	kATUIDynamicMenuCount
};

ATUIMenu *ATUIGetMenu();
void ATUILoadMenu();
void ATUISetMenuEnabled(bool enabled);
bool ATUIIsMenuAutoHideEnabled();
bool ATUIIsMenuAutoHideActive();
void ATUISetMenuAutoHideEnabled(bool autoHide);
bool ATUIIsMenuAutoHidden();
void ATUISetMenuAutoHidden(bool hidden);
void ATUISetMenuHidden(bool hidden);
void ATUISetMenuFullScreenHidden(bool hidden);
bool ATUIIsMenuCharacter(wchar_t c);
void ATUISetDynamicMenuProvider(int index, IATUIDynamicMenuProvider *provider);
void ATUIRebuildDynamicMenu(int index);
void ATUIUpdateMenu();
bool ATUIHandleMenuCommand(uint32 id);
bool ATUIIsCommandMappedMenuItem(uint32 id);
const char *ATUIGetCommandForMenuItem(uint32 id);

#endif
