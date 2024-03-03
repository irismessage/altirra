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

#ifndef f_AT_ATUI_ACCESSIBILITY_H
#define f_AT_ATUI_ACCESSIBILITY_H

// Shutdown the accessibility system. This attempts to disconnect all
// UIAutomation providers and also disables new ones from being created.
void ATUIAccShutdown();

// Mark that accessibility has been used, and thus there is something
// to shut down.
void ATUIAccSetUsed();

bool ATUIAccGetEnabled();
void ATUIAccSetEnabled(bool enabled);

#endif
