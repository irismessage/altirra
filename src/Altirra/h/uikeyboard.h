//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
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

#ifndef f_AT_UIKEYBOARD_H
#define f_AT_UIKEYBOARD_H

struct VDAccelTableEntry;
class VDAccelTableDefinition;

struct ATUIKeyboardOptions {
	enum ArrowKeyMode {
		kAKM_InvertCtrl,	// Ctrl state is inverted between host and emulation
		kAKM_AutoCtrl,		// Ctrl state is injected only for unmodded case
		kAKM_DefaultCtrl,	// Shift/Ctrl states are passed through
		kAKMCount
	};

	bool mbRawKeys;
	bool mbEnableFunctionKeys;
	bool mbAllowShiftOnColdReset;
	ArrowKeyMode mArrowKeyMode;
};

bool ATUIGetScanCodeForCharacter(char c, uint8& ch);
void ATUIInitVirtualKeyMap(const ATUIKeyboardOptions& options);
bool ATUIGetScanCodeForVirtualKey(uint32 virtKey, bool alt, bool ctrl, bool shift, uint8& scanCode);

enum ATUIAccelContext {
	kATUIAccelContext_Global,
	kATUIAccelContext_Display,
	kATUIAccelContextCount
};

void ATUIInitDefaultAccelTables();
void ATUILoadAccelTables();
void ATUISaveAccelTables();
const VDAccelTableDefinition *ATUIGetDefaultAccelTables();
VDAccelTableDefinition *ATUIGetAccelTables();

const VDAccelTableEntry *ATUIGetAccelByCommand(ATUIAccelContext context, const char *command);
bool ATUIActivateVirtKeyMapping(uint32 vk, bool alt, bool ctrl, bool shift, bool ext, bool up, ATUIAccelContext context);

#endif
