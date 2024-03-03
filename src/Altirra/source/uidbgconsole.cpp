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
#include <richedit.h>
#include <vd2/system/thunk.h>
#include <vd2/system/w32assist.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/theme_win32.h>
#include "console.h"
#include "resource.h"
#include "uidbgconsole.h"

void ATConsoleQueueCommand(const char *s);

IATUIDebuggerConsoleWindow *g_pConsoleWindow;

ATConsoleWindow::ATConsoleWindow()
	: ATUIDebuggerPaneWindow(kATUIPaneId_Console, L"Console")
	, mMenu(LoadMenu(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDR_DEBUGGER_MENU)))
	, mhwndLog(NULL)
	, mhwndPrompt(NULL)
	, mhwndEdit(NULL)
	, mpLogThunk(NULL)
	, mLogProc(NULL)
	, mpCmdEditThunk(NULL)
	, mCmdEditProc(NULL)
{
	mPreferredDockCode = kATContainerDockBottom;
}

ATConsoleWindow::~ATConsoleWindow() {
	if (mMenu)
		DestroyMenu(mMenu);
}

LRESULT ATConsoleWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
	case WM_SIZE:
		OnSize();
		return 0;
	case WM_COMMAND:
		switch(LOWORD(wParam)) {
			case ID_CONTEXT_COPY:
				SendMessage(mhwndLog, WM_COPY, 0, 0);
				return 0;

			case ID_CONTEXT_CLEARALL:
				SetWindowText(mhwndLog, _T(""));
				mAppendBuffer.clear();
				return 0;
		}
		break;
	case WM_SETFOCUS:
		SetFocus(mhwndEdit);
		return 0;
	case WM_SYSCOMMAND:
		// block F10... unfortunately, this blocks plain Alt too
		if (!lParam)
			return 0;
		break;
	case WM_CONTEXTMENU:
		{
			int x = GET_X_LPARAM(lParam);
			int y = GET_Y_LPARAM(lParam);

			if (x == -1 && y == -1) {
				const auto& r = GetClientArea();

				vdpoint32 cpt(r.left + (r.width() >> 1), r.top + (r.height() >> 1));
				vdpoint32 spt = TransformClientToScreen(cpt);

				x = spt.x;
				y = spt.y;
			}

			TrackPopupMenu(GetSubMenu(mMenu, 0), TPM_LEFTALIGN|TPM_TOPALIGN, x, y, 0, mhwnd, NULL);
		}
		return 0;

	case WM_TIMER:
		if (wParam == kTimerId_DisableEdit) {
			if (mbRunState && mhwndEdit) {
				if (!mbEditShownDisabled) {
					mbEditShownDisabled = true;

					// If we have the focus, hand it off to the display.
					HWND hwndFocus = GetFocus();
					while(hwndFocus) {
						if (hwndFocus == mhwnd) {
							if (ATGetUIPane(kATUIPaneId_Display))
								ATActivateUIPane(kATUIPaneId_Display, true);
							break;
						}

						hwndFocus = GetAncestor(hwndFocus, GA_PARENT);
					}

					SendMessage(mhwndEdit, EM_SETREADONLY, TRUE, 0);

					if (ATUIIsDarkThemeActive()) {
						CHARFORMAT2 cf = {};
						cf.cbSize = sizeof cf;
						cf.dwMask = CFM_COLOR | CFM_BACKCOLOR | CFM_EFFECTS;
						cf.dwEffects = 0;
						cf.crTextColor = RGB(216, 216, 216);
						cf.crBackColor = RGB(128, 128, 128);

						SendMessage(mhwndEdit, EM_SETCHARFORMAT, SCF_ALL | SCF_DEFAULT, (LPARAM)&cf);
						SendMessage(mhwndEdit, EM_SETBKGNDCOLOR, FALSE, RGB(128, 128, 128));
					} else {
						SendMessage(mhwndEdit, EM_SETBKGNDCOLOR, FALSE, GetSysColor(COLOR_3DFACE));
					}
				}
			}

			KillTimer(mhwnd, wParam);
			return 0;
		} else if (wParam == kTimerId_AddText) {
			if (!mAppendBuffer.empty())
				FlushAppendBuffer();

			mbAppendTimerStarted = false;
			KillTimer(mhwnd, wParam);
			return 0;
		}

		break;
	}

	return ATUIDebuggerPaneWindow::WndProc(msg, wParam, lParam);
}

bool ATConsoleWindow::OnCreate() {
	if (!ATUIDebuggerPaneWindow::OnCreate())
		return false;

	mhwndLog = CreateWindowEx(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, _T(""), ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_VISIBLE|WS_CHILD, 0, 0, 0, 0, mhwnd, (HMENU)100, VDGetLocalModuleHandleW32(), NULL);
	if (!mhwndLog)
		return false;

	mhwndPrompt = CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_VISIBLE|WS_CHILD|ES_READONLY, 0, 0, 0, 0, mhwnd, (HMENU)100, VDGetLocalModuleHandleW32(), NULL);
	if (!mhwndPrompt)
		return false;

	mhwndEdit = CreateWindowEx(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, _T(""), WS_VISIBLE|WS_CHILD|ES_AUTOHSCROLL, 0, 0, 0, 0, mhwnd, (HMENU)100, VDGetLocalModuleHandleW32(), NULL);
	if (!mhwndEdit)
		return false;

	SendMessage(mhwndEdit, EM_SETTEXTMODE, TM_PLAINTEXT | TM_MULTILEVELUNDO | TM_MULTICODEPAGE, 0);

	mpLogThunk = VDCreateFunctionThunkFromMethod(this, &ATConsoleWindow::LogWndProc, true);

	mLogProc = (WNDPROC)GetWindowLongPtr(mhwndLog, GWLP_WNDPROC);
	SetWindowLongPtr(mhwndLog, GWLP_WNDPROC, (LONG_PTR)VDGetThunkFunction<WNDPROC>(mpLogThunk));

	mpCmdEditThunk = VDCreateFunctionThunkFromMethod(this, &ATConsoleWindow::CommandEditWndProc, true);

	mCmdEditProc = (WNDPROC)GetWindowLongPtr(mhwndEdit, GWLP_WNDPROC);
	SetWindowLongPtr(mhwndEdit, GWLP_WNDPROC, (LONG_PTR)VDGetThunkFunction<WNDPROC>(mpCmdEditThunk));

	OnFontsUpdated();

	OnSize();

	g_pConsoleWindow = this;

	IATDebugger *d = ATGetDebugger();

	d->OnPromptChanged() += mDelegatePromptChanged.Bind(this, &ATConsoleWindow::OnPromptChanged);
	d->OnRunStateChanged() += mDelegateRunStateChanged.Bind(this, &ATConsoleWindow::OnRunStateChanged);

	VDSetWindowTextFW32(mhwndPrompt, L"%hs>", d->GetPrompt());

	mbEditShownDisabled = false;		// deliberately wrong
	mbRunState = false;
	OnRunStateChanged(NULL, d->IsRunning());

	return true;
}

void ATConsoleWindow::OnDestroy() {
	IATDebugger *d = ATGetDebugger();

	d->OnPromptChanged() -= mDelegatePromptChanged;
	d->OnRunStateChanged() -= mDelegateRunStateChanged;

	g_pConsoleWindow = NULL;

	if (mhwndEdit) {
		DestroyWindow(mhwndEdit);
		mhwndEdit = NULL;
	}

	if (mpCmdEditThunk) {
		VDDestroyFunctionThunk(mpCmdEditThunk);
		mpCmdEditThunk = NULL;
	}

	if (mhwndLog) {
		DestroyWindow(mhwndLog);
		mhwndLog = NULL;
	}

	if (mpLogThunk) {
		VDDestroyFunctionThunk(mpLogThunk);
		mpLogThunk = NULL;
	}

	mhwndPrompt = NULL;

	ATUIDebuggerPaneWindow::OnDestroy();
}

void ATConsoleWindow::OnSize() {
	RECT r;

	if (GetClientRect(mhwnd, &r)) {
		int charWidth = 0;
		int lineHeight = 0;
		ATConsoleGetCharMetrics(charWidth, lineHeight);

		int prw = 10 * charWidth + 4*GetSystemMetrics(SM_CXEDGE);
		int h = lineHeight + 4*GetSystemMetrics(SM_CYEDGE);

		if (prw * 3 > r.right)
			prw = 0;

		if (mhwndLog) {
			VDVERIFY(SetWindowPos(mhwndLog, NULL, 0, 0, r.right, r.bottom > h ? r.bottom - h : 0, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
		}

		if (mhwndPrompt) {
			if (prw > 0) {
				VDVERIFY(SetWindowPos(mhwndPrompt, NULL, 0, r.bottom - h, prw, h, SWP_NOZORDER|SWP_NOACTIVATE));
				ShowWindow(mhwndPrompt, SW_SHOW);
			} else {
				ShowWindow(mhwndPrompt, SW_HIDE);
			}
		}

		if (mhwndEdit) {
			VDVERIFY(SetWindowPos(mhwndEdit, NULL, prw, r.bottom - h, r.right - prw, h, SWP_NOZORDER|SWP_NOACTIVATE));
		}
	}
}

void ATConsoleWindow::OnFontsUpdated() {
	OnThemeUpdated();
}

void ATConsoleWindow::OnThemeUpdated() {
	// RichEdit50W tries to be cute and retransforms the font given in WM_SETFONT to the
	// current DPI. Unfortunately, this just makes a mess of the font size, so we have to
	// bypass it and send EM_SETCHARFORMAT instead to get the size we actually specified
	// to stick.

	const bool useDark = ATUIIsDarkThemeActive();

	LOGFONT lf {};
	int ptSizeTenths = 0;
	ATConsoleGetFont(lf, ptSizeTenths);

	CHARFORMAT2 cf = {};
	cf.cbSize = sizeof cf;
	cf.dwMask = CFM_SIZE;
	cf.yHeight = ptSizeTenths * 2;

	if (useDark) {
		cf.dwMask |= CFM_COLOR | CFM_BACKCOLOR | CFM_EFFECTS;
		cf.dwEffects = 0;
		cf.crTextColor = ATUIGetThemeColorsW32().mContentFgCRef;
		cf.crBackColor = ATUIGetThemeColorsW32().mContentBgCRef;
	}

	if (mhwndLog) {
		SendMessage(mhwndLog, WM_SETFONT, (WPARAM)ATGetConsoleFontW32(), TRUE);
		SendMessage(mhwndLog, EM_SETCHARFORMAT, SCF_ALL | SCF_DEFAULT, (LPARAM)&cf);

		if (useDark)
			SendMessage(mhwndLog, EM_SETBKGNDCOLOR, FALSE, cf.crBackColor);
		else
			SendMessage(mhwndLog, EM_SETBKGNDCOLOR, TRUE, RGB(255, 255, 255));
	}

	if (mhwndEdit) {
		SendMessage(mhwndEdit, WM_SETFONT, (WPARAM)ATGetConsoleFontW32(), TRUE);
		SendMessage(mhwndEdit, EM_SETCHARFORMAT, SCF_ALL | SCF_DEFAULT, (LPARAM)&cf);

		if (useDark)
			SendMessage(mhwndEdit, EM_SETBKGNDCOLOR, FALSE, cf.crBackColor);
		else
			SendMessage(mhwndEdit, EM_SETBKGNDCOLOR, TRUE, RGB(255, 255, 255));
	}

	if (mhwndPrompt)
		SendMessage(mhwndPrompt, WM_SETFONT, (WPARAM)ATGetConsoleFontW32(), TRUE);

	OnSize();
}

void ATConsoleWindow::OnPromptChanged(IATDebugger *target, const char *prompt) {
	if (mhwndPrompt)
		VDSetWindowTextFW32(mhwndPrompt, L"%hs>", prompt);
}

void ATConsoleWindow::OnRunStateChanged(IATDebugger *target, bool rs) {
	if (mbRunState == rs)
		return;

	mbRunState = rs;

	if (mhwndEdit) {
		if (!rs) {
			if (mbEditShownDisabled) {
				mbEditShownDisabled = false;
				SendMessage(mhwndEdit, EM_SETREADONLY, FALSE, 0);

				if (ATUIIsDarkThemeActive()) {
					SendMessage(mhwndEdit, EM_SETBKGNDCOLOR, FALSE, RGB(32, 32, 32));

					CHARFORMAT2 cf = {};
					cf.cbSize = sizeof cf;
					cf.dwMask = CFM_COLOR | CFM_BACKCOLOR | CFM_EFFECTS;
					cf.dwEffects = 0;
					cf.crTextColor = RGB(216, 216, 216);
					cf.crBackColor = RGB(32, 32, 32);

					SendMessage(mhwndEdit, EM_SETCHARFORMAT, SCF_ALL | SCF_DEFAULT, (LPARAM)&cf);
				} else {
					SendMessage(mhwndEdit, EM_SETBKGNDCOLOR, TRUE, GetSysColor(COLOR_WINDOW));
				}

				// Check if the display currently has the focus. If so, take focus.
				auto *p = ATUIGetActivePane();
				if (p && p->GetUIPaneId() == kATUIPaneId_Display)
					ATActivateUIPane(kATUIPaneId_Console, true);
			}
		} else
			SetTimer(mhwnd, kTimerId_DisableEdit, 100, NULL);
	}
}

void ATConsoleWindow::Write(const char *s) {
	uint32 newLFCount = 0;
	uint32 newLFOffset = 0;
	while(*s) {
		const char *eol = strchr(s, '\n');
		const char *end = eol;

		if (!end)
			end = s + strlen(s);

		for(const char *t = s; t != end; ++t)
			mAppendBuffer += (wchar_t)(uint8)*t;

		if (!eol)
			break;

		mAppendBuffer += '\r';
		mAppendBuffer += '\n';
		++newLFCount;
		newLFOffset = (uint32)mAppendBuffer.size();

		s = eol+1;
	}

	if (newLFCount) {
		mAppendBufferLFCount += newLFCount;

		if (mAppendBufferLFCount >= mAppendBufferSplitLFCount + 4000) {
			if (mAppendBufferSplitLFCount) {
				mAppendBuffer.erase(0, mAppendBufferSplitOffset);

				newLFOffset -= mAppendBufferSplitOffset;
				mAppendBufferLFCount -= mAppendBufferSplitLFCount;
			}

			mAppendBufferSplitOffset = newLFOffset;
			mAppendBufferSplitLFCount = mAppendBufferLFCount;
			mbAppendForceClear = true;

			if (mbAppendTimerStarted && (uint32)(VDGetCurrentTick() - mAppendTimerStartTime) >= 50) {
				FlushAppendBuffer();

				KillTimer(mhwnd, kTimerId_AddText);
				mbAppendTimerStarted = false;
				return;
			}
		}
	}

	if (!mbAppendTimerStarted && !mAppendBuffer.empty()) {
		mbAppendTimerStarted = true;
		mAppendTimerStartTime = VDGetCurrentTick();

		SetTimer(mhwnd, kTimerId_AddText, 10, NULL);
	}
}

void ATConsoleWindow::ShowEnd() {
	// With RichEdit 3.0, EM_SCROLLCARET no longer works when the edit control lacks focus.
	SendMessage(mhwndLog, WM_VSCROLL, SB_BOTTOM, 0);
}

LRESULT ATConsoleWindow::LogWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
		if (wParam == VK_ESCAPE) {
			if (mhwndEdit)
				::SetFocus(mhwndEdit);
			return 0;
		}
	} else if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
		switch(wParam) {
			case VK_ESCAPE:
				return 0;
		}
	} else if (msg == WM_CHAR
		|| msg == WM_SYSCHAR
		|| msg == WM_DEADCHAR
		|| msg == WM_SYSDEADCHAR
		|| msg == WM_UNICHAR
		)
	{
		if (mhwndEdit && wParam >= 0x20) {
			::SetFocus(mhwndEdit);
			return ::SendMessage(mhwndEdit, msg, wParam, lParam);
		}
	}

	return CallWindowProc(mLogProc, hwnd, msg, wParam, lParam);
}

LRESULT ATConsoleWindow::CommandEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
		if (wParam == VK_ESCAPE) {
			ATActivateUIPane(kATUIPaneId_Display, true);
			return 0;
		} else if (wParam == VK_UP) {
			if (GetKeyState(VK_CONTROL) < 0) {
				if (mhwndLog)
					SendMessage(mhwndLog, EM_SCROLL, SB_LINEUP, 0);
			} else {
				if (mHistoryCurrent != mHistoryFront) {
					mHistoryCurrent = (mHistoryCurrent - 1) & (kHistorySize - 1);

					while(mHistoryCurrent != mHistoryFront) {
						uint32 i = (mHistoryCurrent - 1) & (kHistorySize - 1);

						char c = mHistory[i];

						if (!c)
							break;

						mHistoryCurrent = i;
					}
				}

				uint32 len = 0;

				if (mHistoryCurrent == mHistoryBack)
					SetWindowText(hwnd, _T(""));
				else {
					VDStringA s;

					for(uint32 i = mHistoryCurrent; mHistory[i]; i = (i+1) & (kHistorySize - 1))
						s += mHistory[i];

					SetWindowTextA(hwnd, s.c_str());
					len = s.size();
				}

				SendMessage(hwnd, EM_SETSEL, len, len);
			}
			return 0;
		} else if (wParam == VK_DOWN) {
			if (GetKeyState(VK_CONTROL) < 0) {
				if (mhwndLog)
					SendMessage(mhwndLog, EM_SCROLL, SB_LINEDOWN, 0);
			} else {
				if (mHistoryCurrent != mHistoryBack) {
					for(;;) {
						char c = mHistory[mHistoryCurrent];
						mHistoryCurrent = (mHistoryCurrent + 1) & (kHistorySize - 1);

						if (!c)
							break;
					}
				}

				uint32 len = 0;

				if (mHistoryCurrent == mHistoryBack)
					SetWindowText(hwnd, _T(""));
				else {
					VDStringA s;

					for(uint32 i = mHistoryCurrent; mHistory[i]; i = (i+1) & (kHistorySize - 1))
						s += mHistory[i];

					SetWindowTextA(hwnd, s.c_str());
					len = s.size();
				}

				SendMessage(hwnd, EM_SETSEL, len, len);
			}
			return 0;
		} else if (wParam == VK_PRIOR || wParam == VK_NEXT) {
			if (mhwndLog) {
				SendMessage(mhwndLog, msg, wParam, lParam);
				return 0;
			}
		} else if (wParam == VK_RETURN) {
			return 0;
		}
	} else if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
		switch(wParam) {
		case VK_ESCAPE:
		case VK_UP:
		case VK_DOWN:
		case VK_RETURN:
			return 0;

		case VK_PRIOR:
		case VK_NEXT:
			if (mhwndLog) {
				SendMessage(mhwndLog, msg, wParam, lParam);
				return 0;
			}
			break;
		}
	} else if (msg == WM_CHAR || msg == WM_SYSCHAR) {
		if (wParam == '\r') {
			if (mbRunState)
				return 0;

			const VDStringA& s = VDGetWindowTextAW32(mhwndEdit);

			if (!s.empty()) {
				AddToHistory(s.c_str());
				SetWindowTextW(mhwndEdit, L"");
			}

			try {
				ATConsoleQueueCommand(s.c_str());
			} catch(const MyError& e) {
				ATConsolePrintf("%s\n", e.gets());
			}

			return 0;
		}
	}

	return CallWindowProc(mCmdEditProc, hwnd, msg, wParam, lParam);
}

void ATConsoleWindow::AddToHistory(const char *s) {
	uint32 len = (uint32)strlen(s) + 1;

	if (len > kHistorySize)
		return;

	uint32 newLen = mHistorySize + len;

	// Remove strings if the history buffer is too full. We don't allow the
	// buffer to become completely full as that makes the start/end positions
	// ambiguous.
	if (newLen > (kHistorySize - 1)) {
		uint32 overflow = newLen - (kHistorySize - 1);

		if (overflow > 1) {
			mHistoryFront += (overflow - 1);
			if (mHistoryFront >= kHistorySize)
				mHistoryFront -= kHistorySize;

			mHistorySize -= (overflow - 1);
		}

		for(;;) {
			char c = mHistory[mHistoryFront];
			--mHistorySize;

			if (++mHistoryFront >= kHistorySize)
				mHistoryFront = 0;

			if (!c)
				break;
		}
	}

	if (mHistoryBack + len > kHistorySize) {
		uint32 tc1 = kHistorySize - mHistoryBack;
		uint32 tc2 = len - tc1;

		memcpy(mHistory + mHistoryBack, s, tc1);
		memcpy(mHistory, s + tc1, tc2);

		mHistoryBack = tc2;
	} else {
		memcpy(mHistory + mHistoryBack, s, len);
		mHistoryBack += len;
	}

	mHistorySize += len;
	mHistoryCurrent = mHistoryBack;
}

void ATConsoleWindow::FlushAppendBuffer() {
	bool redrawOff = false;

	if (mbAppendForceClear) {
		SETTEXTEX st { ST_DEFAULT | ST_UNICODE, 1200 };

		redrawOff = true;
		SendMessageW(mhwndLog, WM_SETREDRAW, FALSE, 0);
		SendMessageW(mhwndLog, EM_SETTEXTEX, (WPARAM)&st, (LPARAM)mAppendBuffer.data());
	} else {
		redrawOff = true;
		SendMessageW(mhwndLog, WM_SETREDRAW, FALSE, 0);

		if (SendMessage(mhwndLog, EM_GETLINECOUNT, 0, 0) > 5000) {
			POINT pt;
			SendMessage(mhwndLog, EM_GETSCROLLPOS, 0, (LPARAM)&pt);
			int idx = (int)SendMessage(mhwndLog, EM_LINEINDEX, 2000, 0);
			SendMessage(mhwndLog, EM_SETSEL, 0, idx);
			SendMessage(mhwndLog, EM_REPLACESEL, FALSE, (LPARAM)_T(""));
			SendMessage(mhwndLog, EM_SETSCROLLPOS, 0, (LPARAM)&pt);
		}

		SendMessageW(mhwndLog, EM_SETSEL, -1, -1);
		SendMessageW(mhwndLog, EM_REPLACESEL, FALSE, (LPARAM)mAppendBuffer.data());
	}

	SendMessageW(mhwndLog, EM_SETSEL, -1, -1);
	ShowEnd();

	if (redrawOff) {
		SendMessageW(mhwndLog, WM_SETREDRAW, TRUE, 0);
		RedrawWindow(mhwndLog, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	}

	mAppendBuffer.clear();
	mbAppendForceClear = false;
	mAppendBufferLFCount = 0;
	mAppendBufferSplitLFCount = 0;
	mAppendBufferSplitOffset = 0;
}

void ATUIDebuggerRegisterConsolePane() {
	ATRegisterUIPaneType(kATUIPaneId_Console, VDRefCountObjectFactory<ATConsoleWindow, ATUIPane>);
}
