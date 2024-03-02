//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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
#include <at/atcore/wraptime.h>
#include <at/atdebugger/historytree.h>
#include <at/atdebugger/historytreebuilder.h>
#include <at/atnativeui/theme_win32.h>
#include "cassette.h"
#include "simulator.h"
#include "uicommondialogs.h"
#include "uidbghistory.h"

extern ATSimulator g_sim;

ATHistoryWindow::ATHistoryWindow()
	: ATUIDebuggerPaneWindow(kATUIPaneId_History, L"History")
{
	mPreferredDockCode = kATContainerDockRight;
}

ATHistoryWindow::~ATHistoryWindow() {
}

void *ATHistoryWindow::AsInterface(uint32 iid) {
	if (iid == IATUIDebuggerHistoryPane::kTypeID)
		return static_cast<IATUIDebuggerHistoryPane *>(this);

	return ATUIDebuggerPaneWindow::AsInterface(iid);
}

bool ATHistoryWindow::JumpToCycle(uint32 cycle) {
	return mpHistoryView && mpHistoryView->JumpToCycle(cycle);
}

double ATHistoryWindow::DecodeTapeSample(uint32 cycle) {
	auto& cassette = g_sim.GetCassette();
	const bool isRunning = cassette.IsMotorRunning();

	auto& sch = *g_sim.GetScheduler();
	uint32 referenceCycle = isRunning ? sch.GetTick() : (uint32)cassette.GetLastStopCycle();
	double referencePos = isRunning ? cassette.GetSamplePos() : cassette.GetLastStopSamplePos();
	sint32 referenceOffset = isRunning ? cassette.GetSampleCycleOffset() : 0;

	if (!isRunning && ATWrapTime{cycle} >= referenceCycle)
		return referencePos;

	const sint32 tsDelta = (sint32)(cycle - sch.GetTick());

	return referencePos + ((double)(tsDelta + referenceOffset)) / (double)kATCassetteCyclesPerDataSample;
}

double ATHistoryWindow::DecodeTapeSeconds(uint32 cycle) {
	auto& cassette = g_sim.GetCassette();
	const bool isRunning = cassette.IsMotorRunning();

	auto& sch = *g_sim.GetScheduler();
	uint32 referenceCycle = isRunning ? sch.GetTick() : (uint32)cassette.GetLastStopCycle();
	double referencePos = (isRunning ? cassette.GetSamplePos() : cassette.GetLastStopSamplePos()) / (double)kATCassetteDataSampleRate;
	sint32 referenceOffset = isRunning ? cassette.GetSampleCycleOffset() : 0;

	if (!isRunning && ATWrapTime{cycle} >= referenceCycle)
		return referencePos;

	const sint32 tsDelta = (sint32)(cycle - referenceCycle);

	// In PAL we run the tape slightly faster, so we need to use the tape rate here and not the actual machine cycle rate.
	return referencePos + (double)(tsDelta + referenceOffset) / (kATCassetteDataSampleRateD * kATCassetteCyclesPerDataSample);
}

uint32 ATHistoryWindow::ConvertRawTimestamp(uint32 rawCycle) {
	return vdpoly_cast<IATDebugTargetHistory *>(ATGetDebugger()->GetTarget())->ConvertRawTimestamp(rawCycle);
}

float ATHistoryWindow::ConvertRawTimestampDeltaF(sint32 rawCycleDelta) {
	return (float)((double)rawCycleDelta / vdpoly_cast<IATDebugTargetHistory *>(ATGetDebugger()->GetTarget())->GetTimestampFrequency());
}

ATCPUBeamPosition ATHistoryWindow::DecodeBeamPosition(uint32 cycle) {
	return g_sim.GetTimestampDecoder().GetBeamPosition(cycle);
}

bool ATHistoryWindow::IsInterruptPositionVBI(uint32 cycle) {
	return g_sim.GetTimestampDecoder().IsInterruptPositionVBI(cycle);
}

bool ATHistoryWindow::UpdatePreviewNode(ATCPUHistoryEntry& he) {
	IATDebugTarget *target = ATGetDebugger()->GetTarget();

	const auto dmode = target->GetDisasmMode();

	ATCPUExecState state;
	target->GetExecState(state);

	const bool isZ80 = dmode == kATDebugDisasmMode_Z80;
	const bool is6809 = dmode == kATDebugDisasmMode_6809;
	const bool is65xx = !isZ80 && !is6809;

	if (!(is65xx ? state.m6502.mbAtInsnStep : state.mZ80.mbAtInsnStep))
		return false;

	ATCPUHistoryEntry hentLast {};
		
	if (is65xx) {
		const ATCPUExecState6502& state6502 = state.m6502;

		hentLast.mA = state6502.mA;
		hentLast.mX = state6502.mX;
		hentLast.mY = state6502.mY;
		hentLast.mS = state6502.mS;
		hentLast.mPC = state6502.mPC;
		hentLast.mP = state6502.mP;
		hentLast.mbEmulation = state6502.mbEmulationFlag;
		hentLast.mExt.mSH = state6502.mSH;
		hentLast.mExt.mAH = state6502.mAH;
		hentLast.mExt.mXH = state6502.mXH;
		hentLast.mExt.mYH = state6502.mYH;
		hentLast.mB = state6502.mB;
		hentLast.mK = state6502.mK;
		hentLast.mD = state6502.mDP;
		uint32 addr24 = (uint32)state6502.mPC + ((uint32)state6502.mK << 16);
		uint8 opcode = target->DebugReadByte(addr24);
		uint8 byte1 = target->DebugReadByte((addr24+1) & 0xffffff);
		uint8 byte2 = target->DebugReadByte((addr24+2) & 0xffffff);
		uint8 byte3 = target->DebugReadByte((addr24+3) & 0xffffff);

		hentLast.mOpcode[0] = opcode;
		hentLast.mOpcode[1] = byte1;
		hentLast.mOpcode[2] = byte2;
		hentLast.mOpcode[3] = byte3;
	} else {
		const ATCPUExecStateZ80& stateZ80 = state.mZ80;

		hentLast.mZ80_A = stateZ80.mA;
		hentLast.mZ80_F = stateZ80.mF;
		hentLast.mZ80_B = stateZ80.mB;
		hentLast.mZ80_C = stateZ80.mC;
		hentLast.mZ80_D = stateZ80.mD;
		hentLast.mExt.mZ80_E = stateZ80.mE;
		hentLast.mExt.mZ80_H = stateZ80.mH;
		hentLast.mExt.mZ80_L = stateZ80.mL;
		hentLast.mZ80_SP = stateZ80.mSP;
		hentLast.mPC = stateZ80.mPC;
		hentLast.mbEmulation = true;
		hentLast.mB = 0;
		hentLast.mK = 0;
		hentLast.mD = 0;

		uint32 addr16 = stateZ80.mPC;
		uint8 opcode = target->DebugReadByte(addr16);
		uint8 byte1 = target->DebugReadByte((addr16+1) & 0xffff);
		uint8 byte2 = target->DebugReadByte((addr16+2) & 0xffff);
		uint8 byte3 = target->DebugReadByte((addr16+3) & 0xffff);

		hentLast.mOpcode[0] = opcode;
		hentLast.mOpcode[1] = byte1;
		hentLast.mOpcode[2] = byte2;
		hentLast.mOpcode[3] = byte3;
	}

	he = hentLast;
	return true;
}

uint32 ATHistoryWindow::ReadInsns(const ATCPUHistoryEntry **ppInsns, uint32 startIndex, uint32 n) {
	if (!n)
		return 0;

	IATDebugTargetHistory *history = vdpoly_cast<IATDebugTargetHistory *>(ATGetDebugger()->GetTarget());

	return history->ExtractHistory(ppInsns, startIndex, n);
}

void ATHistoryWindow::OnEsc() {
	if (ATGetUIPane(kATUIPaneId_Console))
		ATActivateUIPane(kATUIPaneId_Console, true);
}

void ATHistoryWindow::JumpToInsn(uint32 pc) {
	ATGetDebugger()->SetFrameExtPC(pc);

	ATActivateUIPane(kATUIPaneId_Disassembly, true);
}

void ATHistoryWindow::JumpToSource(uint32 pc) {
	if (!ATConsoleShowSource(pc)) {
		VDStringW s;
		s.sprintf(L"There is no source line associated with the address: %hs.", ATGetDebugger()->GetAddressText(pc, true).c_str());
		ATUIShowError((VDGUIHandle)mhwnd, s.c_str());
	}
}

LRESULT ATHistoryWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_SETFOCUS:
			if (!mbHistoryError)
				SetFocus(mpHistoryView->AsNativeWindow()->GetHandleW32());
			break;

		case WM_PAINT:
			OnPaint();
			return 0;
	}

	return ATUIDebuggerPaneWindow::WndProc(msg, wParam, lParam);
}

bool ATHistoryWindow::OnCreate() {
	if (!ATUIDebuggerPaneWindow::OnCreate())
		return false;

	if (!ATUICreateHistoryView((VDGUIHandle)mhwnd, ~mpHistoryView))
		return false;

	mpHistoryView->SetHistoryModel(this);

	OnFontsUpdated();
	CheckDisasmMode();

	const bool historyEnabled = g_sim.GetCPU().IsHistoryEnabled();

	SetHistoryError(!historyEnabled);

	OnSize();
	RemakeHistoryView();
	ATGetDebugger()->AddClient(this, true);
	return true;
}

void ATHistoryWindow::OnDestroy() {
	if (mpHistoryView) {
		mpHistoryView->AsNativeWindow()->Destroy();
		mpHistoryView = nullptr;
	}

	ATGetDebugger()->RemoveClient(this);
	ATUIDebuggerPaneWindow::OnDestroy();
}

void ATHistoryWindow::OnSize() {
	RECT r;

	if (!GetClientRect(mhwnd, &r))
		return;

	if (mpHistoryView)
		SetWindowPos(mpHistoryView->AsNativeWindow()->GetHandleW32(), nullptr, 0, 0, r.right, r.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
}

void ATHistoryWindow::OnFontsUpdated() {
	mpHistoryView->SetFonts(ATConsoleGetPropFontW32(), ATConsoleGetPropFontLineHeightW32(), ATGetConsoleFontW32(), ATGetConsoleFontLineHeightW32());

	InvalidateRect(mhwnd, nullptr, TRUE);
}

void ATHistoryWindow::OnPaint() {
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(mhwnd, &ps);
	if (!hdc)
		return;

	int sdc = SaveDC(hdc);
	if (sdc) {
		if (mbHistoryError) {
			const ATUIThemeColorsW32& tc = ATUIGetThemeColorsW32();

			SelectObject(hdc, ATConsoleGetPropFontW32());
			SetBkMode(hdc, TRANSPARENT);

			RECT rText {};
			GetClientRect(mhwnd, &rText);
			FillRect(hdc, &rText, tc.mContentBgBrush);

			InflateRect(&rText, -GetSystemMetrics(SM_CXEDGE)*2, -GetSystemMetrics(SM_CYEDGE)*2);

			SetTextColor(hdc, tc.mContentFgCRef);

			static const WCHAR kText[] = L"History cannot be displayed because CPU history tracking is not enabled. History tracking can be enabled in CPU Options.";
			DrawTextW(hdc, kText, (sizeof kText / sizeof(kText[0])) - 1, &rText, DT_TOP | DT_LEFT | DT_NOPREFIX | DT_WORDBREAK);
		}

		RestoreDC(hdc, sdc);
	}

	EndPaint(mhwnd, &ps);
}


void ATHistoryWindow::SetHistoryError(bool error) {
	if (mbHistoryError == error)
		return;

	mbHistoryError = error;

	if (error) {
		ShowWindow(mpHistoryView->AsNativeWindow()->GetHandleW32(), SW_HIDE);
		InvalidateRect(mhwnd, nullptr, TRUE);
	} else {
		ShowWindow(mpHistoryView->AsNativeWindow()->GetHandleW32(), SW_SHOWNOACTIVATE);
	}
}

void ATHistoryWindow::RemakeHistoryView() {
	IATDebugTargetHistory *history = vdpoly_cast<IATDebugTargetHistory *>(ATGetDebugger()->GetTarget());
	const auto historyRange = history->GetHistoryRange();

	mpHistoryView->ClearInsns();
	UpdateOpcodes();
}

void ATHistoryWindow::UpdateOpcodes() {
	IATDebugTargetHistory *history = vdpoly_cast<IATDebugTargetHistory *>(ATGetDebugger()->GetTarget());
	const auto historyRange = history->GetHistoryRange();

	CheckDisasmMode();
	mpHistoryView->UpdateInsns(historyRange.first, historyRange.second);
}

void ATHistoryWindow::CheckDisasmMode() {
	if (!mpHistoryView)
		return;

	const bool historyEnabled = g_sim.GetCPU().IsHistoryEnabled();

	if (historyEnabled) {
		SetHistoryError(false);
	} else {
		SetHistoryError(true);
		return;
	}

	IATDebugger *debugger = ATGetDebugger();
	ATDebugDisasmMode disasmMode = debugger->GetTarget()->GetDisasmMode();
	uint32 subCycles = 1;
	bool decodeAnticNMI = false;

	if (!debugger->GetTargetIndex()) {
		subCycles = g_sim.GetCPU().GetSubCycles();
		decodeAnticNMI = true;
	}

	if (mDisasmMode != disasmMode || mSubCycles != subCycles || mbDecodeAnticNMI != decodeAnticNMI) {
		mDisasmMode = disasmMode;
		mSubCycles = subCycles;
		mbDecodeAnticNMI = decodeAnticNMI;

		mpHistoryView->SetDisasmMode(disasmMode, subCycles, decodeAnticNMI);

		mLastHistoryCounter = vdpoly_cast<IATDebugTargetHistory *>(debugger->GetTarget())->GetHistoryRange().first;
	}
}

void ATHistoryWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	// Don't update while running; there's no need to do so, and
	// it causes extra work when closing the debugger.

	if (!state.mbRunning)
		UpdateOpcodes();
}

void ATHistoryWindow::OnDebuggerEvent(ATDebugEvent eventId) {
	CheckDisasmMode();

	if (mpHistoryView)
		mpHistoryView->RefreshAll();
}

void ATUIDebuggerRegisterHistoryPane() {
	ATRegisterUIPaneType(kATUIPaneId_History, VDRefCountObjectFactory<ATHistoryWindow, ATUIPane>);
}
