//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <malloc.h>
#include <stdio.h>
#include <map>
#include <vd2/Dita/services.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/filewatcher.h>
#include <vd2/system/strutil.h>
#include <vd2/system/thread.h>
#include <vd2/system/thunk.h>
#include <vd2/system/refcount.h>
#include <vd2/system/registry.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/w32assist.h>
#include <vd2/VDDisplay/display.h>
#include "console.h"
#include "ui.h"
#include "uiframe.h"
#include "uikeyboard.h"
#include <at/atui/uiproxies.h>
#include "texteditor.h"
#include "simulator.h"
#include "debugger.h"
#include "debuggerexp.h"
#include "resource.h"
#include "disasm.h"
#include "symbols.h"
#include "printer.h"
#include <at/atui/dialog.h>
#include "debugdisplay.h"

extern HINSTANCE g_hInst;
extern HWND g_hwnd;

extern ATSimulator g_sim;
extern ATContainerWindow *g_pMainWindow;

#define ATWM_APP_DEBUG_TOGGLEBREAKPOINT (WM_APP + 0x200)

void ATConsoleQueueCommand(const char *s);

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

///////////////////////////////////////////////////////////////////////////

class ATUIPane;

namespace {
	LOGFONTW g_monoFontDesc = {
		-10,	// lfHeight
		0,		// lfWidth
		0,		// lfEscapement
		0,		// lfOrientation
		0,		// lfWeight
		FALSE,	// lfItalic
		FALSE,	// lfUnderline
		FALSE,	// lfStrikeOut
		DEFAULT_CHARSET,	// lfCharSet
		OUT_DEFAULT_PRECIS,	// lfOutPrecision
		CLIP_DEFAULT_PRECIS,	// lfClipPrecision
		DEFAULT_QUALITY,	// lfQuality
		DEFAULT_PITCH | FF_DONTCARE,	// lfPitchAndFamily
		L"Lucida Console"
	};

	int g_monoFontPtSizeTenths = 75;
	int g_monoFontDpi;

	HFONT	g_monoFont;
	int		g_monoFontLineHeight;
	int		g_monoFontCharWidth;

	HFONT	g_propFont;
	int		g_propFontLineHeight;

	HMENU	g_hmenuSrcContext;
}

///////////////////////////////////////////////////////////////////////////

class ATUIDebuggerPane : public ATUIPane, public IATUIDebuggerPane {
public:
	void *AsInterface(uint32 iid);

	ATUIDebuggerPane(uint32 paneId, const wchar_t *name);

protected:
	virtual bool OnPaneCommand(ATUIPaneCommandId id);

	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
};

ATUIDebuggerPane::ATUIDebuggerPane(uint32 paneId, const wchar_t *name)
	: ATUIPane(paneId, name)
{
}

void *ATUIDebuggerPane::AsInterface(uint32 iid) {
	if (iid == IATUIDebuggerPane::kTypeID)
		return static_cast<IATUIDebuggerPane *>(this);

	return ATUIPane::AsInterface(iid);
}

bool ATUIDebuggerPane::OnPaneCommand(ATUIPaneCommandId id) {
	return false;
}

LRESULT ATUIDebuggerPane::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case ATWM_PREKEYDOWN:
		case ATWM_PRESYSKEYDOWN:
			{
				const bool ctrl = GetKeyState(VK_CONTROL) < 0;
				const bool shift = GetKeyState(VK_SHIFT) < 0;
				const bool alt = GetKeyState(VK_MENU) < 0;
				const bool ext = (lParam & (1 << 24)) != 0;

				if (ATUIActivateVirtKeyMapping(wParam, alt, ctrl, shift, ext, false, kATUIAccelContext_Debugger))
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

				if (ATUIActivateVirtKeyMapping(wParam, alt, ctrl, shift, ext, true, kATUIAccelContext_Debugger))
					return TRUE;
			}
			break;
	}

	return ATUIPane::WndProc(msg, wParam, lParam);
}

///////////////////////////////////////////////////////////////////////////

class ATDisassemblyWindow : public ATUIDebuggerPane,
							public IATDebuggerClient,
							public IVDTextEditorCallback,
							public IVDTextEditorColorizer,
							public IVDUIMessageFilterW32
{
public:
	ATDisassemblyWindow();
	~ATDisassemblyWindow();

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId);

	void OnTextEditorUpdated();
	void OnTextEditorScrolled(int firstVisiblePara, int lastVisiblePara, int visibleParaCount, int totalParaCount);
	void RecolorLine(int line, const char *text, int length, IVDTextEditorColorization *colorization);

	void SetPosition(uint32 addr);

protected:
	virtual bool OnPaneCommand(ATUIPaneCommandId id);

protected:
	VDGUIHandle Create(uint32 exStyle, uint32 style, int x, int y, int cx, int cy, VDGUIHandle parent, int id);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnMessage(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result);

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnSetFocus();
	void OnFontsUpdated();
	bool OnCommand(UINT cmd);
	void RemakeView(uint16 focusAddr);

	vdrefptr<IVDTextEditor> mpTextEditor;
	HWND	mhwndTextEditor;
	HWND	mhwndAddress;
	HMENU	mhmenu;

	uint32	mViewStart;
	uint32	mViewLength;
	uint8	mViewBank;
	uint16	mFocusAddr;
	int		mPCLine;
	uint32	mPCAddr;
	int		mFramePCLine;
	uint32	mFramePCAddr;
	ATCPUSubMode	mLastSubMode;
	bool	mbShowCodeBytes;
	bool	mbShowLabels;
	
	ATDebuggerSystemState mLastState;

	vdfastvector<uint16> mAddressesByLine;
};

ATDisassemblyWindow::ATDisassemblyWindow()
	: ATUIDebuggerPane(kATUIPaneId_Disassembly, L"Disassembly")
	, mhwndTextEditor(NULL)
	, mhwndAddress(NULL)
	, mhmenu(LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_DISASM_CONTEXT_MENU)))
	, mViewStart(0)
	, mViewLength(0)
	, mFocusAddr(0)
	, mPCLine(-1)
	, mPCAddr(0)
	, mFramePCLine(-1)
	, mFramePCAddr(0)
	, mLastSubMode(kATCPUSubMode_6502)
	, mbShowCodeBytes(true)
	, mbShowLabels(true)
{
	mPreferredDockCode = kATContainerDockRight;

	mLastState.mPC = 0;
	mLastState.mInsnPC = 0;
	mLastState.mFramePC = 0;
	mLastState.mbRunning = false;
}

ATDisassemblyWindow::~ATDisassemblyWindow() {
	if (mhmenu)
		::DestroyMenu(mhmenu);
}

bool ATDisassemblyWindow::OnPaneCommand(ATUIPaneCommandId id) {
	switch(id) {
		case kATUIPaneCommandId_DebugToggleBreakpoint:
			{
				size_t line = (size_t)mpTextEditor->GetCursorLine();

				if (line < mAddressesByLine.size())
					ATGetDebugger()->ToggleBreakpoint(mAddressesByLine[line]);

				return true;
			}
			break;
	}

	return false;
}

LRESULT ATDisassemblyWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_SIZE:
			OnSize();
			break;

		case WM_NOTIFY:
			{
				const NMHDR *hdr = (const NMHDR *)lParam;

				if (hdr->idFrom == 101) {
					if (hdr->code == CBEN_ENDEDIT) {
						const NMCBEENDEDIT *info = (const NMCBEENDEDIT *)hdr;

						sint32 addr = ATGetDebugger()->ResolveSymbol(VDTextWToA(info->szText).c_str());

						if (addr < 0)
							MessageBeep(MB_ICONERROR);
						else
							SetPosition(addr);

						return FALSE;
					}
				}
			}
			break;

		case WM_COMMAND:
			if (OnCommand(LOWORD(wParam)))
				return true;
			break;
	}

	return ATUIDebuggerPane::WndProc(msg, wParam, lParam);
}

bool ATDisassemblyWindow::OnMessage(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result) {
	switch(msg) {
		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
			if (wParam == VK_ESCAPE) {
				if (ATGetUIPane(kATUIPaneId_Console))
					ATActivateUIPane(kATUIPaneId_Console, true);
			}
			break;

		case WM_SYSKEYUP:
			switch(wParam) {
				case VK_ESCAPE:
					return true;
			}
			break;

		case WM_CHAR:
		case WM_DEADCHAR:
		case WM_UNICHAR:
		case WM_SYSCHAR:
		case WM_SYSDEADCHAR:
			if (wParam >= 0x20) {
				ATUIPane *pane = ATGetUIPane(kATUIPaneId_Console);
				if (pane) {
					ATActivateUIPane(kATUIPaneId_Console, true);

					result = SendMessage(::GetFocus(), msg, wParam, lParam);
				} else {
					result = 0;
				}

				return true;
			}
			break;

		case WM_CONTEXTMENU:
			{
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);

				HMENU menu = GetSubMenu(mhmenu, 0);
				VDCheckMenuItemByCommandW32(menu, ID_CONTEXT_SHOWCODEBYTES, mbShowCodeBytes);
				VDCheckMenuItemByCommandW32(menu, ID_CONTEXT_SHOWLABELS, mbShowLabels);

				if (x >= 0 && y >= 0) {
					POINT pt = {x, y};

					if (ScreenToClient(mhwndTextEditor, &pt)) {
						mpTextEditor->SetCursorPixelPos(pt.x, pt.y);
						TrackPopupMenu(menu, TPM_LEFTALIGN|TPM_TOPALIGN, x, y, 0, mhwnd, NULL);
					}
				} else {
					TrackPopupMenu(menu, TPM_LEFTALIGN|TPM_TOPALIGN, x, y, 0, mhwnd, NULL);
				}
			}
			break;
	}

	return false;
}

bool ATDisassemblyWindow::OnCreate() {
	if (!ATUIDebuggerPane::OnCreate())
		return false;

	if (!VDCreateTextEditor(~mpTextEditor))
		return false;

	mhwndAddress = CreateWindowEx(0, WC_COMBOBOXEX, _T(""), WS_VISIBLE|WS_CHILD|WS_CLIPSIBLINGS|CBS_DROPDOWN|CBS_HASSTRINGS|CBS_AUTOHSCROLL, 0, 0, 0, 0, mhwnd, (HMENU)101, g_hInst, NULL);

	mhwndTextEditor = (HWND)mpTextEditor->Create(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd, 100);

	OnFontsUpdated();

	mpTextEditor->SetCallback(this);
	mpTextEditor->SetColorizer(this);
	mpTextEditor->SetMsgFilter(this);
	mpTextEditor->SetReadOnly(true);

	OnSize();
	ATGetDebugger()->AddClient(this, true);
	return true;
}

void ATDisassemblyWindow::OnDestroy() {
	ATGetDebugger()->RemoveClient(this);
	ATUIDebuggerPane::OnDestroy();
}

void ATDisassemblyWindow::OnSize() {
	RECT r;
	if (GetClientRect(mhwnd, &r)) {
		RECT rAddr = {0};
		int comboHt = 0;

		if (mhwndAddress) {
			GetWindowRect(mhwndAddress, &rAddr);

			comboHt = rAddr.bottom - rAddr.top;
			VDVERIFY(SetWindowPos(mhwndAddress, NULL, 0, 0, r.right, comboHt, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
		}

		if (mhwndTextEditor) {
			VDVERIFY(SetWindowPos(mhwndTextEditor, NULL, 0, comboHt, r.right, r.bottom - comboHt, SWP_NOZORDER|SWP_NOACTIVATE));
		}
	}
}

void ATDisassemblyWindow::OnSetFocus() {
	::SetFocus(mhwndTextEditor);
}

bool ATDisassemblyWindow::OnCommand(UINT cmd) {
	switch(cmd) {
		case ID_CONTEXT_GOTOSOURCE:
			{
				int line = mpTextEditor->GetCursorLine();
				bool error = true;

				if ((uint32)line < mAddressesByLine.size()) {
					uint16 addr = mAddressesByLine[line];

					uint32 moduleId;
					ATSourceLineInfo lineInfo;
					IATDebuggerSymbolLookup *lookup = ATGetDebuggerSymbolLookup();
					if (lookup->LookupLine(addr, false, moduleId, lineInfo)) {
						VDStringW path;
						if (lookup->GetSourceFilePath(moduleId, lineInfo.mFileId, path) && lineInfo.mLine) {
							IATSourceWindow *w = ATOpenSourceWindow(path.c_str());
							if (w) {
								w->FocusOnLine(lineInfo.mLine - 1);

								error = false;
							}
						}
					}
				}
				
				if (error)
					MessageBox(mhwnd, _T("There is no source line associated with that location."), _T("Altirra Error"), MB_ICONERROR | MB_OK);
			}
			break;
		case ID_CONTEXT_SHOWNEXTSTATEMENT:
			{
				ATCPUEmulator& cpu = g_sim.GetCPU();

				SetPosition(cpu.GetInsnPC() + ((uint32)cpu.GetK() << 16));
			}
			break;
		case ID_CONTEXT_SETNEXTSTATEMENT:
			{
				int line = mpTextEditor->GetCursorLine();

				if ((uint32)line < mAddressesByLine.size())
					ATGetDebugger()->SetPC(mAddressesByLine[line]);
				else
					MessageBeep(MB_ICONEXCLAMATION);
			}
			break;
		case ID_CONTEXT_TOGGLEBREAKPOINT:
			{
				int line = mpTextEditor->GetCursorLine();

				if ((uint32)line < mAddressesByLine.size())
					ATGetDebugger()->ToggleBreakpoint(mAddressesByLine[line]);
				else
					MessageBeep(MB_ICONEXCLAMATION);
			}
			break;
		case ID_CONTEXT_SHOWCODEBYTES:
			mbShowCodeBytes = !mbShowCodeBytes;
			RemakeView(mFocusAddr);
			break;
		case ID_CONTEXT_SHOWLABELS:
			mbShowLabels = !mbShowLabels;
			RemakeView(mFocusAddr);
			break;
	}

	return false;
}

void ATDisassemblyWindow::OnFontsUpdated() {
	if (mhwndTextEditor)
		SendMessage(mhwndTextEditor, WM_SETFONT, (WPARAM)g_monoFont, TRUE);
}

void ATDisassemblyWindow::RemakeView(uint16 focusAddr) {
	mFocusAddr = focusAddr;

	uint16 pc = mViewStart;
	VDStringA buf;

	mpTextEditor->Clear();
	mAddressesByLine.clear();

	mPCLine = -1;
	mFramePCLine = -1;

	ATCPUHistoryEntry hent0;
	ATDisassembleCaptureRegisterContext(hent0);

	ATCPUHistoryEntry hent(hent0);

	bool autoModeSwitching = false;

	if (g_sim.GetCPU().GetCPUMode() == kATCPUMode_65C816)
		autoModeSwitching = true;

	uint32 viewLength = mpTextEditor->GetVisibleLineCount() * 24;

	if (viewLength < 0x100)
		viewLength = 0x100;

	int line = 0;
	int focusLine = -1;

	mpTextEditor->SetUpdateEnabled(false);

	while((uint32)(pc - mViewStart) < viewLength) {
		if (pc == (uint16)mPCAddr)
			mPCLine = line;
		else if (pc == (uint16)mFramePCAddr)
			mFramePCLine = line;

		if (pc <= focusAddr)
			focusLine = line;

		++line;

		if (autoModeSwitching && pc == (uint16)mPCAddr)
			hent = hent0;

		mAddressesByLine.push_back(pc);
		ATDisassembleCaptureInsnContext(pc, mViewBank, hent);
		buf.clear();
		uint16 newpc = ATDisassembleInsn(buf, hent, false, false, true, mbShowCodeBytes, mbShowLabels);
		buf += '\n';

		// auto-switch mode if necessary
		if (autoModeSwitching) {
			switch(hent.mOpcode[0]) {
				case 0x18:	// CLC
					hent.mP &= ~AT6502::kFlagC;
					break;

				case 0x38:	// SEC
					hent.mP |= AT6502::kFlagC;
					break;

				case 0xC2:	// REP
					if (hent.mbEmulation)
						hent.mP &= ~hent.mOpcode[1] | 0x30;
					else
						hent.mP &= ~hent.mOpcode[1];
					break;

				case 0xE2:	// SEP
					if (hent.mbEmulation)
						hent.mP |= hent.mOpcode[1] & 0xcf;
					else
						hent.mP |= hent.mOpcode[1];
					break;

				case 0xFB:	// XCE
					{
						uint8 e = hent.mbEmulation ? 1 : 0;
						uint8 xorv = hent.mP ^ e;

						if (xorv) {
							e ^= xorv;
							hent.mP ^= xorv;

							hent.mbEmulation = (e & 1) != 0;

							if (hent.mbEmulation)
								hent.mP |= 0x30;
						}
					}
					break;
			}
		}

		// don't allow disassembly to skip the desired PC
		if (newpc > focusAddr && pc < focusAddr)
			newpc = focusAddr;

		pc = newpc;

		mpTextEditor->Append(buf.c_str());
	}

	mpTextEditor->SetUpdateEnabled(true);

	mLastSubMode = g_sim.GetCPU().GetCPUSubMode();

	if (focusLine >= 0) {
		mpTextEditor->CenterViewOnLine(focusLine);
		mpTextEditor->SetCursorPos(focusLine, 0);
	}

	mViewLength = (uint16)(pc - mViewStart);
	mpTextEditor->RecolorAll();
}

void ATDisassemblyWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	if (state.mbRunning) {
		mPCLine = -1;
		mFramePCLine = -1;
		mpTextEditor->RecolorAll();
		return;
	}

	bool changed = false;

	if (mLastState.mPC != state.mPC || mLastState.mFramePC != state.mFramePC || mLastState.mK != state.mK)
		changed = true;

	mLastState = state;

	mPCAddr = (uint32)state.mInsnPC + ((uint32)state.mK << 16);
	mPCLine = -1;
	mFramePCAddr = (uint32)state.mFramePC + ((uint32)state.mK << 16);
	mFramePCLine = -1;

	if (state.mbRunning) {
		mPCAddr = (uint32)-1;
		mFramePCAddr = (uint32)-1;
	}

	if (changed && ((state.mFramePC - mViewStart) >= mViewLength || state.mK != mViewBank || g_sim.GetCPU().GetCPUSubMode() != mLastSubMode)) {
		SetPosition(mFramePCAddr);
	} else {
		const int n = mAddressesByLine.size();

		for(int line = 0; line < n; ++line) {
			uint16 addr = mAddressesByLine[line];

			if (addr == (uint16)mPCAddr)
				mPCLine = line;
			else if (addr == (uint16)mFramePCAddr)
				mFramePCLine = line;
		}

		if (changed) {
			if (mFramePCLine >= 0) {
				mpTextEditor->SetCursorPos(mFramePCLine, 0);
				mpTextEditor->MakeLineVisible(mFramePCLine);
			} else if (mPCLine >= 0) {
				mpTextEditor->SetCursorPos(mPCLine, 0);
				mpTextEditor->MakeLineVisible(mPCLine);
			} else {
				SetPosition(mFramePCAddr);
				return;
			}
		}

		mpTextEditor->RecolorAll();
	}
}

void ATDisassemblyWindow::OnDebuggerEvent(ATDebugEvent eventId) {
	if (eventId == kATDebugEvent_BreakpointsChanged)
		mpTextEditor->RecolorAll();
	else if (eventId == kATDebugEvent_SymbolsChanged)
		RemakeView(mFocusAddr);
}

void ATDisassemblyWindow::OnTextEditorUpdated() {
}

void ATDisassemblyWindow::OnTextEditorScrolled(int firstVisiblePara, int lastVisiblePara, int visibleParaCount, int totalParaCount) {
	if (mpTextEditor->IsSelectionPresent())
		return;

	if (!mAddressesByLine.empty()) {
		bool prev = (firstVisiblePara < visibleParaCount);
		bool next = (lastVisiblePara >= totalParaCount - visibleParaCount);

		if (prev ^ next) {
			int cenIdx = mpTextEditor->GetParagraphForYPos(mpTextEditor->GetVisibleHeight() >> 1);
			
			if (cenIdx < 0)
				cenIdx = 0;
			else if ((uint32)cenIdx >= mAddressesByLine.size())
				cenIdx = mAddressesByLine.size() - 1;

			SetPosition(mAddressesByLine[cenIdx]);
		}
	}
}

void ATDisassemblyWindow::RecolorLine(int line, const char *text, int length, IVDTextEditorColorization *cl) {
	int next = 0;

	if ((size_t)line < mAddressesByLine.size()) {
		ATCPUEmulator& cpu = g_sim.GetCPU();
		uint32 addr = mAddressesByLine[line];

		if (cpu.IsBreakpointSet((uint16)addr)) {
			cl->AddTextColorPoint(next, 0x000000, 0xFF8080);
			next += 4;
		}
	}

	if (line == mPCLine)
		cl->AddTextColorPoint(next, 0x000000, 0xFFFF80);
	else if (line == mFramePCLine)
		cl->AddTextColorPoint(next, 0x000000, 0x80FF80);
}

void ATDisassemblyWindow::SetPosition(uint32 addr) {
	VDStringA text(ATGetDebugger()->GetAddressText(addr, true));
	VDSetWindowTextFW32(mhwndAddress, L"%hs", text.c_str());

	uint32 addr16 = addr & 0xffff;
	uint32 offset = mpTextEditor->GetVisibleLineCount() * 12;

	if (offset < 0x80)
		offset = 0x80;

	mViewBank = (uint8)(addr >> 16);
	mViewStart = ATDisassembleGetFirstAnchor(addr16 >= offset ? addr16 - offset : 0, addr16, mViewBank);

	RemakeView(addr);
}

///////////////////////////////////////////////////////////////////////////

class ATRegistersWindow : public ATUIDebuggerPane, public IATDebuggerClient {
public:
	ATRegistersWindow();
	~ATRegistersWindow();

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId) {}

protected:
	VDGUIHandle Create(uint32 exStyle, uint32 style, int x, int y, int cx, int cy, VDGUIHandle parent, int id);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnFontsUpdated();

	HWND	mhwndEdit;
	VDStringA	mState;
};

ATRegistersWindow::ATRegistersWindow()
	: ATUIDebuggerPane(kATUIPaneId_Registers, L"Registers")
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
	}

	return ATUIDebuggerPane::WndProc(msg, wParam, lParam);
}

bool ATRegistersWindow::OnCreate() {
	if (!ATUIDebuggerPane::OnCreate())
		return false;

	mhwndEdit = CreateWindowEx(0, _T("EDIT"), _T(""), WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_AUTOVSCROLL|ES_MULTILINE, 0, 0, 0, 0, mhwnd, (HMENU)100, VDGetLocalModuleHandleW32(), NULL);
	if (!mhwndEdit)
		return false;

	OnFontsUpdated();
	OnSize();

	ATGetDebugger()->AddClient(this, true);
	return true;
}

void ATRegistersWindow::OnDestroy() {
	ATGetDebugger()->RemoveClient(this);
	ATUIDebuggerPane::OnDestroy();
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
	SendMessage(mhwndEdit, WM_SETFONT, (WPARAM)g_monoFont, TRUE);
}

void ATRegistersWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	mState.clear();

	bool is65C816 = g_sim.GetCPU().GetCPUMode() == kATCPUMode_65C816;

	if (is65C816) {
		if (state.mbEmulation)
			mState += "Emulation\r\n";
		else {
			mState.append_sprintf("Native (M%d X%d)\r\n"
				, state.mP & AT6502::kFlagM ? 8 : 16
				, state.mP & AT6502::kFlagX ? 8 : 16
				);
		}

		mState.append_sprintf("PC = %02X:%04X (%04X)\r\n", state.mK, state.mInsnPC, state.mPC);

		if (state.mbEmulation || (state.mP & AT6502::kFlagM)) {
			mState.append_sprintf("A = %02X\r\n", state.mA);
			mState.append_sprintf("B = %02X\r\n", state.mAH);
		} else
			mState.append_sprintf("A = %02X%02X\r\n", state.mAH, state.mA);

		if (state.mbEmulation || (state.mP & AT6502::kFlagX)) {
			mState.append_sprintf("X = %02X\r\n", state.mX);
			mState.append_sprintf("Y = %02X\r\n", state.mY);
		} else {
			mState.append_sprintf("X = %02X%02X\r\n", state.mXH, state.mX);
			mState.append_sprintf("Y = %02X%02X\r\n", state.mYH, state.mY);
		}

		if (state.mbEmulation)
			mState.append_sprintf("S = %02X\r\n", state.mS);
		else
			mState.append_sprintf("S = %02X%02X\r\n", state.mSH, state.mS);

		mState.append_sprintf("P = %02X\r\n", state.mP);

		if (state.mbEmulation) {
			mState.append_sprintf("    %c%c%c%c%c%c%c\r\n"
				, state.mP & 0x80 ? 'N' : '-'
				, state.mP & 0x40 ? 'V' : '-'
				, state.mP & 0x08 ? 'D' : '-'
				, state.mP & 0x04 ? 'I' : '-'
				, state.mP & 0x02 ? 'Z' : '-'
				, state.mP & 0x01 ? 'C' : '-'
				);
		} else {
			mState.append_sprintf("    %c%c%c%c%c%c%c%c\r\n"
				, state.mP & 0x80 ? 'N' : '-'
				, state.mP & 0x40 ? 'V' : '-'
				, state.mP & 0x20 ? 'M' : '-'
				, state.mP & 0x10 ? 'X' : '-'
				, state.mP & 0x08 ? 'D' : '-'
				, state.mP & 0x04 ? 'I' : '-'
				, state.mP & 0x02 ? 'Z' : '-'
				, state.mP & 0x01 ? 'C' : '-'
				);
		}

		mState.append_sprintf("E = %d\r\n", state.mbEmulation);
		mState.append_sprintf("D = %04X\r\n", state.mD);
		mState.append_sprintf("B = %02X\r\n", state.mB);
	} else {
		mState.append_sprintf("PC = %04X (%04X)\r\n", state.mInsnPC, state.mPC);
		mState.append_sprintf("A = %02X\r\n", state.mA);
		mState.append_sprintf("X = %02X\r\n", state.mX);
		mState.append_sprintf("Y = %02X\r\n", state.mY);
		mState.append_sprintf("S = %02X\r\n", state.mS);
		mState.append_sprintf("P = %02X\r\n", state.mP);
		mState.append_sprintf("    %c%c%c%c%c%c\r\n"
			, state.mP & 0x80 ? 'N' : '-'
			, state.mP & 0x40 ? 'V' : '-'
			, state.mP & 0x08 ? 'D' : '-'
			, state.mP & 0x04 ? 'I' : '-'
			, state.mP & 0x02 ? 'Z' : '-'
			, state.mP & 0x01 ? 'C' : '-'
			);
	}

	SetWindowTextA(mhwndEdit, mState.c_str());
}

///////////////////////////////////////////////////////////////////////////

class ATCallStackWindow : public ATUIDebuggerPane, public IATDebuggerClient {
public:
	ATCallStackWindow();
	~ATCallStackWindow();

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId) {}

protected:
	VDGUIHandle Create(uint32 exStyle, uint32 style, int x, int y, int cx, int cy, VDGUIHandle parent, int id);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnFontsUpdated();

	HWND	mhwndList;
	VDStringA	mState;

	vdfastvector<uint16> mFrames;
};

ATCallStackWindow::ATCallStackWindow()
	: ATUIDebuggerPane(kATUIPaneId_CallStack, L"Call Stack")
	, mhwndList(NULL)
{
	mPreferredDockCode = kATContainerDockRight;
}

ATCallStackWindow::~ATCallStackWindow() {
}

LRESULT ATCallStackWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_SIZE:
			OnSize();
			break;

		case WM_COMMAND:
			if (LOWORD(wParam) == 100 && HIWORD(wParam) == LBN_DBLCLK) {
				int idx = SendMessage(mhwndList, LB_GETCURSEL, 0, 0);

				if ((size_t)idx < mFrames.size())
					ATGetDebugger()->SetFramePC(mFrames[idx]);
			}
			break;

		case WM_VKEYTOITEM:
			if (LOWORD(wParam) == VK_ESCAPE) {
				ATUIPane *pane = ATGetUIPane(kATUIPaneId_Console);

				if (pane)
					ATActivateUIPane(kATUIPaneId_Console, true);
				return -2;
			}
			break;
	}

	return ATUIDebuggerPane::WndProc(msg, wParam, lParam);
}

bool ATCallStackWindow::OnCreate() {
	if (!ATUIDebuggerPane::OnCreate())
		return false;

	mhwndList = CreateWindowEx(0, _T("LISTBOX"), _T(""), WS_CHILD|WS_VISIBLE|LBS_HASSTRINGS|LBS_NOTIFY|LBS_WANTKEYBOARDINPUT, 0, 0, 0, 0, mhwnd, (HMENU)100, VDGetLocalModuleHandleW32(), NULL);
	if (!mhwndList)
		return false;

	OnFontsUpdated();

	OnSize();

	ATGetDebugger()->AddClient(this, true);
	return true;
}

void ATCallStackWindow::OnDestroy() {
	ATGetDebugger()->RemoveClient(this);
	ATUIDebuggerPane::OnDestroy();
}

void ATCallStackWindow::OnSize() {
	RECT r;
	if (mhwndList && GetClientRect(mhwnd, &r)) {
		VDVERIFY(SetWindowPos(mhwndList, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE));
	}
}

void ATCallStackWindow::OnFontsUpdated() {
	if (mhwndList)
		SendMessage(mhwndList, WM_SETFONT, (WPARAM)g_monoFont, TRUE);
}

void ATCallStackWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	IATDebugger *db = ATGetDebugger();
	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();
	ATCallStackFrame frames[16];
	uint32 n = db->GetCallStack(frames, 16);

	mFrames.resize(n);

	SendMessage(mhwndList, LB_RESETCONTENT, 0, 0);
	for(uint32 i=0; i<n; ++i) {
		const ATCallStackFrame& fr = frames[i];

		mFrames[i] = fr.mPC;

		ATSymbol sym;
		const char *symname = "";
		if (dbs->LookupSymbol(fr.mPC, kATSymbol_Execute, sym))
			symname = sym.mpName;

		mState.sprintf("%c%04X: %c%04X (%s)"
			, state.mFramePC == fr.mPC ? '>' : ' '
			, 0x0100 + fr.mS
			, fr.mP & 0x04 ? '*' : ' '
			, fr.mPC
			, symname);
		SendMessageA(mhwndList, LB_ADDSTRING, 0, (LPARAM)mState.c_str());
	}
}

///////////////////////////////////////////////////////////////////////////

typedef vdfastvector<IATSourceWindow *> SourceWindows;
SourceWindows g_sourceWindows;

class ATSourceWindow : public ATUIDebuggerPane
					 , public IATSimulatorCallback
					 , public IATDebuggerClient
					 , public IVDTextEditorCallback
					 , public IVDTextEditorColorizer
					 , public IVDFileWatcherCallback
					 , public IATSourceWindow
{
public:
	ATSourceWindow(uint32 id, const wchar_t *name);
	~ATSourceWindow();

	static bool CreatePane(uint32 id, ATUIPane **pp);

	void LoadFile(const wchar_t *s, const wchar_t *alias);

	const wchar_t *GetPath() const {
		return mPath.c_str();
	}

	const wchar_t *GetPathAlias() const {
		return mPathAlias.empty() ? NULL : mPathAlias.c_str();
	}

protected:
	virtual bool OnPaneCommand(ATUIPaneCommandId id);

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnFontsUpdated();
	bool OnCommand(UINT cmd);

	void OnTextEditorUpdated();
	void OnTextEditorScrolled(int firstVisiblePara, int lastVisiblePara, int visibleParaCount, int totalParaCount);
	void RecolorLine(int line, const char *text, int length, IVDTextEditorColorization *colorization);

	void OnSimulatorEvent(ATSimulatorEvent ev);
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId);
	int GetLineForAddress(uint32 addr);
	bool OnFileUpdated(const wchar_t *path);

	void	ToggleBreakpoint();

	void	ActivateLine(int line);
	void	FocusOnLine(int line);
	void	SetPCLine(int line);
	void	SetFramePCLine(int line);

	sint32	GetCurrentLineAddress() const;
	bool	BindToSymbols();

	VDStringA mModulePath;
	uint32	mModuleId;
	uint16	mFileId;
	sint32	mLastPC;
	sint32	mLastFramePC;

	int		mPCLine;
	int		mFramePCLine;

	vdrefptr<IVDTextEditor>	mpTextEditor;
	HWND	mhwndTextEditor;
	VDStringW	mPath;
	VDStringW	mPathAlias;

	typedef vdhashmap<uint32, uint32> AddressLookup;
	AddressLookup	mAddressToLineLookup;
	AddressLookup	mLineToAddressLookup;

	typedef std::map<int, uint32> LooseLineLookup;
	LooseLineLookup	mLineToAddressLooseLookup;

	typedef std::map<uint32, int> LooseAddressLookup;
	LooseAddressLookup	mAddressToLineLooseLookup;

	VDFileWatcher	mFileWatcher;
};

ATSourceWindow::ATSourceWindow(uint32 id, const wchar_t *name)
	: ATUIDebuggerPane(id, name)
	, mLastPC(-1)
	, mLastFramePC(-1)
	, mPCLine(-1)
	, mFramePCLine(-1)
{

}

ATSourceWindow::~ATSourceWindow() {
}

bool ATSourceWindow::CreatePane(uint32 id, ATUIPane **pp) {
	uint32 index = id - kATUIPaneId_Source;

	if (index >= g_sourceWindows.size())
		return false;

	ATSourceWindow *w = static_cast<ATSourceWindow *>(g_sourceWindows[index]);

	if (!w)
		return false;

	w->AddRef();
	*pp = w;
	return true;
}

void ATSourceWindow::LoadFile(const wchar_t *s, const wchar_t *alias) {
	VDTextInputFile ifile(s);

	mpTextEditor->Clear();

	mAddressToLineLookup.clear();
	mLineToAddressLookup.clear();

	mModuleId = 0;
	mFileId = 0;
	mPath = s;

	BindToSymbols();

	uint32 lineno = 0;
	bool listingMode = false;
	while(const char *line = ifile.GetNextLine()) {
		if (listingMode) {
			char space0;
			int origline;
			int address;
			char dummy;
			char space1;
			char space2;
			char space3;
			char space4;
			int op;

			bool valid = false;
			if (7 == sscanf(line, "%c%d %4x%c%c%2x%c", &space0, &origline, &address, &space1, &space2, &op, &space3)
				&& space0 == ' '
				&& space1 == ' '
				&& space2 == ' '
				&& (space3 == ' ' || space3 == '\t'))
			{
				valid = true;
			} else if (8 == sscanf(line, "%6x%c%c%c%c%c%2x%c", &address, &space0, &space1, &dummy, &space2, &space3, &op, &space4)
				&& space0 == ' '
				&& space1 == ' '
				&& space2 == ' '
				&& space3 == ' '
				&& (space4 == ' ' || space4 == '\t')
				&& isdigit((unsigned char)dummy))
			{
				valid = true;
			} else if (6 == sscanf(line, "%6d%c%4x%c%2x%c", &origline, &space0, &address, &space1, &op, &space2)
				&& space0 == ' '
				&& space1 == ' '
				&& (space2 == ' ' || space2 == '\t'))
			{
				valid = true;
			}

			if (valid) {
				mAddressToLineLookup.insert(AddressLookup::value_type(address, lineno));
				mLineToAddressLookup.insert(AddressLookup::value_type(lineno, address));
			}
		} else if (!mModuleId) {
			if (!lineno && !strncmp(line, "mads ", 5))
				listingMode = true;
		}

		mpTextEditor->Append(line);
		mpTextEditor->Append("\n");
		++lineno;
	}

	mAddressToLineLooseLookup.clear();

	for(AddressLookup::const_iterator it(mAddressToLineLookup.begin()), itEnd(mAddressToLineLookup.end()); it != itEnd; ++it) {
		mAddressToLineLooseLookup.insert(*it);
	}

	mLineToAddressLooseLookup.clear();

	for(AddressLookup::const_iterator it(mLineToAddressLookup.begin()), itEnd(mLineToAddressLookup.end()); it != itEnd; ++it) {
		mLineToAddressLooseLookup.insert(*it);
	}

	if (alias)
		mPathAlias = alias;
	else
		mPathAlias.clear();

	if (mModulePath.empty() && !listingMode)
		mModulePath = VDTextWToA(VDFileSplitPathRight(mPath));

	mpTextEditor->RecolorAll();

	mFileWatcher.Init(s, this);

	VDSetWindowTextFW32(mhwnd, L"%ls [source] - Altirra", VDFileSplitPath(s));
	ATGetDebugger()->RequestClientUpdate(this);
}

bool ATSourceWindow::OnPaneCommand(ATUIPaneCommandId id) {
	switch(id) {
		case kATUIPaneCommandId_DebugToggleBreakpoint:
			ToggleBreakpoint();
			return true;

		case kATUIPaneCommandId_DebugRun:
			ATGetDebugger()->Run(kATDebugSrcMode_Source);
			return true;

		case kATUIPaneCommandId_DebugStepOver:
		case kATUIPaneCommandId_DebugStepInto:
			{
				IATDebugger *dbg = ATGetDebugger();
				IATDebuggerSymbolLookup *dsl = ATGetDebuggerSymbolLookup();
				const uint32 pc = dbg->GetPC();

				void (IATDebugger::*stepMethod)(ATDebugSrcMode, const ATDebuggerStepRange *, uint32)
					= (id == kATUIPaneCommandId_DebugStepOver) ? &IATDebugger::StepOver : &IATDebugger::StepInto;

				LooseAddressLookup::const_iterator it(mAddressToLineLooseLookup.upper_bound(pc));
				if (it != mAddressToLineLooseLookup.end() && it != mAddressToLineLooseLookup.begin()) {
					LooseAddressLookup::const_iterator itNext(it);
					--it;

					uint32 addr1 = it->first;
					uint32 addr2 = itNext->first;

					if (addr2 - addr1 < 100 && addr1 != addr2 && pc + 1 < addr2) {
						ATDebuggerStepRange range = { pc + 1, (addr2 - pc) - 1 };
						(dbg->*stepMethod)(kATDebugSrcMode_Source, &range, 1);
						return true;
					}
				}

				uint32 moduleId;
				ATSourceLineInfo sli1;
				ATSourceLineInfo sli2;
				if (dsl->LookupLine(pc, false, moduleId, sli1)
					&& dsl->LookupLine(pc, true, moduleId, sli2)
					&& sli2.mOffset > pc + 1
					&& sli2.mOffset - sli1.mOffset < 100)
				{
						ATDebuggerStepRange range = { pc + 1, sli2.mOffset - (pc + 1) };
					(dbg->*stepMethod)(kATDebugSrcMode_Source, &range, 1);
					return true;
				}

				(dbg->*stepMethod)(kATDebugSrcMode_Source, 0, 0);
			}

			return true;

		case kATUIPaneCommandId_DebugStepOut:
			ATGetDebugger()->StepOut(kATDebugSrcMode_Source);
			return true;
	}

	return false;
}

LRESULT ATSourceWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_APP+101:
			if (!mPath.empty()) {
				try {
					LoadFile(mPath.c_str(), NULL);
				} catch(const MyError&) {
					// eat
				}
			}
			return 0;

		case WM_COMMAND:
			if (OnCommand(LOWORD(wParam)))
				return 0;
			break;

		case WM_CONTEXTMENU:
			{
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);

				if (x >= 0 && y >= 0) {
					POINT pt = {x, y};

					if (ScreenToClient(mhwndTextEditor, &pt)) {
						mpTextEditor->SetCursorPixelPos(pt.x, pt.y);
						TrackPopupMenu(GetSubMenu(g_hmenuSrcContext, 0), TPM_LEFTALIGN|TPM_TOPALIGN, x, y, 0, mhwnd, NULL);
					}
				} else {
					TrackPopupMenu(GetSubMenu(g_hmenuSrcContext, 0), TPM_LEFTALIGN|TPM_TOPALIGN, x, y, 0, mhwnd, NULL);
				}
			}
			return 0;

		case WM_SETFOCUS:
			::SetFocus(mhwndTextEditor);
			return 0;

		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT && HIWORD(lParam)) {
				::SetCursor(::LoadCursor(NULL, IDC_IBEAM));
				return TRUE;
			}
			break;
	}

	return ATUIDebuggerPane::WndProc(msg, wParam, lParam);
}

bool ATSourceWindow::OnCreate() {
	if (!VDCreateTextEditor(~mpTextEditor))
		return false;

	mhwndTextEditor = (HWND)mpTextEditor->Create(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd, 100);

	OnFontsUpdated();

	mpTextEditor->SetReadOnly(true);
	mpTextEditor->SetCallback(this);
	mpTextEditor->SetColorizer(this);

	ATGetDebugger()->AddClient(this);
	g_sim.AddCallback(this);

	return ATUIDebuggerPane::OnCreate();
}

void ATSourceWindow::OnDestroy() {
	uint32 index = mPaneId - kATUIPaneId_Source;

	if (index < g_sourceWindows.size() && g_sourceWindows[index] == this) {
		g_sourceWindows[index] = NULL;
	} else {
		VDASSERT(!"Source window not found in global map.");
	}

	mFileWatcher.Shutdown();

	g_sim.RemoveCallback(this);
	ATGetDebugger()->RemoveClient(this);

	ATUIDebuggerPane::OnDestroy();
}

void ATSourceWindow::OnSize() {
	RECT r;
	VDVERIFY(GetClientRect(mhwnd, &r));

	if (mhwndTextEditor) {
		VDVERIFY(SetWindowPos(mhwndTextEditor, NULL, 0, 0, r.right, r.bottom, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
	}
}

void ATSourceWindow::OnFontsUpdated() {
	if (mhwndTextEditor)
		SendMessage(mhwndTextEditor, WM_SETFONT, (WPARAM)g_monoFont, TRUE);
}

bool ATSourceWindow::OnCommand(UINT cmd) {
	switch(cmd) {
		case ID_CONTEXT_TOGGLEBREAKPOINT:
			ToggleBreakpoint();
			return true;

		case ID_CONTEXT_SHOWNEXTSTATEMENT:
			{
				uint32 moduleId;
				ATSourceLineInfo lineInfo;
				IATDebuggerSymbolLookup *lookup = ATGetDebuggerSymbolLookup();
				if (lookup->LookupLine(ATGetDebugger()->GetFramePC(), false, moduleId, lineInfo)) {
					VDStringW path;
					if (lookup->GetSourceFilePath(moduleId, lineInfo.mFileId, path) && lineInfo.mLine) {
						IATSourceWindow *w = ATOpenSourceWindow(path.c_str());
						if (w) {
							w->FocusOnLine(lineInfo.mLine - 1);

							return true;
						}
					}
				}
			}
			// fall through to go-to-disassembly

		case ID_CONTEXT_GOTODISASSEMBLY:
			{
				sint32 addr = GetCurrentLineAddress();

				if (addr >= 0) {
					ATActivateUIPane(kATUIPaneId_Disassembly, true);
					ATDisassemblyWindow *disPane = static_cast<ATDisassemblyWindow *>(ATGetUIPane(kATUIPaneId_Disassembly));
					if (disPane) {
						disPane->SetPosition((uint16)addr);
					}
				} else {
					::MessageBoxW(mhwnd, L"There are no symbols associating code with that location.", L"Altirra Error", MB_ICONERROR | MB_OK);
				}
			}
			return true;

		case ID_CONTEXT_SETNEXTSTATEMENT:
			{
				sint32 addr = GetCurrentLineAddress();

				if (addr >= 0) {
					ATGetDebugger()->SetPC((uint16)addr);
				}
			}
			return true;
	}
	return false;
}

void ATSourceWindow::OnTextEditorUpdated() {
}

void ATSourceWindow::OnTextEditorScrolled(int firstVisiblePara, int lastVisiblePara, int visibleParaCount, int totalParaCount) {
}

void ATSourceWindow::RecolorLine(int line, const char *text, int length, IVDTextEditorColorization *cl) {
	int next = 0;

	AddressLookup::const_iterator it(mLineToAddressLookup.find(line));
	bool addressMapped = false;

	if (it != mLineToAddressLookup.end()) {
		uint32 addr = it->second;

		if (g_sim.GetCPU().IsBreakpointSet(addr)) {
			cl->AddTextColorPoint(next, 0x000000, 0xFF8080);
			next += 4;
		}

		addressMapped = true;
	}

	if (!addressMapped && ATGetDebugger()->IsDeferredBreakpointSet(mModulePath.c_str(), line + 1)) {
		cl->AddTextColorPoint(next, 0x000000, 0xFFA880);
		next += 4;
		addressMapped = true;
	}

	if (line == mPCLine) {
		cl->AddTextColorPoint(next, 0x000000, 0xFFFF80);
		return;
	} else if (line == mFramePCLine) {
		cl->AddTextColorPoint(next, 0x000000, 0x80FF80);
		return;
	}

	if (next > 0) {
		cl->AddTextColorPoint(next, -1, -1);
		return;
	}

	sint32 backColor = -1;
	if (!addressMapped) {
		backColor = 0xf0f0f0;
		cl->AddTextColorPoint(next, -1, backColor);
	}

	for(int i=0; i<length; ++i) {
		char c = text[i];

		if (c == ';') {
			cl->AddTextColorPoint(0, 0x008000, backColor);
			return;
		} else if (c == '.') {
			int j = i + 1;

			while(j < length) {
				c = text[j];

				if (!(c >= 'a' && c <= 'z') &&
					!(c >= 'A' && c <= 'Z') &&
					!(c >= '0' && c <= '9'))
				{
					break;
				}

				++j;
			}

			if (j > i+1) {
				cl->AddTextColorPoint(i, 0x0000FF, backColor);
				cl->AddTextColorPoint(j, -1, backColor);
			}

			return;
		}

		if (c != ' ' && c != '\t')
			break;
	}
}

void ATSourceWindow::OnSimulatorEvent(ATSimulatorEvent ev) {
	switch(ev) {
		case kATSimEvent_CPUPCBreakpointsUpdated:
			mpTextEditor->RecolorAll();
			break;
	}
}

void ATSourceWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	if (mLastFramePC == state.mFramePC && mLastPC == state.mPC)
		return;

	mLastFramePC = state.mFramePC;
	mLastPC = state.mPC;

	int pcline = -1;
	int frameline = -1;

	if (!state.mbRunning) {
		pcline = GetLineForAddress(state.mInsnPC);
		frameline = GetLineForAddress(state.mFramePC);
	}

	if (pcline >= 0)
		mpTextEditor->SetCursorPos(pcline, 0);

	SetPCLine(pcline);

	if (frameline >= 0)
		mpTextEditor->SetCursorPos(frameline, 0);

	SetFramePCLine(frameline);
}

void ATSourceWindow::OnDebuggerEvent(ATDebugEvent eventId) {
	switch(eventId) {
		case kATDebugEvent_BreakpointsChanged:
			mpTextEditor->RecolorAll();
			break;

		case kATDebugEvent_SymbolsChanged:
			if (!mModuleId)
				BindToSymbols();

			mpTextEditor->RecolorAll();
			break;
	}
}

int ATSourceWindow::GetLineForAddress(uint32 addr) {
	LooseAddressLookup::const_iterator it(mAddressToLineLooseLookup.upper_bound(addr));
	if (it != mAddressToLineLooseLookup.begin()) {
		--it;

		if (addr - (uint32)it->first < 64)
			return it->second;
	}

	return -1;
}

bool ATSourceWindow::OnFileUpdated(const wchar_t *path) {
	if (mhwnd)
		PostMessage(mhwnd, WM_APP + 101, 0, 0);

	return true;
}

void ATSourceWindow::ToggleBreakpoint() {
	IATDebugger *dbg = ATGetDebugger();
	const int line = mpTextEditor->GetCursorLine();

	if (!mModulePath.empty()) {
		dbg->ToggleSourceBreakpoint(mModulePath.c_str(), line + 1);
	} else {
		AddressLookup::const_iterator it(mLineToAddressLookup.find(line));
		if (it != mLineToAddressLookup.end())
			ATGetDebugger()->ToggleBreakpoint((uint16)it->second);
		else
			MessageBeep(MB_ICONEXCLAMATION);
	}
}

void ATSourceWindow::ActivateLine(int line) {
	mpTextEditor->SetCursorPos(line, 0);

	ATActivateUIPane(mPaneId, true);
}

void ATSourceWindow::FocusOnLine(int line) {
	mpTextEditor->CenterViewOnLine(line);
	mpTextEditor->SetCursorPos(line, 0);

	ATActivateUIPane(mPaneId, true);
}

void ATSourceWindow::SetPCLine(int line) {
	if (line == mPCLine)
		return;

	int oldLine = mPCLine;
	mPCLine = line;
	mpTextEditor->RecolorLine(oldLine);
	mpTextEditor->RecolorLine(line);
}

void ATSourceWindow::SetFramePCLine(int line) {
	if (line == mFramePCLine)
		return;

	int oldLine = mFramePCLine;
	mFramePCLine = line;
	mpTextEditor->RecolorLine(oldLine);
	mpTextEditor->RecolorLine(line);
}

sint32 ATSourceWindow::GetCurrentLineAddress() const {
	int line = mpTextEditor->GetCursorLine();

	AddressLookup::const_iterator it(mLineToAddressLookup.find(line));

	if (it == mLineToAddressLookup.end())
		return -1;

	return it->second;
}

bool ATSourceWindow::BindToSymbols() {
	IATDebuggerSymbolLookup *symlookup = ATGetDebuggerSymbolLookup();
	if (!symlookup->LookupFile(mPath.c_str(), mModuleId, mFileId))
		return false;

	VDStringW modulePath;
	symlookup->GetSourceFilePath(mModuleId, mFileId, modulePath);

	mModulePath = VDTextWToA(modulePath);

	vdfastvector<ATSourceLineInfo> lines;
	ATGetDebuggerSymbolLookup()->GetLinesForFile(mModuleId, mFileId, lines);

	vdfastvector<ATSourceLineInfo>::const_iterator it(lines.begin()), itEnd(lines.end());
	for(; it!=itEnd; ++it) {
		const ATSourceLineInfo& linfo = *it;

		mAddressToLineLookup.insert(AddressLookup::value_type(linfo.mOffset, linfo.mLine - 1));
		mLineToAddressLookup.insert(AddressLookup::value_type(linfo.mLine - 1, linfo.mOffset));
	}

	return true;
}

IATSourceWindow *ATGetSourceWindow(const wchar_t *s) {
	SourceWindows::const_iterator it(g_sourceWindows.begin()), itEnd(g_sourceWindows.end());

	for(; it != itEnd; ++it) {
		IATSourceWindow *w = *it;

		if (!w)
			continue;

		const wchar_t *t = w->GetPath();

		if (VDFileIsPathEqual(s, t))
			return w;

		t = w->GetPathAlias();

		if (t && VDFileIsPathEqual(s, t))
			return w;
	}

	// try a looser check
	s = VDFileSplitPath(s);

	for(it = g_sourceWindows.begin(); it != itEnd; ++it) {
		IATSourceWindow *w = *it;

		if (!w)
			continue;

		const wchar_t *t = VDFileSplitPath(w->GetPath());

		if (VDFileIsPathEqual(s, t))
			return w;

		t = w->GetPathAlias();
		if (t) {
			t = VDFileSplitPath(t);

			if (VDFileIsPathEqual(s, t))
				return w;
		}
	}

	return NULL;
}

IATSourceWindow *ATOpenSourceWindow(const wchar_t *s) {
	IATSourceWindow *w = ATGetSourceWindow(s);
	if (w)
		return w;

	HWND hwndParent = GetActiveWindow();

	if (!hwndParent || (hwndParent != g_hwnd && GetWindow(hwndParent, GA_ROOT) != g_hwnd))
		hwndParent = g_hwnd;

	VDStringW fn;
	if (!VDDoesPathExist(s)) {
		VDStringW title(L"Find source file ");
		title += s;

		static const wchar_t kCatchAllFilter[] = L"\0All files (*.*)\0*.*";
		VDStringW filter(s);
		filter += L'\0';
		filter += s;
		filter.append(kCatchAllFilter, kCatchAllFilter + (sizeof kCatchAllFilter));		// !! intentionally capturing end null

		fn = VDGetLoadFileName('src ', (VDGUIHandle)hwndParent, title.c_str(), filter.c_str(), NULL);

		if (fn.empty())
			return NULL;
	}

	// find a free pane ID
	SourceWindows::iterator it = std::find(g_sourceWindows.begin(), g_sourceWindows.end(), (IATSourceWindow *)NULL);
	const uint32 paneIndex = (uint32)(it - g_sourceWindows.begin());

	if (it == g_sourceWindows.end())
		g_sourceWindows.push_back(NULL);

	const uint32 paneId = kATUIPaneId_Source + paneIndex;
	vdrefptr<ATSourceWindow> srcwin(new ATSourceWindow(paneId, VDFileSplitPath(s)));
	g_sourceWindows[paneIndex] = srcwin;

	// If the active window is a source or disassembly window, dock in the same place.
	// Otherwise, if we have a source window, dock over that, otherwise dock over the
	// disassembly window.
	uint32 activePaneId = ATUIGetActivePaneId();

	if (activePaneId >= kATUIPaneId_Source || activePaneId == kATUIPaneId_Disassembly)
		ATActivateUIPane(paneId, true, true, activePaneId, kATContainerDockCenter);
	else if (paneIndex > 0) {
		// We always add the new pane at the first non-null position or at the end,
		// whichever comes first. Therefore, unless this is pane 0, the previous pane
		// is always present.
		ATActivateUIPane(paneId, true, true, paneId - 1, kATContainerDockCenter);
	} else if (ATGetUIPane(kATUIPaneId_Disassembly))
		ATActivateUIPane(paneId, true, true, kATUIPaneId_Disassembly, kATContainerDockCenter);
	else
		ATActivateUIPane(paneId, true);

	if (srcwin->GetHandleW32()) {
		try {
			if (fn.empty())
				srcwin->LoadFile(s, NULL);
			else
				srcwin->LoadFile(fn.c_str(), s);
		} catch(const MyError& e) {
			ATCloseUIPane(paneId);
			e.post(g_hwnd, "Altirra error");
			return NULL;
		}

		return srcwin;
	}

	g_sourceWindows[paneIndex] = NULL;

	return NULL;
}

///////////////////////////////////////////////////////////////////////////

class ATHistoryWindow : public ATUIDebuggerPane, public IATDebuggerClient {
public:
	ATHistoryWindow();
	~ATHistoryWindow();

protected:
	enum NodeType {
		kNodeTypeInsn,
		kNodeTypeRepeat,
		kNodeTypeInterrupt,
		kNodeTypeLabel
	};

	struct TreeNode {
		uint32	mRelYPos;		// Y position in items, relative to parent
		uint32	mHeight;		// Height, in items
		bool	mbExpanded;
		bool	mbHeightDirty;
		bool	mbVisible;
		TreeNode *mpParent;
		TreeNode *mpPrevSibling;
		TreeNode *mpNextSibling;
		TreeNode *mpFirstChild;
		TreeNode *mpLastChild;
		ATCPUHistoryEntry mHEnt;
		uint16	mHiBaseCycles;
		uint16	mHiBaseUnhaltedCycles;
		uint32	mRepeatCount;
		uint32	mRepeatSize;
		NodeType	mNodeType;
		const char *mpText;
	};

	LRESULT EditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnFontsUpdated();
	void OnLButtonDown(int x, int y, int mods);
	void OnLButtonDblClk(int x, int y, int mods);
	bool OnKeyDown(int code);
	void OnMouseWheel(int lineDelta);
	void OnHScroll(int code);
	void OnVScroll(int code);
	void OnPaint();
	void PaintItems(HDC hdc, const RECT *rPaint, uint32 itemStart, uint32 itemEnd, TreeNode *node, uint32 pos, uint32 level);
	const char *GetNodeText(TreeNode *node);
	void HScrollToPixel(int y);
	void ScrollToPixel(int y);
	void InvalidateNode(TreeNode *node);
	void InvalidateStartingAtNode(TreeNode *node);
	void SelectNode(TreeNode *node);
	void ExpandNode(TreeNode *node);
	void CollapseNode(TreeNode *node);
	void EnsureNodeVisible(TreeNode *node);
	void ValidateNode(TreeNode *node);
	void RefreshNode(TreeNode *node);
	uint32 GetNodeYPos(TreeNode *node) const;
	TreeNode *GetNodeFromClientPoint(int x, int y) const;
	TreeNode *GetPrevVisibleNode(TreeNode *node) const;
	TreeNode *GetNextVisibleNode(TreeNode *node) const;
	TreeNode *GetNearestVisibleNode(TreeNode *node) const;
	void UpdateScrollMax();
	void UpdateScrollBar();
	void UpdateHScrollBar();

	void Reset();
	void UpdateOpcodes();
	void ClearAllNodes();
	TreeNode *InsertNode(TreeNode *parent, TreeNode *after, const char *text, const ATCPUHistoryEntry *hent, NodeType nodeType);
	void InsertNode(TreeNode *parent, TreeNode *after, TreeNode *node);
	void RemoveNode(TreeNode *node);
	TreeNode *AllocNode();
	void FreeNode(TreeNode *);
	void FreeNodes(TreeNode *);

	void CopyVisibleNodes();
	void CopyNodes(TreeNode *begin, TreeNode *end);

	void Search(const char *substr);
	bool SearchNodes(TreeNode *node, const char *substr);

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId);

	enum {
		kMaxNestingDepth = 64,
		kControlIdPanel = 100,
		kControlIdClearButton,
		kControlIdSearchEdit
	};

	HWND mhwndPanel;
	HWND mhwndClear;
	HWND mhwndEdit;
	VDFunctionThunk	*mpEditThunk;
	WNDPROC	mEditProc;
	HMENU mMenu;
	RECT mContentRect;
	TreeNode mRootNode;
	TreeNode *mpSelectedNode;
	uint32 mWidth;
	uint32 mHeight;
	uint32 mClearButtonWidth;
	uint32 mHeaderHeight;
	uint32 mCharWidth;
	uint32 mItemHeight;
	uint32 mItemTextVOffset;
	uint32 mPageItems;
	uint32 mScrollX;
	uint32 mScrollY;
	uint32 mScrollMax;
	sint32 mScrollWheelAccum;

	uint32 mLastCounter;
	uint8 mLastS;

	enum TimestampMode {
		kTsMode_Beam,
		kTsMode_Microseconds,
		kTsMode_Cycles,
		kTsMode_UnhaltedCycles,
	} mTimestampMode;

	bool	mbFocus;
	bool	mbHistoryError;
	bool	mbUpdatesBlocked;
	bool	mbInvalidatesBlocked;
	bool	mbDirtyScrollBar;
	bool	mbDirtyHScrollBar;
	bool	mbSearchActive;
	bool	mbShowPCAddress;
	bool	mbShowRegisters;
	bool	mbShowSpecialRegisters;
	bool	mbShowFlags;
	bool	mbShowCodeBytes;
	bool	mbShowLabels;
	bool	mbShowLabelNamespaces;
	bool	mbCollapseLoops;
	bool	mbCollapseCalls;
	bool	mbCollapseInterrupts;

	uint32	mTimeBaseCycles;
	uint32	mTimeBaseUnhaltedCycles;

	uint16	mHiBaseCycles;
	uint16	mHiBaseUnhaltedCycles;
	uint16	mLastLoCycles;
	uint16	mLastLoUnhaltedCycles;

	VDStringA mTempLine;

	struct NodeBlock {
		TreeNode mNodes[256];
		NodeBlock *mpNext;
	};

	uint32	mNodeCount;
	TreeNode *mpNodeFreeList;
	NodeBlock *mpNodeBlocks;

	enum { kRepeatWindowSize = 32 };
	uint32 mRepeatIPs[kRepeatWindowSize];
	uint8 mRepeatOpcodes[kRepeatWindowSize];
	TreeNode *mpRepeatNode;
	int mRepeatLoopSize;
	int mRepeatLoopCount;
	int mRepeatInsnCount;
	int mRepeatLooseNodeCount;

	struct StackLevel {
		TreeNode *mpTreeNode;
		int mDepth;
	};

	StackLevel mStackLevels[256];

	class Panel : public ATUINativeWindow {
	public:
		Panel(ATHistoryWindow *p) : mpParent(p) {}

		virtual LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
			if (msg == WM_COMMAND) {
				if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == kControlIdClearButton) {
					if (mpParent) {
						if (mpParent->mbSearchActive) {
							mpParent->Search(NULL);
							SetWindowTextW(mpParent->mhwndEdit, L"");
						} else {
							mpParent->Search(VDGetWindowTextAW32(mpParent->mhwndEdit).c_str());
						}
					}
					return 0;
				}
			}

			return ATUINativeWindow::WndProc(msg, wParam, lParam);
		}

		ATHistoryWindow *mpParent;
	};

	vdrefptr<Panel> mpPanel;
	VDStringW mTextSearch;
	VDStringW mTextClear;
};

ATHistoryWindow::ATHistoryWindow()
	: ATUIDebuggerPane(kATUIPaneId_History, L"History")
	, mhwndPanel(NULL)
	, mhwndEdit(NULL)
	, mpEditThunk(NULL)
	, mEditProc(NULL)
	, mMenu(LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_HISTORY_CONTEXT_MENU)))
	, mpSelectedNode(NULL)
	, mWidth(0)
	, mHeight(0)
	, mClearButtonWidth(0)
	, mCharWidth(0)
	, mHeaderHeight(0)
	, mItemHeight(0)
	, mItemTextVOffset(0)
	, mPageItems(1)
	, mScrollX(0)
	, mScrollY(0)
	, mScrollMax(0)
	, mScrollWheelAccum(0)
	, mTimestampMode(kTsMode_Beam)
	, mbFocus(false)
	, mbHistoryError(false)
	, mbUpdatesBlocked(false)
	, mbInvalidatesBlocked(false)
	, mbDirtyScrollBar(false)
	, mbDirtyHScrollBar(false)
	, mbSearchActive(false)
	, mbCollapseLoops(true)
	, mbCollapseCalls(true)
	, mbCollapseInterrupts(true)
	, mbShowPCAddress(true)
	, mbShowRegisters(true)
	, mbShowSpecialRegisters(false)
	, mbShowFlags(true)
	, mbShowCodeBytes(true)
	, mbShowLabels(true)
	, mbShowLabelNamespaces(true)
	, mTimeBaseCycles(0)
	, mTimeBaseUnhaltedCycles(0)
	, mHiBaseCycles(0)
	, mHiBaseUnhaltedCycles(0)
	, mLastLoCycles(0)
	, mLastLoUnhaltedCycles(0)
	, mNodeCount(0)
	, mpNodeFreeList(NULL)
	, mpNodeBlocks(NULL)
	, mpPanel(new Panel(this))
{
	SetTouchMode(kATUITouchMode_2DPanSmooth);
	mPreferredDockCode = kATContainerDockRight;

	memset(&mContentRect, 0, sizeof mContentRect);

	memset(&mRootNode, 0, sizeof mRootNode);
	mRootNode.mbVisible = true;
	ClearAllNodes();

	mTextSearch = L"Search";
	mTextClear = L"Clear";
}

ATHistoryWindow::~ATHistoryWindow() {
	if (mMenu)
		DestroyMenu(mMenu);
}

LRESULT ATHistoryWindow::EditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
		if (wParam == VK_ESCAPE) {
			SetFocus(mhwnd);
			return 0;
		}
	} else if (msg == WM_CHAR || msg == WM_SYSCHAR) {
		if (wParam == '\r') {
			Search(VDGetWindowTextAW32(hwnd).c_str());
			return 0;
		}
	}

	return CallWindowProc(mEditProc, hwnd, msg, wParam, lParam);
}

LRESULT ATHistoryWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_PAINT:
			OnPaint();
			return 0;

		case WM_ERASEBKGND:
			return 0;

		case WM_HSCROLL:
			OnHScroll(LOWORD(wParam));
			return 0;

		case WM_VSCROLL:
			OnVScroll(LOWORD(wParam));
			return 0;

		case WM_KEYDOWN:
			if (OnKeyDown(wParam))
				return 0;
			break;

		case WM_LBUTTONDOWN:
			::SetFocus(mhwnd);
			OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
			return 0;

		case WM_LBUTTONDBLCLK:
			::SetFocus(mhwnd);
			OnLButtonDblClk(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
			return 0;

		case WM_MOUSEWHEEL:
			OnMouseWheel((short)HIWORD(wParam));
			break;

		case WM_CONTEXTMENU:
			{
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);

				HMENU menu = GetSubMenu(mMenu, 0);

				VDCheckMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_SHOWPCADDRESS, mbShowPCAddress);
				VDCheckMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_SHOWREGISTERS, mbShowRegisters);
				VDCheckMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_SHOWSPECIALREGISTERS, mbShowSpecialRegisters);
				VDEnableMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_SHOWSPECIALREGISTERS, mbShowRegisters);
				VDCheckMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_SHOWFLAGS, mbShowFlags);
				VDCheckMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_SHOWCODEBYTES, mbShowCodeBytes);
				VDCheckMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_SHOWLABELS, mbShowLabels);
				VDCheckMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_SHOWLABELNAMESPACES, mbShowLabelNamespaces);
				VDCheckMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_COLLAPSELOOPS, mbCollapseLoops);
				VDCheckMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_COLLAPSECALLS, mbCollapseCalls);
				VDCheckMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_COLLAPSEINTERRUPTS, mbCollapseInterrupts);
				VDCheckRadioMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_SHOWBEAMPOSITION, mTimestampMode == kTsMode_Beam);
				VDCheckRadioMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_SHOWMICROSECONDS, mTimestampMode == kTsMode_Microseconds);
				VDCheckRadioMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_SHOWCYCLES, mTimestampMode == kTsMode_Cycles);
				VDCheckRadioMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_SHOWUNHALTEDCYCLES, mTimestampMode == kTsMode_UnhaltedCycles);

				POINT pt = {x, y};
				ScreenToClient(mhwnd, &pt);
				TreeNode *pNode = GetNodeFromClientPoint(pt.x, pt.y);
				SelectNode(pNode);

				VDEnableMenuItemByCommandW32(menu, ID_HISTORYCONTEXTMENU_SETTIMESTAMPORIGIN, pNode && pNode->mNodeType == kNodeTypeInsn);

				TrackPopupMenu(menu, TPM_LEFTALIGN|TPM_TOPALIGN, x, y, 0, mhwnd, NULL);
			}
			return 0;

		case WM_COMMAND:
			switch(LOWORD(wParam)) {
				case ID_HISTORYCONTEXTMENU_SHOWPCADDRESS:
					mbShowPCAddress = !mbShowPCAddress;
					InvalidateRect(mhwnd, NULL, TRUE);
					return true;
				case ID_HISTORYCONTEXTMENU_SHOWREGISTERS:
					mbShowRegisters = !mbShowRegisters;
					InvalidateRect(mhwnd, NULL, TRUE);
					return true;
				case ID_HISTORYCONTEXTMENU_SHOWSPECIALREGISTERS:
					mbShowSpecialRegisters = !mbShowSpecialRegisters;
					InvalidateRect(mhwnd, NULL, TRUE);
					return true;
				case ID_HISTORYCONTEXTMENU_SHOWFLAGS:
					mbShowFlags = !mbShowFlags;
					InvalidateRect(mhwnd, NULL, TRUE);
					return true;
				case ID_HISTORYCONTEXTMENU_SHOWCODEBYTES:
					mbShowCodeBytes = !mbShowCodeBytes;
					InvalidateRect(mhwnd, NULL, TRUE);
					return true;
				case ID_HISTORYCONTEXTMENU_SHOWLABELS:
					mbShowLabels = !mbShowLabels;
					InvalidateRect(mhwnd, NULL, TRUE);
					return true;
				case ID_HISTORYCONTEXTMENU_SHOWLABELNAMESPACES:
					mbShowLabelNamespaces = !mbShowLabelNamespaces;
					InvalidateRect(mhwnd, NULL, TRUE);
					return true;

				case ID_HISTORYCONTEXTMENU_COLLAPSELOOPS:
					mbCollapseLoops = !mbCollapseLoops;
					Reset();
					UpdateOpcodes();
					return true;

				case ID_HISTORYCONTEXTMENU_COLLAPSECALLS:
					mbCollapseCalls = !mbCollapseCalls;
					Reset();
					UpdateOpcodes();
					return true;

				case ID_HISTORYCONTEXTMENU_COLLAPSEINTERRUPTS:
					mbCollapseInterrupts = !mbCollapseInterrupts;
					Reset();
					UpdateOpcodes();
					return true;

				case ID_HISTORYCONTEXTMENU_SHOWBEAMPOSITION:
					mTimestampMode = kTsMode_Beam;
					InvalidateRect(mhwnd, NULL, TRUE);
					return true;

				case ID_HISTORYCONTEXTMENU_SHOWMICROSECONDS:
					mTimestampMode = kTsMode_Microseconds;
					InvalidateRect(mhwnd, NULL, TRUE);
					return true;

				case ID_HISTORYCONTEXTMENU_SHOWCYCLES:
					mTimestampMode = kTsMode_Cycles;
					InvalidateRect(mhwnd, NULL, TRUE);
					return true;

				case ID_HISTORYCONTEXTMENU_SHOWUNHALTEDCYCLES:
					mTimestampMode = kTsMode_UnhaltedCycles;
					InvalidateRect(mhwnd, NULL, TRUE);
					return true;

				case ID_HISTORYCONTEXTMENU_RESETTIMESTAMPORIGIN:
					mTimeBaseCycles = 0;
					mTimeBaseUnhaltedCycles = 0;
					InvalidateRect(mhwnd, NULL, TRUE);
					return true;

				case ID_HISTORYCONTEXTMENU_SETTIMESTAMPORIGIN:
					if (mpSelectedNode && mpSelectedNode->mNodeType == kNodeTypeInsn) {
						mTimeBaseCycles = mpSelectedNode->mHEnt.mCycle + ((uint32)mpSelectedNode->mHiBaseCycles << 16);
						mTimeBaseUnhaltedCycles = mpSelectedNode->mHEnt.mUnhaltedCycle + ((uint32)mpSelectedNode->mHiBaseUnhaltedCycles << 16);
						InvalidateRect(mhwnd, NULL, TRUE);
					}

					return true;

				case ID_HISTORYCONTEXTMENU_COPYVISIBLETOCLIPBOARD:
					CopyVisibleNodes();
					return true;
			}

			break;

		case WM_SETFOCUS:
			if (!mbFocus) {
				mbFocus = true;
				if (mpSelectedNode)
					InvalidateNode(mpSelectedNode);
			}
			break;

		case WM_KILLFOCUS:
			if (mbFocus) {
				mbFocus = false;
				if (mpSelectedNode)
					InvalidateNode(mpSelectedNode);
			}
			break;
	}

	return ATUIDebuggerPane::WndProc(msg, wParam, lParam);
}

bool ATHistoryWindow::OnCreate() {
	if (!ATUIDebuggerPane::OnCreate())
		return false;

	mhwndPanel = CreateWindowEx(0, MAKEINTATOM(ATUINativeWindow::Register()), _T(""), WS_VISIBLE|WS_CHILD|WS_CLIPCHILDREN, 0, 0, 0, 0, mhwnd, (HMENU)kControlIdPanel, g_hInst, mpPanel);
	if (!mhwndPanel)
		return false;

	mhwndClear = CreateWindowEx(0, WC_BUTTON, _T(""), WS_VISIBLE|WS_CHILD|BS_CENTER, 0, 0, 0, 0, mhwndPanel, (HMENU)kControlIdClearButton, g_hInst, NULL);
	if (!mhwndClear)
		return false;

	mhwndEdit = CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, _T(""), WS_VISIBLE|WS_CHILD, 0, 0, 0, 0, mhwndPanel, (HMENU)kControlIdSearchEdit, g_hInst, NULL);
	if (!mhwndEdit)
		return false;

	SendMessageW(mhwndEdit, EM_SETCUEBANNER, FALSE, (LPARAM)L"substring");

	OnFontsUpdated();

	VDSetWindowTextW32(mhwndClear, mTextSearch.c_str());

	mpEditThunk = VDCreateFunctionThunkFromMethod(this, &ATHistoryWindow::EditWndProc, true);
	mEditProc = (WNDPROC)GetWindowLongPtr(mhwndEdit, GWLP_WNDPROC);
	SetWindowLongPtr(mhwndEdit, GWLP_WNDPROC, (LONG_PTR)mpEditThunk);

	if (!g_sim.GetCPU().IsHistoryEnabled()) {
		mbHistoryError = true;
		UpdateHScrollBar();
		UpdateScrollBar();
	}

	OnSize();
	Reset();
	UpdateOpcodes();
	ATGetDebugger()->AddClient(this, true);
	return true;
}

void ATHistoryWindow::OnDestroy() {
	if (mpPanel) {
		mpPanel->mpParent = NULL;
		mpPanel = NULL;
	}

	mRootNode.mpFirstChild = NULL;
	mRootNode.mpLastChild = NULL;

	for(;;) {
		NodeBlock *b = mpNodeBlocks;
		if (!b)
			break;

		mpNodeBlocks = b->mpNext;
		delete b;
	}

	mpNodeFreeList = NULL;

	mhwndPanel = NULL;

	if (mhwndEdit) {
		DestroyWindow(mhwndEdit);
		mhwndEdit = NULL;
	}

	if (mpEditThunk) {
		VDDestroyFunctionThunk(mpEditThunk);
		mpEditThunk = NULL;
	}

	ATGetDebugger()->RemoveClient(this);
	ATUIDebuggerPane::OnDestroy();
}

void ATHistoryWindow::OnSize() {
	RECT r;

	if (!GetClientRect(mhwnd, &r))
		return;

	mWidth = r.right;
	mHeight = r.bottom;
	mHeaderHeight = 0;

	if (mhwndPanel) {
		mHeaderHeight = std::max(g_monoFontLineHeight, g_propFontLineHeight) + 2*GetSystemMetrics(SM_CYEDGE);

		VDVERIFY(SetWindowPos(mhwndPanel, NULL, 0, 0, mWidth, mHeaderHeight, SWP_NOZORDER|SWP_NOACTIVATE));

		if (mhwndClear)
			VDVERIFY(SetWindowPos(mhwndClear, NULL, 0, 0, mClearButtonWidth, mHeaderHeight, SWP_NOZORDER|SWP_NOACTIVATE));

		if (mhwndEdit)
			VDVERIFY(SetWindowPos(mhwndEdit, NULL, mClearButtonWidth, 0, mWidth < mClearButtonWidth ? 0 : mWidth - mClearButtonWidth, mHeaderHeight, SWP_NOZORDER|SWP_NOACTIVATE));
	}

	mContentRect = r;
	mContentRect.top = mHeaderHeight;
	if (mContentRect.top > mContentRect.bottom)
		mContentRect.top = mContentRect.bottom;

	if (mbHistoryError) {
		InvalidateRect(mhwnd, NULL, TRUE);
		return;
	}

	mPageItems = 0;

	if (mHeight > mHeaderHeight && mItemHeight)
		mPageItems = (mHeight - mHeaderHeight) / mItemHeight;

	if (!mPageItems)
		mPageItems = 1;

	UpdateScrollMax();
	UpdateScrollBar();
	UpdateHScrollBar();

	ScrollToPixel(mScrollY);
	HScrollToPixel(mScrollX);
}

void ATHistoryWindow::OnFontsUpdated() {
	SendMessage(mhwndClear, WM_SETFONT, (WPARAM)g_propFont, TRUE);
	SendMessage(mhwndEdit, WM_SETFONT, (WPARAM)g_monoFont, TRUE);

	mCharWidth = 12;
	mItemHeight = 16;
	mItemTextVOffset = 0;

	if (HDC hdc = GetDC(mhwnd)) {
		SelectObject(hdc, g_monoFont);

		TEXTMETRIC tm = {0};
		if (GetTextMetrics(hdc, &tm)) {
			mCharWidth = tm.tmAveCharWidth;
			mItemHeight = tm.tmHeight;
			mItemTextVOffset = 0;
		}

		SelectObject(hdc, g_propFont);

		mClearButtonWidth = 0;

		SIZE sz;
		if (GetTextExtentPoint32W(hdc, mTextSearch.data(), mTextSearch.size(), &sz))
			mClearButtonWidth = sz.cx;

		if (GetTextExtentPoint32W(hdc, mTextClear.data(), mTextClear.size(), &sz)) {
			if (sz.cx > 0 && mClearButtonWidth < (uint32)sz.cx)
				mClearButtonWidth = (uint32)sz.cx;
		}

		mClearButtonWidth += 8 * GetSystemMetrics(SM_CXEDGE);

		ReleaseDC(mhwnd, hdc);
	}

	OnSize();
	InvalidateRect(mhwnd, NULL, TRUE);
}

void ATHistoryWindow::OnLButtonDown(int x, int y, int mods) {
	TreeNode *node = GetNodeFromClientPoint(x, y);

	if (!node) {
		mpSelectedNode = NULL;
		InvalidateRect(mhwnd, NULL, FALSE);
		return;
	}

	int level = 0;
	for(TreeNode *p = node->mpParent; p; p = p->mpParent)
		++level;

	if (x >= level * (int)mItemHeight) {
		mpSelectedNode = node;
		InvalidateRect(mhwnd, NULL, FALSE);
	} else if (x >= (level - 1)*(int)mItemHeight && node->mpFirstChild) {
		if (node->mbExpanded)
			CollapseNode(node);
		else
			ExpandNode(node);
	}
}

void ATHistoryWindow::OnLButtonDblClk(int x, int y, int mods) {
	TreeNode *node = GetNodeFromClientPoint(x, y);

	if (!node) {
		mpSelectedNode = NULL;
		InvalidateRect(mhwnd, NULL, FALSE);
		return;
	}

	int level = 0;
	for(TreeNode *p = node->mpParent; p; p = p->mpParent)
		++level;

	if (x >= level * (int)mItemHeight) {
		mpSelectedNode = node;
		InvalidateRect(mhwnd, NULL, FALSE);

		if (!*node->mpText)
			ATGetDebugger()->SetFramePC(node->mHEnt.mPC);
	} else if (x >= (level - 1)*(int)mItemHeight && node->mpFirstChild) {
		if (node->mbExpanded)
			CollapseNode(node);
		else
			ExpandNode(node);
	}
}

bool ATHistoryWindow::OnKeyDown(int code) {
	switch(code) {
		case VK_ESCAPE:
			if (ATGetUIPane(kATUIPaneId_Console))
				ATActivateUIPane(kATUIPaneId_Console, true);
			break;

		case VK_PRIOR:
			if (mpSelectedNode) {
				for(uint32 i=0; i<mPageItems; ++i) {
					TreeNode *p = GetPrevVisibleNode(mpSelectedNode);
					if (!p)
						break;

					SelectNode(p);
				}
			}
			break;
		case VK_NEXT:
			if (mpSelectedNode) {
				for(uint32 i=0; i<mPageItems; ++i) {
					TreeNode *p = GetNextVisibleNode(mpSelectedNode);
					if (!p)
						break;

					SelectNode(p);
				}
			}
			break;
		case VK_UP:
			if (mpSelectedNode) {
				TreeNode *p = GetPrevVisibleNode(mpSelectedNode);

				if (p) {
					SelectNode(p);
				}
			}
			break;
		case VK_DOWN:
			if (mpSelectedNode) {
				TreeNode *p = GetNextVisibleNode(mpSelectedNode);

				if (p) {
					SelectNode(p);
				}
			}
			break;
		case VK_LEFT:
			if (mpSelectedNode) {
				if (mpSelectedNode->mbExpanded) {
					CollapseNode(mpSelectedNode);
					EnsureNodeVisible(mpSelectedNode);
				} else {
					TreeNode *p = mpSelectedNode->mpParent;
					if (p != &mRootNode) {
						SelectNode(p);
					}
				}
			}
			break;
		case VK_RIGHT:
			if (mpSelectedNode) {
				TreeNode *child = mpSelectedNode->mpFirstChild;
				
				if (child) {
					if (!mpSelectedNode->mbExpanded) {
						ExpandNode(mpSelectedNode);
						EnsureNodeVisible(mpSelectedNode);
					} else {
						SelectNode(child);
					}
				}
			}
			break;
		case VK_HOME:
			if (mRootNode.mpFirstChild)
				SelectNode(mRootNode.mpFirstChild);
			break;
		case VK_END:
			{
				TreeNode *p = mRootNode.mpLastChild;

				if (!p)
					break;

				for(;;) {
					TreeNode *q = p->mpLastChild;

					if (!q || !p->mbExpanded) {
						SelectNode(p);
						break;
					}

					p = q;
				}
			}
			break;
		default:
			return false;
	}

	return true;
}

void ATHistoryWindow::OnMouseWheel(int dz) {
	mScrollWheelAccum += dz;

	int actions = mScrollWheelAccum / WHEEL_DELTA;
	if (!actions)
		return;

	mScrollWheelAccum -= actions * WHEEL_DELTA;

	UINT linesPerAction;
	if (SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &linesPerAction, FALSE)) {
		ScrollToPixel(mScrollY - (int)linesPerAction * actions * (int)mItemHeight);
	}
}

void ATHistoryWindow::OnHScroll(int code) {
	SCROLLINFO si = {0};
	si.cbSize = sizeof(SCROLLINFO);
	si.fMask = SIF_TRACKPOS | SIF_POS | SIF_PAGE | SIF_RANGE;

	GetScrollInfo(mhwnd, SB_HORZ, &si);

	int pos = si.nPos;

	switch(code) {
		case SB_TOP:
			pos = si.nMin;
			break;

		case SB_BOTTOM:
			pos = si.nMax;
			break;

		case SB_ENDSCROLL:
			break;

		case SB_LINEDOWN:
			if (si.nMax - pos >= 16 * (int)mItemHeight)
				pos += 16 * mItemHeight;
			else
				pos = si.nMax;
			break;

		case SB_LINEUP:
			if (pos - si.nMin >= 16 * (int)mItemHeight)
				pos -= 16 * mItemHeight;
			else
				pos = si.nMin;
			break;

		case SB_PAGEDOWN:
			if (si.nMax - pos >= (int)si.nPage)
				pos += si.nPage;
			else
				pos = si.nMax;
			break;

		case SB_PAGEUP:
			if (pos - si.nMin >= (int)si.nPage)
				pos -= si.nPage;
			else
				pos = si.nMin;
			break;

		case SB_THUMBPOSITION:
		case SB_THUMBTRACK:
			pos = si.nTrackPos;
			break;
	}

	if (pos != si.nPos) {
		si.nPos = pos;
		si.fMask = SIF_POS;
		SetScrollInfo(mhwnd, SB_HORZ, &si, TRUE);
	}

	HScrollToPixel(pos);
}

void ATHistoryWindow::OnVScroll(int code) {
	SCROLLINFO si = {0};
	si.cbSize = sizeof(SCROLLINFO);
	si.fMask = SIF_TRACKPOS | SIF_POS | SIF_PAGE | SIF_RANGE;

	GetScrollInfo(mhwnd, SB_VERT, &si);

	int pos = si.nPos;

	switch(code) {
		case SB_TOP:
			pos = si.nMin;
			break;

		case SB_BOTTOM:
			pos = si.nMax;
			break;

		case SB_ENDSCROLL:
			break;

		case SB_LINEDOWN:
			if (si.nMax - pos >= (int)mItemHeight)
				pos += mItemHeight;
			else
				pos = si.nMax;
			break;

		case SB_LINEUP:
			if (pos - si.nMin >= (int)mItemHeight)
				pos -= mItemHeight;
			else
				pos = si.nMin;
			break;

		case SB_PAGEDOWN:
			if (si.nMax - pos >= (int)si.nPage)
				pos += si.nPage;
			else
				pos = si.nMax;
			break;

		case SB_PAGEUP:
			if (pos - si.nMin >= (int)si.nPage)
				pos -= si.nPage;
			else
				pos = si.nMin;
			break;

		case SB_THUMBPOSITION:
		case SB_THUMBTRACK:
			pos = si.nTrackPos;
			break;
	}

	if (pos != si.nPos) {
		si.nPos = pos;
		si.fMask = SIF_POS;
		SetScrollInfo(mhwnd, SB_VERT, &si, TRUE);
	}

	ScrollToPixel(pos);
}

void ATHistoryWindow::OnPaint() {
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(mhwnd, &ps);
	if (!hdc)
		return;

	int sdc = SaveDC(hdc);
	if (sdc) {
		if (mbHistoryError) {
			SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
			SetBkMode(hdc, TRANSPARENT);

			FillRect(hdc, &mContentRect, (HBRUSH)(COLOR_WINDOW + 1));

			RECT rText = mContentRect;
			InflateRect(&rText, -GetSystemMetrics(SM_CXEDGE)*2, -GetSystemMetrics(SM_CYEDGE)*2);

			static const TCHAR kText[]=_T("History cannot be displayed because CPU history tracking is not enabled. History tracking can be enabled in CPU Options.");
			DrawText(hdc, kText, (sizeof kText / sizeof(kText[0])) - 1, &rText, DT_TOP | DT_LEFT | DT_NOPREFIX | DT_WORDBREAK);
		} else {
			SelectObject(hdc, g_monoFont);

			int y1 = ps.rcPaint.top + mScrollY - mHeaderHeight;
			int y2 = ps.rcPaint.bottom + mScrollY - mHeaderHeight;

			if (y1 < 0)
				y1 = 0;

			if (y2 < y1)
				y2 = y1;

			uint32 itemStart = (uint32)y1 / mItemHeight;
			uint32 itemEnd = (uint32)(y2 + mItemHeight - 1) / mItemHeight;

			SetBkMode(hdc, OPAQUE);
			SetTextAlign(hdc, TA_LEFT | TA_TOP);

			if (mRootNode.mpFirstChild) {
				PaintItems(hdc, &ps.rcPaint, itemStart, itemEnd, mRootNode.mpFirstChild, 0, 0);
			}

			if (itemEnd > mRootNode.mHeight - 1) {
				SetBkColor(hdc, GetSysColor(COLOR_WINDOW));

				RECT r = ps.rcPaint;
				r.top = (mRootNode.mHeight - 1) * mItemHeight - mScrollY + mHeaderHeight;
				ExtTextOut(hdc, r.left, r.top, ETO_OPAQUE, &r, _T(""), 0, NULL);
			}
		}

		RestoreDC(hdc, sdc);
	}

	EndPaint(mhwnd, &ps);
}

void ATHistoryWindow::PaintItems(HDC hdc, const RECT *rPaint, uint32 itemStart, uint32 itemEnd, TreeNode *baseNode, uint32 pos, uint32 level) {
	if (!baseNode)
		return;

	const bool is65C816 = g_sim.GetCPU().GetCPUMode() == kATCPUMode_65C816;
	TreeNode *baseParent = baseNode->mpParent;
	TreeNode *node = baseNode;

	for(;;) {
		if (pos >= itemEnd)
			return;

		// Check if the node is visible at all. If not, we can skip rendering and traversal.
		if (!node->mbVisible) {
			// skip invisible node
		} else if (pos + node->mHeight <= itemStart) {
			pos += node->mHeight;
		} else {
			if (pos >= itemStart) {
				bool selected = false;

				if (mpSelectedNode == node) {
					selected = true;
					
					uint32 bgc = GetSysColor(mbFocus ? COLOR_HIGHLIGHT : COLOR_3DFACE);
					uint32 fgc = GetSysColor(mbFocus ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);

					SetBkColor(hdc, bgc);
					SetTextColor(hdc, fgc);
				} else {
					SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
					SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
				}

				int x = mItemHeight * level - mScrollX;
				int y = pos * mItemHeight + mHeaderHeight - mScrollY;

				RECT rOpaque;
				rOpaque.left = x;
				rOpaque.top = y;
				rOpaque.right = mWidth;
				rOpaque.bottom = y + mItemHeight;

				const char *s = GetNodeText(node);

				ExtTextOutA(hdc, x + mItemHeight, rOpaque.top + mItemTextVOffset, ETO_OPAQUE | ETO_CLIPPED, &rOpaque, s, strlen(s), NULL);

				RECT rPad;
				rPad.left = rPaint->left;
				rPad.top = y;
				rPad.right = x;
				rPad.bottom = y + mItemHeight;

				FillRect(hdc, &rPad, (HBRUSH)(COLOR_WINDOW + 1));

				if (node->mpFirstChild) {
					SelectObject(hdc, selected ? GetStockObject(WHITE_PEN) : GetStockObject(BLACK_PEN));

					int boxsize = (mItemHeight - 3) & ~1;
					int x1 = x + 1;
					int y1 = y + 1;
					int x2 = x1 + boxsize;
					int y2 = y1 + boxsize;

					MoveToEx(hdc, x1, y1, NULL);
					LineTo(hdc, x2, y1);
					LineTo(hdc, x2, y2);
					LineTo(hdc, x1, y2);
					LineTo(hdc, x1, y1);

					int xh = (x1 + x2) >> 1;
					int yh = (y1 + y2) >> 1;
					MoveToEx(hdc, x1 + 2, yh, NULL);
					LineTo(hdc, x2 - 1, yh);

					if (!node->mbExpanded) {
						MoveToEx(hdc, xh, y1 + 2, NULL);
						LineTo(hdc, xh, y2 - 1);
					}
				}
			}

			// Check if we should recurse.
			if (node->mbExpanded && node->mpFirstChild) {
				++pos;
				++level;

				node = node->mpFirstChild;
				continue;
			} else
				pos += node->mHeight;
		}

		for(;;) {
			if (node == baseParent)
				return;

			if (node->mpNextSibling) {
				node = node->mpNextSibling;
				break;
			} else {
				node = node->mpParent;
				--level;
			}
		}
	}
}

const char *ATHistoryWindow::GetNodeText(TreeNode *node) {
	const char *s = node->mpText;

	switch(node->mNodeType) {
		case kNodeTypeRepeat:
			mTempLine.sprintf("Last %d insns repeated %d times", node->mRepeatSize, node->mRepeatCount);
			s = mTempLine.c_str();
			break;

		case kNodeTypeInterrupt:
			{
				const ATCPUHistoryEntry& hent = node->mHEnt;

				int beamy = (hent.mTimestamp >> 8) & 0xfff;

				mTempLine = hent.mbNMI ? beamy >= 248 ? "NMI interrupt (VBI)" : "NMI interrupt (DLI)" : "IRQ interrupt";
				s = mTempLine.c_str();
			}
			break;

		case kNodeTypeInsn:
			{
				const bool is65C816 = g_sim.GetCPU().GetCPUMode() == kATCPUMode_65C816;
				const ATCPUHistoryEntry& hent = node->mHEnt;

				switch(mTimestampMode) {
					case kTsMode_Beam:{
						uint32 frameBase = g_sim.GetAntic().GetFrameCounter();
						uint32 tsframelo = hent.mTimestamp >> 20;
						uint32 tsframe = (frameBase & ~0xfff) + tsframelo;
						if (tsframe > frameBase)
							tsframe -= 0x1000;

						if (g_sim.GetCPU().GetSubCycles() > 1) {
							mTempLine.sprintf("%d:%3d:%3d.%u | "
									, tsframe
									, (hent.mTimestamp >> 8) & 0xfff
									, hent.mTimestamp & 0xff
									, hent.mSubCycle
								);
						} else {
							mTempLine.sprintf("%d:%3d:%3d | "
									, tsframe
									, (hent.mTimestamp >> 8) & 0xfff
									, hent.mTimestamp & 0xff
								);
						}
						break;
					}

					case kTsMode_Microseconds: {
						mTempLine.sprintf("T%+.6f | ", (float)(sint32)(((uint32)node->mHiBaseCycles << 16) + hent.mCycle - mTimeBaseCycles) *
							(g_sim.GetVideoStandard() == kATVideoStandard_NTSC ? 1.0f / 1773447.0f : 1.0f / 1789772.5f));
						break;
				    }

					case kTsMode_Cycles: {
						mTempLine.sprintf("T%+d | ", (sint32)(((uint32)node->mHiBaseCycles << 16) + hent.mCycle - mTimeBaseCycles));
						mTimeBaseCycles;
						break;
					}

					case kTsMode_UnhaltedCycles: {
						mTempLine.sprintf("T%+d | ", (sint16)(((uint32)node->mHiBaseUnhaltedCycles << 16) + hent.mUnhaltedCycle - mTimeBaseUnhaltedCycles));
						break;
					}
				}

				if (mbShowRegisters) {
					if (is65C816) {
						if (!hent.mbEmulation) {
							if (hent.mP & AT6502::kFlagM) {
								mTempLine.append_sprintf("C=%02X%02X"
									, hent.mAH
									, hent.mA
									);
							} else {
								mTempLine.append_sprintf("A=%02X%02X"
									, hent.mAH
									, hent.mA
									);
							}

							if (hent.mP & AT6502::kFlagX) {
								mTempLine.append_sprintf(" X=--%02X Y=--%02X"
									, hent.mX
									, hent.mY
									);
							} else {
								mTempLine.append_sprintf(" X=%02X%02X Y=%02X%02X"
									, hent.mXH
									, hent.mX
									, hent.mYH
									, hent.mY
									);
							}

							if (mbShowSpecialRegisters) {
								mTempLine.append_sprintf(" S=%02X%02X B=%02X D=%04X P=%02X"
									, hent.mSH
									, hent.mS
									, hent.mP
									, hent.mB
									, hent.mD
									);
							}
						} else {
							mTempLine.append_sprintf("A=%02X:%02X X=%02X Y=%02X"
								, hent.mAH
								, hent.mA
								, hent.mX
								, hent.mY
								);

							if (mbShowSpecialRegisters) {
								mTempLine.append_sprintf(" S=%02X P=%02X"
									, hent.mS
									, hent.mP
									);
							}
						}
					} else {
						mTempLine.append_sprintf("A=%02X X=%02X Y=%02X"
							, hent.mA
							, hent.mX
							, hent.mY
							);

						if (mbShowSpecialRegisters) {
							mTempLine.append_sprintf(" S=%02X P=%02X"
								, hent.mS
								, hent.mP
								);
						}
					}
				}

				if (mbShowFlags) {
					if (is65C816) {
						mTempLine.append_sprintf(" (%c%c%c%c%c%c%c%c)"
							, (hent.mP & AT6502::kFlagN) ? 'N' : ' '
							, (hent.mP & AT6502::kFlagV) ? 'V' : ' '
							, (hent.mP & AT6502::kFlagM) ? 'M' : ' '
							, (hent.mP & AT6502::kFlagX) ? 'X' : ' '
							, (hent.mP & AT6502::kFlagD) ? 'D' : ' '
							, (hent.mP & AT6502::kFlagI) ? 'I' : ' '
							, (hent.mP & AT6502::kFlagZ) ? 'Z' : ' '
							, (hent.mP & AT6502::kFlagC) ? 'C' : ' '
							);
					} else {
						mTempLine.append_sprintf(" (%c%c%c%c%c%c)"
							, (hent.mP & AT6502::kFlagN) ? 'N' : ' '
							, (hent.mP & AT6502::kFlagV) ? 'V' : ' '
							, (hent.mP & AT6502::kFlagD) ? 'D' : ' '
							, (hent.mP & AT6502::kFlagI) ? 'I' : ' '
							, (hent.mP & AT6502::kFlagZ) ? 'Z' : ' '
							, (hent.mP & AT6502::kFlagC) ? 'C' : ' '
							);
					}
				}

				if (mbShowRegisters || mbShowFlags)
					mTempLine += " | ";

				ATDisassembleInsn(mTempLine, hent, false, true, mbShowPCAddress, mbShowCodeBytes, mbShowLabels, false, false, mbShowLabelNamespaces);

				s = mTempLine.c_str();
			}
			break;
	}

	return s;
}

void ATHistoryWindow::HScrollToPixel(int pos) {
	if (pos < 0)
		pos = 0;

	if (mScrollX != pos) {
		sint32 delta = (sint32)mScrollX - (sint32)pos;
		mScrollX = pos;

		ScrollWindowEx(mhwnd, delta, 0, &mContentRect, &mContentRect, NULL, NULL, SW_INVALIDATE);
		UpdateHScrollBar();
	}
}

void ATHistoryWindow::ScrollToPixel(int pos) {
	if (pos < 0)
		pos = 0;

	if ((uint32)pos > mScrollMax)
		pos = mScrollMax;

	if (mScrollY != pos) {
		sint32 delta = (sint32)mScrollY - (sint32)pos;
		mScrollY = pos;

		ScrollWindowEx(mhwnd, 0, delta, &mContentRect, &mContentRect, NULL, NULL, SW_INVALIDATE);
		UpdateScrollBar();
	}
}

void ATHistoryWindow::InvalidateNode(TreeNode *node) {
	uint32 y = GetNodeYPos(node) * mItemHeight;

	if (y < mScrollY + (mContentRect.bottom - mContentRect.top) && y + mItemHeight > mScrollY) {
		RECT r;
		r.left = 0;
		r.top = (int)y - (int)mScrollY + mHeaderHeight;
		r.right = mWidth;
		r.bottom = r.top + mItemHeight;

		InvalidateRect(mhwnd, &r, TRUE);
	}
}

void ATHistoryWindow::InvalidateStartingAtNode(TreeNode *node) {
	int y = GetNodeYPos(node) * mItemHeight;

	if ((uint32)y < mScrollY + (mContentRect.bottom - mContentRect.top))
		InvalidateRect(mhwnd, NULL, TRUE);
}

void ATHistoryWindow::SelectNode(TreeNode *node) {
	if (node == mpSelectedNode)
		return;

	mpSelectedNode = node;
	EnsureNodeVisible(node);
	InvalidateRect(mhwnd, NULL, TRUE);
}

void ATHistoryWindow::ExpandNode(TreeNode *node) {
	if (node->mbExpanded || !node->mpFirstChild)
		return;

	VDASSERT(node->mHeight == 1);

	node->mbExpanded = true;

	uint32 newHeight = 1;

	TreeNode *lastChild = node->mpLastChild;
	if (lastChild)
		newHeight = lastChild->mRelYPos + lastChild->mHeight + 1;

	node->mHeight = newHeight;

	VDASSERT(newHeight >= 1);
	uint32 delta = newHeight - 1;

	if (delta) {
		TreeNode *c = node;

		for(;;) {
			// adjust relative positions of siblings
			for(TreeNode *s = c->mpNextSibling; s; s = s->mpNextSibling)
				s->mRelYPos += delta;

			// go up to parent
			c = c->mpParent;
			if (!c)
				break;

			if (!c->mbExpanded)
				return;

			// adjust parent height
			c->mHeight += delta;
		}
	}

	UpdateScrollMax();
 
	InvalidateStartingAtNode(node);
	UpdateScrollBar();
	ScrollToPixel(mScrollY);
}

void ATHistoryWindow::CollapseNode(TreeNode *node) {
	if (!node->mbExpanded)
		return;

	node->mbExpanded = false;

	uint32 delta = node->mHeight - 1;
	node->mHeight = 1;

	if (delta) {
		TreeNode *c = node;

		for(;;) {
			// adjust relative positions of siblings
			for(TreeNode *s = c->mpNextSibling; s; s = s->mpNextSibling) {
				VDASSERT(s->mRelYPos >= delta);
				s->mRelYPos -= delta;
			}

			// go up to parent
			c = c->mpParent;
			if (!c)
				break;

			if (!c->mbExpanded)
				return;

			// adjust parent height
			VDASSERT(c->mHeight >= delta);
			c->mHeight -= delta;
		}
	}

	UpdateScrollMax();

	InvalidateStartingAtNode(node);
	UpdateScrollBar();
	ScrollToPixel(mScrollY);
}

void ATHistoryWindow::EnsureNodeVisible(TreeNode *node) {
	if (!node)
		return;

	for(TreeNode *p = node->mpParent; p; p = p->mpParent) {
		if (!p->mbExpanded)
			ExpandNode(p);
	}

	uint32 ypos = GetNodeYPos(node);
	uint32 y = ypos * mItemHeight;

	if (y < mScrollY)
		ScrollToPixel(y);
	else if (y + mItemHeight > mScrollY + (mContentRect.bottom - mContentRect.top))
		ScrollToPixel(y + mItemHeight - (mContentRect.bottom - mContentRect.top));
}

void ATHistoryWindow::ValidateNode(TreeNode *node) {
	uint32 h = 0;

	if (node->mbExpanded) {
		for(TreeNode *c = node->mpFirstChild; c; c = c->mpNextSibling) {
			VDASSERT(c->mRelYPos == h);
			h += c->mHeight;
		}
	}

	++h;
	VDASSERT(h == node->mHeight);
}

void ATHistoryWindow::RefreshNode(TreeNode *node) {
	uint32 ypos1 = GetNodeYPos(node);
	uint32 ypos2 = ypos1 + mItemHeight;

	if (ypos2 >= mScrollY && ypos1 < mScrollY + mHeight) {
		RECT r = { 0, ypos1 - mScrollY, mWidth, ypos2 - mScrollY };

		InvalidateRect(mhwnd, &r, TRUE);
	}
}

uint32 ATHistoryWindow::GetNodeYPos(TreeNode *node) const {
	sint32 y = node->mRelYPos;

	for(TreeNode *p = node->mpParent; p != &mRootNode; p = p->mpParent)
		y += p->mRelYPos + 1;

	return y;
}

ATHistoryWindow::TreeNode *ATHistoryWindow::GetNodeFromClientPoint(int x, int y) const {
	if (y < (int)mHeaderHeight || !mItemHeight)
		return NULL;

	uint32 idx = ((uint32)y - mHeaderHeight + mScrollY) / mItemHeight;

	TreeNode *p = mRootNode.mpFirstChild;
	while(p) {
		if (idx < p->mRelYPos)
			break;

		uint32 offset = idx - p->mRelYPos;
		if (p->mbVisible && offset < p->mHeight) {
			if (!offset || !p->mbExpanded)
				return p;

			idx = offset - 1;
			p = p->mpFirstChild;
		} else
			p = p->mpNextSibling;
	}

	return NULL;
}

ATHistoryWindow::TreeNode *ATHistoryWindow::GetPrevVisibleNode(TreeNode *node) const {
	TreeNode *p = node->mpPrevSibling;

	if (p) {
		while(p->mbExpanded) {
			TreeNode *q = p->mpLastChild;

			if (!q)
				break;

			p = q;
		}

		return p;
	}

	// pop up to parent
	node = node->mpParent;
	if (node != &mRootNode)
		return node;

	return NULL;
}

ATHistoryWindow::TreeNode *ATHistoryWindow::GetNextVisibleNode(TreeNode *node) const {
	if (node->mbExpanded) {
		TreeNode *q = node->mpFirstChild;

		if (q)
			return q;
	}

	do {
		TreeNode *p = node->mpNextSibling;

		if (p)
			return p;

		node = node->mpParent;
	} while(node != &mRootNode);

	return NULL;
}

ATHistoryWindow::TreeNode *ATHistoryWindow::GetNearestVisibleNode(TreeNode *node) const {
	TreeNode *firstCollapsedParent = NULL;

	if (!node)
		return node;

	for(TreeNode *p = node->mpParent; p; p = p->mpParent) {
		if (!p->mbExpanded)
			firstCollapsedParent = p;
	}

	if (firstCollapsedParent)
		return GetNextVisibleNode(firstCollapsedParent);

	return node;
}

void ATHistoryWindow::UpdateHScrollBar() {
	if (mbUpdatesBlocked) {
		mbDirtyHScrollBar = true;
		return;
	}

	if (mbHistoryError) {
		ShowScrollBar(mhwnd, SB_HORZ, FALSE);
	} else {
		SCROLLINFO si;
		si.cbSize = sizeof si;
		si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		si.nPage = mWidth;
		si.nMin = 0;
		si.nMax = mItemHeight * 64 + mCharWidth * 64;
		si.nPos = mScrollX;
		si.nTrackPos = 0;
		SetScrollInfo(mhwnd, SB_HORZ, &si, TRUE);

		ShowScrollBar(mhwnd, SB_HORZ, TRUE);
	}

	mbDirtyHScrollBar = false;
}

void ATHistoryWindow::UpdateScrollMax() {
	mScrollMax = (mRootNode.mHeight - 1) <= mPageItems ? 0 : ((mRootNode.mHeight - 1) - mPageItems) * mItemHeight;
}

void ATHistoryWindow::UpdateScrollBar() {
	if (mbUpdatesBlocked) {
		mbDirtyScrollBar = true;
		return;
	}

	if (mbHistoryError) {
		ShowScrollBar(mhwnd, SB_VERT, FALSE);
	} else {
		SCROLLINFO si;
		si.cbSize = sizeof si;
		si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		si.nPage = mPageItems * mItemHeight;
		si.nMin = 0;
		si.nMax = mScrollMax + si.nPage - 1;
		si.nPos = mScrollY;
		si.nTrackPos = 0;
		SetScrollInfo(mhwnd, SB_VERT, &si, TRUE);

		ShowScrollBar(mhwnd, SB_VERT, si.nMax > 0);
	}

	mbDirtyScrollBar = false;
}

void ATHistoryWindow::Reset() {
	memset(mStackLevels, 0, sizeof mStackLevels);

	ATCPUEmulator& cpu = g_sim.GetCPU();
	uint32 c = cpu.GetHistoryCounter();
	uint32 l = cpu.GetHistoryLength();
	mLastCounter = c > l ? c - l : 0;
	mLastS = 0xFF;

	for(int i=0; i<kRepeatWindowSize; ++i) {
		mRepeatIPs[i] = 0xFFFFFFFFUL;
		mRepeatOpcodes[i] = 0;
	}

	mpRepeatNode = NULL;
	mRepeatLoopSize = 0;
	mRepeatLoopCount = 0;
	mRepeatInsnCount = 0;
	mHiBaseCycles = 0;
	mHiBaseUnhaltedCycles = 0;
	mLastLoCycles = 0;
	mLastLoUnhaltedCycles = 0;
}

void ATHistoryWindow::UpdateOpcodes() {
	ATCPUEmulator& cpu = g_sim.GetCPU();
	if (mbHistoryError)  {
		if (!cpu.IsHistoryEnabled())
			return;

		Reset();
		mbHistoryError = false;
		OnSize();
		InvalidateRect(mhwnd, NULL, TRUE);
	} else {
		if (!cpu.IsHistoryEnabled()) {
			ClearAllNodes();
			Reset();
			mbHistoryError = true;
			UpdateHScrollBar();
			UpdateScrollBar();
			InvalidateRect(mhwnd, NULL, TRUE);
			return;
		}
	}

	bool is65C02 = cpu.GetCPUMode() == kATCPUMode_65C02;
	bool is65C816 = cpu.GetCPUMode() == kATCPUMode_65C816;
	uint32 c = cpu.GetHistoryCounter();
	uint32 dist = c - mLastCounter;
	uint32 l = cpu.GetHistoryLength();

	if (dist == 0)
		return;

	if (dist > l || mNodeCount > 500000) {
		ClearAllNodes();
		Reset();
		dist = l;
		mLastCounter = c - l;
	}

	if (mbSearchActive) {
		Search(NULL);
		if (mhwndEdit)
			SetWindowTextW(mhwndEdit, L"");
	}

	bool quickMode = false;
	if (dist > 1000) {
		quickMode = true;
		mbInvalidatesBlocked = true;
	}

	mbUpdatesBlocked = true;

	TreeNode *last = NULL;
	while(dist--) {
		const ATCPUHistoryEntry& hent = cpu.GetHistory(c - ++mLastCounter);

		// If we've had a change in stack height or an interrupt, terminate
		// any loop tracking.
		if (hent.mbIRQ || hent.mbNMI || hent.mS != mLastS) {
			mpRepeatNode = NULL;
			mRepeatLoopSize = 0;
			mRepeatLoopCount = 0;
			mRepeatInsnCount = 0;
			mRepeatLooseNodeCount = 0;
		}

		// If we're higher on the stack than before (pop/return), pop entries off the tree parent stack.
		// Note that we try to gracefully handle wrapping here. The idea is that generally the stack
		// won't go down by more than 8 entries or so (JSL+interrupt), whereas it may go up way more
		// than that when TXS is used.

		if (mLastS != hent.mS) {
			if ((uint8)(mLastS - hent.mS) >= 8) {		// hent.mS > mLastS, with some wraparound slop
				while(hent.mS != mLastS) {				// note that mLastS is a uint8 and will wrap
					mStackLevels[mLastS].mpTreeNode = NULL;
					mStackLevels[mLastS].mDepth = 0;
					++mLastS;
				}
			} else {
				while(hent.mS != mLastS) {				// note that mLastS is a uint8 and will wrap
					--mLastS;
					mStackLevels[mLastS].mpTreeNode = NULL;
					mStackLevels[mLastS].mDepth = 0;
				}
			}
		}

		// Check if we have a parent to use.
		TreeNode *parent = mStackLevels[hent.mS].mpTreeNode;
		int parentDepth = mStackLevels[hent.mS].mDepth;

		if (!parent) {
			uint8 s = hent.mS + 1;
			for(int i=0; i<8; ++i, ++s) {
				parent = mStackLevels[s].mpTreeNode;

				if (parent) {
					parentDepth = mStackLevels[s].mDepth;
					break;
				}
			}

			if (!parent)
				parent = &mRootNode;

			if (hent.mbNMI || hent.mbIRQ) {
				if (mbCollapseInterrupts) {
					if (parentDepth >= kMaxNestingDepth) {
						parent = &mRootNode;
						parentDepth = 0;
					}

					parent = InsertNode(parent, parent->mpLastChild, "", &hent, kNodeTypeInterrupt);
					++parentDepth;
				}
			} else if (mbCollapseCalls) {
				if (parentDepth >= kMaxNestingDepth) {
					parent = &mRootNode;
					parentDepth = 0;
				}

				if (parent->mpLastChild)
					parent = parent->mpLastChild;
				else
					parent = InsertNode(parent, parent->mpLastChild, "Subroutine call", NULL, kNodeTypeLabel);

				++parentDepth;
			}

			mStackLevels[hent.mS].mpTreeNode = parent;
			mStackLevels[hent.mS].mDepth = parentDepth;
		}

		// Note that there is a serious problem here in that we are only tracking an 8-bit
		// stack, when the stack is 16-bit in 65C816 native mode.
		mLastS = hent.mS;

		switch(hent.mOpcode[0]) {
			case 0x48:	// PHA
				if (is65C816 && !(hent.mP & 0x20)) {
					--mLastS;
					mStackLevels[(uint8)mLastS].mpTreeNode = parent;
					mStackLevels[(uint8)mLastS].mDepth = parentDepth;
				}
				// fall through
			case 0x08:	// PHP
				--mLastS;
				mStackLevels[(uint8)mLastS].mpTreeNode = parent;
				mStackLevels[(uint8)mLastS].mDepth = parentDepth;
				break;

			case 0x5A:	// PHY
			case 0xDA:	// PHX
				if (is65C02 || is65C816) {
					if (is65C816 && !(hent.mP & 0x10))
						--mLastS;

					--mLastS;
					mStackLevels[(uint8)mLastS].mpTreeNode = parent;
					mStackLevels[(uint8)mLastS].mDepth = parentDepth;
				}
				break;

			case 0x8B:	// PHB
			case 0x4B:	// PHK
				if (is65C816) {
					--mLastS;
					mStackLevels[(uint8)mLastS].mpTreeNode = parent;
					mStackLevels[(uint8)mLastS].mDepth = parentDepth;
				}
				break;

			case 0x0B:	// PHD
			case 0xF4:	// PEA
			case 0x62:	// PER
			case 0xD4:	// PEI
				if (is65C816) {
					mLastS -= 2;
					mStackLevels[(uint8)mLastS].mpTreeNode = parent;
					mStackLevels[(uint8)mLastS].mDepth = parentDepth;
				}
				break;
		}

		// add new node
		last = InsertNode(parent, parent->mpLastChild, "", &hent, kNodeTypeInsn);

		// update time base
		if (hent.mCycle < mLastLoCycles)
			++mHiBaseCycles;

		if (hent.mUnhaltedCycle < mLastLoUnhaltedCycles)
			++mHiBaseUnhaltedCycles;

		mLastLoCycles = hent.mCycle;
		mLastLoUnhaltedCycles = hent.mUnhaltedCycle;

		// check if we have a match on the repeat window
		int repeatOffset = -1;

		if (mRepeatLoopSize) {
			if (mRepeatIPs[mRepeatLoopSize - 1] == hent.mPC && mRepeatOpcodes[mRepeatLoopSize - 1] == hent.mOpcode[0])
				repeatOffset = mRepeatLoopSize - 1;
		}

		if (repeatOffset < 0 && mbCollapseLoops) {
			for(int i=0; i<kRepeatWindowSize; ++i) {
				if (mRepeatIPs[i] == hent.mPC && mRepeatOpcodes[i] == hent.mOpcode[0]) {
					repeatOffset = i;
					break;
				}
			}
		}

		if (repeatOffset >= 0) {
			if (mRepeatLoopSize != repeatOffset + 1) {
				mpRepeatNode = NULL;
				mRepeatLoopSize = repeatOffset + 1;
				mRepeatLoopCount = 0;
				mRepeatInsnCount = 0;
				mRepeatLooseNodeCount = 0;
			}

			++mRepeatLooseNodeCount;

			if (++mRepeatInsnCount >= mRepeatLoopSize) {
				mRepeatInsnCount = 0;
				++mRepeatLoopCount;

				if (!mpRepeatNode && mRepeatLoopCount == 3) {
					TreeNode *pred = parent->mpLastChild;

					for(int i=0; i<mRepeatLooseNodeCount; ++i)
						pred = pred->mpPrevSibling;

					mpRepeatNode = InsertNode(parent, pred, "", NULL, kNodeTypeRepeat);
				}

				if (mpRepeatNode) {
					mpRepeatNode->mRepeatCount = mRepeatLoopCount;
					mpRepeatNode->mRepeatSize = mRepeatLoopSize;
					RefreshNode(mpRepeatNode);

					TreeNode *looseNode = mpRepeatNode->mpParent->mpLastChild;

					for(int i=1; i<mRepeatLooseNodeCount; ++i)
						looseNode = looseNode->mpPrevSibling;

					while(looseNode) {
						TreeNode *nextLooseNode = looseNode->mpNextSibling;

						RemoveNode(looseNode);
						InsertNode(mpRepeatNode, mpRepeatNode->mpLastChild, looseNode);

						looseNode = nextLooseNode;
					}

					last = mpRepeatNode;

					mRepeatLooseNodeCount = 0;
				}
			}
		} else if (mRepeatLoopSize) {
			mpRepeatNode = NULL;
			mRepeatLoopSize = 0;
			mRepeatLoopCount = 0;
			mRepeatInsnCount = 0;
			mRepeatLooseNodeCount = 0;
		}

		// shift in new instruction into repeat window
		for(int i=kRepeatWindowSize - 1; i; --i) {
			mRepeatIPs[i] = mRepeatIPs[i-1];
			mRepeatOpcodes[i] = mRepeatOpcodes[i - 1];
		}

		mRepeatIPs[0] = hent.mPC;
		mRepeatOpcodes[0] = hent.mOpcode[0];
	}

	if (last)
		SelectNode(last);

	if (quickMode) {
		InvalidateRect(mhwnd, NULL, TRUE);
		mbInvalidatesBlocked = false;
	}

	mbUpdatesBlocked = false;

	if (mbDirtyScrollBar)
		UpdateScrollBar();

	if (mbDirtyHScrollBar)
		UpdateHScrollBar();
}

void ATHistoryWindow::ClearAllNodes() {
	mpSelectedNode = NULL;

	FreeNodes(&mRootNode);
	memset(&mRootNode, 0, sizeof mRootNode);
	mRootNode.mbExpanded = true;
	mRootNode.mHeight = 1;
	mRootNode.mRepeatCount = 0;

	UpdateScrollMax();

	if (mhwnd)
		InvalidateRect(mhwnd, NULL, TRUE);
}

ATHistoryWindow::TreeNode *ATHistoryWindow::InsertNode(TreeNode *parent, TreeNode *insertAfter, const char *text, const ATCPUHistoryEntry *hent, NodeType nodeType) {
	TreeNode *node = AllocNode();
	node->mpText = text;
	node->mRelYPos = 0;
	node->mbExpanded = false;
	node->mbVisible = true;
	node->mpFirstChild = NULL;
	node->mpLastChild = NULL;
	node->mHeight = 1;
	node->mRepeatCount = 0;
	node->mNodeType = nodeType;
	node->mHiBaseCycles = mHiBaseCycles;
	node->mHiBaseUnhaltedCycles = mHiBaseUnhaltedCycles;

	if (hent)
		node->mHEnt = *hent;

	InsertNode(parent, insertAfter, node);

	return node;
}

void ATHistoryWindow::InsertNode(TreeNode *parent, TreeNode *insertAfter, TreeNode *node) {
	if (!parent)
		parent = &mRootNode;

	node->mpParent = parent;

	if (insertAfter) {
		TreeNode *next = insertAfter->mpNextSibling;
		node->mpNextSibling = next;

		if (next)
			next->mpPrevSibling = node;
		else
			parent->mpLastChild = node;

		insertAfter->mpNextSibling = node;
		node->mpPrevSibling = insertAfter;

		node->mRelYPos = insertAfter->mRelYPos + insertAfter->mHeight;
	} else {
		TreeNode *next = parent->mpFirstChild;
		node->mpNextSibling = next;

		node->mpPrevSibling = NULL;
		parent->mpFirstChild = node;

		if (next)
			next->mpPrevSibling = node;
		else
			parent->mpLastChild = node;

		node->mRelYPos = 0;
	}

	// adjust positions of siblings
	for(TreeNode *s = node->mpNextSibling; s; s = s->mpNextSibling)
		++s->mRelYPos;

	// adjust heights of parents and parent siblings
	for(TreeNode *p = parent; p; p = p->mpParent) {
		if (!p->mbExpanded)
			break;

		++p->mHeight;

		for(TreeNode *ps = p->mpNextSibling; ps; ps = ps->mpNextSibling)
			++ps->mRelYPos;
	}

	UpdateScrollMax();

	if (!mbInvalidatesBlocked)
		InvalidateStartingAtNode(node);
}

void ATHistoryWindow::RemoveNode(TreeNode *node) {
	VDASSERT(node);

	TreeNode *successorNode = NULL;
	
	if (!mbInvalidatesBlocked) {
		successorNode = node;

		while(successorNode) {
			if (successorNode->mpNextSibling)
				break;

			successorNode = successorNode->mpParent;
		}
	}

	// adjust heights of parents and siblings
	TreeNode *parentNode = node->mpParent;
	uint32 nodeHeight = node->mHeight;
	for(TreeNode *p = parentNode; p && p->mbExpanded; p = p->mpParent) {
		p->mHeight -= nodeHeight;

		for(TreeNode *q = p->mpNextSibling; q; q = q->mpNextSibling)
			q->mRelYPos -= nodeHeight;
	}

	// unlink nodes
	TreeNode *nextNode = node->mpNextSibling;
	TreeNode *prevNode = node->mpPrevSibling;

	if (prevNode)
		prevNode->mpNextSibling = nextNode;
	else
		parentNode->mpFirstChild = nextNode;

	if (nextNode)
		nextNode->mpPrevSibling = prevNode;
	else
		parentNode->mpLastChild = prevNode;

	if (successorNode && !mbInvalidatesBlocked)
		InvalidateStartingAtNode(successorNode);
}

ATHistoryWindow::TreeNode *ATHistoryWindow::AllocNode() {
	if (!mpNodeFreeList) {
		NodeBlock *b = new NodeBlock;
		b->mpNext = mpNodeBlocks;
		mpNodeBlocks = b;

		TreeNode *next = mpNodeFreeList;
		
		for(int i=0; i<255; ++i) {
			TreeNode *n = &b->mNodes[i];

			n->mpPrevSibling = n;
			n->mpNextSibling = next;
			next = n;
		}

		mpNodeFreeList = next;

		return &b->mNodes[255];
	}

	TreeNode *p = mpNodeFreeList;

	VDASSERT(p->mpPrevSibling == p);

	mpNodeFreeList = p->mpNextSibling;

	p->mpPrevSibling = NULL;

	++mNodeCount;
	return p;
}

void ATHistoryWindow::FreeNode(TreeNode *node) {
	node->mpFirstChild = NULL;
	node->mpLastChild = NULL;
	node->mpNextSibling = NULL;
	node->mpPrevSibling = NULL;

	if (node != &mRootNode) {
		VDASSERT(node->mpPrevSibling != node);

		node->mpPrevSibling = node;
		node->mpNextSibling = mpNodeFreeList;
		mpNodeFreeList = node;

		--mNodeCount;
	}
}

void ATHistoryWindow::FreeNodes(TreeNode *node) {
	if (!node)
		return;

	TreeNode *p = node;

	for(;;) {
		// find first descendent that doesn't have its own children
		while(p->mpFirstChild) {
			VDASSERT(p->mpFirstChild->mpParent == p);

			p = p->mpFirstChild;
		}

		// free current node
		TreeNode *next = p->mpNextSibling;
		TreeNode *parent = p->mpParent;

		FreeNode(p);

		// exit if this is the original node
		if (p == node)
			break;

		// advance to next sibling if we have one
		if (next)
			p = next;
		else {
			p = parent;

			// this node no longer has any children as we just freed them all
			p->mpFirstChild = NULL;
		}
	}
}

void ATHistoryWindow::CopyVisibleNodes() {
	TreeNode *node1 = GetNodeFromClientPoint(0, mHeaderHeight);
	TreeNode *node2 = GetNodeFromClientPoint(0, mHeight);

	CopyNodes(node1, node2);
}

void ATHistoryWindow::CopyNodes(TreeNode *begin, TreeNode *end) {
	begin = GetNearestVisibleNode(begin);
	end = GetNearestVisibleNode(end);

	if (!begin || (end && GetNodeYPos(end) <= GetNodeYPos(begin)))
		return;

	int minLevel = INT_MAX;
	for(TreeNode *p = begin; p && p != end; p = GetNextVisibleNode(p)) {
		int level = 0;

		for(TreeNode *q = p; q; q = q->mpParent)
			++level;

		if (minLevel > level)
			minLevel = level;
	}

	if (minLevel == INT_MAX)
		return;

	VDStringA s;

	for(TreeNode *p = begin; p && p != end; p = GetNextVisibleNode(p)) {
		int level = -minLevel;

		for(TreeNode *q = p; q; q = q->mpParent)
			++level;

		if (level > 0) {
			for(int i=0; i<level; ++i) {
				s += ' ';
				s += ' ';
			}
		}

		s += p->mpFirstChild ? p->mbExpanded ? '-' : '+' : ' ';
		s += ' ';
		s += GetNodeText(p);
		s += '\r';
		s += '\n';
	}
	
	if (::OpenClipboard((HWND)mhwnd)) {
		if (::EmptyClipboard()) {
			HANDLE hMem;
			void *lpvMem;

			if (hMem = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, s.size() + 1)) {
				if (lpvMem = ::GlobalLock(hMem)) {
					memcpy(lpvMem, s.c_str(), s.size() + 1);

					::GlobalUnlock(lpvMem);
					::SetClipboardData(CF_TEXT, hMem);
					::CloseClipboard();
					return;
				}
				::GlobalFree(hMem);
			}
		}
		::CloseClipboard();
	}
}

void ATHistoryWindow::Search(const char *substr) {
	if (substr && !*substr)
		substr = NULL;

	if (!substr && !mbSearchActive)
		return;

	if (mRootNode.mpFirstChild) {
		SearchNodes(&mRootNode, substr);
	}

	const bool searchActive = (substr != NULL);

	if (mbSearchActive != searchActive) {
		mbSearchActive = searchActive;

		VDSetWindowTextW32(mhwndClear, searchActive ? mTextClear.c_str() : mTextSearch.c_str());
	}

	UpdateScrollMax();
	InvalidateRect(mhwnd, NULL, TRUE);
	UpdateScrollBar();
	EnsureNodeVisible(mpSelectedNode);
}

bool ATHistoryWindow::SearchNodes(TreeNode *node, const char *substr) {
	uint32 ht = 0;
	bool anyVisible = false;

	const size_t substrlen = substr ? strlen(substr) : 0;
	const char ch0 = substr ? substr[0] : 0;
	const char mask0 = (unsigned)(((unsigned char)ch0 & 0xdf) - 'A') < 26 ? (char)0xdf : (char)0xff;

	for(TreeNode *child = node->mpFirstChild; child; child = child->mpNextSibling) {
		bool visible = !substr;

		child->mbExpanded = (substr != NULL);

		if (child->mpFirstChild)
			visible = SearchNodes(child, substr);
		else
			node->mHeight = 1;

		if (!visible) {
			const char *s = GetNodeText(child);

			const size_t len = strlen(s);

			if (len >= substrlen) {
				size_t maxoffset = len - substrlen;
				for(size_t i = 0; i <= maxoffset; ++i) {
					if (!((s[i] ^ ch0) & mask0) && !vdstricmp(substr, s + i, substrlen)) {
						visible = true;
						break;
					}
				}
			}
		}

		child->mRelYPos = ht;
		child->mbVisible = visible;

		if (visible) {
			anyVisible = true;
			ht += child->mHeight;
		}
	}

	node->mHeight = (node->mbExpanded ? ht : 0) + 1;

	return anyVisible;
}

void ATHistoryWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	UpdateOpcodes();
}

void ATHistoryWindow::OnDebuggerEvent(ATDebugEvent eventId) {
	InvalidateRect(mhwnd, NULL, TRUE);
}

///////////////////////////////////////////////////////////////////////////

class ATConsoleWindow : public ATUIDebuggerPane {
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

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnFontsUpdated();

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

	VDFunctionThunk	*mpLogThunk;
	WNDPROC	mLogProc;
	VDFunctionThunk	*mpCmdEditThunk;
	WNDPROC	mCmdEditProc;

	bool	mbRunState;
	bool	mbEditShownDisabled;
	bool	mbAppendTimerStarted;

	VDDelegate	mDelegatePromptChanged;
	VDDelegate	mDelegateRunStateChanged;

	enum { kHistorySize = 8192 };

	char	mHistory[kHistorySize];
	int		mHistoryFront;
	int		mHistoryBack;
	int		mHistorySize;
	int		mHistoryCurrent;

	VDStringW	mAppendBuffer;
};

ATConsoleWindow *g_pConsoleWindow;

ATConsoleWindow::ATConsoleWindow()
	: ATUIDebuggerPane(kATUIPaneId_Console, L"Console")
	, mMenu(LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_DEBUGGER_MENU)))
	, mhwndLog(NULL)
	, mhwndPrompt(NULL)
	, mhwndEdit(NULL)
	, mpLogThunk(NULL)
	, mLogProc(NULL)
	, mpCmdEditThunk(NULL)
	, mCmdEditProc(NULL)
	, mbRunState(false)
	, mbEditShownDisabled(false)
	, mbAppendTimerStarted(false)
	, mHistoryFront(0)
	, mHistoryBack(0)
	, mHistorySize(0)
	, mHistoryCurrent(0)
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

			TrackPopupMenu(GetSubMenu(mMenu, 0), TPM_LEFTALIGN|TPM_TOPALIGN, x, y, 0, mhwnd, NULL);
		}
		return 0;

	case WM_TIMER:
		if (wParam == kTimerId_DisableEdit) {
			if (mbRunState && mhwndEdit) {
				if (!mbEditShownDisabled) {
					mbEditShownDisabled = true;
					SendMessage(mhwndEdit, EM_SETREADONLY, TRUE, 0);
					SendMessage(mhwndEdit, EM_SETBKGNDCOLOR, FALSE, GetSysColor(COLOR_3DFACE));
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

	return ATUIDebuggerPane::WndProc(msg, wParam, lParam);
}

bool ATConsoleWindow::OnCreate() {
	if (!ATUIDebuggerPane::OnCreate())
		return false;

	mhwndLog = CreateWindowEx(WS_EX_CLIENTEDGE, _T("RICHEDIT"), _T(""), ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_VISIBLE|WS_CHILD, 0, 0, 0, 0, mhwnd, (HMENU)100, g_hInst, NULL);
	if (!mhwndLog)
		return false;

	mhwndPrompt = CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_VISIBLE|WS_CHILD|ES_READONLY, 0, 0, 0, 0, mhwnd, (HMENU)100, g_hInst, NULL);
	if (!mhwndPrompt)
		return false;

	mhwndEdit = CreateWindowEx(WS_EX_CLIENTEDGE, _T("RICHEDIT"), _T(""), WS_VISIBLE|WS_CHILD|ES_AUTOHSCROLL, 0, 0, 0, 0, mhwnd, (HMENU)100, g_hInst, NULL);
	if (!mhwndEdit)
		return false;

	mpLogThunk = VDCreateFunctionThunkFromMethod(this, &ATConsoleWindow::LogWndProc, true);

	mLogProc = (WNDPROC)GetWindowLongPtr(mhwndLog, GWLP_WNDPROC);
	SetWindowLongPtr(mhwndLog, GWLP_WNDPROC, (LONG_PTR)mpLogThunk);

	mpCmdEditThunk = VDCreateFunctionThunkFromMethod(this, &ATConsoleWindow::CommandEditWndProc, true);

	mCmdEditProc = (WNDPROC)GetWindowLongPtr(mhwndEdit, GWLP_WNDPROC);
	SetWindowLongPtr(mhwndEdit, GWLP_WNDPROC, (LONG_PTR)mpCmdEditThunk);

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

	ATUIDebuggerPane::OnDestroy();
}

void ATConsoleWindow::OnSize() {
	RECT r;

	if (GetClientRect(mhwnd, &r)) {
		int prw = 8 * g_monoFontCharWidth + 4*GetSystemMetrics(SM_CXEDGE);
		int h = g_monoFontLineHeight + 4*GetSystemMetrics(SM_CYEDGE);

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
	if (mhwndLog)
		SendMessage(mhwndLog, WM_SETFONT, (WPARAM)g_monoFont, TRUE);

	if (mhwndEdit)
		SendMessage(mhwndEdit, WM_SETFONT, (WPARAM)g_monoFont, TRUE);

	if (mhwndPrompt)
		SendMessage(mhwndPrompt, WM_SETFONT, (WPARAM)g_monoFont, TRUE);

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
				SendMessage(mhwndEdit, EM_SETBKGNDCOLOR, TRUE, GetSysColor(COLOR_WINDOW));
			}
		} else
			SetTimer(mhwnd, kTimerId_DisableEdit, 100, NULL);
	}
}

void ATConsoleWindow::Write(const char *s) {
	if (mAppendBuffer.size() >= 4096)
		FlushAppendBuffer();

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

		s = eol+1;
	}

	if (!mbAppendTimerStarted && !mAppendBuffer.empty()) {
		mbAppendTimerStarted = true;

		SetTimer(mhwnd, kTimerId_AddText, 10, NULL);
	}
}

void ATConsoleWindow::ShowEnd() {
	SendMessage(mhwndLog, EM_SCROLLCARET, 0, 0);
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
		}
	} else if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
		switch(wParam) {
		case VK_ESCAPE:
		case VK_UP:
		case VK_DOWN:
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
	if (SendMessage(mhwndLog, EM_GETLINECOUNT, 0, 0) > 5000) {
		POINT pt;
		SendMessage(mhwndLog, EM_GETSCROLLPOS, 0, (LPARAM)&pt);
		int idx = SendMessage(mhwndLog, EM_LINEINDEX, 2000, 0);
		SendMessage(mhwndLog, EM_SETSEL, 0, idx);
		SendMessage(mhwndLog, EM_REPLACESEL, FALSE, (LPARAM)_T(""));
		SendMessage(mhwndLog, EM_SETSCROLLPOS, 0, (LPARAM)&pt);
	}

	SendMessageW(mhwndLog, EM_SETSEL, -1, -1);
	SendMessageW(mhwndLog, EM_REPLACESEL, FALSE, (LPARAM)mAppendBuffer.data());
	SendMessageW(mhwndLog, EM_SETSEL, -1, -1);
	SendMessageW(mhwndLog, EM_SCROLLCARET, 0, 0);

	mAppendBuffer.clear();
}

///////////////////////////////////////////////////////////////////////////

class ATMemoryWindow : public ATUIDebuggerPane,
							public IATDebuggerClient,
							public IVDUIMessageFilterW32
{
public:
	ATMemoryWindow(uint32 id = kATUIPaneId_Memory);
	~ATMemoryWindow();

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId);

	void SetPosition(uint32 addr);

protected:
	VDGUIHandle Create(uint32 exStyle, uint32 style, int x, int y, int cx, int cy, VDGUIHandle parent, int id);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnMessage(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result);

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnFontsUpdated();
	void OnPaint();
	void RemakeView(uint32 focusAddr);
	bool GetAddressFromPoint(int x, int y, uint32& addr) const;

	HWND	mhwndAddress;
	HMENU	mMenu;

	RECT	mTextArea;
	uint32	mViewStart;
	uint32	mCharWidth;
	uint32	mLineHeight;

	VDStringW	mName;
	VDStringA	mTempLine;
	VDStringA	mTempLine2;

	vdfastvector<uint8> mViewData;
	vdfastvector<uint32> mChangedBits;
};

ATMemoryWindow::ATMemoryWindow(uint32 id)
	: ATUIDebuggerPane(id, L"Memory")
	, mhwndAddress(NULL)
	, mMenu(LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_MEMORY_CONTEXT_MENU)))
	, mTextArea()
	, mViewStart(0)
	, mLineHeight(16)
{
	mPreferredDockCode = kATContainerDockRight;

	if (id >= kATUIPaneId_MemoryN) {
		mName.sprintf(L"Memory %u", (id & kATUIPaneId_IndexMask) + 1);
		SetName(mName.c_str());
	}
}

ATMemoryWindow::~ATMemoryWindow() {
	if (mMenu)
		DestroyMenu(mMenu);
}

LRESULT ATMemoryWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_SIZE:
			OnSize();
			break;

		case WM_NOTIFY:
			{
				const NMHDR *hdr = (const NMHDR *)lParam;

				if (hdr->idFrom == 101) {
					if (hdr->code == CBEN_ENDEDIT) {
						const NMCBEENDEDIT *info = (const NMCBEENDEDIT *)hdr;

						sint32 addr = ATGetDebugger()->ResolveSymbol(VDTextWToA(info->szText).c_str(), true);

						if (addr < 0)
							MessageBeep(MB_ICONERROR);
						else
							SetPosition(addr);

						return FALSE;
					}
				}
			}
			break;

		case WM_PAINT:
			OnPaint();
			return 0;

		case WM_CONTEXTMENU:
			{
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);

				UINT id = TrackPopupMenu(GetSubMenu(mMenu, 0), TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD, x, y, 0, mhwnd, NULL);

				switch(id) {
					case ID_CONTEXT_TOGGLEREADBREAKPOINT:
						{
							POINT pt = {x, y};
							ScreenToClient(mhwnd, &pt);

							uint32 addr;
							if (GetAddressFromPoint(pt.x, pt.y, addr)) {
								ATGetDebugger()->ToggleAccessBreakpoint(addr, false);
							}
						}
						break;

					case ID_CONTEXT_TOGGLEWRITEBREAKPOINT:
						{							
							POINT pt = {x, y};
							ScreenToClient(mhwnd, &pt);

							uint32 addr;
							if (GetAddressFromPoint(pt.x, pt.y, addr)) {
								ATGetDebugger()->ToggleAccessBreakpoint(addr, true);
							}
						}
						break;
				}
			}
			break;
	}

	return ATUIDebuggerPane::WndProc(msg, wParam, lParam);
}

bool ATMemoryWindow::OnMessage(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result) {
	return false;
}

bool ATMemoryWindow::OnCreate() {
	if (!ATUIDebuggerPane::OnCreate())
		return false;

	mhwndAddress = CreateWindowEx(0, WC_COMBOBOXEX, _T(""), WS_VISIBLE|WS_CHILD|WS_CLIPSIBLINGS|CBS_DROPDOWN|CBS_HASSTRINGS|CBS_AUTOHSCROLL, 0, 0, 0, 0, mhwnd, (HMENU)101, g_hInst, NULL);
	if (!mhwndAddress)
		return false;

	VDSetWindowTextFW32(mhwndAddress, L"%hs", ATGetDebugger()->GetAddressText(mViewStart, true).c_str());

	OnFontsUpdated();

	OnSize();
	ATGetDebugger()->AddClient(this, true);
	return true;
}

void ATMemoryWindow::OnDestroy() {
	ATGetDebugger()->RemoveClient(this);
	ATUIDebuggerPane::OnDestroy();
}

void ATMemoryWindow::OnSize() {
	RECT r;
	if (!GetClientRect(mhwnd, &r))
		return;

	RECT rAddr = {0};
	int comboHt = 0;

	if (mhwndAddress) {
		GetWindowRect(mhwndAddress, &rAddr);

		comboHt = rAddr.bottom - rAddr.top;
		VDVERIFY(SetWindowPos(mhwndAddress, NULL, 0, 0, r.right, comboHt, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
	}

	mTextArea.left = 0;
	mTextArea.top = comboHt;
	mTextArea.right = r.right;
	mTextArea.bottom = r.bottom;

	if (mTextArea.bottom < mTextArea.top)
		mTextArea.bottom = mTextArea.top;

	int rows = (mTextArea.bottom - mTextArea.top + mLineHeight - 1) / mLineHeight;

	mChangedBits.resize(rows, 0);

	RemakeView(mViewStart);
}

void ATMemoryWindow::OnFontsUpdated() {
	HDC hdc = GetDC(mhwnd);
	if (hdc) {
		HGDIOBJ hOldFont = SelectObject(hdc, g_monoFont);
		if (hOldFont) {
			TEXTMETRIC tm = {0};
			if (GetTextMetrics(hdc, &tm)) {
				mCharWidth = tm.tmAveCharWidth;
				mLineHeight = tm.tmHeight;
			}

			SelectObject(hdc, hOldFont);
		}

		ReleaseDC(mhwnd, hdc);
	}

	InvalidateRect(mhwnd, NULL, TRUE);
}

void ATMemoryWindow::OnPaint() {
	PAINTSTRUCT ps;
	HDC hdc = ::BeginPaint(mhwnd, &ps);
	if (!hdc)
		return;

	int saveHandle = ::SaveDC(hdc);
	if (saveHandle) {
		uint8 data[16];

		COLORREF normalTextColor = GetSysColor(COLOR_WINDOWTEXT);
		COLORREF changedTextColor = GetSysColor(COLOR_WINDOWTEXT) | 0x0000ff;

		::SelectObject(hdc, g_monoFont);
		::SetTextAlign(hdc, TA_TOP | TA_LEFT);
		::SetBkMode(hdc, TRANSPARENT);
		::SetBkColor(hdc, GetSysColor(COLOR_WINDOW));

		int rowStart	= (ps.rcPaint.top - mTextArea.top) / mLineHeight;
		int rowEnd		= (ps.rcPaint.bottom - mTextArea.top + mLineHeight - 1) / mLineHeight;

		uint32 incMask = 0xffff;

		switch(mViewStart & kATAddressSpaceMask) {
			case kATAddressSpace_PORTB:
				incMask = 0xfffff;
				break;

			case kATAddressSpace_VBXE:
				incMask = 0x7ffff;
				break;
		}

		uint32 addrBase = mViewStart & ~incMask;
		for(int rowIndex = rowStart; rowIndex < rowEnd; ++rowIndex) {
			uint32 addr = addrBase + ((mViewStart + (rowIndex << 4)) & incMask);

			mTempLine.sprintf("%s:", ATGetDebugger()->GetAddressText(addr, false).c_str());
			mTempLine2.clear();
			mTempLine2.resize(mTempLine.size(), ' ');

			uint32 changeMask = (rowIndex < (int)mChangedBits.size()) ? mChangedBits[rowIndex] : 0;
			for(int i=0; i<16; ++i) {
				uint32 baddr = addrBase + ((addr + i) & incMask);

				data[i] = g_sim.DebugGlobalReadByte(baddr);

				if (changeMask & (1 << i)) {
					mTempLine += "   ";
					mTempLine2.append_sprintf(" %02X", data[i]);
				} else {
					mTempLine.append_sprintf(" %02X", data[i]);
					mTempLine2 += "   ";
				}
			}

			mTempLine += " |";
			for(int i=0; i<16; ++i) {
				uint8 c = data[i];

				if ((uint8)(c - 0x20) >= 0x5f)
					c = '.';

				mTempLine += c;
			}

			mTempLine += '|';

			RECT rLine;
			rLine.left = ps.rcPaint.left;
			rLine.top = mTextArea.top + mLineHeight * rowIndex;
			rLine.right = ps.rcPaint.right;
			rLine.bottom = rLine.top + mLineHeight;

			::SetTextColor(hdc, normalTextColor);
			::ExtTextOutA(hdc, mTextArea.left, rLine.top, ETO_OPAQUE, &rLine, mTempLine.data(), mTempLine.size(), NULL);

			if (changeMask) {
				::SetTextColor(hdc, changedTextColor);
				::ExtTextOutA(hdc, mTextArea.left, rLine.top, 0, NULL, mTempLine2.data(), mTempLine2.size(), NULL);
			}
		}

		::RestoreDC(hdc, saveHandle);
	}

	::EndPaint(mhwnd, &ps);
}

void ATMemoryWindow::RemakeView(uint32 focusAddr) {
	ATCPUEmulatorMemory& mem = g_sim.GetCPUMemory();

	bool changed = false;
	uint32 changeMask = 0xFFFFFFFFU;

	if (mViewStart != focusAddr) {
		mViewStart = focusAddr;
		changed = true;
		changeMask = 0;
	}

	int rows = mChangedBits.size();

	int limit = mViewData.size();

	if (limit != (rows << 4))
		changed = true;

	mViewData.resize(rows << 4, 0);

	for(int i=0; i<rows; ++i) {
		uint32 mask = 0;

		for(int j=0; j<16; ++j) {
			int offset = (i<<4) + j;
			uint32 addr = mViewStart + offset;
			uint8 c = g_sim.DebugExtReadByte(addr);

			if (offset < limit && mViewData[offset] != c)
				mask |= (1 << j);

			mViewData[offset] = c;
		}

		// clear the change mask if we are changing address
		mask &= changeMask;

		if (mChangedBits[i] | mask)
			changed = true;

		mChangedBits[i] = mask;
	}

	if (changed)
		::InvalidateRect(mhwnd, NULL, TRUE);
}

bool ATMemoryWindow::GetAddressFromPoint(int x, int y, uint32& addr) const {
	if (!mLineHeight || !mCharWidth)
		return false;
	
	if ((uint32)(x - mTextArea.left) >= (uint32)(mTextArea.right - mTextArea.left))
		return false;

	if ((uint32)(y - mTextArea.top) >= (uint32)(mTextArea.bottom - mTextArea.top))
		return false;

	int addrLen = 4;

	switch(mViewStart & kATAddressSpaceMask) {
		case kATAddressSpace_CPU:
			if (mViewStart >= 0x010000)
				addrLen = 6;
			break;

		case kATAddressSpace_ANTIC:
		case kATAddressSpace_RAM:
			addrLen = 6;
			break;

		case kATAddressSpace_VBXE:
			addrLen = 7;
			break;

		case kATAddressSpace_PORTB:
			addrLen = 7;
			break;
	}

	int xc = (x - mTextArea.left) / mCharWidth - addrLen;
	int yc = (y - mTextArea.top) / mLineHeight;

	// 0000: 00 00 00 00 00 00 00 00-00 00 00 00 00 00 00 00 |................|
	if (xc >= 2 && xc < 50) {
		addr = mViewStart + (uint16)((yc << 4) + (xc - 2)/3);
		return true;
	} else if (xc >= 51 && xc < 67) {
		addr = mViewStart + (uint16)((yc << 4) + (xc - 51));
		return true;
	}

	return false;
}

void ATMemoryWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	RemakeView(mViewStart);
}

void ATMemoryWindow::OnDebuggerEvent(ATDebugEvent eventId) {
}

void ATMemoryWindow::SetPosition(uint32 addr) {
	VDSetWindowTextFW32(mhwndAddress, L"%hs", ATGetDebugger()->GetAddressText(addr, true).c_str());

	RemakeView(addr);
}

///////////////////////////////////////////////////////////////////////////

class ATWatchWindow : public ATUIDebuggerPane, public IATDebuggerClient
{
public:
	ATWatchWindow(uint32 id = kATUIPaneId_WatchN);
	~ATWatchWindow();

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId);

	void SetPosition(uint32 addr);

protected:
	VDGUIHandle Create(uint32 exStyle, uint32 style, int x, int y, int cx, int cy, VDGUIHandle parent, int id);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void OnSize();

	void OnItemLabelChanged(VDUIProxyListView *sender, VDUIProxyListView::LabelChangedEvent *event);
	LRESULT ListViewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	HWND	mhwndList;
	WNDPROC	mpListViewPrevWndProc;

	VDStringW	mName;

	VDFunctionThunk	*mpListViewThunk;

	VDUIProxyListView mListView;
	VDUIProxyMessageDispatcherW32 mDispatcher;
	VDDialogResizerW32 mResizer;

	VDDelegate	mDelItemLabelChanged;

	struct WatchItem : public vdrefcounted<IVDUIListViewVirtualItem> {
	public:
		WatchItem() {}
		void GetText(int subItem, VDStringW& s) const {
			if (subItem)
				s = mValueStr;
			else
				s = mExprStr;
		}

		void SetExpr(const wchar_t *expr) {
			mExprStr = expr;
			mpExpr.reset();

			try {
				mpExpr = ATDebuggerParseExpression(VDTextWToA(expr).c_str(), ATGetDebugger(), ATGetDebugger()->GetExprOpts());
			} catch(const ATDebuggerExprParseException& ex) {
				mValueStr = L"<Evaluation error: ";
				mValueStr += VDTextAToW(ex.gets());
				mValueStr += L'>';
			}
		}

		bool Update() {
			if (!mpExpr)
				return false;

			ATDebugExpEvalContext ctx = {};
			ctx.mpCPU = &g_sim.GetCPU();
			ctx.mpMemory = &g_sim.GetCPUMemory();
			ctx.mpAntic = &g_sim.GetAntic();
			ctx.mpMMU = g_sim.GetMMU();

			sint32 result;
			if (mpExpr->Evaluate(result, ctx))
				mNextValueStr.sprintf(L"%d ($%04x)", result, result);
			else
				mNextValueStr = L"<Unable to evaluate>";

			if (mValueStr == mNextValueStr)
				return false;

			mValueStr = mNextValueStr;
			return true;
		}

	protected:
		VDStringW	mExprStr;
		VDStringW	mValueStr;
		VDStringW	mNextValueStr;
		vdautoptr<ATDebugExpNode> mpExpr;
	};
};

ATWatchWindow::ATWatchWindow(uint32 id)
	: ATUIDebuggerPane(id, L"")
	, mhwndList(NULL)
	, mpListViewThunk(NULL)
{
	mPreferredDockCode = kATContainerDockRight;

	mName.sprintf(L"Watch %u", (id & kATUIPaneId_IndexMask) + 1);
	SetName(mName.c_str());

	mListView.OnItemLabelChanged() += mDelItemLabelChanged.Bind(this, &ATWatchWindow::OnItemLabelChanged);

	mpListViewThunk = VDCreateFunctionThunkFromMethod(this, &ATWatchWindow::ListViewWndProc, true);
}

ATWatchWindow::~ATWatchWindow() {
	if (mpListViewThunk) {
		VDDestroyFunctionThunk(mpListViewThunk);
		mpListViewThunk = NULL;
	}
}

LRESULT ATWatchWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_SIZE:
			OnSize();
			break;

		case WM_NCDESTROY:
			mDispatcher.RemoveAllControls(true);
			break;

		case WM_COMMAND:
			return mDispatcher.Dispatch_WM_COMMAND(wParam, lParam);

		case WM_NOTIFY:
			return mDispatcher.Dispatch_WM_NOTIFY(wParam, lParam);

		case WM_ERASEBKGND:
			{
				HDC hdc = (HDC)wParam;
				mResizer.Erase(&hdc);
			}
			return TRUE;
	}

	return ATUIDebuggerPane::WndProc(msg, wParam, lParam);
}

bool ATWatchWindow::OnCreate() {
	if (!ATUIDebuggerPane::OnCreate())
		return false;

	mhwndList = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_VISIBLE | WS_CHILD | LVS_SHOWSELALWAYS | LVS_REPORT | LVS_ALIGNLEFT | LVS_EDITLABELS | LVS_NOSORTHEADER | LVS_SINGLESEL, 0, 0, 0, 0, mhwnd, (HMENU)101, g_hInst, NULL);
	if (!mhwndList)
		return false;

	mpListViewPrevWndProc = (WNDPROC)GetWindowLongPtrW(mhwndList, GWLP_WNDPROC);
	SetWindowLongPtrW(mhwndList, GWLP_WNDPROC, (LONG_PTR)(WNDPROC)mpListViewThunk);

	mListView.Attach(mhwndList);
	mDispatcher.AddControl(&mListView);

	mListView.SetFullRowSelectEnabled(true);
	mListView.SetGridLinesEnabled(true);

	mListView.InsertColumn(0, L"Expression", 0);
	mListView.InsertColumn(1, L"Value", 0);

	mListView.AutoSizeColumns();

	mListView.InsertVirtualItem(0, new WatchItem);

	mResizer.Init(mhwnd);
	mResizer.Add(101, VDDialogResizerW32::kMC | VDDialogResizerW32::kAvoidFlicker);

	OnSize();
	ATGetDebugger()->AddClient(this, false);
	return true;
}

void ATWatchWindow::OnDestroy() {
	mListView.Clear();
	ATGetDebugger()->RemoveClient(this);
	ATUIDebuggerPane::OnDestroy();
}

void ATWatchWindow::OnSize() {
	mResizer.Relayout();
}

void ATWatchWindow::OnItemLabelChanged(VDUIProxyListView *sender, VDUIProxyListView::LabelChangedEvent *event) {
	const int n = sender->GetItemCount();
	const bool isLast = event->mIndex == n - 1;

	if (*event->mpNewLabel) {
		if (isLast)
			sender->InsertVirtualItem(n, new WatchItem);

		WatchItem *item = static_cast<WatchItem *>(sender->GetVirtualItem(event->mIndex));
		if (item) {
			item->SetExpr(event->mpNewLabel);
			item->Update();
			mListView.RefreshItem(event->mIndex);
			mListView.AutoSizeColumns();
		}
	} else {
		if (!isLast)
			sender->DeleteItem(event->mIndex);

		event->mbAllowEdit = false;
	}
}

void ATWatchWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	const int n = mListView.GetItemCount() - 1;
	
	for(int i=0; i<n; ++i) {
		WatchItem *item = static_cast<WatchItem *>(mListView.GetVirtualItem(i));

		if (item && item->Update())
			mListView.RefreshItem(i);
	}
}

LRESULT ATWatchWindow::ListViewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_KEYDOWN:
			switch(LOWORD(wParam)) {
				case VK_DELETE:
					{
						int idx = mListView.GetSelectedIndex();

						if (idx >= 0 && idx < mListView.GetItemCount() - 1)
							mListView.DeleteItem(idx);
					}
					return 0;

				case VK_F2:
					{
						int idx = mListView.GetSelectedIndex();

						if (idx >= 0)
							mListView.EditItemLabel(idx);
					}
					break;
			}
			break;

		case WM_KEYUP:
			switch(LOWORD(wParam)) {
				case VK_DELETE:
				case VK_F2:
					return 0;
			}
			break;

		case WM_CHAR:
			{
				int idx = mListView.GetSelectedIndex();

				if (idx < 0)
					idx = mListView.GetItemCount() - 1;

				if (idx >= 0) {
					HWND hwndEdit = (HWND)SendMessageW(hwnd, LVM_EDITLABELW, idx, 0);

					if (hwndEdit) {
						SendMessage(hwndEdit, msg, wParam, lParam);
						return 0;
					}
				}
			}
			break;
	}

	return CallWindowProcW(mpListViewPrevWndProc, hwnd, msg, wParam, lParam);
}

void ATWatchWindow::OnDebuggerEvent(ATDebugEvent eventId) {
}

///////////////////////////////////////////////////////////////////////////

class ATDebugDisplayWindow : public ATUIDebuggerPane, public IATDebuggerClient
{
public:
	ATDebugDisplayWindow(uint32 id = kATUIPaneId_DebugDisplay);
	~ATDebugDisplayWindow();

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId);

protected:
	VDGUIHandle Create(uint32 exStyle, uint32 style, int x, int y, int cx, int cy, VDGUIHandle parent, int id);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void OnSize();

	LRESULT DLAddrEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT PFAddrEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	HWND	mhwndDisplay;
	HWND	mhwndDLAddrCombo;
	HWND	mhwndPFAddrCombo;
	HMENU	mhmenu;
	int		mComboResizeInProgress;

	VDFunctionThunk *mpThunkDLEditCombo;
	VDFunctionThunk *mpThunkPFEditCombo;
	WNDPROC	mWndProcDLAddrEdit;
	WNDPROC	mWndProcPFAddrEdit;

	IVDVideoDisplay *mpDisplay;
	ATDebugDisplay	mDebugDisplay;
};

ATDebugDisplayWindow::ATDebugDisplayWindow(uint32 id)
	: ATUIDebuggerPane(id, L"Debug Display")
	, mhwndDisplay(NULL)
	, mhwndDLAddrCombo(NULL)
	, mhwndPFAddrCombo(NULL)
	, mhmenu(LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_DEBUGDISPLAY_CONTEXT_MENU)))
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
						int sel = SendMessageW(mhwndDLAddrCombo, CB_GETCURSEL, 0, 0);

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
						int sel = SendMessageW(mhwndDLAddrCombo, CB_GETCURSEL, 0, 0);

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

	return ATUIDebuggerPane::WndProc(msg, wParam, lParam);
}

bool ATDebugDisplayWindow::OnCreate() {
	if (!ATUIDebuggerPane::OnCreate())
		return false;

	HFONT hfont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	mhwndDLAddrCombo = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_VISIBLE|WS_CHILD|WS_CLIPSIBLINGS|CBS_DROPDOWN|CBS_HASSTRINGS|CBS_AUTOHSCROLL, 0, 0, 0, 0, mhwnd, (HMENU)102, g_hInst, NULL);
	if (mhwndDLAddrCombo) {
		SendMessageW(mhwndDLAddrCombo, WM_SETFONT, (WPARAM)hfont, TRUE);
		SendMessageW(mhwndDLAddrCombo, CB_ADDSTRING, 0, (LPARAM)L"Auto DL (History)");
		SendMessageW(mhwndDLAddrCombo, CB_ADDSTRING, 0, (LPARAM)L"Auto DL (History start)");
		SendMessageW(mhwndDLAddrCombo, CB_SETCURSEL, 0, 0);
		SendMessageW(mhwndDLAddrCombo, CB_SETEDITSEL, 0, MAKELONG(-1, 0));

		COMBOBOXINFO cbi = {sizeof(COMBOBOXINFO)};
		if (mpThunkDLEditCombo && GetComboBoxInfo(mhwndDLAddrCombo, &cbi)) {
			mWndProcDLAddrEdit = (WNDPROC)GetWindowLongPtrW(cbi.hwndItem, GWLP_WNDPROC);
			SetWindowLongPtrW(cbi.hwndItem, GWLP_WNDPROC, (LONG_PTR)mpThunkDLEditCombo);
		}
	}

	mhwndPFAddrCombo = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_VISIBLE|WS_CHILD|WS_CLIPSIBLINGS|CBS_DROPDOWN|CBS_HASSTRINGS|CBS_AUTOHSCROLL, 0, 0, 0, 0, mhwnd, (HMENU)103, g_hInst, NULL);
	if (mhwndPFAddrCombo) {
		SendMessageW(mhwndPFAddrCombo, WM_SETFONT, (WPARAM)hfont, TRUE);
		SendMessageW(mhwndPFAddrCombo, CB_ADDSTRING, 0, (LPARAM)L"Auto PF Address");
		SendMessageW(mhwndPFAddrCombo, CB_SETCURSEL, 0, 0);
		SendMessageW(mhwndPFAddrCombo, CB_SETEDITSEL, 0, MAKELONG(-1, 0));

		COMBOBOXINFO cbi = {sizeof(COMBOBOXINFO)};
		if (mpThunkPFEditCombo && GetComboBoxInfo(mhwndPFAddrCombo, &cbi)) {
			mWndProcPFAddrEdit = (WNDPROC)GetWindowLongPtrW(cbi.hwndItem, GWLP_WNDPROC);
			SetWindowLongPtrW(cbi.hwndItem, GWLP_WNDPROC, (LONG_PTR)mpThunkPFEditCombo);
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
	mDebugDisplay.Update();

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
	ATUIDebuggerPane::OnDestroy();
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
		vdrect32 rd(r.left, r.top, r.right, r.bottom);
		sint32 w = rd.width();
		sint32 h = std::max(rd.height() - comboHeight, 0);
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
	mDebugDisplay.Update();
}

void ATDebugDisplayWindow::OnDebuggerEvent(ATDebugEvent eventId) {
}

///////////////////////////////////////////////////////////////////////////

class ATPrinterOutputWindow : public ATUIPane,
							  public IATPrinterOutput
{
public:
	ATPrinterOutputWindow();
	~ATPrinterOutputWindow();

	void WriteLine(const char *line);

protected:
	VDGUIHandle Create(uint32 exStyle, uint32 style, int x, int y, int cx, int cy, VDGUIHandle parent, int id);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnFontsUpdated();
	void OnSetFocus();

	vdrefptr<IVDTextEditor> mpTextEditor;
	HWND	mhwndTextEditor;
};

ATPrinterOutputWindow::ATPrinterOutputWindow()
	: ATUIPane(kATUIPaneId_PrinterOutput, L"Printer Output")
	, mhwndTextEditor(NULL)
{
	mPreferredDockCode = kATContainerDockBottom;
}

ATPrinterOutputWindow::~ATPrinterOutputWindow() {
}

void ATPrinterOutputWindow::WriteLine(const char *line) {
	if (mpTextEditor)
		mpTextEditor->Append(line);
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

	return ATUIPane::WndProc(msg, wParam, lParam);
}

bool ATPrinterOutputWindow::OnCreate() {
	if (!ATUIPane::OnCreate())
		return false;

	if (!VDCreateTextEditor(~mpTextEditor))
		return false;

	mhwndTextEditor = (HWND)mpTextEditor->Create(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd, 100);

	OnFontsUpdated();

	mpTextEditor->SetReadOnly(true);

	OnSize();

	IATPrinterEmulator *p = g_sim.GetPrinter();

	if (p)
		p->SetOutput(this);

	return true;
}

void ATPrinterOutputWindow::OnDestroy() {
	IATPrinterEmulator *p = g_sim.GetPrinter();

	if (p)
		p->SetOutput(NULL);

	ATUIPane::OnDestroy();
}

void ATPrinterOutputWindow::OnSize() {
	RECT r;
	if (mhwndTextEditor && GetClientRect(mhwnd, &r)) {
		VDVERIFY(SetWindowPos(mhwndTextEditor, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER|SWP_NOACTIVATE));
	}
}

void ATPrinterOutputWindow::OnFontsUpdated() {
	if (mhwndTextEditor)
		SendMessage(mhwndTextEditor, WM_SETFONT, (WPARAM)g_monoFont, TRUE);
}

void ATPrinterOutputWindow::OnSetFocus() {
	::SetFocus(mhwndTextEditor);
}

///////////////////////////////////////////////////////////////////////////

bool g_uiDebuggerMode;

void ATShowConsole() {
	if (!g_uiDebuggerMode) {
		ATSavePaneLayout(NULL);
		g_uiDebuggerMode = true;

		if (!ATRestorePaneLayout(NULL)) {
			ATActivateUIPane(kATUIPaneId_Console, !ATGetUIPane(kATUIPaneId_Console));
			ATActivateUIPane(kATUIPaneId_Registers, false);
		}
	}
}

void ATOpenConsole() {
	if (!g_uiDebuggerMode) {
		ATSavePaneLayout(NULL);
		g_uiDebuggerMode = true;
		if (ATRestorePaneLayout(NULL)) {
			if (ATGetUIPane(kATUIPaneId_Console))
				ATActivateUIPane(kATUIPaneId_Console, true);
		} else {
			ATActivateUIPane(kATUIPaneId_Console, true);
			ATActivateUIPane(kATUIPaneId_Registers, false);
		}
	}
}

void ATCloseConsole() {
	if (g_uiDebuggerMode) {
		ATSavePaneLayout(NULL);
		g_uiDebuggerMode = false;
		ATRestorePaneLayout(NULL);

		if (ATGetUIPane(kATUIPaneId_Display))
			ATActivateUIPane(kATUIPaneId_Display, true);
	}
}

bool ATIsDebugConsoleActive() {
	return g_uiDebuggerMode;
}

void *ATGetConsoleFontW32() {
	return g_monoFont;
}

int ATGetConsoleFontLineHeightW32() {
	return g_monoFontLineHeight;
}

///////////////////////////////////////////////////////////////////////////

void ATConsoleGetFont(LOGFONTW& font, int& pointSizeTenths) {
	font = g_monoFontDesc;
	pointSizeTenths = g_monoFontPtSizeTenths;
}

void ATConsoleSetFontDpi(UINT dpi) {
	if (g_monoFontDpi != dpi) {
		g_monoFontDpi = dpi;

		ATConsoleSetFont(g_monoFontDesc, g_monoFontPtSizeTenths);
	}
}

void ATConsoleSetFont(const LOGFONTW& font0, int pointSizeTenths) {
	LOGFONTW font = font0;

	if (g_monoFontDpi && g_monoFontPtSizeTenths)
		font.lfHeight = -MulDiv(pointSizeTenths, g_monoFontDpi, 720);

	HFONT newMonoFont = CreateFontIndirectW(&font);

	if (!newMonoFont) {
		vdwcslcpy(font.lfFaceName, L"Courier New", vdcountof(font.lfFaceName));

		newMonoFont = CreateFontIndirectW(&font);

		if (!newMonoFont)
			newMonoFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	}

	HFONT newPropFont = CreateFontW(g_monoFontDpi ? -MulDiv(8, g_monoFontDpi, 72) : -10, 0, 0, 0, 0, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"MS Shell Dlg 2");
	if (!newPropFont)
		newPropFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

	g_monoFontLineHeight = 10;
	g_monoFontCharWidth = 10;
	g_propFontLineHeight = 10;

	HDC hdc = GetDC(NULL);
	if (hdc) {
		HGDIOBJ hgoFont = SelectObject(hdc, newMonoFont);
		if (hgoFont) {
			TEXTMETRIC tm={0};

			if (GetTextMetrics(hdc, &tm)) {
				g_monoFontLineHeight = tm.tmHeight + tm.tmExternalLeading;
				g_monoFontCharWidth = tm.tmAveCharWidth;
			}
		}

		if (SelectObject(hdc, newPropFont)) {
			TEXTMETRIC tm={0};

			if (GetTextMetrics(hdc, &tm)) {
				g_propFontLineHeight = tm.tmHeight + tm.tmExternalLeading;
			}
		}

		SelectObject(hdc, hgoFont);
		ReleaseDC(NULL, hdc);
	}

	g_monoFontDesc = font;
	g_monoFontPtSizeTenths = pointSizeTenths;

	HFONT prevMonoFont = g_monoFont;
	HFONT prevPropFont = g_propFont;
	g_monoFont = newMonoFont;
	g_propFont = newPropFont;

	if (g_pMainWindow)
		g_pMainWindow->NotifyFontsUpdated();

	if (prevMonoFont)
		DeleteObject(prevMonoFont);

	if (prevPropFont)
		DeleteObject(prevPropFont);

	VDRegistryAppKey key("Settings");
	key.setString("Console: Font family", font.lfFaceName);
	key.setInt("Console: Font size", font.lfHeight);
	key.setInt("Console: Font point size tenths", pointSizeTenths);
}

namespace {
	template<class T, class U>
	bool ATUIPaneClassFactory(uint32 id, U **pp) {
		T *p = new_nothrow T(id);
		if (!p)
			return false;

		*pp = static_cast<U *>(p);
		p->AddRef();
		return true;
	}
}

void ATInitUIPanes() {
	VDLoadSystemLibraryW32("riched32");

	ATRegisterUIPaneType(kATUIPaneId_Registers, VDRefCountObjectFactory<ATRegistersWindow, ATUIPane>);
	ATRegisterUIPaneType(kATUIPaneId_Console, VDRefCountObjectFactory<ATConsoleWindow, ATUIPane>);
	ATRegisterUIPaneType(kATUIPaneId_Disassembly, VDRefCountObjectFactory<ATDisassemblyWindow, ATUIPane>);
	ATRegisterUIPaneType(kATUIPaneId_CallStack, VDRefCountObjectFactory<ATCallStackWindow, ATUIPane>);
	ATRegisterUIPaneType(kATUIPaneId_History, VDRefCountObjectFactory<ATHistoryWindow, ATUIPane>);
	ATRegisterUIPaneType(kATUIPaneId_Memory, VDRefCountObjectFactory<ATMemoryWindow, ATUIPane>);
	ATRegisterUIPaneType(kATUIPaneId_PrinterOutput, VDRefCountObjectFactory<ATPrinterOutputWindow, ATUIPane>);
	ATRegisterUIPaneType(kATUIPaneId_DebugDisplay, VDRefCountObjectFactory<ATDebugDisplayWindow, ATUIPane>);
	ATRegisterUIPaneClass(kATUIPaneId_MemoryN, ATUIPaneClassFactory<ATMemoryWindow, ATUIPane>);
	ATRegisterUIPaneClass(kATUIPaneId_WatchN, ATUIPaneClassFactory<ATWatchWindow, ATUIPane>);
	ATRegisterUIPaneClass(kATUIPaneId_Source, ATSourceWindow::CreatePane);

	if (!g_monoFont) {
		LOGFONTW consoleFont(g_monoFontDesc);

		VDRegistryAppKey key("Settings");
		VDStringW family;
		int fontSize;
		int pointSizeTenths = g_monoFontPtSizeTenths;

		if (key.getString("Console: Font family", family)
			&& (fontSize = key.getInt("Console: Font size", 0))) {

			consoleFont.lfHeight = fontSize;
			vdwcslcpy(consoleFont.lfFaceName, family.c_str(), sizeof(consoleFont.lfFaceName)/sizeof(consoleFont.lfFaceName[0]));

			int dpiY = 96;
			HDC hdc = GetDC(NULL);

			if (hdc) {
				dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
				ReleaseDC(NULL, hdc);
			}

			pointSizeTenths = (abs(consoleFont.lfHeight) * 720 + (dpiY >> 1)) / dpiY;
		}

		pointSizeTenths = key.getInt("Console: Font point size tenths", pointSizeTenths);

		ATConsoleSetFont(consoleFont, pointSizeTenths);
	}

	g_hmenuSrcContext = LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_SOURCE_CONTEXT_MENU));
}

void ATShutdownUIPanes() {
	if (g_monoFont) {
		DeleteObject(g_monoFont);
		g_monoFont = NULL;
	}
}

namespace {
	uint32 GetIdFromFrame(ATFrameWindow *fw) {
		uint32 contentId = 0;

		if (fw) {
			ATUIPane *pane = ATGetUIPaneByFrame(fw);
			if (pane)
				contentId = pane->GetUIPaneId();
		}

		return contentId;
	}

	void SerializeDockingPane(VDStringA& s, ATContainerDockingPane *pane) {
		ATFrameWindow *visFrame = pane->GetVisibleFrame();

		// serialize local content
		s += '{';

		uint32 nc = pane->GetContentCount();
		for(uint32 i=0; i<nc; ++i) {
			ATFrameWindow *w = pane->GetContent(i);
			uint32 id = GetIdFromFrame(w);

			if (id && id < kATUIPaneId_Source) {
				s.append_sprintf(",%x" + (s.back() == '{'), id);

				if (w == visFrame)
					s += '*';
			}
		}

		s += '}';

		// serialize children
		const uint32 n = pane->GetChildCount();
		for(uint32 i=0; i<n; ++i) {
			ATContainerDockingPane *child = pane->GetChildPane(i);

			s.append_sprintf(",(%d,%.4f:"
				, child->GetDockCode()
				, child->GetDockFraction()
				);

			SerializeDockingPane(s, child);

			s += ')';
		}
	}
}

void ATSavePaneLayout(const char *name) {
	if (!name)
		name = g_uiDebuggerMode ? "Debugger" : "Standard";

	VDStringA s;
	SerializeDockingPane(s, g_pMainWindow->GetBasePane());

	uint32 n = g_pMainWindow->GetUndockedPaneCount();
	for(uint32 i=0; i<n; ++i) {
		ATFrameWindow *w = g_pMainWindow->GetUndockedPane(i);
		HWND hwndFrame = w->GetHandleW32();

		WINDOWPLACEMENT wp = {sizeof(WINDOWPLACEMENT)};
		if (!GetWindowPlacement(hwndFrame, &wp))
			continue;

		s.append_sprintf(";%d,%d,%d,%d,%d,%x"
			, wp.rcNormalPosition.left
			, wp.rcNormalPosition.top
			, wp.rcNormalPosition.right
			, wp.rcNormalPosition.bottom
			, wp.showCmd == SW_MAXIMIZE
			, GetIdFromFrame(w)
			);
	}

	VDRegistryAppKey key("Pane layouts 2");
	key.setString(name, s.c_str());
}

namespace {
	const char *UnserializeDockablePane(const char *s, ATContainerWindow *cont, ATContainerDockingPane *pane, int dockingCode, float dockingFraction, vdhashmap<uint32, ATFrameWindow *>& frames) {
		// Format we are reading:
		//	node = '{' [id[','id...]] '}' [',' '(' dock_code, fraction: sub_node ')' ...]

		if (*s != '{')
			return NULL;

		++s;

		if (*s != '}') {
			ATFrameWindow *visFrame = NULL;

			for(;;) {
				char *next = NULL;
				uint32 id = (uint32)strtoul(s, &next, 16);
				if (!id || s == next)
					return NULL;

				s = next;

				ATFrameWindow *w = NULL;
				vdhashmap<uint32, ATFrameWindow *>::iterator it(frames.find(id));

				if (it != frames.end()) {
					w = it->second;
					frames.erase(it);
				} else {
					ATActivateUIPane(id, false, false);
					ATUIPane *uipane = ATGetUIPane(id);

					if (uipane) {
						HWND hwndPane = uipane->GetHandleW32();
						if (hwndPane) {
							HWND hwndFrame = GetParent(hwndPane);

							if (hwndFrame)
								w = ATFrameWindow::GetFrameWindow(hwndFrame);
						}
					}
				}

				if (w) {
					if (*s == '*') {
						++s;
						visFrame = w;
					}

					ATContainerDockingPane *nextPane = cont->DockFrame(w, pane, dockingCode);
					if (dockingCode != kATContainerDockCenter) {
						nextPane->SetDockFraction(dockingFraction);
						dockingCode = kATContainerDockCenter;
					}

					pane = nextPane;
				}

				if (*s != ',')
					break;

				++s;
			}

			if (visFrame)
				pane->SetVisibleFrame(visFrame);
		}

		if (*s++ != '}')
			return NULL;

		while(*s == ',') {
			++s;

			if (*s++ != '(')
				return NULL;

			int code;
			float frac;
			int len = -1;
			int n = sscanf(s, "%d,%f%n", &code, &frac, &len);

			if (n < 2 || len <= 0)
				return NULL;

			s += len;

			if (*s++ != ':')
				return NULL;

			s = UnserializeDockablePane(s, cont, pane, code, frac, frames);
			if (!s)
				return NULL;

			if (*s++ != ')')
				return NULL;
		}

		return s;
	}
}

bool ATRestorePaneLayout(const char *name) {
	if (!name)
		name = g_uiDebuggerMode ? "Debugger" : "Standard";

	VDRegistryAppKey key("Pane layouts 2");
	VDStringA str;

	if (!key.getString(name, str))
		return false;

	ATUIPane *activePane = ATGetUIPaneByFrame(g_pMainWindow->GetActiveFrame());
	uint32 activePaneId = activePane ? activePane->GetUIPaneId() : 0;
	HWND hwndActiveFocus = NULL;

	if (activePaneId) {
		HWND hwndFrame = activePane->GetHandleW32();
		HWND hwndFocus = ::GetFocus();

		if (hwndFrame && hwndFocus && (hwndFrame == hwndFocus || IsChild(hwndFrame, hwndFocus)))
			hwndActiveFocus = hwndFocus;
	}

	g_pMainWindow->SuspendLayout();

	// undock and hide all docked panes
	typedef vdhashmap<uint32, ATFrameWindow *> Frames;
	Frames frames;

	vdfastvector<ATUIPane *> activePanes;
	ATGetUIPanes(activePanes);

	while(!activePanes.empty()) {
		ATUIPane *pane = activePanes.back();
		activePanes.pop_back();

		HWND hwndPane = pane->GetHandleW32();
		if (hwndPane) {
			HWND hwndParent = GetParent(hwndPane);
			if (hwndParent) {
				ATFrameWindow *w = ATFrameWindow::GetFrameWindow(hwndParent);

				if (w) {
					frames[pane->GetUIPaneId()] = w;

					if (w->GetPane()) {
						ATContainerWindow *c = w->GetContainer();

						c->UndockFrame(w, false);
					}
				}
			}
		}
	}

	g_pMainWindow->RemoveAnyEmptyNodes();

	const char *s = str.c_str();

	// parse dockable panes
	s = UnserializeDockablePane(s, g_pMainWindow, g_pMainWindow->GetBasePane(), kATContainerDockCenter, 0, frames);
	g_pMainWindow->RemoveAnyEmptyNodes();
	g_pMainWindow->Relayout();

	// parse undocked panes
	if (s) {
		while(*s++ == ';') {
			int x1;
			int y1;
			int x2;
			int y2;
			int maximized;
			int id;
			int len = -1;

			int n = sscanf(s, "%d,%d,%d,%d,%d,%x%n"
				, &x1
				, &y1
				, &x2
				, &y2
				, &maximized
				, &id
				, &len);

			if (n < 6 || len < 0)
				break;

			s += len;

			if (*s && *s != ';')
				break;

			if (!id || id >= kATUIPaneId_Count)
				continue;

			vdhashmap<uint32, ATFrameWindow *>::iterator it(frames.find(id));
			ATFrameWindow *w = NULL;

			if (it != frames.end()) {
				w = it->second;
				frames.erase(it);
			} else {
				ATActivateUIPane(id, false, false);
				ATUIPane *uipane = ATGetUIPane(id);

				if (!uipane)
					continue;

				HWND hwndPane = uipane->GetHandleW32();
				if (!hwndPane)
					continue;

				HWND hwndFrame = GetParent(hwndPane);
				if (!hwndFrame)
					continue;

				w = ATFrameWindow::GetFrameWindow(hwndFrame);
				if (!w)
					continue;
			}

			HWND hwndFrame = w->GetHandleW32();
			WINDOWPLACEMENT wp = {sizeof(WINDOWPLACEMENT)};

			wp.rcNormalPosition.left = x1;
			wp.rcNormalPosition.top = y1;
			wp.rcNormalPosition.right = x2;
			wp.rcNormalPosition.bottom = y2;
			wp.showCmd = maximized ? SW_MAXIMIZE : SW_RESTORE;
			::SetWindowPlacement(hwndFrame, &wp);
			::ShowWindow(hwndFrame, SW_SHOWNOACTIVATE);
		}
	}

	if (activePaneId && !frames[activePaneId]) {
		activePane = ATGetUIPane(activePaneId);
		if (activePane) {
			HWND hwndPane = activePane->GetHandleW32();
			if (hwndPane) {
				HWND hwndFrame = ::GetParent(hwndPane);
				if (hwndFrame) {
					ATFrameWindow *f = ATFrameWindow::GetFrameWindow(hwndFrame);

					g_pMainWindow->ActivateFrame(f);

					if (IsWindow(hwndActiveFocus))
						::SetFocus(hwndActiveFocus);
					else
						::SetFocus(f->GetHandleW32());
				}
			}
		}
	}

	// delete any unwanted panes
	for(Frames::const_iterator it = frames.begin(), itEnd = frames.end();
		it != itEnd;
		++it)
	{
		ATFrameWindow *w = it->second;

		if (w) {
			HWND hwndFrame = w->GetHandleW32();

			if (hwndFrame)
				DestroyWindow(hwndFrame);
		}
	}

	g_pMainWindow->ResumeLayout();

	return true;
}

void ATLoadDefaultPaneLayout() {
	g_pMainWindow->Clear();

	if (g_uiDebuggerMode) {
		ATActivateUIPane(kATUIPaneId_Display, false);
		ATActivateUIPane(kATUIPaneId_Console, true, true, kATUIPaneId_Display, kATContainerDockBottom);
		ATActivateUIPane(kATUIPaneId_Registers, false, true, kATUIPaneId_Display, kATContainerDockRight);
		ATActivateUIPane(kATUIPaneId_Disassembly, false, true, kATUIPaneId_Registers, kATContainerDockCenter);
		ATActivateUIPane(kATUIPaneId_History, false, true, kATUIPaneId_Registers, kATContainerDockCenter);
	} else {
		ATActivateUIPane(kATUIPaneId_Display, true);
	}
}

void ATConsoleWrite(const char *s) {
	if (g_pConsoleWindow) {
		g_pConsoleWindow->Write(s);
		g_pConsoleWindow->ShowEnd();
	}
}

void ATConsolePrintf(const char *format, ...) {
	if (g_pConsoleWindow) {
		char buf[3072];
		va_list val;

		va_start(val, format);
		if ((unsigned)_vsnprintf(buf, 3072, format, val) < 3072)
			g_pConsoleWindow->Write(buf);
		va_end(val);
		g_pConsoleWindow->ShowEnd();
	}
}

void ATConsoleTaggedPrintf(const char *format, ...) {
	if (g_pConsoleWindow) {
		ATAnticEmulator& antic = g_sim.GetAntic();
		ATConsolePrintf("(%3d:%3d,%3d) ", antic.GetFrameCounter(), antic.GetBeamY(), antic.GetBeamX());

		char buf[3072];
		va_list val;

		va_start(val, format);
		if ((unsigned)_vsnprintf(buf, 3072, format, val) < 3072)
			g_pConsoleWindow->Write(buf);
		va_end(val);
		g_pConsoleWindow->ShowEnd();
	}
}

bool ATConsoleCheckBreak() {
	return GetAsyncKeyState(VK_CONTROL) < 0 && (GetAsyncKeyState(VK_CANCEL) < 0 || GetAsyncKeyState(VK_PAUSE) < 0 || GetAsyncKeyState('A' + 2) < 0);
}
