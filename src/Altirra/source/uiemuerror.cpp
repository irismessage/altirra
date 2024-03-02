//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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
#include <windows.h>
#include "Dialog.h"
#include "resource.h"
#include "simulator.h"
#include "debugger.h"
#include "cpu.h"
#include "options.h"

///////////////////////////////////////////////////////////////////////////

enum ATErrorAction {
	kATErrorAction_Pause,
	kATErrorAction_Restart,
	kATErrorAction_Reset,
	kATErrorAction_Debug
};

///////////////////////////////////////////////////////////////////////////

class ATUIDialogEmuError : public VDDialogFrameW32 {
public:
	ATUIDialogEmuError(ATSimulator *sim);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	bool OnOK();
	bool OnCancel();
	bool OnCommand(uint32 id, uint32 extcode);

	ATSimulator *mpSim;

	ATHardwareMode	mNewHardwareMode;
	ATKernelMode	mNewKernelMode;
	ATMemoryMode	mNewMemoryMode;
	bool			mbNewPALMode;
};

ATUIDialogEmuError::ATUIDialogEmuError(ATSimulator *sim)
	: VDDialogFrameW32(IDD_PROGRAM_ERROR)
	, mpSim(sim)
{
}

bool ATUIDialogEmuError::OnLoaded() {
	OnDataExchange(false);

	if (mpSim->IsBASICEnabled() && mpSim->GetHardwareMode() != kATHardwareMode_5200)
		SetFocusToControl(IDC_RESET);
	else
		SetFocusToControl(IDOK);

	CenterOnParent();
	return true;
}

void ATUIDialogEmuError::OnDataExchange(bool write) {
	if (write) {
	} else {
		VDStringW s;

		// Hardware mode
		mNewHardwareMode = kATHardwareMode_800XL;

		GetControlText(IDC_CHANGE_HARDWARE, s);
		switch(mpSim->GetHardwareMode()) {
			case kATHardwareMode_800:
			case kATHardwareMode_XEGS:
				s += L"XL/XE";
				break;

			case kATHardwareMode_800XL:
			case kATHardwareMode_5200:
			default:
				EnableControl(IDC_CHANGE_HARDWARE, false);
				break;
		}
		SetControlText(IDC_CHANGE_HARDWARE, s.c_str());

		// Firmware mode
		mNewKernelMode = kATKernelMode_Default;

		if (mpSim->GetHardwareMode() != kATHardwareMode_5200) {
			switch(mpSim->GetKernelMode()) {
				case kATKernelMode_Default:
				case kATKernelMode_XL:
					EnableControl(IDC_CHANGE_FIRMWARE, false);
					break;

				default:
					s += L"Default";
					break;
			}
		}

		// Memory mode
		mNewMemoryMode = kATMemoryMode_320K;

		if (mpSim->GetHardwareMode() != kATHardwareMode_5200) {
			GetControlText(IDC_CHANGE_MEMORY, s);

			if (mpSim->GetMemoryMode() != kATMemoryMode_320K) {
				s += L"320K";
			} else {
				EnableControl(IDC_CHANGE_MEMORY, false);
			}

			SetControlText(IDC_CHANGE_MEMORY, s.c_str());
		}

		// Video mode
		mbNewPALMode = false;
		
		if (mpSim->GetHardwareMode() != kATHardwareMode_5200) {
			mbNewPALMode = mpSim->GetVideoStandard() == kATVideoStandard_NTSC;
	
			GetControlText(IDC_CHANGE_VIDEO, s);
			if (mbNewPALMode)
				s += L"PAL";
			else
				s += L"NTSC";
			SetControlText(IDC_CHANGE_VIDEO, s.c_str());
		}

		// BASIC
		EnableControl(IDC_CHANGE_BASIC, mpSim->IsBASICEnabled());

		// Diagnostic options
		ATCPUEmulator& cpu = mpSim->GetCPU();
		EnableControl(IDC_CHANGE_CPU, cpu.GetCPUMode() != kATCPUMode_6502);
		EnableControl(IDC_CHANGE_DEBUGGING, cpu.IsPathBreakEnabled() || !cpu.AreIllegalInsnsEnabled() || cpu.GetStopOnBRK());

		// Disk
		EnableControl(IDC_CHANGE_DISKIO, !mpSim->IsDiskAccurateTimingEnabled() || mpSim->IsDiskSIOPatchEnabled());
	}
}

bool ATUIDialogEmuError::OnOK() {
	if (IsButtonChecked(IDC_CHANGE_HARDWARE)) {
		mpSim->SetHardwareMode(mNewHardwareMode);

		if (mNewHardwareMode == kATHardwareMode_800XL || mNewHardwareMode == kATHardwareMode_XEGS) {
			switch(mpSim->GetMemoryMode()) {
				case kATMemoryMode_8K:
				case kATMemoryMode_24K:
				case kATMemoryMode_32K:
				case kATMemoryMode_40K:
				case kATMemoryMode_48K:
				case kATMemoryMode_52K:
					mpSim->SetMemoryMode(kATMemoryMode_64K);
					break;
			}
		}
	}

	if (IsButtonChecked(IDC_CHANGE_MEMORY))
		mpSim->SetMemoryMode(mNewMemoryMode);

	if (IsButtonChecked(IDC_CHANGE_FIRMWARE))
		mpSim->SetKernelMode(mNewKernelMode);

	if (IsButtonChecked(IDC_CHANGE_VIDEO))
		mpSim->SetVideoStandard(mbNewPALMode ? kATVideoStandard_PAL : kATVideoStandard_NTSC);

	if (IsButtonChecked(IDC_CHANGE_BASIC))
		mpSim->SetBASICEnabled(false);

	ATCPUEmulator& cpu = mpSim->GetCPU();
	if (IsButtonChecked(IDC_CHANGE_CPU))
		cpu.SetCPUMode(kATCPUMode_6502);

	if (IsButtonChecked(IDC_CHANGE_DEBUGGING)) {
		cpu.SetIllegalInsnsEnabled(true);
		cpu.SetPathBreakEnabled(false);
		cpu.SetStopOnBRK(false);
	}

	if (IsButtonChecked(IDC_CHANGE_DISKIO)) {
		mpSim->SetDiskSIOPatchEnabled(false);
		mpSim->SetDiskAccurateTimingEnabled(true);
	}

	End(kATErrorAction_Restart);
	return true;
}

bool ATUIDialogEmuError::OnCancel() {
	End(kATErrorAction_Pause);
	return true;
}

bool ATUIDialogEmuError::OnCommand(uint32 id, uint32 extcode) {
	switch(id) {
		case IDC_DEBUG:
			End(kATErrorAction_Debug);
			return true;

		case IDC_RESET:
			End(kATErrorAction_Reset);
			return true;
	}

	return false;
}

ATErrorAction ATUIShowDialogEmuError(VDGUIHandle h, ATSimulator *sim) {
	ATUIDialogEmuError dlg(sim);

	return (ATErrorAction)dlg.ShowDialog(h);
}

///////////////////////////////////////////////////////////////////////////

class ATEmuErrorHandler {
public:
	void Init(VDGUIHandle h, ATSimulator *sim);
	void Shutdown();

private:
	void OnDebuggerOpen(IATDebugger *dbg, ATDebuggerOpenEvent *event);

	VDGUIHandle mParent;
	ATSimulator *mpSim;
	VDDelegate mDelDebuggerOpen;
};

void ATEmuErrorHandler::Init(VDGUIHandle h, ATSimulator *sim) {
	mParent = h;
	mpSim = sim;
	ATGetDebugger()->OnDebuggerOpen() += mDelDebuggerOpen.Bind(this, &ATEmuErrorHandler::OnDebuggerOpen);
}

void ATEmuErrorHandler::Shutdown() {
	ATGetDebugger()->OnDebuggerOpen() -= mDelDebuggerOpen;
}

void ATEmuErrorHandler::OnDebuggerOpen(IATDebugger *dbg, ATDebuggerOpenEvent *event) {
	switch(g_ATOptions.mErrorMode) {
		case kATErrorMode_Dialog:
			break;

		case kATErrorMode_Debug:
			return;

		case kATErrorMode_Pause:
			event->mbAllowOpen = false;
			return;

		case kATErrorMode_ColdReset:
			mpSim->ColdReset();
			mpSim->Resume();
			event->mbAllowOpen = false;
			return;
	}

	switch(ATUIShowDialogEmuError(mParent, mpSim)) {
		case kATErrorAction_Debug:
			break;

		case kATErrorAction_Pause:
			event->mbAllowOpen = false;
			break;

		case kATErrorAction_Reset:
			mpSim->WarmReset();
			mpSim->Resume();
			event->mbAllowOpen = false;
			break;

		case kATErrorAction_Restart:
			mpSim->ColdReset();
			mpSim->Resume();
			event->mbAllowOpen = false;
			break;
	}
}

ATEmuErrorHandler g_emuErrorHandler;

void ATInitEmuErrorHandler(VDGUIHandle h, ATSimulator *sim) {
	g_emuErrorHandler.Init(h, sim);
}

void ATShutdownEmuErrorHandler() {
	g_emuErrorHandler.Shutdown();
}