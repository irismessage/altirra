//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2018 Avery Lee
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

#include "stdafx.h"
#include <at/atui/uicommandmanager.h>
#include <at/ataudio/pokey.h>
#include "cmdhelpers.h"
#include "simulator.h"
#include "inputcontroller.h"
#include "uiaccessors.h"
#include "uikeyboard.h"

extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;

bool ATUIShowDialogKeyboardCustomize(VDGUIHandle hParent);

void OnCommandInputKeyboardLayout(ATUIKeyboardOptions::LayoutMode layoutMode) {
	if (g_kbdOpts.mLayoutMode != layoutMode) {
		g_kbdOpts.mLayoutMode = layoutMode;

		ATUIInitVirtualKeyMap(g_kbdOpts);
	}
}

void OnCommandInputKeyboardLayoutNatural() {
	OnCommandInputKeyboardLayout(ATUIKeyboardOptions::kLM_Natural);
}

void OnCommandInputKeyboardLayoutDirect() {
	OnCommandInputKeyboardLayout(ATUIKeyboardOptions::kLM_Raw);
}

void OnCommandInputKeyboardLayoutCustom() {
	OnCommandInputKeyboardLayout(ATUIKeyboardOptions::kLM_Custom);
}

void OnCommandInputKeyboardModeCooked() {
	g_kbdOpts.mbRawKeys = false;
	g_kbdOpts.mbFullRawKeys = false;
}

void OnCommandInputKeyboardModeRaw() {
	g_kbdOpts.mbRawKeys = true;
	g_kbdOpts.mbFullRawKeys = false;
}

void OnCommandInputKeyboardModeFullScan() {
	g_kbdOpts.mbRawKeys = true;
	g_kbdOpts.mbFullRawKeys = true;
}

void OnCommandInputKeyboardArrowMode(ATUIKeyboardOptions::ArrowKeyMode mode) {
	if (g_kbdOpts.mArrowKeyMode != mode) {
		g_kbdOpts.mArrowKeyMode = mode;

		ATUIInitVirtualKeyMap(g_kbdOpts);
	}
}

void OnCommandInputKeyboardArrowModeDefault() {
	OnCommandInputKeyboardArrowMode(ATUIKeyboardOptions::kAKM_InvertCtrl);
}

void OnCommandInputKeyboardArrowModeAutoCtrl() {
	OnCommandInputKeyboardArrowMode(ATUIKeyboardOptions::kAKM_AutoCtrl);
}

void OnCommandInputKeyboardArrowModeRaw() {
	OnCommandInputKeyboardArrowMode(ATUIKeyboardOptions::kAKM_DefaultCtrl);
}

void OnCommandInputToggleKeyboardUnshiftOnReset() {
	g_kbdOpts.mbAllowShiftOnColdReset = !g_kbdOpts.mbAllowShiftOnColdReset;
}

void OnCommandInputToggle1200XLFunctionKeys() {
	g_kbdOpts.mbEnableFunctionKeys = !g_kbdOpts.mbEnableFunctionKeys;

	ATUIInitVirtualKeyMap(g_kbdOpts);
}

void OnCommandInputToggleAllowInputMapKeyboardOverlap() {
	g_kbdOpts.mbAllowInputMapOverlap = !g_kbdOpts.mbAllowInputMapOverlap;
}

void OnCommandInputToggleAllowInputMapKeyboardModifierOverlap() {
	g_kbdOpts.mbAllowInputMapModifierOverlap = !g_kbdOpts.mbAllowInputMapModifierOverlap;
}

void OnCommandInputKeyboardCopyToCustomLayout() {
	if (g_kbdOpts.mLayoutMode == ATUIKeyboardOptions::kLM_Custom)
		return;

	vdfastvector<uint32> mappings;
	ATUIGetDefaultKeyMap(g_kbdOpts, mappings);
	ATUISetCustomKeyMap(mappings.data(), mappings.size());

	g_kbdOpts.mLayoutMode = ATUIKeyboardOptions::kLM_Custom;
	ATUIInitVirtualKeyMap(g_kbdOpts);
}

void OnCommandInputKeyboardCustomizeLayout() {
	ATUIShowDialogKeyboardCustomize(ATUIGetNewPopupOwner());
}

namespace ATCommands {	
	bool IsCommandInputKeyboardLayout(ATUIKeyboardOptions::LayoutMode mode) {
		return g_kbdOpts.mLayoutMode == mode;
	}

	static constexpr ATUICommand kATCommandsInput[] = {
		{ "Input.KeyboardLayoutNatural", OnCommandInputKeyboardLayoutNatural, nullptr, [] { return ToRadio(IsCommandInputKeyboardLayout(ATUIKeyboardOptions::kLM_Natural)); } },
		{ "Input.KeyboardLayoutDirect", OnCommandInputKeyboardLayoutDirect, nullptr, [] { return ToRadio(IsCommandInputKeyboardLayout(ATUIKeyboardOptions::kLM_Raw)); } },
		{ "Input.KeyboardLayoutCustom", OnCommandInputKeyboardLayoutCustom, nullptr, [] { return ToRadio(IsCommandInputKeyboardLayout(ATUIKeyboardOptions::kLM_Custom)); } },
		{ "Input.KeyboardModeCooked", OnCommandInputKeyboardModeCooked, nullptr, [] { return ToRadio(!g_kbdOpts.mbRawKeys); } },
		{ "Input.KeyboardModeRaw", OnCommandInputKeyboardModeRaw, nullptr, [] { return ToRadio(g_kbdOpts.mbRawKeys && !g_kbdOpts.mbFullRawKeys); } },
		{ "Input.KeyboardModeFullScan", OnCommandInputKeyboardModeFullScan, nullptr, [] { return ToRadio(g_kbdOpts.mbFullRawKeys); } },
		{ "Input.KeyboardArrowModeDefault", OnCommandInputKeyboardArrowModeDefault, nullptr, [] { return ToRadio(g_kbdOpts.mArrowKeyMode == ATUIKeyboardOptions::kAKM_InvertCtrl); } },
		{ "Input.KeyboardArrowModeAutoCtrl", OnCommandInputKeyboardArrowModeAutoCtrl, nullptr, [] { return ToRadio(g_kbdOpts.mArrowKeyMode == ATUIKeyboardOptions::kAKM_AutoCtrl); } },
		{ "Input.KeyboardArrowModeRaw", OnCommandInputKeyboardArrowModeRaw, nullptr, [] { return ToRadio(g_kbdOpts.mArrowKeyMode == ATUIKeyboardOptions::kAKM_DefaultCtrl); } },
		{ "Input.ToggleAllowShiftOnReset", OnCommandInputToggleKeyboardUnshiftOnReset, nullptr, [] { return ToChecked(g_kbdOpts.mbAllowShiftOnColdReset); } },
		{ "Input.Toggle1200XLFunctionKeys", OnCommandInputToggle1200XLFunctionKeys, [] { return g_kbdOpts.mLayoutMode != ATUIKeyboardOptions::kLM_Custom; }, [] { return ToChecked(g_kbdOpts.mbEnableFunctionKeys); } },
		{ "Input.ToggleAllowInputMapKeyboardOverlap", OnCommandInputToggleAllowInputMapKeyboardOverlap, nullptr, [] { return ToChecked(g_kbdOpts.mbAllowInputMapOverlap); } },
		{ "Input.ToggleAllowInputMapKeyboardModifierOverlap", OnCommandInputToggleAllowInputMapKeyboardModifierOverlap, nullptr, [] { return ToChecked(g_kbdOpts.mbAllowInputMapModifierOverlap); } },

		{ "Input.KeyboardCopyToCustomLayout", OnCommandInputKeyboardCopyToCustomLayout, [] { return g_kbdOpts.mLayoutMode != ATUIKeyboardOptions::kLM_Custom; } },
		{ "Input.KeyboardCustomizeLayoutDialog", OnCommandInputKeyboardCustomizeLayout, [] { return g_kbdOpts.mLayoutMode == ATUIKeyboardOptions::kLM_Custom; } },

		{ "Input.ToggleRawInputEnabled"
			, [] { ATUISetRawInputEnabled(!ATUIGetRawInputEnabled()); }
			, nullptr
			, [] { return ToChecked(ATUIGetRawInputEnabled()); }
		},
		{ "Input.ToggleImmediatePotUpdate"
			, [] {
				const bool enable = !g_sim.GetPokey().IsImmediatePotUpdateEnabled();
				g_sim.GetPokey().SetImmediatePotUpdateEnabled(enable);
			}
			, nullptr
			, [] { return ToChecked(g_sim.GetPokey().IsImmediatePotUpdateEnabled()); }
		},
		{ "Input.ToggleImmediateLightPenUpdate"
			, [] {
				const bool enable = !g_sim.GetLightPenPort()->GetImmediateUpdateEnabled();
				g_sim.GetLightPenPort()->SetImmediateUpdateEnabled(enable);
			}
			, nullptr
			, [] { return ToChecked(g_sim.GetLightPenPort()->GetImmediateUpdateEnabled()); }
		},

		{ "Input.RecalibrateLightPen", ATUIRecalibrateLightPen },
	};
}

void ATUIInitCommandMappingsInput(ATUICommandManager& cmdMgr) {
	using namespace ATCommands;

	cmdMgr.RegisterCommands(kATCommandsInput, vdcountof(kATCommandsInput));
}
