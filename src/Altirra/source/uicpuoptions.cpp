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

#include "stdafx.h"
#include <at/atui/dialog.h>
#include "resource.h"
#include "cpu.h"
#include "simulator.h"

extern ATSimulator g_sim;

class ATUICPUOptionsDialog : public VDDialogFrameW32 {
public:
	ATUICPUOptionsDialog();

protected:
	void OnDataExchange(bool write);
};

ATUICPUOptionsDialog::ATUICPUOptionsDialog()
	: VDDialogFrameW32(IDD_CPU_OPTIONS)
{
}

void ATUICPUOptionsDialog::OnDataExchange(bool write) {
	if (write) {
		ATCPUEmulator& cpuem = g_sim.GetCPU();

		cpuem.SetHistoryEnabled(IsButtonChecked(IDC_ENABLE_HISTORY));
		cpuem.SetPathfindingEnabled(IsButtonChecked(IDC_ENABLE_PATHS));
		cpuem.SetIllegalInsnsEnabled(IsButtonChecked(IDC_ENABLE_ILLEGALS));
		cpuem.SetStopOnBRK(IsButtonChecked(IDC_STOP_ON_BRK));
		cpuem.SetNMIBlockingEnabled(IsButtonChecked(IDC_ALLOWNMIBLOCKING));

		ATCPUMode cpuMode = kATCPUMode_6502;
		uint32 subCycles = 1;

		if (IsButtonChecked(IDC_CPUMODEL_65C816_21MHZ)) {
			cpuMode = kATCPUMode_65C816;
			subCycles = 12;
		} else if (IsButtonChecked(IDC_CPUMODEL_65C816_17MHZ)) {
			cpuMode = kATCPUMode_65C816;
			subCycles = 10;
		} else if (IsButtonChecked(IDC_CPUMODEL_65C816_14MHZ)) {
			cpuMode = kATCPUMode_65C816;
			subCycles = 8;
		} else if (IsButtonChecked(IDC_CPUMODEL_65C816_10MHZ)) {
			cpuMode = kATCPUMode_65C816;
			subCycles = 6;
		} else if (IsButtonChecked(IDC_CPUMODEL_65C816_7MHZ)) {
			cpuMode = kATCPUMode_65C816;
			subCycles = 4;
		} else if (IsButtonChecked(IDC_CPUMODEL_65C816_3MHZ)) {
			cpuMode = kATCPUMode_65C816;
			subCycles = 2;
		} else if (IsButtonChecked(IDC_CPUMODEL_65C816))
			cpuMode = kATCPUMode_65C816;
		else if (IsButtonChecked(IDC_CPUMODEL_65C02))
			cpuMode = kATCPUMode_65C02;

		bool reset = false;

		if (cpuem.GetCPUMode() != cpuMode || cpuem.GetSubCycles() != subCycles) {
			cpuem.SetCPUMode(cpuMode, subCycles);
			reset = true;
		}

		bool shadowROM = IsButtonChecked(IDC_SHADOW_ROM);
		bool shadowCarts = IsButtonChecked(IDC_SHADOW_CARTS);

		if (g_sim.GetShadowROMEnabled() != shadowROM) {
			g_sim.SetShadowROMEnabled(shadowROM);
			reset = true;
		}

		if (g_sim.GetShadowCartridgeEnabled() != shadowCarts) {
			g_sim.SetShadowCartridgeEnabled(shadowCarts);
			reset = true;
		}

		if (reset)
			g_sim.ColdReset();
	} else {
		ATCPUEmulator& cpuem = g_sim.GetCPU();

		CheckButton(IDC_ENABLE_HISTORY, cpuem.IsHistoryEnabled());
		CheckButton(IDC_ENABLE_PATHS, cpuem.IsPathfindingEnabled());
		CheckButton(IDC_ENABLE_ILLEGALS, cpuem.AreIllegalInsnsEnabled());
		CheckButton(IDC_STOP_ON_BRK, cpuem.GetStopOnBRK());
		CheckButton(IDC_ALLOWNMIBLOCKING, cpuem.IsNMIBlockingEnabled());
		CheckButton(IDC_SHADOW_ROM, g_sim.GetShadowROMEnabled());
		CheckButton(IDC_SHADOW_CARTS, g_sim.GetShadowCartridgeEnabled());

		switch(cpuem.GetCPUMode()) {
			case kATCPUMode_6502:
				CheckButton(IDC_CPUMODEL_6502C, true);
				break;
			case kATCPUMode_65C02:
				CheckButton(IDC_CPUMODEL_65C02, true);
				break;
			case kATCPUMode_65C816:
				{
					uint32 subCycles = cpuem.GetSubCycles();

					if (subCycles >= 12)
						CheckButton(IDC_CPUMODEL_65C816_21MHZ, true);
					else if (subCycles >= 10)
						CheckButton(IDC_CPUMODEL_65C816_17MHZ, true);
					else if (subCycles >= 8)
						CheckButton(IDC_CPUMODEL_65C816_14MHZ, true);
					else if (subCycles >= 6)
						CheckButton(IDC_CPUMODEL_65C816_10MHZ, true);
					else if (subCycles >= 4)
						CheckButton(IDC_CPUMODEL_65C816_7MHZ, true);
					else if (subCycles >= 2)
						CheckButton(IDC_CPUMODEL_65C816_3MHZ, true);
					else
						CheckButton(IDC_CPUMODEL_65C816, true);
				}
				break;
		}
	}
}

void ATUIShowCPUOptionsDialog(VDGUIHandle h) {
	ATUICPUOptionsDialog dlg;

	dlg.ShowDialog(h);
}
