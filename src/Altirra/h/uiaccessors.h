//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2014 Avery Lee
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

#ifndef f_AT_UIACCESSORS_H
#define f_AT_UIACCESSORS_H

#include <vd2/system/vdtypes.h>

// UI accessors are scattered all over the code base, so we at least collect
// the prototypes here.

enum ATHardwareMode : uint32;
enum ATMemoryMode : uint32;
enum ATVideoStandard : uint32;
enum ATDisplayFilterMode : uint32;
enum ATDisplayStretchMode : uint32;

bool ATUIGetXEPViewEnabled();
void ATUISetXEPViewEnabled(bool enabled);

bool ATUIGetXEPViewAutoswitchingEnabled();
void ATUISetXEPViewAutoswitchingEnabled(bool enabled);

ATDisplayStretchMode ATUIGetDisplayStretchMode();
void ATUISetDisplayStretchMode(ATDisplayStretchMode mode);

void ATSetVideoStandard(ATVideoStandard vs);
bool ATUISwitchHardwareMode(VDGUIHandle h, ATHardwareMode mode);
void ATUISwitchMemoryMode(VDGUIHandle h, ATMemoryMode mode);

bool ATUIGetDriveSoundsEnabled();
void ATUISetDriveSoundsEnabled(bool enabled);

void ATUIOpenOnScreenKeyboard();

int ATUIGetViewFilterSharpness();
void ATUISetViewFilterSharpness(int sharpness);
int ATUIGetViewFilterSharpness();
void ATUISetViewFilterSharpness(int sharpness);
ATDisplayFilterMode ATUIGetDisplayFilterMode();
void ATUISetDisplayFilterMode(ATDisplayFilterMode mode);
bool ATUIGetShowFPS();
void ATUISetShowFPS(bool enabled);
bool ATUIGetFullscreen();
void ATSetFullscreen(bool);

bool ATUIGetTurbo();
void ATUISetTurbo(bool turbo);

#endif	// f_AT_UIACCESSORS_H
