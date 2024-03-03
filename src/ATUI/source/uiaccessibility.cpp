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

#include <stdafx.h>
#include <combaseapi.h>
#include <UIAutomation.h>
#include <vd2/system/w32assist.h>
#include <at/atnativeui/accessibility_win32.h>

////////////////////////////////////////////////////////////////////////////////

bool g_ATUIAccEnabled = false;
bool g_ATUIAccessibilityUsedW32 = false;

void ATUIAccShutdown() {
	g_ATUIAccEnabled = false;

	if (g_ATUIAccessibilityUsedW32) {
		g_ATUIAccessibilityUsedW32 = false;

		ATUiaDisconnectAllProviders();
	}
}

void ATUIAccSetUsed() {
	g_ATUIAccessibilityUsedW32 = true;
}

bool ATUIAccGetEnabled() {
	return g_ATUIAccEnabled;
}

void ATUIAccSetEnabled(bool enabled) {
	g_ATUIAccEnabled = enabled;
}
