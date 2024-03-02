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
#include <hash_map>
#include <vd2/Dita/services.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/filewatcher.h>
#include <vd2/system/thread.h>
#include <vd2/system/thunk.h>
#include <vd2/system/refcount.h>
#include <vd2/system/registry.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/w32assist.h>
#include "console.h"
#include "ui.h"
#include "uiframe.h"
#include "texteditor.h"
#include "simulator.h"
#include "debugger.h"
#include "resource.h"
#include "disasm.h"
#include "symbols.h"
#include "printer.h"

extern HINSTANCE g_hInst;
extern HWND g_hwnd;

extern ATSimulator g_sim;
extern ATContainerWindow *g_pMainWindow;

void ATConsoleExecuteCommand(char *s);

///////////////////////////////////////////////////////////////////////////

class ATUIPane;

namespace {
	HFONT	g_monoFont;
	int		g_monoFontLineHeight;
	HMENU	g_hmenuSrcContext;
}

///////////////////////////////////////////////////////////////////////////

class ATDisassemblyWindow : public ATUIPane,
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
	VDGUIHandle Create(uint32 exStyle, uint32 style, int x, int y, int cx, int cy, VDGUIHandle parent, int id);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnMessage(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result);

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnSetFocus();
	bool OnCommand(UINT cmd);
	void RemakeView(uint16 focusAddr);

	vdrefptr<IVDTextEditor> mpTextEditor;
	HWND	mhwndTextEditor;
	HWND	mhwndAddress;
	HMENU	mhmenu;

	uint32	mViewStart;
	uint32	mViewLength;
	uint8	mViewBank;
	int		mPCLine;
	uint32	mPCAddr;
	int		mFramePCLine;
	uint32	mFramePCAddr;
	ATCPUSubMode	mLastSubMode;
	
	ATDebuggerSystemState mLastState;

	vdfastvector<uint16> mAddressesByLine;
};

ATDisassemblyWindow::ATDisassemblyWindow()
	: ATUIPane(kATUIPaneId_Disassembly, "Disassembly")
	, mhwndTextEditor(NULL)
	, mhwndAddress(NULL)
	, mhmenu(LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_DISASM_CONTEXT_MENU)))
	, mViewStart(0)
	, mViewLength(0)
	, mPCLine(-1)
	, mPCAddr(0)
	, mFramePCLine(-1)
	, mFramePCAddr(0)
	, mLastSubMode(kATCPUSubMode_6502)
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

						sint32 addr = ATGetDebugger()->ResolveSymbol(info->szText);

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

	return ATUIPane::WndProc(msg, wParam, lParam);
}

bool ATDisassemblyWindow::OnMessage(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result) {
	switch(msg) {
		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
			if (wParam == VK_PAUSE || wParam == VK_CANCEL) {
				if (GetKeyState(VK_CONTROL) < 0) {
					ATGetDebugger()->Break();
				}
			} else if (wParam == VK_F8) {
				if (ATGetDebugger()->IsRunning())
					ATGetDebugger()->Break();
				else
					ATGetDebugger()->Run(kATDebugSrcMode_Disasm);
			} else if (wParam == VK_F9) {
				size_t line = (size_t)mpTextEditor->GetCursorLine();

				if (line < mAddressesByLine.size())
					ATGetDebugger()->ToggleBreakpoint(mAddressesByLine[line]);
				return true;
			} else if (wParam == VK_F10) {
				ATGetDebugger()->StepOver(kATDebugSrcMode_Disasm);
				return true;
			} else if (wParam == VK_F11) {
				if (GetKeyState(VK_SHIFT) < 0)
					ATGetDebugger()->StepOut(kATDebugSrcMode_Disasm);
				else
					ATGetDebugger()->StepInto(kATDebugSrcMode_Disasm);
				return true;
			}
			break;

		case WM_SYSKEYUP:
			switch(wParam) {
				case VK_PAUSE:
				case VK_CANCEL:
				case VK_F8:
				case VK_F9:
				case VK_F10:
				case VK_F11:
					return true;
			}
			break;

		case WM_CONTEXTMENU:
			{
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);

				if (x >= 0 && y >= 0) {
					POINT pt = {x, y};

					if (ScreenToClient(mhwndTextEditor, &pt)) {
						mpTextEditor->SetCursorPixelPos(pt.x, pt.y);
						TrackPopupMenu(GetSubMenu(mhmenu, 0), TPM_LEFTALIGN|TPM_TOPALIGN, x, y, 0, mhwnd, NULL);
					}
				} else {
					TrackPopupMenu(GetSubMenu(mhmenu, 0), TPM_LEFTALIGN|TPM_TOPALIGN, x, y, 0, mhwnd, NULL);
				}
			}
			break;
	}

	return false;
}

bool ATDisassemblyWindow::OnCreate() {
	if (!ATUIPane::OnCreate())
		return false;

	if (!VDCreateTextEditor(~mpTextEditor))
		return false;

	mhwndAddress = CreateWindowEx(0, WC_COMBOBOXEX, "", WS_VISIBLE|WS_CHILD|WS_CLIPSIBLINGS|CBS_DROPDOWN|CBS_HASSTRINGS|CBS_AUTOHSCROLL, 0, 0, 0, 0, mhwnd, (HMENU)101, g_hInst, NULL);

	mhwndTextEditor = (HWND)mpTextEditor->Create(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd, 100);
	SendMessage(mhwndTextEditor, WM_SETFONT, (WPARAM)g_monoFont, NULL);

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
	ATUIPane::OnDestroy();
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
					if (lookup->LookupLine(addr, moduleId, lineInfo)) {
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
					MessageBox(mhwnd, "There is no source line associated with that location.", "Altirra Error", MB_ICONERROR | MB_OK);
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
	}

	return false;
}

void ATDisassemblyWindow::RemakeView(uint16 focusAddr) {
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
		if (pc == mPCAddr)
			mPCLine = line;
		else if (pc == mFramePCAddr)
			mFramePCLine = line;

		if (pc <= focusAddr)
			focusLine = line;

		++line;

		if (autoModeSwitching && pc == mPCAddr)
			hent = hent0;

		mAddressesByLine.push_back(pc);
		ATDisassembleCaptureInsnContext(pc, mViewBank, hent);
		buf.clear();
		uint16 newpc = ATDisassembleInsn(buf, hent, false);
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
						uint8 xor = hent.mP ^ e;

						if (xor) {
							e ^= xor;
							hent.mP ^= xor;

							hent.mbEmulation = e != 0;

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
		mpTextEditor->RecolorAll();
		return;
	}

	bool changed = false;

	if (mLastState.mPC != state.mPC || mLastState.mFramePC != state.mFramePC)
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

	if (changed && ((state.mFramePC - mViewStart) >= mViewLength || g_sim.GetCPU().GetCPUSubMode() != mLastSubMode)) {
		SetPosition(state.mFramePC);
	} else {
		const int n = mAddressesByLine.size();

		for(int line = 0; line < n; ++line) {
			uint16 addr = mAddressesByLine[line];

			if (addr == mPCAddr)
				mPCLine = line;
			else if (addr == mFramePCAddr)
				mFramePCLine = line;
		}

		if (changed) {
			if (mPCLine < 0) {
				SetPosition(state.mFramePC);
				return;
			}

			if (mPCLine >= 0) {
				mpTextEditor->SetCursorPos(mPCLine, 0);
				mpTextEditor->MakeLineVisible(mPCLine);
			} else if (mFramePCLine >= 0) {
				mpTextEditor->SetCursorPos(mFramePCLine, 0);
				mpTextEditor->MakeLineVisible(mFramePCLine);
			}
		}

		mpTextEditor->RecolorAll();
	}
}

void ATDisassemblyWindow::OnDebuggerEvent(ATDebugEvent eventId) {
	if (eventId == kATDebugEvent_BreakpointsChanged)
		mpTextEditor->RecolorAll();
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
		uint16 addr = mAddressesByLine[line];

		if (cpu.IsBreakpointSet(addr)) {
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

	uint32 offset = mpTextEditor->GetVisibleLineCount() * 12;

	if (offset < 0x80)
		offset = 0x80;

	mViewStart = ATDisassembleGetFirstAnchor(addr >= offset ? addr - offset : 0, addr);
	mViewBank = (uint8)(addr >> 16);

	RemakeView(addr);
}

///////////////////////////////////////////////////////////////////////////

class ATRegistersWindow : public ATUIPane, public IATDebuggerClient {
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

	HWND	mhwndEdit;
	VDStringA	mState;
};

ATRegistersWindow::ATRegistersWindow()
	: ATUIPane(kATUIPaneId_Registers, "Registers")
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

	return ATUIPane::WndProc(msg, wParam, lParam);
}

bool ATRegistersWindow::OnCreate() {
	if (!ATUIPane::OnCreate())
		return false;

	mhwndEdit = CreateWindowEx(0, "EDIT", "", WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_AUTOVSCROLL|ES_MULTILINE, 0, 0, 0, 0, mhwnd, (HMENU)100, VDGetLocalModuleHandleW32(), NULL);
	if (!mhwndEdit)
		return false;

	SendMessage(mhwndEdit, WM_SETFONT, (WPARAM)g_monoFont, TRUE);

	OnSize();

	ATGetDebugger()->AddClient(this, true);
	return true;
}

void ATRegistersWindow::OnDestroy() {
	ATGetDebugger()->RemoveClient(this);
	ATUIPane::OnDestroy();
}

void ATRegistersWindow::OnSize() {
	RECT r;
	if (mhwndEdit && GetClientRect(mhwnd, &r)) {
		VDVERIFY(SetWindowPos(mhwndEdit, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE));
	}
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
			mState.append_sprintf("    %c%c1%c%c%c%c%c\r\n"
				, state.mP & 0x80 ? 'N' : '-'
				, state.mP & 0x40 ? 'V' : '-'
				, state.mP & 0x10 ? 'B' : '-'
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
		mState.append_sprintf("    %c%c1%c%c%c%c%c\r\n"
			, state.mP & 0x80 ? 'N' : '-'
			, state.mP & 0x40 ? 'V' : '-'
			, state.mP & 0x10 ? 'B' : '-'
			, state.mP & 0x08 ? 'D' : '-'
			, state.mP & 0x04 ? 'I' : '-'
			, state.mP & 0x02 ? 'Z' : '-'
			, state.mP & 0x01 ? 'C' : '-'
			);
	}

	SetWindowText(mhwndEdit, mState.c_str());
}

///////////////////////////////////////////////////////////////////////////

class ATCallStackWindow : public ATUIPane, public IATDebuggerClient {
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

	HWND	mhwndList;
	VDStringA	mState;

	vdfastvector<uint16> mFrames;
};

ATCallStackWindow::ATCallStackWindow()
	: ATUIPane(kATUIPaneId_CallStack, "Call Stack")
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
	}

	return ATUIPane::WndProc(msg, wParam, lParam);
}

bool ATCallStackWindow::OnCreate() {
	if (!ATUIPane::OnCreate())
		return false;

	mhwndList = CreateWindowEx(0, "LISTBOX", "", WS_CHILD|WS_VISIBLE|LBS_HASSTRINGS|LBS_NOTIFY, 0, 0, 0, 0, mhwnd, (HMENU)100, VDGetLocalModuleHandleW32(), NULL);
	if (!mhwndList)
		return false;

	SendMessage(mhwndList, WM_SETFONT, (WPARAM)g_monoFont, TRUE);

	OnSize();

	ATGetDebugger()->AddClient(this, true);
	return true;
}

void ATCallStackWindow::OnDestroy() {
	ATGetDebugger()->RemoveClient(this);
	ATUIPane::OnDestroy();
}

void ATCallStackWindow::OnSize() {
	RECT r;
	if (mhwndList && GetClientRect(mhwnd, &r)) {
		VDVERIFY(SetWindowPos(mhwndList, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE));
	}
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
		SendMessage(mhwndList, LB_ADDSTRING, 0, (LPARAM)mState.c_str());
	}
}

///////////////////////////////////////////////////////////////////////////

typedef vdfastvector<IATSourceWindow *> SourceWindows;
SourceWindows g_sourceWindows;

class ATSourceWindow : public VDShaderEditorBaseWindow
					 , public IATSimulatorCallback
					 , public IATDebuggerClient
					 , public IVDTextEditorCallback
					 , public IVDTextEditorColorizer
					 , public IVDFileWatcherCallback
					 , public IATSourceWindow
{
public:
	ATSourceWindow();
	~ATSourceWindow();

	VDGUIHandle Create(uint32 exStyle, uint32 style, int x, int y, int cx, int cy, VDGUIHandle parent, int id);

	void LoadFile(const wchar_t *s, const wchar_t *alias);

	const wchar_t *GetPath() const {
		return mPath.c_str();
	}

	const wchar_t *GetPathAlias() const {
		return mPathAlias.empty() ? NULL : mPathAlias.c_str();
	}

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	bool OnCommand(UINT cmd);

	void OnTextEditorUpdated();
	void OnTextEditorScrolled(int firstVisiblePara, int lastVisiblePara, int visibleParaCount, int totalParaCount);
	void RecolorLine(int line, const char *text, int length, IVDTextEditorColorization *colorization);

	void OnSimulatorEvent(ATSimulatorEvent ev);
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId);
	int GetLineForAddress(uint32 addr);
	bool OnFileUpdated(const wchar_t *path);

	void	ActivateLine(int line);
	void	FocusOnLine(int line);
	void	SetPCLine(int line);
	void	SetFramePCLine(int line);

	sint32	GetCurrentLineAddress() const;

	uint32	mModuleId;
	uint16	mFileId;
	sint32	mLastPC;
	sint32	mLastFramePC;

	int		mPCLine;
	int		mFramePCLine;

	vdrefptr<IVDTextEditor>	mpTextEditor;
	HWND	mhwndTextEditor;
	HACCEL	mhAccel;
	VDStringW	mPath;
	VDStringW	mPathAlias;

	typedef stdext::hash_map<uint32, uint32> AddressLookup;
	AddressLookup	mAddressToLineLookup;
	AddressLookup	mLineToAddressLookup;

	typedef std::map<uint32, int> LooseAddressLookup;
	LooseAddressLookup	mAddressToLineLooseLookup;

	VDFileWatcher	mFileWatcher;
};

ATSourceWindow::ATSourceWindow()
	: mLastPC(-1)
	, mLastFramePC(-1)
	, mPCLine(-1)
	, mFramePCLine(-1)
	, mhAccel(LoadAccelerators(g_hInst, MAKEINTRESOURCE(IDR_DEBUGGER_ACCEL)))
{
}

ATSourceWindow::~ATSourceWindow() {
}

VDGUIHandle ATSourceWindow::Create(uint32 exStyle, uint32 style, int x, int y, int cx, int cy, VDGUIHandle parent, int id) {
	return (VDGUIHandle)CreateWindowEx(exStyle, (LPCSTR)sWndClass, "", style, x, y, cx, cy, (HWND)parent, (HMENU)id, VDGetLocalModuleHandleW32(), static_cast<VDShaderEditorBaseWindow *>(this));
}

void ATSourceWindow::LoadFile(const wchar_t *s, const wchar_t *alias) {
	VDTextInputFile ifile(s);

	mpTextEditor->Clear();

	mAddressToLineLookup.clear();
	mLineToAddressLookup.clear();

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
		} else {
			if (!lineno && !strncmp(line, "mads ", 5))
				listingMode = true;
		}

		mpTextEditor->Append(line);
		mpTextEditor->Append("\n");
		++lineno;
	}

	if (ATGetDebuggerSymbolLookup()->LookupFile(s, mModuleId, mFileId)) {
		vdfastvector<ATSourceLineInfo> lines;

		ATGetDebuggerSymbolLookup()->GetLinesForFile(mModuleId, mFileId, lines);

		vdfastvector<ATSourceLineInfo>::const_iterator it(lines.begin()), itEnd(lines.end());
		for(; it!=itEnd; ++it) {
			const ATSourceLineInfo& linfo = *it;

			mAddressToLineLookup.insert(AddressLookup::value_type(linfo.mOffset, linfo.mLine - 1));
			mLineToAddressLookup.insert(AddressLookup::value_type(linfo.mLine - 1, linfo.mOffset));
		}
	} else {
		mModuleId = 0;
		mFileId = 0;
	}

	mAddressToLineLooseLookup.clear();

	for(AddressLookup::const_iterator it(mAddressToLineLookup.begin()), itEnd(mAddressToLineLookup.end()); it != itEnd; ++it) {
		mAddressToLineLooseLookup.insert(*it);
	}

	mPath = s;

	if (alias)
		mPathAlias = alias;
	else
		mPathAlias.clear();

	mFileWatcher.Init(s, this);

	VDSetWindowTextFW32(mhwnd, L"%ls [source] - Altirra", VDFileSplitPath(s));
	ATGetDebugger()->RequestClientUpdate(this);
}

LRESULT ATSourceWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			return OnCreate() ? 0 : -1;

		case WM_DESTROY:
			OnDestroy();
			break;

		case WM_SIZE:
			OnSize();
			return 0;

		case WM_APP+100:
			{
				MSG& msg = *(MSG *)lParam;

				if (TranslateAccelerator(mhwnd, mhAccel, &msg))
					return TRUE;
			}
			return FALSE;

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

	return VDShaderEditorBaseWindow::WndProc(msg, wParam, lParam);
}

bool ATSourceWindow::OnCreate() {
	if (!VDCreateTextEditor(~mpTextEditor))
		return false;

	mhwndTextEditor = (HWND)mpTextEditor->Create(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd, 100);
	SendMessage(mhwndTextEditor, WM_SETFONT, (WPARAM)g_monoFont, NULL);

	mpTextEditor->SetReadOnly(true);
	mpTextEditor->SetCallback(this);
	mpTextEditor->SetColorizer(this);

	OnSize();
	ATGetDebugger()->AddClient(this);
	g_sim.AddCallback(this);

	g_sourceWindows.push_back(this);
	return true;
}

void ATSourceWindow::OnDestroy() {
	SourceWindows::iterator it(std::find(g_sourceWindows.begin(), g_sourceWindows.end(), this));
	if (it != g_sourceWindows.end())
		g_sourceWindows.erase(it);

	mFileWatcher.Shutdown();

	g_sim.RemoveCallback(this);
	ATGetDebugger()->RemoveClient(this);
}

void ATSourceWindow::OnSize() {
	RECT r;
	VDVERIFY(GetClientRect(mhwnd, &r));

	if (mhwndTextEditor) {
		VDVERIFY(SetWindowPos(mhwndTextEditor, NULL, 0, 0, r.right, r.bottom, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
	}
}

bool ATSourceWindow::OnCommand(UINT cmd) {
	switch(cmd) {
		case ID_DEBUG_BREAK:
			ATGetDebugger()->Break();
			return true;
		case ID_DEBUG_RUN:
			ATGetDebugger()->Run(kATDebugSrcMode_Source);
			return true;
		case ID_DEBUG_TOGGLEBREAKPOINT:
		case ID_CONTEXT_TOGGLEBREAKPOINT:
			{
				int line = mpTextEditor->GetCursorLine();

				AddressLookup::const_iterator it(mLineToAddressLookup.find(line));
				if (it != mLineToAddressLookup.end())
					ATGetDebugger()->ToggleBreakpoint((uint16)it->second);
				else
					MessageBeep(MB_ICONEXCLAMATION);
			}
			return true;
		case ID_DEBUG_STEPINTO:
			ATGetDebugger()->StepInto(kATDebugSrcMode_Source);
			return true;
		case ID_DEBUG_STEPOVER:
			ATGetDebugger()->StepOver(kATDebugSrcMode_Source);
			return true;
		case ID_DEBUG_STEPOUT:
			ATGetDebugger()->StepOut(kATDebugSrcMode_Source);
			return true;

		case ID_CONTEXT_SHOWNEXTSTATEMENT:
			{
				uint32 moduleId;
				ATSourceLineInfo lineInfo;
				IATDebuggerSymbolLookup *lookup = ATGetDebuggerSymbolLookup();
				if (lookup->LookupLine(ATGetDebugger()->GetFramePC(), moduleId, lineInfo)) {
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
	if (it != mLineToAddressLookup.end()) {
		uint32 addr = it->second;

		if (g_sim.GetCPU().IsBreakpointSet(addr)) {
			cl->AddTextColorPoint(next, 0x000000, 0xFF8080);
			next += 4;
		}
	}

	if (line == mPCLine) {
		cl->AddTextColorPoint(next, 0x000000, 0xFFFF80);
		return;
	} else if (line == mFramePCLine) {
		cl->AddTextColorPoint(next, 0x000000, 0x80FF80);
		return;
	}

	if (next > 0)
		return;

	for(int i=0; i<length; ++i) {
		char c = text[i];

		if (c == ';') {
			cl->AddTextColorPoint(0, 0x008000, -1);
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
				cl->AddTextColorPoint(i, 0x0000FF, -1);
				cl->AddTextColorPoint(j, -1, -1);
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
	mpTextEditor->RecolorAll();
}

int ATSourceWindow::GetLineForAddress(uint32 addr) {
	LooseAddressLookup::const_iterator it(mAddressToLineLooseLookup.upper_bound(addr));
	if (it != mAddressToLineLooseLookup.begin()) {
		--it;

		if (addr - (uint32)it->first < 16)
			return it->second;
	}

	return -1;
}

bool ATSourceWindow::OnFileUpdated(const wchar_t *path) {
	if (mhwnd)
		PostMessage(mhwnd, WM_APP + 101, 0, 0);

	return true;
}

void ATSourceWindow::ActivateLine(int line) {
	mpTextEditor->SetCursorPos(line, 0);

	::SetActiveWindow(mhwnd);
	::SetFocus(mhwndTextEditor);
}

void ATSourceWindow::FocusOnLine(int line) {
	mpTextEditor->CenterViewOnLine(line);
	mpTextEditor->SetCursorPos(line, 0);

	::SetActiveWindow(mhwnd);
	::SetFocus(mhwndTextEditor);
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

IATSourceWindow *ATGetSourceWindow(const wchar_t *s) {
	SourceWindows::const_iterator it(g_sourceWindows.begin()), itEnd(g_sourceWindows.end());

	for(; it != itEnd; ++it) {
		IATSourceWindow *w = *it;

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

	VDStringW fn;
	if (!VDDoesPathExist(s)) {
		VDStringW title(L"Find source file ");
		title += s;
		fn = VDGetLoadFileName('src ', (VDGUIHandle)g_hwnd, title.c_str(), L"All files (*.*)\0*.*\0", NULL);

		if (fn.empty())
			return NULL;
	}

	vdrefptr<ATSourceWindow> srcwin(new ATSourceWindow);

	HWND hwndSrcWin = (HWND)srcwin->Create(WS_EX_NOPARENTNOTIFY, WS_OVERLAPPEDWINDOW|WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, (VDGUIHandle)g_hwnd, 0);

	if (hwndSrcWin) {
		try {
			if (fn.empty())
				srcwin->LoadFile(s, NULL);
			else
				srcwin->LoadFile(fn.c_str(), s);
		} catch(const MyError& e) {
			DestroyWindow(hwndSrcWin);
			e.post(g_hwnd, "Altirra error");
			return NULL;
		}

		return srcwin;
	}

	return NULL;
}

///////////////////////////////////////////////////////////////////////////

class ATHistoryWindow : public ATUIPane, public IATDebuggerClient {
public:
	ATHistoryWindow();
	~ATHistoryWindow();

protected:
	struct TreeNode {
		uint32	mRelYPos;		// Y position, relative to parent
		uint32	mHeight;
		bool	mbExpanded;
		bool	mbHeightDirty;
		TreeNode *mpParent;
		TreeNode *mpPrevSibling;
		TreeNode *mpNextSibling;
		TreeNode *mpFirstChild;
		TreeNode *mpLastChild;
		ATCPUHistoryEntry mHEnt;
		VDStringA	mText;
	};

	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnLButtonDown(int x, int y, int mods);
	bool OnKeyDown(int code);
	void OnMouseWheel(int lineDelta);
	void OnVScroll(int code);
	void OnPaint();
	void PaintItems(HDC hdc, const RECT *rPaint, uint32 itemStart, uint32 itemEnd, TreeNode *node, uint32 pos, uint32 level);
	void ScrollToPixel(int y);
	void InvalidateStartingAtNode(TreeNode *node);
	void SelectNode(TreeNode *node);
	void ExpandNode(TreeNode *node);
	void CollapseNode(TreeNode *node);
	void EnsureNodeVisible(TreeNode *node);
	void ValidateNode(TreeNode *node);
	uint32 GetNodeYPos(TreeNode *node) const;
	TreeNode *GetNodeFromClientPoint(int x, int y) const;
	TreeNode *GetPrevVisibleNode(TreeNode *node) const;
	TreeNode *GetNextVisibleNode(TreeNode *node) const;
	void UpdateScrollMax();
	void UpdateScrollBar();

	void Reset();
	void UpdateOpcodes();
	void ClearAllNodes();
	TreeNode *InsertNode(TreeNode *parent, TreeNode *after, const char *text, const ATCPUHistoryEntry *hent);
	TreeNode *AllocNode();
	void FreeNode(TreeNode *);
	void FreeNodes(TreeNode *);

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId) {}

	HWND mhwndHeader;
	RECT mContentRect;
	TreeNode mRootNode;
	TreeNode *mpSelectedNode;
	uint32 mWidth;
	uint32 mHeight;
	uint32 mHeaderHeight;
	uint32 mItemHeight;
	uint32 mItemTextVOffset;
	uint32 mPageItems;
	uint32 mScrollY;
	uint32 mScrollMax;
	sint32 mScrollWheelAccum;

	uint32 mLastCounter;
	uint8 mLastS;

	bool	mbHistoryError;
	bool	mbUpdatesBlocked;
	bool	mbDirtyScrollBar;

	VDStringA mTempLine;

	struct NodeBlock {
		TreeNode mNodes[256];
		NodeBlock *mpNext;
	};

	TreeNode *mpNodeFreeList;
	NodeBlock *mpNodeBlocks;

	TreeNode *mStackLevels[256];
};

ATHistoryWindow::ATHistoryWindow()
	: ATUIPane(kATUIPaneId_History, "History")
	, mhwndHeader(NULL)
	, mpSelectedNode(NULL)
	, mWidth(0)
	, mHeight(0)
	, mHeaderHeight(0)
	, mItemHeight(0)
	, mItemTextVOffset(0)
	, mPageItems(1)
	, mScrollY(0)
	, mScrollMax(0)
	, mScrollWheelAccum(0)
	, mbHistoryError(false)
	, mbUpdatesBlocked(false)
	, mbDirtyScrollBar(false)
	, mpNodeFreeList(NULL)
	, mpNodeBlocks(NULL)
{
	mPreferredDockCode = kATContainerDockRight;

	memset(&mContentRect, 0, sizeof mContentRect);

	memset(&mRootNode, 0, sizeof mRootNode);
	ClearAllNodes();
}

ATHistoryWindow::~ATHistoryWindow() {
}

LRESULT ATHistoryWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_PAINT) {
		OnPaint();
		return 0;
	} else if (msg == WM_ERASEBKGND) {
		return 0;
	} else if (msg == WM_VSCROLL) {
		OnVScroll(LOWORD(wParam));
		return 0;
	} else if (msg == WM_KEYDOWN) {
		if (OnKeyDown(wParam))
			return 0;
	} else if (msg == WM_LBUTTONDOWN) {
		::SetFocus(mhwnd);
		OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
		return 0;
	} else if (msg == WM_MOUSEWHEEL) {
		OnMouseWheel((short)HIWORD(wParam));
	}

	return ATUIPane::WndProc(msg, wParam, lParam);
}

bool ATHistoryWindow::OnCreate() {
	if (!ATUIPane::OnCreate())
		return false;

	mItemHeight = 16;
	mItemTextVOffset = 0;

	if (HDC hdc = GetDC(mhwnd)) {
		SelectObject(hdc, g_monoFont);

		TEXTMETRIC tm = {0};
		if (GetTextMetrics(hdc, &tm)) {
			mItemHeight = tm.tmHeight + 2*GetSystemMetrics(SM_CYEDGE);
			mItemTextVOffset = GetSystemMetrics(SM_CYEDGE);
		}

		ReleaseDC(mhwnd, hdc);
	}

	mhwndHeader = CreateWindowEx(WS_EX_CLIENTEDGE, WC_HEADER, "", WS_VISIBLE|WS_CHILD, 0, 0, 0, 0, mhwnd, (HMENU)100, g_hInst, NULL);
	if (!mhwndHeader)
		return false;

	if (!g_sim.GetCPU().IsHistoryEnabled())
		mbHistoryError = true;

	OnSize();
	Reset();
	UpdateOpcodes();
	ATGetDebugger()->AddClient(this, true);
	return true;
}

void ATHistoryWindow::OnDestroy() {
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

	ATGetDebugger()->RemoveClient(this);
	ATUIPane::OnDestroy();
}

void ATHistoryWindow::OnSize() {
	RECT r;

	if (!GetClientRect(mhwnd, &r))
		return;

	mWidth = r.right;
	mHeight = r.bottom;

	if (mhwndHeader) {
		HDLAYOUT hdl;
		WINDOWPOS wp;
		hdl.prc = &r;
		hdl.pwpos = &wp;
		if (Header_Layout(mhwndHeader, &hdl)) {
			mHeaderHeight = wp.cy;
			VDVERIFY(SetWindowPos(mhwndHeader, NULL, wp.x, wp.y, wp.cx, wp.cy, SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS));
		}
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

	ScrollToPixel(mScrollY);
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

bool ATHistoryWindow::OnKeyDown(int code) {
	switch(code) {
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

			static const char kText[]="History cannot be displayed because CPU history tracking is not enabled. History tracking can be enabled in CPU Options.";
			DrawText(hdc, kText, (sizeof kText) - 1, &rText, DT_TOP | DT_LEFT | DT_NOPREFIX | DT_WORDBREAK);
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
				ExtTextOut(hdc, r.left, r.top, ETO_OPAQUE, &r, "", 0, NULL);
			}
		}

		RestoreDC(hdc, sdc);
	}

	EndPaint(mhwnd, &ps);
}

void ATHistoryWindow::PaintItems(HDC hdc, const RECT *rPaint, uint32 itemStart, uint32 itemEnd, TreeNode *node, uint32 pos, uint32 level) {
	bool is65C816 = g_sim.GetCPU().GetCPUMode() == kATCPUMode_65C816;
	uint32 basePos = pos;

	while(node) {
		VDASSERT(basePos + node->mRelYPos == pos);
		if (pos >= itemEnd)
			return;

		if (pos >= itemStart) {
			bool selected = false;

			if (mpSelectedNode == node) {
				selected = true;
				SetBkColor(hdc, GetSysColor(COLOR_HIGHLIGHT));
				SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
			} else {
				SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
				SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
			}

			int x = mItemHeight * level;
			int y = pos * mItemHeight + mHeaderHeight - mScrollY;

			RECT rOpaque;
			rOpaque.left = x;
			rOpaque.top = y;
			rOpaque.right = mWidth;
			rOpaque.bottom = y + mItemHeight;

			const VDStringA *s = &node->mText;

			if (s->empty()) {
				const ATCPUHistoryEntry& hent = node->mHEnt;

				mTempLine.sprintf("%d:%3d:%3d | "
						, hent.mTimestamp >> 20
						, (hent.mTimestamp >> 8) & 0xfff
						, hent.mTimestamp & 0xff
					);

				if (is65C816 && !hent.mbEmulation) {
					if (hent.mP & AT6502::kFlagM) {
						mTempLine.append_sprintf("B=%02X A=%02X"
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
						mTempLine.append_sprintf(" X=%02X Y=%02X"
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

					mTempLine.append_sprintf(" P=%02X S=%02X%02X"
						, hent.mP
						, hent.mSH
						, hent.mS
						);
				} else {
					mTempLine.append_sprintf("A=%02X X=%02X Y=%02X P=%02X S=%02X"
						, hent.mA
						, hent.mX
						, hent.mY
						, hent.mP
						, hent.mS);
				}

				mTempLine += " | ";

				ATDisassembleInsn(mTempLine, hent, false);

				s = &mTempLine;
			}

			ExtTextOut(hdc, x + mItemHeight, rOpaque.top + mItemTextVOffset, ETO_OPAQUE | ETO_CLIPPED, &rOpaque, s->data(), s->size(), NULL);

			RECT rPad;
			rPad.left = rPaint->left;
			rPad.top = y;
			rPad.right = x;
			rPad.bottom = y + mItemHeight;

			FillRect(hdc, &rPad, (HBRUSH)(COLOR_WINDOW + 1));

			if (node->mpFirstChild) {
				SelectObject(hdc, selected ? GetStockObject(WHITE_PEN) : GetStockObject(BLACK_PEN));

				int boxsize = (mItemHeight - 5) & ~1;
				int x1 = x + 2;
				int y1 = y + 2;
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

		if (pos + node->mHeight > itemStart) {
			if (node->mbExpanded && node->mpFirstChild)
				PaintItems(hdc, rPaint, itemStart, itemEnd, node->mpFirstChild, pos + 1, level + 1);
		}

		pos += node->mHeight;
		node = node->mpNextSibling;
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

void ATHistoryWindow::InvalidateStartingAtNode(TreeNode *node) {
	int y = GetNodeYPos(node);

	if ((uint32)y < mScrollY + mHeight)
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
		if (offset < p->mHeight) {
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

void ATHistoryWindow::UpdateScrollMax() {
	mScrollMax = (mRootNode.mHeight - 1) <= mPageItems ? 0 : ((mRootNode.mHeight - 1) - mPageItems) * mItemHeight;
}

void ATHistoryWindow::UpdateScrollBar() {
	if (mbUpdatesBlocked) {
		mbDirtyScrollBar = true;
		return;
	}

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

	mbDirtyScrollBar = false;
}

void ATHistoryWindow::Reset() {
	memset(mStackLevels, 0, sizeof mStackLevels);

	ATCPUEmulator& cpu = g_sim.GetCPU();
	uint32 c = cpu.GetHistoryCounter();
	uint32 l = cpu.GetHistoryLength();
	mLastCounter = c > l ? c - l : 0;
	mLastS = 0xFF;
}

void ATHistoryWindow::UpdateOpcodes() {
	if (mbHistoryError)
		return;

	ATCPUEmulator& cpu = g_sim.GetCPU();
	bool is65C02 = cpu.GetCPUMode() == kATCPUMode_65C02;
	bool is65C816 = cpu.GetCPUMode() == kATCPUMode_65C816;
	uint32 c = cpu.GetHistoryCounter();
	uint32 dist = c - mLastCounter;
	uint32 l = cpu.GetHistoryLength();

	if (dist == 0)
		return;

	if (dist > l) {
		ClearAllNodes();
		Reset();
		dist = l;
		mLastCounter = c - l;
	}

	bool quickMode = false;
	if (dist > 1000) {
		quickMode = true;
	}

	mbUpdatesBlocked = true;

	TreeNode *last = NULL;
	while(dist--) {
		const ATCPUHistoryEntry& hent = cpu.GetHistory(c - ++mLastCounter);

		// If we're higher on the stack than before (pop/return), pop entries off the tree parent stack.
		while(hent.mS > mLastS)
			mStackLevels[mLastS++] = NULL;

		// Check if we have a parent to use.
		TreeNode *parent = mStackLevels[hent.mS];

		if (!parent) {
			uint8 s;

			for(s = hent.mS + 1; s; ++s) {
				parent = mStackLevels[s];

				if (parent)
					break;
			}

			if (!parent)
				parent = &mRootNode;

			if (!hent.mbNMI && !hent.mbIRQ && parent->mpLastChild)
				parent = parent->mpLastChild;
			else
				parent = InsertNode(parent, parent ? parent->mpLastChild : NULL, hent.mbNMI ? "NMI interrupt" : hent.mbIRQ ? "IRQ interrupt" : "Subroutine call", NULL);

			mStackLevels[hent.mS] = parent;
		}

		// Note that there is a serious problem here in that we are only tracking an 8-bit
		// stack, when the stack is 16-bit in 65C816 native mode.
		switch(hent.mOpcode[0]) {
			case 0x48:	// PHA
			case 0x08:	// PHP
				mStackLevels[(uint8)(hent.mS - 1)] = parent;
				break;

			case 0x6A:	// PHY
			case 0xDA:	// PHX
				if (is65C02 || is65C816)
					mStackLevels[(uint8)(hent.mS - 1)] = parent;
				break;

			case 0x8B:	// PHB
			case 0x4B:	// PHK
				if (is65C816)
					mStackLevels[(uint8)(hent.mS - 1)] = parent;
				break;

			case 0x0B:	// PHD
				if (is65C816)
					mStackLevels[(uint8)(hent.mS - 2)] = parent;
				break;
		}

		mLastS = hent.mS;
		
		last = InsertNode(parent, parent ? parent->mpLastChild : NULL, "", &hent);
	}

	if (last)
		SelectNode(last);

	if (quickMode)
		InvalidateRect(mhwnd, NULL, TRUE);

	mbUpdatesBlocked = false;

	if (mbDirtyScrollBar)
		UpdateScrollBar();
}

void ATHistoryWindow::ClearAllNodes() {
	mpSelectedNode = NULL;

	FreeNodes(&mRootNode);
	memset(&mRootNode, 0, sizeof mRootNode);
	mRootNode.mbExpanded = true;
	mRootNode.mHeight = 1;

	if (mhwnd)
		InvalidateRect(mhwnd, NULL, TRUE);
}

ATHistoryWindow::TreeNode *ATHistoryWindow::InsertNode(TreeNode *parent, TreeNode *insertAfter, const char *text, const ATCPUHistoryEntry *hent) {
	if (!parent)
		parent = &mRootNode;

	TreeNode *node = AllocNode();
	node->mText = text;
	node->mRelYPos = 0;
	node->mbExpanded = false;
	node->mpFirstChild = NULL;
	node->mpLastChild = NULL;
	node->mpParent = parent;
	node->mHeight = 1;
	if (hent)
		node->mHEnt = *hent;

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

		if (p == &mRootNode)
			mScrollMax += mItemHeight;
	}

	InvalidateStartingAtNode(node);
	return node;
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
	return p;
}

void ATHistoryWindow::FreeNode(TreeNode *node) {
	VDASSERT(node->mpPrevSibling != node);

	node->mpPrevSibling = node;
	node->mpNextSibling = mpNodeFreeList;
	mpNodeFreeList = node;
}

void ATHistoryWindow::FreeNodes(TreeNode *node) {
	if (!node)
		return;

	TreeNode *c = node->mpFirstChild;
	while(c) {
		TreeNode *n = c->mpNextSibling;

		FreeNodes(c);

		c = n;
	}

	node->mpFirstChild = NULL;
	node->mpLastChild = NULL;
	node->mpNextSibling = NULL;
	node->mpPrevSibling = NULL;

	if (node != &mRootNode)
		FreeNode(node);
}

void ATHistoryWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	UpdateOpcodes();
}

///////////////////////////////////////////////////////////////////////////

class ATConsoleWindow : public ATUIPane {
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
	LRESULT CommandEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void OnSize();

	HWND	mhwndLog;
	HWND	mhwndEdit;
	HMENU	mMenu;

	VDFunctionThunk	*mpCmdEditThunk;
	WNDPROC	mCmdEditProc;

	VDStringA	mLastCommand;
};

ATConsoleWindow *g_pConsoleWindow;

ATConsoleWindow::ATConsoleWindow()
	: ATUIPane(kATUIPaneId_Console, "Console")
	, mMenu(LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_DEBUGGER_MENU)))
	, mhwndLog(NULL)
	, mhwndEdit(NULL)
	, mpCmdEditThunk(NULL)
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
				SetWindowText(mhwndLog, "");
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
	}

	return ATUIPane::WndProc(msg, wParam, lParam);
}

bool ATConsoleWindow::OnCreate() {
	if (!ATUIPane::OnCreate())
		return false;

	mhwndLog = CreateWindowEx(WS_EX_CLIENTEDGE, "RICHEDIT", "", ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_VISIBLE|WS_CHILD, 0, 0, 0, 0, mhwnd, (HMENU)100, g_hInst, NULL);
	if (!mhwndLog)
		return false;

	SendMessage(mhwndLog, WM_SETFONT, (WPARAM)g_monoFont, NULL);

	mhwndEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "RICHEDIT", "", WS_VISIBLE|WS_CHILD, 0, 0, 0, 0, mhwnd, (HMENU)100, g_hInst, NULL);
	if (!mhwndEdit)
		return false;

	mpCmdEditThunk = VDCreateFunctionThunkFromMethod(this, &ATConsoleWindow::CommandEditWndProc, true);

	mCmdEditProc = (WNDPROC)GetWindowLongPtr(mhwndEdit, GWLP_WNDPROC);
	SetWindowLongPtr(mhwndEdit, GWLP_WNDPROC, (LONG_PTR)mpCmdEditThunk);

	SendMessage(mhwndEdit, WM_SETFONT, (WPARAM)g_monoFont, NULL);

	OnSize();

	g_pConsoleWindow = this;
	return true;
}

void ATConsoleWindow::OnDestroy() {
	g_pConsoleWindow = NULL;

	if (mhwndEdit) {
		DestroyWindow(mhwndEdit);
		mhwndEdit = NULL;
	}

	if (mpCmdEditThunk) {
		VDDestroyFunctionThunk(mpCmdEditThunk);
		mpCmdEditThunk = NULL;
	}

	ATUIPane::OnDestroy();
}

void ATConsoleWindow::OnSize() {
	RECT r;

	if (GetClientRect(mhwnd, &r)) {
		int h = 20;

		if (mhwndLog) {
			VDVERIFY(SetWindowPos(mhwndLog, NULL, 0, 0, r.right, r.bottom > h ? r.bottom - h : 0, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS));
		}

		if (mhwndEdit) {
			VDVERIFY(SetWindowPos(mhwndEdit, NULL, 0, r.bottom - h, r.right, h, SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS));
		}
	}
}

void ATConsoleWindow::Write(const char *s) {
	if (SendMessage(mhwndLog, EM_GETLINECOUNT, 0, 0) > 5000) {
		POINT pt;
		SendMessage(mhwndLog, EM_GETSCROLLPOS, 0, (LPARAM)&pt);
		int idx = SendMessage(mhwndLog, EM_LINEINDEX, 2000, 0);
		SendMessage(mhwndLog, EM_SETSEL, 0, idx);
		SendMessage(mhwndLog, EM_REPLACESEL, FALSE, (LPARAM)"");
		SendMessage(mhwndLog, EM_SETSCROLLPOS, 0, (LPARAM)&pt);
	}

	char buf[2048];
	while(*s) {
		const char *eol = strchr(s, '\n');
		if (eol) {
			size_t len = eol - s;
			if (len < 2046) {
				memcpy(buf, s, len);
				buf[len] = '\r';
				buf[len+1] = '\n';
				buf[len+2] = 0;
				SendMessage(mhwndLog, EM_SETSEL, -1, -1);
				SendMessage(mhwndLog, EM_REPLACESEL, FALSE, (LPARAM)buf);
				s = eol+1;
				continue;
			}
		}

		SendMessage(mhwndLog, EM_SETSEL, -1, -1);
		SendMessage(mhwndLog, EM_REPLACESEL, FALSE, (LPARAM)s);
		break;
	}
}

void ATConsoleWindow::ShowEnd() {
	SendMessage(mhwndLog, EM_SCROLLCARET, 0, 0);
}

LRESULT ATConsoleWindow::CommandEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
		if (wParam == VK_CANCEL) {
			ATGetDebugger()->Break();
			return 0;
		} else if (wParam == VK_F11) {
			if (GetKeyState(VK_SHIFT) < 0)
				ATGetDebugger()->StepOut(kATDebugSrcMode_Disasm);
			else
				ATGetDebugger()->StepInto(kATDebugSrcMode_Disasm);
			return 0;
		} else if (wParam == VK_F10) {
			ATGetDebugger()->StepOver(kATDebugSrcMode_Disasm);
			return 0;
		} else if (wParam == VK_F8) {
			if (ATGetDebugger()->IsRunning())
				ATGetDebugger()->Break();
			else
				ATGetDebugger()->Run(kATDebugSrcMode_Disasm);
			return 0;
		} else if (wParam == VK_ESCAPE) {
			ATActivateUIPane(kATUIPaneId_Display, true);
			return 0;
		} else if (wParam == VK_UP) {
			const int len = mLastCommand.size();
			SetWindowText(hwnd, mLastCommand.c_str());
			SendMessage(hwnd, EM_SETSEL, len, len);
			return 0;
		} else if (wParam == VK_DOWN) {
			SetWindowText(hwnd, "");
			return 0;
		}
	} else if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
		switch(wParam) {
		case VK_CANCEL:
		case VK_F11:
		case VK_F10:
		case VK_F8:
		case VK_ESCAPE:
		case VK_UP:
		case VK_DOWN:
			return true;
		}
	} else if (msg == WM_CHAR || msg == WM_SYSCHAR) {
		if (wParam == '\r') {
			int len = GetWindowTextLength(mhwndEdit);
			if (len) {
				char *buf = (char *)_alloca(len+1);
				buf[0] = 0;

				if (GetWindowText(mhwndEdit, buf, len+1)) {
					mLastCommand = buf;
					SetWindowText(mhwndEdit, "");

					try {
						ATConsoleExecuteCommand(buf);
					} catch(const MyError& e) {
						ATConsolePrintf("%s\n", e.gets());
					}
				}
			}
			return true;
		}
	}

	return CallWindowProc(mCmdEditProc, hwnd, msg, wParam, lParam);
}

///////////////////////////////////////////////////////////////////////////

class ATMemoryWindow : public ATUIPane,
							public IATDebuggerClient,
							public IVDUIMessageFilterW32
{
public:
	ATMemoryWindow();
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
	void OnPaint();
	void RemakeView(uint32 focusAddr);
	bool GetAddressFromPoint(int x, int y, uint32& addr) const;

	HWND	mhwndAddress;
	HMENU	mMenu;

	RECT	mTextArea;
	uint32	mViewStart;
	uint32	mCharWidth;
	uint32	mLineHeight;

	VDStringA	mTempLine;
	VDStringA	mTempLine2;

	vdfastvector<uint8> mViewData;
	vdfastvector<uint32> mChangedBits;
};

ATMemoryWindow::ATMemoryWindow()
	: ATUIPane(kATUIPaneId_Memory, "Memory")
	, mhwndAddress(NULL)
	, mMenu(LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_MEMORY_CONTEXT_MENU)))
	, mTextArea()
	, mViewStart(0)
	, mLineHeight(16)
{
	mPreferredDockCode = kATContainerDockRight;
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

						sint32 addr = ATGetDebugger()->ResolveSymbol(info->szText, true);

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
								if (g_sim.IsReadBreakEnabled() && g_sim.GetReadBreakAddress() == addr)
									g_sim.SetReadBreakAddress();
								else
									g_sim.SetReadBreakAddress(addr);
							}
						}
						break;

					case ID_CONTEXT_TOGGLEWRITEBREAKPOINT:
						{							
							POINT pt = {x, y};
							ScreenToClient(mhwnd, &pt);

							uint32 addr;
							if (GetAddressFromPoint(pt.x, pt.y, addr)) {
								if (g_sim.IsWriteBreakEnabled() && g_sim.GetWriteBreakAddress() == addr)
									g_sim.SetWriteBreakAddress();
								else
									g_sim.SetWriteBreakAddress(addr);
							}
						}
						break;
				}
			}
			break;
	}

	return ATUIPane::WndProc(msg, wParam, lParam);
}

bool ATMemoryWindow::OnMessage(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result) {
	switch(msg) {
		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
			if (wParam == VK_PAUSE || wParam == VK_CANCEL) {
				if (GetKeyState(VK_CONTROL) < 0) {
					ATGetDebugger()->Break();
				}
			} else if (wParam == VK_F8) {
				if (ATGetDebugger()->IsRunning())
					ATGetDebugger()->Break();
				else
					ATGetDebugger()->Run(kATDebugSrcMode_Same);
			} else if (wParam == VK_F10) {
				ATGetDebugger()->StepOver(kATDebugSrcMode_Same);
				return true;
			} else if (wParam == VK_F11) {
				if (GetKeyState(VK_SHIFT) < 0)
					ATGetDebugger()->StepOut(kATDebugSrcMode_Same);
				else
					ATGetDebugger()->StepInto(kATDebugSrcMode_Same);
				return true;
			}
			break;

		case WM_SYSKEYUP:
			switch(wParam) {
				case VK_PAUSE:
				case VK_CANCEL:
				case VK_F8:
				case VK_F10:
				case VK_F11:
					return true;
			}
			break;
	}

	return false;
}

bool ATMemoryWindow::OnCreate() {
	if (!ATUIPane::OnCreate())
		return false;

	mhwndAddress = CreateWindowEx(0, WC_COMBOBOXEX, "", WS_VISIBLE|WS_CHILD|WS_CLIPSIBLINGS|CBS_DROPDOWN|CBS_HASSTRINGS|CBS_AUTOHSCROLL, 0, 0, 0, 0, mhwnd, (HMENU)101, g_hInst, NULL);
	if (!mhwndAddress)
		return false;

	VDSetWindowTextFW32(mhwndAddress, L"%hs", ATGetDebugger()->GetAddressText(mViewStart, true).c_str());

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

	OnSize();
	ATGetDebugger()->AddClient(this, true);
	return true;
}

void ATMemoryWindow::OnDestroy() {
	ATGetDebugger()->RemoveClient(this);
	ATUIPane::OnDestroy();
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

	if (mViewStart != focusAddr) {
		mViewStart = focusAddr;
		changed = true;
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
			uint16 addr = mViewStart + offset;
			uint8 c = g_sim.DebugReadByte(addr);

			if (offset < limit && mViewData[offset] != c)
				mask |= (1 << j);

			mViewData[offset] = c;
		}

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
	void OnSetFocus();

	vdrefptr<IVDTextEditor> mpTextEditor;
	HWND	mhwndTextEditor;
};

ATPrinterOutputWindow::ATPrinterOutputWindow()
	: ATUIPane(kATUIPaneId_PrinterOutput, "Printer Output")
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
	}

	return ATUIPane::WndProc(msg, wParam, lParam);
}

bool ATPrinterOutputWindow::OnCreate() {
	if (!ATUIPane::OnCreate())
		return false;

	if (!VDCreateTextEditor(~mpTextEditor))
		return false;

	mhwndTextEditor = (HWND)mpTextEditor->Create(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd, 100);
	SendMessage(mhwndTextEditor, WM_SETFONT, (WPARAM)g_monoFont, NULL);

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

void ATInitUIPanes() {
	LoadLibrary("riched32");

	ATRegisterUIPaneType(kATUIPaneId_Registers, VDRefCountObjectFactory<ATRegistersWindow, ATUIPane>);
	ATRegisterUIPaneType(kATUIPaneId_Console, VDRefCountObjectFactory<ATConsoleWindow, ATUIPane>);
	ATRegisterUIPaneType(kATUIPaneId_Disassembly, VDRefCountObjectFactory<ATDisassemblyWindow, ATUIPane>);
	ATRegisterUIPaneType(kATUIPaneId_CallStack, VDRefCountObjectFactory<ATCallStackWindow, ATUIPane>);
	ATRegisterUIPaneType(kATUIPaneId_History, VDRefCountObjectFactory<ATHistoryWindow, ATUIPane>);
	ATRegisterUIPaneType(kATUIPaneId_Memory, VDRefCountObjectFactory<ATMemoryWindow, ATUIPane>);
	ATRegisterUIPaneType(kATUIPaneId_PrinterOutput, VDRefCountObjectFactory<ATPrinterOutputWindow, ATUIPane>);

	if (!g_monoFont) {
		g_monoFont = CreateFont(-10, 0, 0, 0, 0, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Lucida Console");

		if (!g_monoFont)
			g_monoFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

		g_monoFontLineHeight = 10;
		HDC hdc = GetDC(NULL);
		if (hdc) {
			HGDIOBJ hgoFont = SelectObject(hdc, g_monoFont);
			if (hgoFont) {
				TEXTMETRIC tm={0};

				if (GetTextMetrics(hdc, &tm))
					g_monoFontLineHeight = tm.tmHeight + tm.tmExternalLeading;
			}

			SelectObject(hdc, hgoFont);
			ReleaseDC(NULL, hdc);
		}
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
		const uint32 n = pane->GetChildCount();

		for(uint32 i=0; i<n; ++i) {
			ATContainerDockingPane *child = pane->GetChildPane(i);

			if (i)
				s += ',';

			s.append_sprintf("(%x,%d,%.4f"
				, GetIdFromFrame(child->GetContent())
				, child->GetDockCode()
				, child->GetDockFraction()
				);

			if (child->GetChildCount()) {
				s += ':';
				SerializeDockingPane(s, child);
			}

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

	VDRegistryAppKey key("Pane layouts");
	key.setString(name, s.c_str());
}

namespace {
	const char *UnserializeDockablePane(const char *s, ATContainerWindow *cont, ATContainerDockingPane *pane, ATFrameWindow **frames) {
		for(;;) {
			if (*s++ != '(')
				return NULL;

			int id;
			int code;
			float frac;
			int len = -1;
			int n = sscanf(s, "%x,%d,%f%n", &id, &code, &frac, &len);

			if (n < 3 || len <= 0)
				return NULL;

			s += len;

			ATFrameWindow *w = NULL;
			
			if (id && id < kATUIPaneId_Count) {
				w = frames[id];
				if (w) {
					frames[id] = NULL;
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
			}

			ATContainerDockingPane *child = cont->DockFrame(w, pane, code);
			child->SetDockFraction(frac);

			if (*s == ':')
				s = UnserializeDockablePane(s+1, cont, child, frames);

			if (*s++ != ')')
				return NULL;

			if (*s != ',')
				break;
			++s;
		}

		return s;
	}
}

bool ATRestorePaneLayout(const char *name) {
	if (!name)
		name = g_uiDebuggerMode ? "Debugger" : "Standard";

	VDRegistryAppKey key("Pane layouts");
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

	// undock and hide all docked panes
	vdfastvector<ATFrameWindow *> frames(kATUIPaneId_Count, NULL);

	for(int paneId = kATUIPaneId_Display; paneId < kATUIPaneId_Count; ++paneId) {
		ATUIPane *pane = ATGetUIPane(paneId);

		if (pane) {
			HWND hwndPane = pane->GetHandleW32();
			if (hwndPane) {
				HWND hwndParent = GetParent(hwndPane);
				if (hwndParent) {
					ATFrameWindow *w = ATFrameWindow::GetFrameWindow(hwndParent);

					if (w) {
						frames[paneId] = w;

						if (w->GetPane()) {
							ATContainerWindow *c = w->GetContainer();

							c->UndockFrame(w, false);
						}
					}
				}
			}
		}
	}

	g_pMainWindow->RemoveAnyEmptyNodes();

	const char *s = str.c_str();

	// parse dockable panes
	s = UnserializeDockablePane(s, g_pMainWindow, g_pMainWindow->GetBasePane(), frames.data());
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

			ATFrameWindow *w = frames[id];
			if (w) {
				frames[id] = NULL;
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
	for(int i=0; i<kATUIPaneId_Count; ++i) {
		ATFrameWindow *w = frames[i];

		if (w) {
			HWND hwndFrame = w->GetHandleW32();

			if (hwndFrame)
				DestroyWindow(hwndFrame);
		}
	}

	return true;
}

void ATLoadDefaultPaneLayout() {
	g_pMainWindow->Clear();

	if (g_uiDebuggerMode) {
		ATActivateUIPane(kATUIPaneId_Display, false);
		ATActivateUIPane(kATUIPaneId_Console, true);
		ATActivateUIPane(kATUIPaneId_Registers, false);
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
