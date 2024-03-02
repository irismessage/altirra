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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/uiframe.h>
#include "uidbgpane.h"
#include "uikeyboard.h"

void *ATUIDebuggerPane::AsInterface(uint32 iid) {
	if (iid == IATUIDebuggerPane::kTypeID)
		return static_cast<IATUIDebuggerPane *>(this);

	return ATUIPane::AsInterface(iid);
}

bool ATUIDebuggerPane::OnPaneCommand(ATUIPaneCommandId id) {
	return false;
}

std::optional<LRESULT> ATUIDebuggerPane::TryHandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case ATWM_PREKEYDOWN:
		case ATWM_PRESYSKEYDOWN:
			{
				const bool ctrl = GetKeyState(VK_CONTROL) < 0;
				const bool shift = GetKeyState(VK_SHIFT) < 0;
				const bool alt = GetKeyState(VK_MENU) < 0;
				const bool ext = (lParam & (1 << 24)) != 0;

				if (ATUIActivateVirtKeyMapping((uint32)wParam, alt, ctrl, shift, ext, false, kATUIAccelContext_Debugger))
					return TRUE;
			}
			break;

		case ATWM_PREKEYUP:
		case ATWM_PRESYSKEYUP:
			{
				const bool ctrl = GetKeyState(VK_CONTROL) < 0;
				const bool shift = GetKeyState(VK_SHIFT) < 0;
				const bool alt = GetKeyState(VK_MENU) < 0;
				const bool ext = (lParam & (1 << 24)) != 0;

				if (ATUIActivateVirtKeyMapping((uint32)wParam, alt, ctrl, shift, ext, true, kATUIAccelContext_Debugger))
					return TRUE;
			}
			break;

		case WM_CTLCOLORLISTBOX:
			if (ATUIIsDarkThemeActive()) {
				const ATUIThemeColors& tc = ATUIGetThemeColors();
				HDC hdc = (HDC)wParam;

				SetTextColor(hdc, VDSwizzleU32(tc.mContentFg) >> 8);
				SetDCBrushColor(hdc, VDSwizzleU32(tc.mContentBg) >> 8);

				return (LRESULT)GetStockObject(DC_BRUSH);
			}
			break;
	}

	return std::nullopt;
}

/////////////////////////////////////////////////////////////////////////////////////////

LRESULT ATUIDebuggerPaneWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	auto r = ATUIDebuggerPane::TryHandleMessage(msg, wParam, lParam);

	if (r.has_value())
		return r.value();

	switch(msg) {
		case WM_CTLCOLORLISTBOX:
			if (ATUIIsDarkThemeActive()) {
				const ATUIThemeColors& tc = ATUIGetThemeColors();
				HDC hdc = (HDC)wParam;

				SetTextColor(hdc, VDSwizzleU32(tc.mContentFg) >> 8);
				SetDCBrushColor(hdc, VDSwizzleU32(tc.mContentBg) >> 8);

				return (LRESULT)GetStockObject(DC_BRUSH);
			}
			break;
	}

	return ATUIPaneWindowBase::WndProc(msg, wParam, lParam);
}

VDZINT_PTR ATUIDebuggerPaneDialog::DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	auto r = ATUIDebuggerPane::TryHandleMessage(msg, wParam, lParam);

	if (r.has_value())
		return r.value();

	return ATUIPaneDialogBase::DlgProc(msg, wParam, lParam);
}
