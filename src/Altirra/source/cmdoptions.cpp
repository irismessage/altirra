//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
#include <at/atui/uicommandmanager.h>
#include "cmdhelpers.h"
#include "options.h"

template<auto T_Field>
constexpr ATUICommand ATMakeOnCommandOptionToggle(const char *name) {
	using namespace ATCommands;

	return ATUICommand {
		name,
		[] {
			auto prev = g_ATOptions;
			g_ATOptions.*T_Field = !(g_ATOptions.*T_Field);
			ATOptionsRunUpdateCallbacks(&prev);
			g_ATOptions.mbDirty = true;
			ATOptionsSave();
		},
		nullptr,
		[] {
			return ToChecked(g_ATOptions.*T_Field);
		},
		nullptr
	};
}

void ATUIInitCommandMappingsOption(ATUICommandManager& cmdMgr) {
	static constexpr ATUICommand kCommands[]={
		ATMakeOnCommandOptionToggle<&ATOptions::mbSingleInstance>("Options.ToggleSingleInstance"),
		ATMakeOnCommandOptionToggle<&ATOptions::mbPauseDuringMenu>("Options.PauseDuringMenu"),
		ATMakeOnCommandOptionToggle<&ATOptions::mbPollDirectories>("Options.ToggleDirectoryPolling"),
	};

	cmdMgr.RegisterCommands(kCommands, vdcountof(kCommands));
}
