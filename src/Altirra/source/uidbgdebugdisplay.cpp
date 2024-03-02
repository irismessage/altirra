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
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <vd2/system/thunk.h>
#include <vd2/system/w32assist.h>
#include <vd2/VDDisplay/display.h>
#include "resource.h"
#include "simulator.h"
#include "uidbgdebugdisplay.h"

extern ATSimulator g_sim;

ATDebugDisplayWindow::ATDebugDisplayWindow(uint32 id)
	: ATUIDebuggerPaneWindow(id, L"Debug Display")
	, mhwndDisplay(NULL)
	, mhwndDLAddrCombo(NULL)
	, mhwndPFAddrCombo(NULL)
	, mhmenu(LoadMenu(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDR_DEBUGDISPLAY_CONTEXT_MENU)))
	, mComboResizeInProgress(0)
	, mpThunkDLEditCombo(NULL)
	, mpThunkPFEditCombo(NULL)
	, mpDisplay(NULL)
{
	mPreferredDockCode = kATContainerDockRight;

	mpThunkDLEditCombo = VDCreateFunctionThunkFromMethod(this, &ATDebugDisplayWindow::DLAddrEditWndProc, true);
	mpThunkPFEditCombo = VDCreateFunctionThunkFromMethod(this, &ATDebugDisplayWindow::PFAddrEditWndProc, true);
}

ATDebugDisplayWindow::~ATDebugDisplayWindow() {
	if (mhmenu)
		::DestroyMenu(mhmenu);

	if (mpThunkDLEditCombo)
		VDDestroyFunctionThunk(mpThunkDLEditCombo);

	if (mpThunkPFEditCombo)
		VDDestroyFunctionThunk(mpThunkPFEditCombo);
}

LRESULT ATDebugDisplayWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_SIZE:
			OnSize();
			break;

		case WM_ERASEBKGND:
			return TRUE;

		case WM_CONTEXTMENU:
			{
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);

				HMENU menu = GetSubMenu(mhmenu, 0);

				const IVDVideoDisplay::FilterMode filterMode = mpDisplay->GetFilterMode();
				VDCheckRadioMenuItemByCommandW32(menu, ID_FILTERMODE_POINT, filterMode == IVDVideoDisplay::kFilterPoint);
				VDCheckRadioMenuItemByCommandW32(menu, ID_FILTERMODE_BILINEAR, filterMode == IVDVideoDisplay::kFilterBilinear);
				VDCheckRadioMenuItemByCommandW32(menu, ID_FILTERMODE_BICUBIC, filterMode == IVDVideoDisplay::kFilterBicubic);

				const ATDebugDisplay::PaletteMode paletteMode = mDebugDisplay.GetPaletteMode();
				VDCheckRadioMenuItemByCommandW32(menu, ID_PALETTE_CURRENTREGISTERVALUES, paletteMode == ATDebugDisplay::kPaletteMode_Registers);
				VDCheckRadioMenuItemByCommandW32(menu, ID_PALETTE_ANALYSIS, paletteMode == ATDebugDisplay::kPaletteMode_Analysis);

				if (x != -1 && y != -1) {
					TrackPopupMenu(menu, TPM_LEFTALIGN|TPM_TOPALIGN, x, y, 0, mhwnd, NULL);
				} else {
					POINT pt = {0, 0};

					if (ClientToScreen(mhwnd, &pt))
						TrackPopupMenu(menu, TPM_LEFTALIGN|TPM_TOPALIGN, pt.x, pt.y, 0, mhwnd, NULL);
				}
			}
			break;

		case WM_COMMAND:
			if (lParam) {
				if (lParam == (LPARAM)mhwndDLAddrCombo) {
					if (HIWORD(wParam) == CBN_SELCHANGE) {
						int sel = (int)SendMessageW(mhwndDLAddrCombo, CB_GETCURSEL, 0, 0);

						switch(sel) {
							case 0:
								mDebugDisplay.SetMode(ATDebugDisplay::kMode_AnticHistory);
								break;

							case 1:
								mDebugDisplay.SetMode(ATDebugDisplay::kMode_AnticHistoryStart);
								break;
						}

						mDebugDisplay.Update();
						return 0;
					}
				} else if (lParam == (LPARAM)mhwndPFAddrCombo) {
					if (HIWORD(wParam) == CBN_SELCHANGE) {
						int sel = (int)SendMessageW(mhwndDLAddrCombo, CB_GETCURSEL, 0, 0);

						if (sel >= 0)
							mDebugDisplay.SetPFAddrOverride(-1);

						mDebugDisplay.Update();
						return 0;
					}
				}
			} else {
				switch(LOWORD(wParam)) {
					case ID_CONTEXT_FORCEUPDATE:
						mDebugDisplay.Update();
						break;

					case ID_FILTERMODE_POINT:
						mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterPoint);
						mDebugDisplay.Update();
						break;

					case ID_FILTERMODE_BILINEAR:
						mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterBilinear);
						mDebugDisplay.Update();
						break;

					case ID_FILTERMODE_BICUBIC:
						mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterBicubic);
						mDebugDisplay.Update();
						break;

					case ID_PALETTE_CURRENTREGISTERVALUES:
						mDebugDisplay.SetPaletteMode(ATDebugDisplay::kPaletteMode_Registers);
						mDebugDisplay.Update();
						break;

					case ID_PALETTE_ANALYSIS:
						mDebugDisplay.SetPaletteMode(ATDebugDisplay::kPaletteMode_Analysis);
						mDebugDisplay.Update();
						break;
				}
			}
			break;
	}

	return ATUIDebuggerPaneWindow::WndProc(msg, wParam, lParam);
}

bool ATDebugDisplayWindow::OnCreate() {
	if (!ATUIDebuggerPaneWindow::OnCreate())
		return false;

	mhwndDLAddrCombo = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_VISIBLE|WS_CHILD|WS_CLIPSIBLINGS|CBS_DROPDOWN|CBS_HASSTRINGS|CBS_AUTOHSCROLL, 0, 0, 0, 0, mhwnd, (HMENU)102, VDGetLocalModuleHandleW32(), NULL);
	if (mhwndDLAddrCombo) {
		SendMessageW(mhwndDLAddrCombo, WM_SETFONT, (WPARAM)ATConsoleGetPropFontW32(), TRUE);
		SendMessageW(mhwndDLAddrCombo, CB_ADDSTRING, 0, (LPARAM)L"Auto DL (History)");
		SendMessageW(mhwndDLAddrCombo, CB_ADDSTRING, 0, (LPARAM)L"Auto DL (History start)");
		SendMessageW(mhwndDLAddrCombo, CB_SETCURSEL, 0, 0);
		SendMessageW(mhwndDLAddrCombo, CB_SETEDITSEL, 0, MAKELONG(-1, 0));

		COMBOBOXINFO cbi = {sizeof(COMBOBOXINFO)};
		if (mpThunkDLEditCombo && GetComboBoxInfo(mhwndDLAddrCombo, &cbi)) {
			mWndProcDLAddrEdit = (WNDPROC)GetWindowLongPtrW(cbi.hwndItem, GWLP_WNDPROC);
			SetWindowLongPtrW(cbi.hwndItem, GWLP_WNDPROC, (LONG_PTR)VDGetThunkFunction<WNDPROC>(mpThunkDLEditCombo));
		}
	}

	mhwndPFAddrCombo = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_VISIBLE|WS_CHILD|WS_CLIPSIBLINGS|CBS_DROPDOWN|CBS_HASSTRINGS|CBS_AUTOHSCROLL, 0, 0, 0, 0, mhwnd, (HMENU)103, VDGetLocalModuleHandleW32(), NULL);
	if (mhwndPFAddrCombo) {
		SendMessageW(mhwndPFAddrCombo, WM_SETFONT, (WPARAM)ATConsoleGetPropFontW32(), TRUE);
		SendMessageW(mhwndPFAddrCombo, CB_ADDSTRING, 0, (LPARAM)L"Auto PF Address");
		SendMessageW(mhwndPFAddrCombo, CB_SETCURSEL, 0, 0);
		SendMessageW(mhwndPFAddrCombo, CB_SETEDITSEL, 0, MAKELONG(-1, 0));

		COMBOBOXINFO cbi = {sizeof(COMBOBOXINFO)};
		if (mpThunkPFEditCombo && GetComboBoxInfo(mhwndPFAddrCombo, &cbi)) {
			mWndProcPFAddrEdit = (WNDPROC)GetWindowLongPtrW(cbi.hwndItem, GWLP_WNDPROC);
			SetWindowLongPtrW(cbi.hwndItem, GWLP_WNDPROC, (LONG_PTR)VDGetThunkFunction<WNDPROC>(mpThunkPFEditCombo));
		}
	}

	mhwndDisplay = (HWND)VDCreateDisplayWindowW32(0, WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, (VDGUIHandle)mhwnd);
	if (!mhwndDisplay)
		return false;

	SetWindowLong(mhwndDisplay, GWL_ID, 101);

	mpDisplay = VDGetIVideoDisplay((VDGUIHandle)mhwndDisplay);
	mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterBilinear);
	mpDisplay->SetAccelerationMode(IVDVideoDisplay::kAccelAlways);

	mDebugDisplay.Init(g_sim.GetMemoryManager(), &g_sim.GetAntic(), &g_sim.GetGTIA(), mpDisplay);
	DeferUpdate();

	OnSize();
	ATGetDebugger()->AddClient(this, false);
	return true;
}

void ATDebugDisplayWindow::OnDestroy() {
	mDebugDisplay.Shutdown();
	mpDisplay = NULL;
	mhwndDisplay = NULL;
	mhwndDLAddrCombo = NULL;
	mhwndPFAddrCombo = NULL;

	ATGetDebugger()->RemoveClient(this);
	ATUIDebuggerPaneWindow::OnDestroy();
}

void ATDebugDisplayWindow::OnSize() {
	RECT r;
	if (!GetClientRect(mhwnd, &r))
		return;

	RECT rCombo;
	int comboHeight = 0;
	if (mhwndDLAddrCombo && GetWindowRect(mhwndDLAddrCombo, &rCombo)) {
		comboHeight = rCombo.bottom - rCombo.top;

		// The Win32 combo box has bizarre behavior where it will highlight and reselect items
		// when it is resized. This has to do with the combo box updating the listbox drop
		// height and thinking that the drop down has gone away, causing an autocomplete
		// action. Since we already have the edit controls subclassed, we block the WM_SETTEXT
		// and EM_SETSEL messages during the resize to prevent this goofy behavior.

		++mComboResizeInProgress;

		SetWindowPos(mhwndDLAddrCombo, NULL, 0, 0, r.right >> 1, comboHeight * 5, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

		if (mhwndPFAddrCombo) {
			SetWindowPos(mhwndPFAddrCombo, NULL, r.right >> 1, 0, (r.right + 1) >> 1, comboHeight * 5, SWP_NOZORDER | SWP_NOACTIVATE);
		}

		--mComboResizeInProgress;
	}

	if (mpDisplay) {
		vdrect32 rd(0, 0, r.right, r.bottom - comboHeight);
		sint32 w = rd.width();
		sint32 h = std::max(rd.height(), 0);
		int sw = 376;
		int sh = 240;

		SetWindowPos(mhwndDisplay, NULL, 0, comboHeight, w, h, SWP_NOZORDER | SWP_NOACTIVATE);

		if (w && h) {
			if (w*sh < h*sw) {		// (w / sw) < (h / sh) -> w*sh < h*sw
				// width is smaller ratio -- compute restricted height
				int restrictedHeight = (sh * w + (sw >> 1)) / sw;

				rd.top = (h - restrictedHeight) >> 1;
				rd.bottom = rd.top + restrictedHeight;
			} else {
				// height is smaller ratio -- compute restricted width
				int restrictedWidth = (sw * h + (sh >> 1)) / sh;

				rd.left = (w - restrictedWidth) >> 1;
				rd.right = rd.left+ restrictedWidth;
			}
		}

		mpDisplay->SetDestRect(&rd, 0);
	}
}

void ATDebugDisplayWindow::OnFontsUpdated() {
	if (mhwndDLAddrCombo)
		SendMessage(mhwndDLAddrCombo, WM_SETFONT, (WPARAM)ATConsoleGetPropFontW32(), TRUE);

	if (mhwndPFAddrCombo)
		SendMessage(mhwndPFAddrCombo, WM_SETFONT, (WPARAM)ATConsoleGetPropFontW32(), TRUE);

	OnSize();
}

LRESULT ATDebugDisplayWindow::DLAddrEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (mComboResizeInProgress) {
		switch(msg) {
			case WM_SETTEXT:
			case EM_SETSEL:
				return 0;
		}
	}

	if (msg == WM_CHAR) {
		if (wParam == '\r') {
			VDStringA s = VDGetWindowTextAW32(hwnd);

			sint32 addr = ATGetDebugger()->ResolveSymbol(s.c_str());
			if (addr < 0 || addr > 0xffff)
				MessageBeep(MB_ICONERROR);
			else {
				mDebugDisplay.SetMode(ATDebugDisplay::kMode_AnticHistoryStart);
				mDebugDisplay.SetDLAddrOverride(addr);
				mDebugDisplay.Update();
				SendMessageW(mhwndDLAddrCombo, CB_SETEDITSEL, 0, MAKELONG(0, -1));
			}
			return 0;
		}
	}

	return CallWindowProcW(mWndProcDLAddrEdit, hwnd, msg, wParam, lParam);
}

LRESULT ATDebugDisplayWindow::PFAddrEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (mComboResizeInProgress) {
		switch(msg) {
			case WM_SETTEXT:
			case EM_SETSEL:
				return 0;
		}
	}

	if (msg == WM_CHAR) {
		if (wParam == '\r') {
			VDStringA s = VDGetWindowTextAW32(hwnd);

			sint32 addr = ATGetDebugger()->ResolveSymbol(s.c_str());
			if (addr < 0 || addr > 0xffff)
				MessageBeep(MB_ICONERROR);
			else {
				mDebugDisplay.SetPFAddrOverride(addr);
				mDebugDisplay.Update();
				SendMessageW(mhwndPFAddrCombo, CB_SETEDITSEL, 0, MAKELONG(0, -1));
			}
			return 0;
		}
	}

	return CallWindowProcW(mWndProcPFAddrEdit, hwnd, msg, wParam, lParam);
}

void ATDebugDisplayWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	DeferUpdate();
}

void ATDebugDisplayWindow::OnDebuggerEvent(ATDebugEvent eventId) {
}

void ATDebugDisplayWindow::OnDeferredUpdate() {
	mDebugDisplay.Update();
}

void ATUIDebuggerRegisterDebugDisplayPane() {
	ATRegisterUIPaneType(kATUIPaneId_DebugDisplay, VDRefCountObjectFactory<ATDebugDisplayWindow, ATUIPane>);
}
