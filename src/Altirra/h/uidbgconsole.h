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

#ifndef f_AT_UIDBGCONSOLE_H
#define f_AT_UIDBGCONSOLE_H

#include "console.h"
#include "debugger.h"
#include "uidbgpane.h"

class ATConsoleWindow final : public ATUIDebuggerPaneWindow, public IATUIDebuggerConsoleWindow {
public:
	ATConsoleWindow();
	~ATConsoleWindow();

	void Activate() {
		if (mhwnd)
			SetForegroundWindow(mhwnd);
	}

	void Write(const char *s);
	void ShowEnd();

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT LogWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT CommandEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate() override;
	void OnDestroy() override;
	void OnSize() override;
	void OnFontsUpdated() override;
	void OnThemeUpdated() override;

	void OnPromptChanged(IATDebugger *target, const char *prompt);
	void OnRunStateChanged(IATDebugger *target, bool runState);
	
	void AddToHistory(const char *s);
	void FlushAppendBuffer();

	enum {
		kTimerId_DisableEdit = 500,
		kTimerId_AddText = 501
	};

	HWND	mhwndLog;
	HWND	mhwndEdit;
	HWND	mhwndPrompt;
	HMENU	mMenu;

	VDFunctionThunkInfo	*mpLogThunk;
	WNDPROC	mLogProc;
	VDFunctionThunkInfo	*mpCmdEditThunk;
	WNDPROC	mCmdEditProc;

	bool	mbRunState = false;
	bool	mbEditShownDisabled = false;
	bool	mbAppendTimerStarted = false;
	bool	mbAppendForceClear = false;
	uint32	mAppendTimerStartTime = 0;

	VDDelegate	mDelegatePromptChanged;
	VDDelegate	mDelegateRunStateChanged;

	enum { kHistorySize = 8192 };

	int		mHistoryFront = 0;
	int		mHistoryBack = 0;
	int		mHistorySize = 0;
	int		mHistoryCurrent = 0;

	VDStringW	mAppendBuffer;
	uint32	mAppendBufferLFCount = 0;
	uint32	mAppendBufferSplitOffset = 0;
	uint32	mAppendBufferSplitLFCount = 0;

	vdfunction<void()> mpFnThemeUpdated;

	char	mHistory[kHistorySize];
};

#endif
