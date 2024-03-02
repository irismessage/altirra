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
#include <vd2/system/binary.h>
#include <vd2/system/w32assist.h>
#include <at/atnativeui/theme.h>
#include "cpu.h"
#include "uidbgregisters.h"

ATRegistersWindow::ATRegistersWindow()
	: ATUIDebuggerPaneWindow(kATUIPaneId_Registers, L"Registers")
	, mhwndEdit(NULL)
{
	mPreferredDockCode = kATContainerDockRight;
}

ATRegistersWindow::~ATRegistersWindow() {
}

LRESULT ATRegistersWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_SIZE:
			OnSize();
			break;

		case WM_CTLCOLORSTATIC:
			{
				const auto& tc = ATUIGetThemeColors();
				HDC hdc = (HDC)wParam;
				const COLORREF bg = VDSwizzleU32(tc.mContentBg) >> 8;

				SetBkColor(hdc, bg);
				SetDCBrushColor(hdc, bg);
				SetTextColor(hdc, VDSwizzleU32(tc.mContentFg) >> 8);

				return (LRESULT)(HBRUSH)GetStockObject(DC_BRUSH);
			}
			break;
	}

	return ATUIDebuggerPaneWindow::WndProc(msg, wParam, lParam);
}

bool ATRegistersWindow::OnCreate() {
	if (!ATUIDebuggerPaneWindow::OnCreate())
		return false;

	mhwndEdit = CreateWindowEx(0, _T("EDIT"), _T(""), WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_AUTOVSCROLL|ES_MULTILINE|ES_READONLY, 0, 0, 0, 0, mhwnd, (HMENU)100, VDGetLocalModuleHandleW32(), NULL);
	if (!mhwndEdit)
		return false;

	OnFontsUpdated();
	OnSize();

	ATGetDebugger()->AddClient(this, true);
	return true;
}

void ATRegistersWindow::OnDestroy() {
	ATGetDebugger()->RemoveClient(this);
	ATUIDebuggerPaneWindow::OnDestroy();
}

void ATRegistersWindow::OnSize() {
	RECT r;
	if (mhwndEdit && GetClientRect(mhwnd, &r)) {
		RECT r2;
		r2.left = GetSystemMetrics(SM_CXEDGE);
		r2.top = GetSystemMetrics(SM_CYEDGE);
		r2.right = std::max<int>(r2.left, r.right - r2.left);
		r2.bottom = std::max<int>(r2.top, r.bottom - r2.top);
		SendMessage(mhwndEdit, EM_SETRECT, 0, (LPARAM)&r2);
		VDVERIFY(SetWindowPos(mhwndEdit, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE));
	}
}

void ATRegistersWindow::OnFontsUpdated() {
	if (mhwndEdit)
		SendMessage(mhwndEdit, WM_SETFONT, (WPARAM)ATGetConsoleFontW32(), TRUE);
}

void ATRegistersWindow::OnThemeUpdated() {
	if (mhwndEdit)
		InvalidateRect(mhwndEdit, NULL, TRUE);
}

void ATRegistersWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	mState.clear();

	mState.append_sprintf("Target: %s\r\n", state.mpDebugTarget->GetName());

	const auto dmode = state.mpDebugTarget->GetDisasmMode();
	if (dmode == kATDebugDisasmMode_8048) {
		const ATCPUExecState8048& state8048 = state.mExecState.m8048;
		mState.append_sprintf("PC = %04X\r\n", state8048.mPC);
		mState.append_sprintf("PSW = %02X\r\n", state8048.mPSW);
		mState.append_sprintf("A = %02X\r\n", state8048.mA);
	} else if (dmode == kATDebugDisasmMode_Z80) {
		const ATCPUExecStateZ80& stateZ80 = state.mExecState.mZ80;

		mState.append_sprintf("PC = %04X\r\n", stateZ80.mPC);
		mState.append_sprintf("SP = %04X\r\n", stateZ80.mSP);
		mState.append("\r\n");
		mState.append_sprintf("A = %02X\r\n", stateZ80.mA);
		mState.append_sprintf("F = %02X (%c%c%c%c%c%c)\r\n"
			, stateZ80.mF
			, (stateZ80.mF & 0x80) ? 'S' : '-'
			, (stateZ80.mF & 0x40) ? 'Z' : '-'
			, (stateZ80.mF & 0x10) ? 'H' : '-'
			, (stateZ80.mF & 0x04) ? 'P' : '-'
			, (stateZ80.mF & 0x02) ? 'N' : '-'
			, (stateZ80.mF & 0x01) ? 'C' : '-'
		);
		mState.append_sprintf("BC = %02X%02X\r\n", stateZ80.mB, stateZ80.mC);
		mState.append_sprintf("DE = %02X%02X\r\n", stateZ80.mD, stateZ80.mE);
		mState.append_sprintf("HL = %02X%02X\r\n", stateZ80.mH, stateZ80.mL);
		mState.append_sprintf("IX = %04X\r\n", stateZ80.mIX);
		mState.append_sprintf("IY = %04X\r\n", stateZ80.mIY);
		mState.append("\r\n");
		mState.append_sprintf("AF' = %02X%02X\r\n", stateZ80.mAltA, stateZ80.mAltF);
		mState.append_sprintf("BC' = %02X%02X\r\n", stateZ80.mAltB, stateZ80.mAltC);
		mState.append_sprintf("DE' = %02X%02X\r\n", stateZ80.mAltD, stateZ80.mAltE);
		mState.append_sprintf("HL' = %02X%02X\r\n", stateZ80.mAltH, stateZ80.mAltL);
		mState.append("\r\n");
		mState.append_sprintf("I = %02X\r\n", stateZ80.mI);
		mState.append_sprintf("R = %02X\r\n", stateZ80.mR);
	} else if (dmode == kATDebugDisasmMode_6809) {
		const ATCPUExecState6809& state6809 = state.mExecState.m6809;
		mState.append_sprintf("PC = %04X\r\n", state6809.mPC);
		mState.append_sprintf("A = %02X\r\n", state6809.mA);
		mState.append_sprintf("B = %02X\r\n", state6809.mB);
		mState.append_sprintf("X = %02X\r\n", state6809.mX);
		mState.append_sprintf("Y = %02X\r\n", state6809.mY);
		mState.append_sprintf("U = %02X\r\n", state6809.mU);
		mState.append_sprintf("S = %02X\r\n", state6809.mS);
		mState.append_sprintf("CC = %02X (%c%c%c%c%c%c%c%c)\r\n"
			, state6809.mCC
			, state6809.mCC & 0x80 ? 'E' : '-'
			, state6809.mCC & 0x40 ? 'F' : '-'
			, state6809.mCC & 0x20 ? 'H' : '-'
			, state6809.mCC & 0x10 ? 'I' : '-'
			, state6809.mCC & 0x08 ? 'N' : '-'
			, state6809.mCC & 0x04 ? 'Z' : '-'
			, state6809.mCC & 0x02 ? 'V' : '-'
			, state6809.mCC & 0x01 ? 'C' : '-'
		);
	} else if (dmode == kATDebugDisasmMode_65C816) {
		const ATCPUExecState6502& state6502 = state.mExecState.m6502;

		if (state6502.mbEmulationFlag)
			mState += "Mode: Emulation\r\n";
		else {
			mState.append_sprintf("Mode: Native (M%d X%d)\r\n"
				, state6502.mP & AT6502::kFlagM ? 8 : 16
				, state6502.mP & AT6502::kFlagX ? 8 : 16
				);
		}

		mState.append_sprintf("PC = %02X:%04X (%04X)\r\n", state6502.mK, state6502.mPC, state.mPC);

		if (state6502.mbEmulationFlag || (state6502.mP & AT6502::kFlagM)) {
			mState.append_sprintf("A = %02X\r\n", state6502.mA);
			mState.append_sprintf("B = %02X\r\n", state6502.mAH);
		} else
			mState.append_sprintf("A = %02X%02X\r\n", state6502.mAH, state6502.mA);

		if (state6502.mbEmulationFlag || (state6502.mP & AT6502::kFlagX)) {
			mState.append_sprintf("X = %02X\r\n", state6502.mX);
			mState.append_sprintf("Y = %02X\r\n", state6502.mY);
		} else {
			mState.append_sprintf("X = %02X%02X\r\n", state6502.mXH, state6502.mX);
			mState.append_sprintf("Y = %02X%02X\r\n", state6502.mYH, state6502.mY);
		}

		if (state6502.mbEmulationFlag)
			mState.append_sprintf("S = %02X\r\n", state6502.mS);
		else
			mState.append_sprintf("S = %02X%02X\r\n", state6502.mSH, state6502.mS);

		mState.append_sprintf("P = %02X\r\n", state6502.mP);

		if (state6502.mbEmulationFlag) {
			mState.append_sprintf("    %c%c%c%c%c%c\r\n"
				, state6502.mP & 0x80 ? 'N' : '-'
				, state6502.mP & 0x40 ? 'V' : '-'
				, state6502.mP & 0x08 ? 'D' : '-'
				, state6502.mP & 0x04 ? 'I' : '-'
				, state6502.mP & 0x02 ? 'Z' : '-'
				, state6502.mP & 0x01 ? 'C' : '-'
				);
		} else {
			mState.append_sprintf("    %c%c%c%c%c%c%c%c\r\n"
				, state6502.mP & 0x80 ? 'N' : '-'
				, state6502.mP & 0x40 ? 'V' : '-'
				, state6502.mP & 0x20 ? 'M' : '-'
				, state6502.mP & 0x10 ? 'X' : '-'
				, state6502.mP & 0x08 ? 'D' : '-'
				, state6502.mP & 0x04 ? 'I' : '-'
				, state6502.mP & 0x02 ? 'Z' : '-'
				, state6502.mP & 0x01 ? 'C' : '-'
				);
		}

		mState.append_sprintf("E = %d\r\n", state6502.mbEmulationFlag);
		mState.append_sprintf("D = %04X\r\n", state6502.mDP);
		mState.append_sprintf("B = %02X\r\n", state6502.mB);
	} else {
		const ATCPUExecState6502& state6502 = state.mExecState.m6502;

		mState.append_sprintf("PC = %04X (%04X)\r\n", state.mInsnPC, state6502.mPC);
		mState.append_sprintf("A = %02X\r\n", state6502.mA);
		mState.append_sprintf("X = %02X\r\n", state6502.mX);
		mState.append_sprintf("Y = %02X\r\n", state6502.mY);
		mState.append_sprintf("S = %02X\r\n", state6502.mS);
		mState.append_sprintf("P = %02X\r\n", state6502.mP);
		mState.append_sprintf("    %c%c%c%c%c%c\r\n"
			, state6502.mP & 0x80 ? 'N' : '-'
			, state6502.mP & 0x40 ? 'V' : '-'
			, state6502.mP & 0x08 ? 'D' : '-'
			, state6502.mP & 0x04 ? 'I' : '-'
			, state6502.mP & 0x02 ? 'Z' : '-'
			, state6502.mP & 0x01 ? 'C' : '-'
			);
	}

	SetWindowTextA(mhwndEdit, mState.c_str());
}

void ATUIDebuggerRegisterRegistersPane() {
	ATRegisterUIPaneType(kATUIPaneId_Registers, VDRefCountObjectFactory<ATRegistersWindow, ATUIPane>);
}
