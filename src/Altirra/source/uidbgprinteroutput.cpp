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
#include "resource.h"
#include "simulator.h"
#include "texteditor.h"
#include "uidbgprinteroutput.h"

extern ATSimulator g_sim;

ATPrinterOutputWindow::ATPrinterOutputWindow()
	: ATUIPaneWindow(kATUIPaneId_PrinterOutput, L"Printer Output")
	, mhwndTextEditor(NULL)
	, mLineBufIdx(0)
{
	mPreferredDockCode = kATContainerDockBottom;
}

ATPrinterOutputWindow::~ATPrinterOutputWindow() {
}

void ATPrinterOutputWindow::WriteASCII(const void *buf, size_t len) {
	const uint8 *s = (const uint8 *)buf;

	while(len--) {
		uint8 c = *s++;

		if (c != 0x0A && c != 0x0D) {
			c &= 0x7f;
			
			if (c < 0x20 || c > 0x7F)
				c = '?';
		}

		if (mDropNextChar) {
			if (mDropNextChar == c) {
				mDropNextChar = 0;

				continue;
			}

			mDropNextChar = 0;
		}

		mLineBuf[mLineBufIdx++] = c;

		if (mLineBufIdx >= 130) {
			c = 0x0A;
			mLineBuf[mLineBufIdx++] = c;
		}

		if (c == 0x0D || c == 0x0A) {
			mDropNextChar = c ^ (uint8)(0x0D ^ 0x0A);
			mLineBuf[mLineBufIdx++] = 0x0A;
			mLineBuf[mLineBufIdx] = 0;

			if (mpTextEditor)
				mpTextEditor->Append((const char *)mLineBuf);

			mLineBufIdx = 0;
		}
	}
}

void ATPrinterOutputWindow::WriteATASCII(const void *buf, size_t len) {
	const uint8 *s = (const uint8 *)buf;

	while(len--) {
		uint8 c = *s++;

		if (c == 0x9B)
			c = '\n';
		else {
			c &= 0x7f;
			
			if (c < 0x20 || c > 0x7F)
				c = '?';
		}

		mLineBuf[mLineBufIdx++] = c;

		if (mLineBufIdx >= 130) {
			c = '\n';
			mLineBuf[mLineBufIdx++] = c;
		}

		if (c == '\n') {
			mLineBuf[mLineBufIdx] = 0;

			if (mpTextEditor)
				mpTextEditor->Append((const char *)mLineBuf);

			mLineBufIdx = 0;
		}
	}
}

LRESULT ATPrinterOutputWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_SIZE:
			OnSize();
			break;

		case WM_CONTEXTMENU:
			{
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);

				HMENU menu0 = LoadMenu(NULL, MAKEINTRESOURCE(IDR_PRINTER_CONTEXT_MENU));
				if (menu0) {
					HMENU menu = GetSubMenu(menu0, 0);
					BOOL cmd = 0;

					if (x >= 0 && y >= 0) {
						POINT pt = {x, y};

						if (ScreenToClient(mhwndTextEditor, &pt)) {
							mpTextEditor->SetCursorPixelPos(pt.x, pt.y);
							cmd = TrackPopupMenu(menu, TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD, x, y, 0, mhwnd, NULL);
						}
					} else {
						cmd = TrackPopupMenu(menu, TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD, x, y, 0, mhwnd, NULL);
					}

					DestroyMenu(menu0);

					switch(cmd) {
					case ID_CONTEXT_CLEAR:
						if (mhwndTextEditor)
							SetWindowText(mhwndTextEditor, L"");
						break;
					}
				}
			}
			break;
	}

	return ATUIPaneWindow::WndProc(msg, wParam, lParam);
}

bool ATPrinterOutputWindow::OnCreate() {
	if (!ATUIPaneWindow::OnCreate())
		return false;

	if (!VDCreateTextEditor(~mpTextEditor))
		return false;

	mhwndTextEditor = (HWND)mpTextEditor->Create(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd, 100);

	OnFontsUpdated();

	mpTextEditor->SetReadOnly(true);

	OnSize();

	g_sim.SetPrinterDefaultOutput(this);
	return true;
}

void ATPrinterOutputWindow::OnDestroy() {
	g_sim.SetPrinterDefaultOutput(nullptr);

	ATUIPaneWindow::OnDestroy();
}

void ATPrinterOutputWindow::OnSize() {
	RECT r;
	if (mhwndTextEditor && GetClientRect(mhwnd, &r)) {
		VDVERIFY(SetWindowPos(mhwndTextEditor, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER|SWP_NOACTIVATE));
	}
}

void ATPrinterOutputWindow::OnFontsUpdated() {
	if (mhwndTextEditor)
		SendMessage(mhwndTextEditor, WM_SETFONT, (WPARAM)ATGetConsoleFontW32(), TRUE);
}

void ATPrinterOutputWindow::OnSetFocus() {
	::SetFocus(mhwndTextEditor);
}

void ATUIDebuggerRegisterPrinterOutputPane() {
	ATRegisterUIPaneType(kATUIPaneId_PrinterOutput, VDRefCountObjectFactory<ATPrinterOutputWindow, ATUIPane>);
}
