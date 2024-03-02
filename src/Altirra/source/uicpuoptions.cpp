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

#include <stdafx.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/genericdialog.h>
#include "resource.h"
#include "cpu.h"
#include "simulator.h"
#include "uiconfirm.h"

extern ATSimulator g_sim;

class ATUICPUOptionsDialog final : public VDDialogFrameW32 {
public:
	ATUICPUOptionsDialog();

protected:
	bool OnOK() override;
	bool OnLoaded() override;
	void OnDataExchange(bool write) override;

	typedef std::pair<ATCPUMode, uint32> CPUConfig;
	CPUConfig GetCPUConfig() const;
	void SetCPUConfig(const CPUConfig& config);
};

ATUICPUOptionsDialog::ATUICPUOptionsDialog()
	: VDDialogFrameW32(IDD_CPU_OPTIONS)
{
}

bool ATUICPUOptionsDialog::OnOK() {
	if (!g_sim.IsCPUModeOverridden() && g_sim.GetCPUMode() != GetCPUConfig().first) {
		if (!ATUIConfirmDiscardMemory((VDGUIHandle)GetControl(IDOK), L"Changing CPU type"))
			return true;
	}

	return VDDialogFrameW32::OnOK();
}

bool ATUICPUOptionsDialog::OnLoaded() {
	if (g_sim.IsCPUModeOverridden()) {
		static constexpr uint32 kControlIDs[] = {
			IDC_CPUMODEL_6502C,
			IDC_CPUMODEL_65C02,
			IDC_CPUMODEL_65C816_21MHZ,
			IDC_CPUMODEL_65C816_17MHZ,
			IDC_CPUMODEL_65C816_14MHZ,
			IDC_CPUMODEL_65C816_10MHZ,
			IDC_CPUMODEL_65C816_7MHZ,
			IDC_CPUMODEL_65C816_3MHZ,
			IDC_CPUMODEL_65C816,
		};

		for(uint32 id : kControlIDs)
			EnableControl(id, false);
	}

	return VDDialogFrameW32::OnLoaded();
}


void ATUICPUOptionsDialog::OnDataExchange(bool write) {
	if (write) {
		ATCPUEmulator& cpuem = g_sim.GetCPU();

		cpuem.SetHistoryEnabled(IsButtonChecked(IDC_ENABLE_HISTORY));
		cpuem.SetPathfindingEnabled(IsButtonChecked(IDC_ENABLE_PATHS));
		cpuem.SetIllegalInsnsEnabled(IsButtonChecked(IDC_ENABLE_ILLEGALS));
		cpuem.SetStopOnBRK(IsButtonChecked(IDC_STOP_ON_BRK));
		cpuem.SetNMIBlockingEnabled(IsButtonChecked(IDC_ALLOWNMIBLOCKING));

		bool reset = false;
		if (!g_sim.IsCPUModeOverridden()) {
			const auto cpuConfig = GetCPUConfig();

			bool cpuModeChange = g_sim.GetCPUMode() != cpuConfig.first;

			if (cpuModeChange || g_sim.GetCPUSubCycles() != cpuConfig.second) {
				g_sim.SetCPUMode(cpuConfig.first, cpuConfig.second);

				if (cpuModeChange)
					reset = true;
			}
		}

		bool shadowROM = IsButtonChecked(IDC_SHADOW_ROM);
		bool shadowCarts = IsButtonChecked(IDC_SHADOW_CARTS);

		g_sim.SetShadowROMEnabled(shadowROM);
		g_sim.SetShadowCartridgeEnabled(shadowCarts);

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

		SetCPUConfig({ g_sim.GetCPUMode(), g_sim.GetCPUSubCycles() });
	}
}

ATUICPUOptionsDialog::CPUConfig ATUICPUOptionsDialog::GetCPUConfig() const {
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

	return { cpuMode, subCycles };
}

void ATUICPUOptionsDialog::SetCPUConfig(const CPUConfig& config) {
	switch(config.first) {
		case kATCPUMode_6502:
			CheckButton(IDC_CPUMODEL_6502C, true);
			break;
		case kATCPUMode_65C02:
			CheckButton(IDC_CPUMODEL_65C02, true);
			break;
		case kATCPUMode_65C816:
			{
				uint32 subCycles = config.second;

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

///////////////////////////////////////////////////////////////////////////

void ATUIShowCPUOptionsDialog(VDGUIHandle h) {
	ATUICPUOptionsDialog dlg;

	dlg.ShowDialog(h);
}
