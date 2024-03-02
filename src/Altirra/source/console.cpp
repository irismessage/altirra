//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2017 Avery Lee
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

#include <stdafx.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <malloc.h>
#include <stdio.h>
#include <map>
#include <array>
#include <vd2/Dita/services.h>
#include <vd2/system/binary.h>
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
#include <at/atui/constants.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/genericdialog.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/uiframe.h>
#include <at/atnativeui/uiproxies.h>
#include <at/atcore/address.h>
#include <at/atcore/deviceprinter.h>
#include <at/atdebugger/historytree.h>
#include <at/atdebugger/historytreebuilder.h>
#include <at/atdebugger/target.h>
#include <at/atio/cassetteimage.h>
#include "console.h"
#include "uiaccessors.h"
#include "uicommondialogs.h"
#include "uikeyboard.h"
#include "uihistoryview.h"
#include "texteditor.h"
#include "simulator.h"
#include "debugger.h"
#include "debuggerexp.h"
#include "debuggersettings.h"
#include "resource.h"
#include "disasm.h"
#include "symbols.h"
#include "debugdisplay.h"
#include "cassette.h"
#include "oshelper.h"

//#define VERIFY_HISTORY_TREE

extern HINSTANCE g_hInst;
extern HWND g_hwnd;

extern ATSimulator g_sim;
extern ATContainerWindow *g_pMainWindow;

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
		FIXED_PITCH | FF_DONTCARE,	// lfPitchAndFamily
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

	VDFileStream *g_pLogFile;
	VDTextOutputStream *g_pLogOutput;
}

///////////////////////////////////////////////////////////////////////////
bool ATConsoleShowSource(uint32 addr) {
	uint32 moduleId;
	ATSourceLineInfo lineInfo;
	IATDebuggerSymbolLookup *lookup = ATGetDebuggerSymbolLookup();
	if (!lookup->LookupLine(addr, false, moduleId, lineInfo))
		return false;

	VDStringW path;
	if (!lookup->GetSourceFilePath(moduleId, lineInfo.mFileId, path) && lineInfo.mLine)
		return false;

	IATSourceWindow *w = ATOpenSourceWindow(path.c_str());
	if (!w)
		return false;

	w->FocusOnLine(lineInfo.mLine - 1);
	return true;
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

	return ATUIPane::WndProc(msg, wParam, lParam);
}

///////////////////////////////////////////////////////////////////////////

class ATDisassemblyWindow final : public ATUIDebuggerPane,
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

	void OnTextEditorUpdated() override;
	void OnTextEditorScrolled(int firstVisiblePara, int lastVisiblePara, int visibleParaCount, int totalParaCount) override;
	void OnLinkSelected(uint32 selectionCode, int para, int offset) override;

	void RecolorLine(int line, const char *text, int length, IVDTextEditorColorization *colorization);

	void SetPosition(uint32 addr);

private:
	bool OnPaneCommand(ATUIPaneCommandId id) override;

private:
	enum : uint32 {
		kID_TextView = 100,
		kID_Address,
		kID_ButtonPrev,
		kID_ButtonNext,
	};

	enum : uint8 {
		kSelCode_JumpTarget = 1,
		kSelCode_Expand = 2
	};

	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnMessage(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result);

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnSetFocus();
	void OnFontsUpdated();
	bool OnCommand(UINT cmd);
	void PushAndJump(uint32 fromXAddr, uint32 toXAddr);
	void GoPrev();
	void GoNext();
	void RemakeView(uint16 focusAddr);

	struct DisasmResult {
		uint32 mEndingPC;
	};

	struct LineInfo {
		uint32 mXAddress;
		uint32 mTargetXAddr;
		uint8 mNestingLevel;
		uint8 mP;
		bool mbEmulation;
		bool mbIsComment;
		bool mbIsExpandable;
		uint8 mOperandSelCode;
		uint32 mOperandStart;
		uint32 mOperandEnd;
		uint32 mCommentPos;
	};

	DisasmResult Disassemble(VDStringA& buf,
		vdfastvector<LineInfo>& lines,
		uint32 nestingLevel,
		const ATCPUHistoryEntry& initialState,
		uint32 startAddr,
		uint32 focusAddr,
		uint32 maxBytes,
		uint32 maxLines,
		bool stopOnProcedureEnd);

	uint32 ComputeViewChecksum() const;

	vdrefptr<IVDTextEditor> mpTextEditor;
	HWND	mhwndTextEditor = nullptr;
	HWND	mhwndAddress = nullptr;
	HWND	mhwndButtonPrev = nullptr;
	HWND	mhwndButtonNext = nullptr;
	HMENU	mhmenu = nullptr;

	uint16	mViewStart = 0;
	uint32	mViewLength = 0;
	uint8	mViewBank = 0;
	uint32	mViewAddrBank = 0;
	uint32	mViewChecksum = 0;
	uint16	mFocusAddr = 0;
	int		mPCLine = -1;
	uint32	mPCAddr = 0;
	int		mFramePCLine = -1;
	uint32	mFramePCAddr = 0;
	ATCPUSubMode	mLastSubMode = kATCPUSubMode_6502;

	ATDebuggerSettingView<bool> mbShowCodeBytes;
	ATDebuggerSettingView<bool> mbShowLabels;
	ATDebuggerSettingView<bool> mbShowLabelNamespaces;
	ATDebuggerSettingView<bool> mbShowProcedureBreaks;
	ATDebuggerSettingView<bool> mbShowCallPreviews;
	
	ATDebuggerSystemState mLastState = {};

	vdfastvector<LineInfo> mLines;

	uint32	mHistory[32];
	uint8	mHistoryNext = 0;
	uint8	mHistoryLenForward = 0;
	uint8	mHistoryLenBack = 0;
};

ATDisassemblyWindow::ATDisassemblyWindow()
	: ATUIDebuggerPane(kATUIPaneId_Disassembly, L"Disassembly")
{
	mhmenu = LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_DISASM_CONTEXT_MENU));
	mPreferredDockCode = kATContainerDockRight;

	const auto refresh = [this] { RemakeView(mFocusAddr); };
	mbShowCodeBytes.Attach(g_ATDbgSettingShowCodeBytes, refresh);
	mbShowLabels.Attach(g_ATDbgSettingShowLabels, refresh);
	mbShowLabelNamespaces.Attach(g_ATDbgSettingShowLabelNamespaces, refresh);
	mbShowProcedureBreaks.Attach(g_ATDbgSettingShowProcedureBreaks, refresh);
	mbShowCallPreviews.Attach(g_ATDbgSettingShowCallPreviews, refresh);
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

				if (line < mLines.size())
					ATGetDebugger()->ToggleBreakpoint(mLines[line].mXAddress);

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

						sint32 addr = ATGetDebugger()->ResolveSymbol(VDTextWToA(info->szText).c_str(), true);

						if (addr == -1)
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
				VDCheckMenuItemByCommandW32(menu, ID_CONTEXT_SHOWLABELNAMESPACES, mbShowLabelNamespaces);
				VDCheckMenuItemByCommandW32(menu, ID_CONTEXT_SHOWPROCEDUREBREAKS, mbShowProcedureBreaks);
				VDCheckMenuItemByCommandW32(menu, ID_CONTEXT_SHOWCALLPREVIEWS, mbShowCallPreviews);

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

	mhwndAddress = CreateWindowEx(0, WC_COMBOBOXEX, _T(""), WS_VISIBLE|WS_CHILD|WS_TABSTOP|WS_CLIPSIBLINGS|CBS_DROPDOWN|CBS_HASSTRINGS|CBS_AUTOHSCROLL, 0, 0, 0, 0, mhwnd, (HMENU)kID_Address, g_hInst, NULL);

	mhwndButtonPrev = CreateWindowEx(0, WC_BUTTON, _T("<"), WS_VISIBLE|WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON, 0, 0, 0, 0, mhwnd, (HMENU)kID_ButtonPrev, g_hInst, nullptr);
	mhwndButtonNext = CreateWindowEx(0, WC_BUTTON, _T(">"), WS_VISIBLE|WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON, 0, 0, 0, 0, mhwnd, (HMENU)kID_ButtonNext, g_hInst, nullptr);

	mhwndTextEditor = (HWND)mpTextEditor->Create(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_TABSTOP|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd, kID_TextView);

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
		int comboX = 0;

		if (mhwndAddress) {
			GetWindowRect(mhwndAddress, &rAddr);

			comboHt = rAddr.bottom - rAddr.top;
			VDVERIFY(SetWindowPos(mhwndAddress, NULL, comboHt * 2, 0, r.right - comboHt * 2, comboHt, SWP_NOZORDER|SWP_NOACTIVATE));
		}

		if (mhwndButtonNext) {
			VDVERIFY(SetWindowPos(mhwndButtonNext, NULL, comboHt, 0, comboHt, comboHt, SWP_NOZORDER|SWP_NOACTIVATE));
		}

		if (mhwndButtonPrev) {
			VDVERIFY(SetWindowPos(mhwndButtonPrev, NULL, 0, 0, comboHt, comboHt, SWP_NOZORDER|SWP_NOACTIVATE));
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
	const auto UnsafeTruncateAddress = [](uint32 addr) { return (uint16)addr; };

	switch(cmd) {
		case ID_CONTEXT_GOTOSOURCE:
			{
				int line = mpTextEditor->GetCursorLine();

				if ((uint32)line < mLines.size()) {
					uint32 xaddr = mLines[line].mXAddress;

					if (!ATConsoleShowSource(xaddr)) {
						VDStringW s;
						s.sprintf(L"There is no source line associated with the address: %hs.", ATGetDebugger()->GetAddressText(xaddr, true).c_str());
						ATUIShowError((VDGUIHandle)mhwnd, s.c_str());
					}
				}
			}
			return true;
		case ID_CONTEXT_SHOWNEXTSTATEMENT:
			SetPosition(ATGetDebugger()->GetExtPC());
			return true;
		case ID_CONTEXT_SETNEXTSTATEMENT:
			{
				int line = mpTextEditor->GetCursorLine();

				if ((uint32)line < mLines.size()) {
					ATGetDebugger()->SetPC(UnsafeTruncateAddress(mLines[line].mXAddress));
				} else
					MessageBeep(MB_ICONEXCLAMATION);
			}
			return true;
		case ID_CONTEXT_TOGGLEBREAKPOINT:
			{
				int line = mpTextEditor->GetCursorLine();

				if ((uint32)line < mLines.size())
					ATGetDebugger()->ToggleBreakpoint(mLines[line].mXAddress);
				else
					MessageBeep(MB_ICONEXCLAMATION);
			}
			return true;
		case ID_CONTEXT_SHOWCODEBYTES:
			mbShowCodeBytes = !mbShowCodeBytes;
			return true;
		case ID_CONTEXT_SHOWLABELS:
			mbShowLabels = !mbShowLabels;
			return true;
		case ID_CONTEXT_SHOWLABELNAMESPACES:
			mbShowLabelNamespaces = !mbShowLabelNamespaces;
			return true;
		case ID_CONTEXT_SHOWPROCEDUREBREAKS:
			mbShowProcedureBreaks = !mbShowProcedureBreaks;
			return true;
		case ID_CONTEXT_SHOWCALLPREVIEWS:
			mbShowCallPreviews = !mbShowCallPreviews;
			return true;

		case kID_ButtonPrev:
			GoPrev();
			return true;

		case kID_ButtonNext:
			GoNext();
			return true;
	}

	return false;
}

void ATDisassemblyWindow::OnFontsUpdated() {
	if (mhwndButtonPrev)
		SendMessage(mhwndButtonPrev, WM_SETFONT, (WPARAM)g_monoFont, TRUE);

	if (mhwndButtonNext)
		SendMessage(mhwndButtonNext, WM_SETFONT, (WPARAM)g_monoFont, TRUE);

	if (mhwndTextEditor)
		SendMessage(mhwndTextEditor, WM_SETFONT, (WPARAM)g_monoFont, TRUE);
}

void ATDisassemblyWindow::PushAndJump(uint32 fromXAddr, uint32 toXAddr) {
	mHistory[mHistoryNext] = fromXAddr;
	if (++mHistoryNext >= vdcountof(mHistory))
		mHistoryNext = 0;

	if (mHistoryLenBack < vdcountof(mHistory))
		++mHistoryLenBack;

	mHistoryLenForward = 0;

	SetPosition(toXAddr);
}

void ATDisassemblyWindow::GoPrev() {
	if (!mHistoryLenBack)
		return;

	++mHistoryLenForward;
	--mHistoryLenBack;

	if (mHistoryNext == 0)
		mHistoryNext = (uint32)vdcountof(mHistory);

	--mHistoryNext;

	const uint32 nextXAddr = mHistory[mHistoryNext];

	mHistory[mHistoryNext] = mFocusAddr;

	SetPosition(nextXAddr);
}

void ATDisassemblyWindow::GoNext() {
	if (!mHistoryLenForward)
		return;

	++mHistoryLenBack;
	--mHistoryLenForward;

	const uint32 nextXAddr = mHistory[mHistoryNext];
	if (++mHistoryNext >= (uint32)vdcountof(mHistory))
		mHistoryNext = 0;

	mHistory[mHistoryNext] = mFocusAddr;

	SetPosition(nextXAddr);
}

void ATDisassemblyWindow::RemakeView(uint16 focusAddr) {
	mFocusAddr = focusAddr;

	uint16 pc = mViewStart;

	mpTextEditor->Clear();
	mLines.clear();

	mPCLine = -1;
	mFramePCLine = -1;

	int focusLine = -1;

	if (mLastState.mpDebugTarget) {
		IATDebugTarget *target = mLastState.mpDebugTarget;

		// checksum a small area around the focus address
		mViewChecksum = ComputeViewChecksum();

		ATCPUHistoryEntry hent0;
		ATDisassembleCaptureRegisterContext(target, hent0);

		uint32 viewLength = mpTextEditor->GetVisibleLineCount() * 24;

		if (viewLength < 0x100)
			viewLength = 0x100;

		VDStringA buf;
		const auto& result = Disassemble(buf,
			mLines,
			0,
			hent0,
			mViewStart + mViewAddrBank,
			focusAddr + mViewAddrBank,
			viewLength,
			viewLength,
			false);

		pc = (uint16)result.mEndingPC;

		size_t n = mLines.size();
		for(uint32 i = 0; i < n; ++i) {
			const LineInfo& li = mLines[i];

			if (li.mbIsComment)
				continue;

			if (li.mXAddress == mPCAddr)
				mPCLine = i;
			else if (li.mXAddress == mFramePCAddr)
				mFramePCLine = i;

			// Set focus line to earliest line that reaches the focus address.
			// Note that we must be wrap-safe here, thus the modulo comparison.
			if ((uint16)(li.mXAddress - focusAddr - 1) >= 0x8000)
				focusLine = (int)i;
		}

		mpTextEditor->SetUpdateEnabled(false);
		mpTextEditor->Append(buf.c_str());
		mpTextEditor->SetUpdateEnabled(true);
	}

	mLastSubMode = g_sim.GetCPU().GetCPUSubMode();

	if (focusLine >= 0) {
		mpTextEditor->CenterViewOnLine(focusLine);
		mpTextEditor->SetCursorPos(focusLine, 0);
	}

	mViewLength = (uint16)(pc - mViewStart);
	mpTextEditor->RecolorAll();
}

ATDisassemblyWindow::DisasmResult ATDisassemblyWindow::Disassemble(
	VDStringA& buf,
	vdfastvector<LineInfo>& lines,
	uint32 nestingLevel,
	const ATCPUHistoryEntry& initialState,
	uint32 startAddr,
	uint32 focusAddr,
	uint32 maxBytes,
	uint32 maxLines,
	bool stopOnProcedureEnd)
{
	ATCPUHistoryEntry hent(initialState);

	IATDebugTarget *target = mLastState.mpDebugTarget;
	const ATDebugDisasmMode disasmMode = target->GetDisasmMode();
	bool autoModeSwitching = false;

	if (disasmMode == kATDebugDisasmMode_65C816)
		autoModeSwitching = true;

	const uint32 bankBase = startAddr & UINT32_C(0xFFFF0000);
	const uint8 bank = (uint8)(bankBase >> 16);
	const uint16 pc0 = (uint16)startAddr;
	uint16 pc = pc0;
	bool procedureOpen = stopOnProcedureEnd;
	const uint32 indent = nestingLevel * 2;

	enum : uint8 {
		kBM_ExpandNone = 0x00,
		kBM_ExpandAbs16 = 0x01,
		kBM_ExpandAbs16BE = 0x02,
		kBM_ExpandAbs24 = 0x03,
		kBM_ExpandRel8 = 0x04,
		kBM_ExpandRel16BE = 0x05,
		kBM_ExpandMask = 0x07,

		kBM_EndBlock = 0x08,
		kBM_Special = 0x10
	};

	struct BreakMap {
		uint8 fl[256];
	};

	static constexpr std::array<uint8, 256> kBreakMap6502 = [] {
		std::array<uint8, 256> breakMap {};

		// JSR abs
		breakMap[0x20] |= kBM_ExpandAbs16;

		// JMP abs
		breakMap[0x4C] |= kBM_ExpandAbs16 | kBM_EndBlock;

		// JMP (abs)
		breakMap[0x6C] |= kBM_EndBlock;

		// RTI
		breakMap[0x40] |= kBM_EndBlock;

		// RTS
		breakMap[0x60] |= kBM_EndBlock;

		return breakMap;
	}();

	static constexpr std::array<uint8, 256> kBreakMap65C02 = [] {
		std::array<uint8, 256> breakMap = kBreakMap6502;

		// BRA rel8
		breakMap[0x80] |= kBM_EndBlock;

		// JMP (abs,X)
		breakMap[0x7C] |= kBM_EndBlock;

		return breakMap;
	}();

	static constexpr std::array<uint8, 256> kBreakMap65C816 = [] {
		std::array<uint8, 256> breakMap = kBreakMap65C02;

		// JSL al
		breakMap[0x22] |= kBM_ExpandAbs24;

		// JML al
		breakMap[0x5C] |= kBM_EndBlock | kBM_ExpandAbs24;

		// JSR (abs,X)
		breakMap[0xFC] |= kBM_EndBlock;

		return breakMap;
	}();

	static constexpr std::array<uint8, 256> kBreakMap6809 = [] {
		std::array<uint8, 256> breakMap {};

		breakMap[0x16] |= kBM_EndBlock;		// LBRA
		breakMap[0x17] |= kBM_ExpandRel16BE;		// LBSR rel16
		breakMap[0x20] |= kBM_EndBlock;		// BRA
		breakMap[0x39] |= kBM_EndBlock;		// RTS
		breakMap[0x6E] |= kBM_EndBlock;		// JMP indexed
		breakMap[0x7E] |= kBM_EndBlock | kBM_ExpandAbs16BE;		// JMP extended
		breakMap[0x8D] |= kBM_ExpandRel8;			// BSR rel8
		breakMap[0xBD] |= kBM_ExpandAbs16BE;		// JSR extended

		breakMap[0x35] |= kBM_Special;		// PULS PC
		breakMap[0x37] |= kBM_Special;		// PULU PC

		return breakMap;
	}();

	const uint8 *VDRESTRICT breakMap = nullptr;
	uint8 (*breakMapSpecialHandler)(const uint8 *insn) = nullptr;

	switch(disasmMode) {
		case kATDebugDisasmMode_6502:
			breakMap = kBreakMap6502.data();
			break;

		case kATDebugDisasmMode_65C02:
			breakMap = kBreakMap65C02.data();
			break;

		case kATDebugDisasmMode_65C816:
			breakMap = kBreakMap65C816.data();
			break;

		case kATDebugDisasmMode_6809:
			breakMap = kBreakMap6809.data();
			breakMapSpecialHandler = [](const uint8 *insn) -> uint8 {
				switch(insn[0]) {
					case 0x35:	// PULS
					case 0x37:	// PULU
						return insn[1] & 0x80 ? kBM_EndBlock : 0;
				}

				return 0;
			};
			break;
	}

	while((uint16)(pc - pc0) < maxBytes) {
		if (!maxLines--)
			break;

		if (autoModeSwitching && pc == (uint16)mPCAddr)
			hent = initialState;

		LineInfo li {};
		li.mXAddress = bankBase + pc;
		li.mNestingLevel = nestingLevel;
		li.mP = hent.mP;
		li.mbEmulation = hent.mbEmulation;
		li.mbIsComment = false;

		if (disasmMode == kATDebugDisasmMode_6502)
			ATDisassembleCaptureInsnContext(target, bankBase + pc, hent);
		else
			ATDisassembleCaptureInsnContext(target, pc, bank, hent);

		buf.append(indent, ' ');
		size_t lineStart = buf.size();
		const ATDisasmResult& result = ATDisassembleInsn(buf, target, disasmMode, hent, false, false, true, mbShowCodeBytes, mbShowLabels, false, false, mbShowLabelNamespaces, true, true);

		uint16 newpc = result.mNextPC;

		bool procBreak = false;
		bool procSep = false;

		if (breakMap) {
			const uint8 opcode = hent.mOpcode[0];
			uint8 breakInfo = breakMap[opcode];

			if (breakInfo & kBM_Special)
				breakInfo = breakMapSpecialHandler(hent.mOpcode);

			if (breakInfo & kBM_ExpandMask) {
				switch(breakInfo & kBM_ExpandMask) {
					case kBM_ExpandAbs16:
						li.mTargetXAddr = bankBase + VDReadUnalignedLEU16(&hent.mOpcode[1]);
						break;

					case kBM_ExpandAbs16BE:
						li.mTargetXAddr = bankBase + VDReadUnalignedBEU16(&hent.mOpcode[1]);
						break;

					case kBM_ExpandAbs24:
						li.mTargetXAddr = hent.mOpcode[1] + ((uint32)hent.mOpcode[2] << 8) + ((uint32)hent.mOpcode[3] << 16);
						break;

					case kBM_ExpandRel8:
						li.mTargetXAddr = bankBase + ((newpc + (sint8)hent.mOpcode[1]) & 0xFFFF);
						break;

					case kBM_ExpandRel16BE:
						li.mTargetXAddr = bankBase + ((newpc + VDReadUnalignedBES16(&hent.mOpcode[1])) & 0xFFFF);
						break;
				}

				li.mOperandSelCode = kSelCode_JumpTarget;
				li.mOperandStart = (uint32)(result.mOperandStart - lineStart) + indent;
				li.mOperandEnd = (uint32)(result.mOperandEnd - lineStart) + indent;

				if (mbShowCallPreviews) {
					auto len = buf.size() - lineStart;

					if (len < 50)
						buf.append(50 - len, ' ');

					li.mCommentPos = (uint32)(buf.size() - lineStart) + indent + 1;
					li.mbIsExpandable = true;
					buf += " ;[expand]";
				}
			}

			bool isProcEnd = false;
			if (breakInfo & kBM_EndBlock) {
				if (stopOnProcedureEnd)
					procBreak = true;
				else
					procSep = mbShowProcedureBreaks;
			}
		}

		lines.push_back(li);

		buf += '\n';

		if (procSep) {
			LineInfo sepLI {};
			sepLI.mbIsComment = true;
			lines.push_back(sepLI);

			buf.append(indent, ' ');
			buf += ';';
			buf.append(50, '-');
			buf += '\n';
		}

		// auto-switch mode if necessary
		if (autoModeSwitching) {
			ATDisassemblePredictContext(hent, disasmMode);
		}

		// don't allow disassembly to skip the desired PC
		if (newpc > (uint16)focusAddr && pc < (uint16)focusAddr) {
			newpc = (uint16)focusAddr;

			buf.append(indent, ' ');
			buf.append_sprintf("; reverse disassembly mismatch -- address forced from $%04X to $%04X\n", pc, newpc);

			li.mbIsComment = true;
			li.mbIsExpandable = false;
			lines.push_back(li);
		}

		pc = newpc;

		if (procBreak) {
			procedureOpen = false;
			break;
		}
	}

	if (procedureOpen) {
		LineInfo li {};
		li.mNestingLevel = nestingLevel;
		li.mbIsComment = true;
		lines.push_back(li);

		buf.append(indent, ' ');
		buf.append("    ...\n");
	}

	return DisasmResult { (startAddr & ~UINT32_C(0xFFFF)) + pc };
}

uint32 ATDisassemblyWindow::ComputeViewChecksum() const {
	if (!mLastState.mpDebugTarget)
		return 0;

	uint32 buf[4] {};
	mLastState.mpDebugTarget->DebugReadMemory(mViewAddrBank + ((mViewStart + mViewLength / 2) & 0xFFFF), buf, 16);

	return buf[0]
		+ (uint32)(buf[1] << 2) + (uint32)(buf[1] << (32 - 2))
		+ (uint32)(buf[2] << 4) + (uint32)(buf[2] << (32 - 4))
		+ (uint32)(buf[3] << 6) + (uint32)(buf[3] << (32 - 6));
}

void ATDisassemblyWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	if (state.mbRunning) {
		if (mLastState.mpDebugTarget != state.mpDebugTarget)
			mLastState.mpDebugTarget = nullptr;

		mPCLine = -1;
		mFramePCLine = -1;
		mpTextEditor->RecolorAll();
		return;
	}

	bool memoryViewChanged = false;
	bool changed = false;

	if (mLastState.mpDebugTarget != state.mpDebugTarget) {
		changed = true;
		memoryViewChanged = true;
	} else if (mLastState.mPC != state.mPC || mLastState.mFramePC != state.mFramePC || mLastState.mPCBank != state.mPCBank)
		changed = true;

	if (changed && !memoryViewChanged) {
		if ((uint16)(state.mFramePC - mViewStart) >= mViewLength || state.mPCBank != mViewBank || g_sim.GetCPU().GetCPUSubMode() != mLastSubMode)
			memoryViewChanged = true;
	}

	// if we haven't detected a full change and cycles have gone by, check whether the memory we last scanned is still the
	// same
	if (!memoryViewChanged && mLastState.mCycle != state.mCycle) {
		if (mViewChecksum != ComputeViewChecksum()) {
			changed = true;
			memoryViewChanged = true;
		}
	}

	mLastState = state;

	mPCAddr = (uint32)state.mInsnPC + ((uint32)state.mPCBank << 16);
	mPCLine = -1;
	mFramePCAddr = (uint32)state.mFramePC + ((uint32)state.mPCBank << 16);
	mFramePCLine = -1;

	if (changed && memoryViewChanged) {
		SetPosition(mFramePCAddr);
	} else {
		const int n = (int)mLines.size();

		for(int line = 0; line < n; ++line) {
			const LineInfo& li = mLines[line];

			if (li.mNestingLevel == 0) {
				uint32 addr = li.mXAddress;

				if (addr == mPCAddr)
					mPCLine = line;
				else if (addr == mFramePCAddr)
					mFramePCLine = line;
			}
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

	if (!mLines.empty()) {
		bool prev = (firstVisiblePara < visibleParaCount);
		bool next = (lastVisiblePara >= totalParaCount - visibleParaCount);

		if (prev ^ next) {
			int cenIdx = mpTextEditor->GetParagraphForYPos(mpTextEditor->GetVisibleHeight() >> 1);
			
			cenIdx = std::clamp(cenIdx, 0, (int)mLines.size() - 1);

			SetPosition(mLines[cenIdx].mXAddress);
		}
	}
}

void ATDisassemblyWindow::OnLinkSelected(uint32 selectionCode, int para, int offset) {
	if (para < 0)
		return;

	const uint32 line = (uint32)para;
	const uint32 n = mLines.size();
	if (line >= n)
		return;

	const LineInfo& li = mLines[line];

	if (selectionCode == kSelCode_JumpTarget) {
		PushAndJump(li.mXAddress, li.mTargetXAddr);
	} else if (selectionCode == kSelCode_Expand) {
		VDASSERT(li.mbIsExpandable);

		bool expanded = false;

		if (line + 1 < n) {
			if (mLines[line + 1].mNestingLevel > li.mNestingLevel)
				expanded = true;
		}

		if (expanded) {
			// [contract] -> [expand]
			mpTextEditor->RemoveAt(line, li.mCommentPos + 2, line, li.mCommentPos + 10);
			mpTextEditor->InsertAt(line, li.mCommentPos + 2, "expand");

			uint32 rangeStart = line + 1;
			uint32 rangeEnd = rangeStart;

			while(rangeEnd < n && mLines[rangeEnd].mNestingLevel > li.mNestingLevel)
				++rangeEnd;

			const int rangeLen = (int)(rangeEnd - rangeStart);

			if (mPCLine >= (int)rangeEnd)
				mPCLine -= rangeLen;

			if (mFramePCLine >= (int)rangeEnd)
				mFramePCLine -= rangeLen;

			mLines.erase(mLines.begin() + rangeStart, mLines.begin() + rangeEnd);
			mpTextEditor->RemoveAt(rangeStart, 0, rangeEnd, 0);
		} else {
			// [expand] -> [contract]
			mpTextEditor->RemoveAt(line, li.mCommentPos + 2, line, li.mCommentPos + 8);
			mpTextEditor->InsertAt(line, li.mCommentPos + 2, "contract");

			IATDebugTarget *target = mLastState.mpDebugTarget;
			ATCPUHistoryEntry initialState;
			ATDisassembleCaptureRegisterContext(target, initialState);

			initialState.mP = li.mP;
			initialState.mbEmulation = li.mbEmulation;

			vdfastvector<LineInfo> newLines;
			VDStringA buf;

			Disassemble(buf,
				newLines,
				li.mNestingLevel + 1,
				initialState,
				li.mTargetXAddr,
				li.mTargetXAddr,
				200,
				20,
				true);

			const uint32 rangeLen = (uint32)newLines.size();

			if (mPCLine > (int)line)
				mPCLine += rangeLen;

			if (mFramePCLine > (int)line)
				mFramePCLine += rangeLen;

			// mLines[] must be updated first as the colorizer will get invoked as
			// soon as we insert the text.
			mLines.insert(mLines.begin() + line + 1, newLines.begin(), newLines.end());
			mpTextEditor->InsertAt(line + 1, 0, buf.c_str());
		}
	}
}

void ATDisassemblyWindow::RecolorLine(int line, const char *text, int length, IVDTextEditorColorization *cl) {
	int next = 0;
	sint32 bg = -1;
	sint32 fg = -1;
	const LineInfo *li = nullptr;
	const auto& tc = ATUIGetThemeColors();

	if ((size_t)line < mLines.size()) {
		li = &mLines[line];

		if (li->mNestingLevel)
			bg = tc.mStaticBg;

		if (li->mbIsComment) {
			cl->AddTextColorPoint(0, tc.mCommentText, bg);
			return;
		}

		uint32 addr = li->mXAddress;

		if (auto *dbg = ATGetDebugger(); dbg->IsBreakpointAtPC(addr)) {
			cl->AddTextColorPoint(next, 0x000000, 0xFF8080);
			next += 4;
		}
	}

	bool suppressLinkHighlights = false;
	if (line == mPCLine) {
		bg = tc.mDirectLocationBg;
		fg = tc.mDirectLocationFg;
		suppressLinkHighlights = true;
	} else if (line == mFramePCLine) {
		bg = tc.mIndirectLocationBg;
		fg = tc.mIndirectLocationFg;
		suppressLinkHighlights = true;
	}

	if (bg >= 0)
		cl->AddTextColorPoint(next, fg, bg);

	if (li) {
		const sint32 linkColor = suppressLinkHighlights ? fg : ATUIGetThemeColors().mHyperlinkText;

		if (li->mOperandSelCode) {
			cl->AddTextColorPoint(li->mOperandStart, linkColor, bg, li->mOperandSelCode);
			cl->AddTextColorPoint(li->mOperandEnd, fg, bg);
		}

		if (li->mbIsExpandable)
			cl->AddTextColorPoint(li->mCommentPos, linkColor, bg, kSelCode_Expand);
	}
}

void ATDisassemblyWindow::SetPosition(uint32 addr) {
	IATDebugger *dbg = ATGetDebugger();
	VDStringA text(dbg->GetAddressText(addr, true));
	VDSetWindowTextFW32(mhwndAddress, L"%hs", text.c_str());

	uint32 addr16 = addr & 0xffff;
	uint32 offset = mpTextEditor->GetVisibleLineCount() * 12;

	if (offset < 0x80)
		offset = 0x80;

	mViewAddrBank = addr & 0xFFFF0000;
	mViewBank = (uint8)(addr >> 16);
	mViewStart = ATDisassembleGetFirstAnchor(dbg->GetTarget(), addr16 >= offset ? addr16 - offset : 0, addr16, mViewAddrBank);

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

	bool OnCreate() override;
	void OnDestroy() override;
	void OnSize() override;
	void OnFontsUpdated() override;
	void OnThemeUpdated() override;

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

	return ATUIDebuggerPane::WndProc(msg, wParam, lParam);
}

bool ATRegistersWindow::OnCreate() {
	if (!ATUIDebuggerPane::OnCreate())
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
	if (mhwndEdit)
		SendMessage(mhwndEdit, WM_SETFONT, (WPARAM)g_monoFont, TRUE);
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
				int idx = (int)SendMessage(mhwndList, LB_GETCURSEL, 0, 0);

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
			, fr.mSP
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

	void OnTextEditorUpdated() override;
	void OnTextEditorScrolled(int firstVisiblePara, int lastVisiblePara, int visibleParaCount, int totalParaCount) override;
	void OnLinkSelected(uint32 selectionCode, int para, int offset) override;

	void RecolorLine(int line, const char *text, int length, IVDTextEditorColorization *colorization) override;

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

	uint32	mEventCallbackId = 0;

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
		case WM_USER + 101:
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

	mEventCallbackId = g_sim.GetEventManager()->AddEventCallback(kATSimEvent_CPUPCBreakpointsUpdated,
		[this] { mpTextEditor->RecolorAll(); });

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

	if (mEventCallbackId) {
		g_sim.GetEventManager()->RemoveEventCallback(mEventCallbackId);
		mEventCallbackId = 0;
	}

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

				if (addr != -1) {
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

				if (addr != -1) {
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

void ATSourceWindow::OnLinkSelected(uint32 selectionCode, int para, int offset) {
}

void ATSourceWindow::RecolorLine(int line, const char *text, int length, IVDTextEditorColorization *cl) {
	int next = 0;

	AddressLookup::const_iterator it(mLineToAddressLookup.find(line));
	bool addressMapped = false;

	const auto& tc = ATUIGetThemeColors();

	if (it != mLineToAddressLookup.end()) {
		uint32 addr = it->second;

		if (ATGetDebugger()->IsBreakpointAtPC(addr)) {
			cl->AddTextColorPoint(next, tc.mHiMarkedFg, tc.mHiMarkedBg);
			next += 4;
		}

		addressMapped = true;
	}

	if (!addressMapped && ATGetDebugger()->IsDeferredBreakpointSet(mModulePath.c_str(), line + 1)) {
		cl->AddTextColorPoint(next, tc.mPendingHiMarkedFg, tc.mPendingHiMarkedBg);
		next += 4;
		addressMapped = true;
	}

	if (line == mPCLine) {
		cl->AddTextColorPoint(next, tc.mDirectLocationFg, tc.mDirectLocationBg);
		return;
	} else if (line == mFramePCLine) {
		cl->AddTextColorPoint(next, tc.mIndirectLocationFg, tc.mIndirectLocationBg);
		return;
	}

	if (next > 0) {
		cl->AddTextColorPoint(next, -1, -1);
		return;
	}

	sint32 backColor = -1;
	if (!addressMapped) {
		backColor = tc.mInactiveTextBg;
		cl->AddTextColorPoint(next, -1, backColor);
	}

	for(int i=0; i<length; ++i) {
		char c = text[i];

		if (c == ';') {
			cl->AddTextColorPoint(0, tc.mCommentText, backColor);
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
				cl->AddTextColorPoint(i, tc.mKeywordText, backColor);
				cl->AddTextColorPoint(j, -1, backColor);
			}

			return;
		}

		if (c != ' ' && c != '\t')
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
		PostMessage(mhwnd, WM_USER + 101, 0, 0);

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
		filter.append(std::begin(kCatchAllFilter), std::end(kCatchAllFilter));		// !! intentionally capturing end null

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

class ATUIDebuggerSourceListDialog final : public VDResizableDialogFrameW32 {
public:
	ATUIDebuggerSourceListDialog();

private:
	bool OnLoaded() override;
	void OnDataExchange(bool write) override;
	bool OnOK() override;

	VDUIProxyListBoxControl mFileListView;

	vdvector<VDStringW> mFiles;
};

ATUIDebuggerSourceListDialog::ATUIDebuggerSourceListDialog()
	: VDResizableDialogFrameW32(IDD_DEBUG_OPENSOURCEFILE)
{
	mFileListView.SetOnSelectionChanged([this](int sel) { EnableControl(IDOK, sel >= 0); });
}

bool ATUIDebuggerSourceListDialog::OnLoaded() {
	AddProxy(&mFileListView, IDC_LIST);

	mResizer.Add(IDC_LIST, mResizer.kMC);
	mResizer.Add(IDOK, mResizer.kBR);
	mResizer.Add(IDCANCEL, mResizer.kBR);

	OnDataExchange(false);
	mFileListView.Focus();
	return true;
}

void ATUIDebuggerSourceListDialog::OnDataExchange(bool write) {
	if (!write) {
		mFiles.clear();
		ATGetDebugger()->EnumSourceFiles(
			[this](const wchar_t *s, uint32 numLines) {
				// drop source files that have no lines... CC65 symbols are littered with these
				if (numLines)
					mFiles.emplace_back(s);
			}
		);

		std::sort(mFiles.begin(), mFiles.end());
		
		mFileListView.Clear();

		if (mFiles.empty()) {
			EnableControl(IDOK, false);
			mFileListView.SetEnabled(false);
			mFileListView.AddItem(L"No symbols loaded with source file information.");
		} else {
			EnableControl(IDOK, true);
			mFileListView.SetEnabled(true);
			for(const VDStringW& s : mFiles) {
				mFileListView.AddItem(s.c_str());
			}

			mFileListView.SetSelection(0);
		}
	}
}

bool ATUIDebuggerSourceListDialog::OnOK() {
	int sel = mFileListView.GetSelection();

	if ((unsigned)sel >= mFiles.size())
		return true;

	if (!ATOpenSourceWindow(mFiles[sel].c_str()))
		return true;

	return false;
}

void ATUIShowSourceListDialog() {
	ATUIDebuggerSourceListDialog dlg;
	dlg.ShowDialog(ATUIGetNewPopupOwner());
}

///////////////////////////////////////////////////////////////////////////

class ATHistoryWindow final : public ATUIDebuggerPane, private IATDebuggerClient, private IATUIHistoryModel {
public:
	ATHistoryWindow();
	~ATHistoryWindow();

	bool JumpToCycle(uint32 cycle);

private:
	double DecodeTapeSample(uint32 cycle) override;
	double DecodeTapeSeconds(uint32 cycle) override;
	uint32 ConvertRawTimestamp(uint32 rawCycle) override;
	float ConvertRawTimestampDeltaF(sint32 rawCycleDelta) override;
	ATCPUBeamPosition DecodeBeamPosition(uint32 cycle) override;
	bool IsInterruptPositionVBI(uint32 cycle) override;
	bool UpdatePreviewNode(ATCPUHistoryEntry& he) override;
	uint32 ReadInsns(const ATCPUHistoryEntry **ppInsns, uint32 startIndex, uint32 n) override;
	void OnEsc() override;
	void OnInsnSelected(uint32 index) override {}
	void JumpToInsn(uint32 pc) override;
	void JumpToSource(uint32 pc) override;

private:
	typedef ATHTNode TreeNode;
	typedef ATHTLineIterator LineIterator;

	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) override;
	bool OnCreate() override;
	void OnDestroy() override;
	void OnSize() override;
	void OnFontsUpdated() override;
	void OnPaint();

	void SetHistoryError(bool error);
	void RemakeHistoryView();
	void UpdateOpcodes();
	void CheckDisasmMode();

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId);

	vdrefptr<IATUIHistoryView> mpHistoryView;
	bool mbHistoryError = false;

	ATDebugDisasmMode mDisasmMode {};
	uint32 mSubCycles = (uint32)-1;
	bool mbDecodeAnticNMI = false;
	
	uint32 mLastHistoryCounter = 0;

	vdfastvector<uint32> mFilteredInsnLookup;
};

ATHistoryWindow::ATHistoryWindow()
	: ATUIDebuggerPane(kATUIPaneId_History, L"History")
{
	mPreferredDockCode = kATContainerDockRight;
}

ATHistoryWindow::~ATHistoryWindow() {
}

bool ATHistoryWindow::JumpToCycle(uint32 cycle) {
	return mpHistoryView && mpHistoryView->JumpToCycle(cycle);
}

double ATHistoryWindow::DecodeTapeSample(uint32 cycle) {
	auto& cassette = g_sim.GetCassette();

	if (!cassette.IsMotorRunning())
		return -1;

	auto& sch = *g_sim.GetScheduler();
	const sint32 tsDelta = (sint32)(cycle - sch.GetTick());

	return (double)cassette.GetSamplePos() + (double)tsDelta / (double)kATCassetteCyclesPerDataSample;
}

double ATHistoryWindow::DecodeTapeSeconds(uint32 cycle) {
	auto& cassette = g_sim.GetCassette();

	if (!cassette.IsMotorRunning())
		return -1;

	auto& sch = *g_sim.GetScheduler();
	const sint32 tsDelta = (sint32)(cycle - sch.GetTick());

	return (double)cassette.GetSamplePos() / (double)kATCassetteDataSampleRate + (double)tsDelta * sch.GetRate().AsInverseDouble();
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

	const bool is65C02 = dmode == kATDebugDisasmMode_65C02;
	const bool is65C816 = dmode == kATDebugDisasmMode_65C816;
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

	history->ExtractHistory(ppInsns, startIndex, n);
	return n;
}

void ATHistoryWindow::OnEsc() {
	if (ATGetUIPane(kATUIPaneId_Console))
		ATActivateUIPane(kATUIPaneId_Console, true);
}

void ATHistoryWindow::JumpToInsn(uint32 pc) {
	ATGetDebugger()->SetFramePC(pc);

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

	return ATUIDebuggerPane::WndProc(msg, wParam, lParam);
}

bool ATHistoryWindow::OnCreate() {
	if (!ATUIDebuggerPane::OnCreate())
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
	ATUIDebuggerPane::OnDestroy();
}

void ATHistoryWindow::OnSize() {
	RECT r;

	if (!GetClientRect(mhwnd, &r))
		return;

	if (mpHistoryView)
		SetWindowPos(mpHistoryView->AsNativeWindow()->GetHandleW32(), nullptr, 0, 0, r.right, r.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
}

void ATHistoryWindow::OnFontsUpdated() {
	mpHistoryView->SetFonts(g_propFont, g_propFontLineHeight, g_monoFont, g_monoFontLineHeight);

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
			SelectObject(hdc, g_propFont);
			SetBkMode(hdc, TRANSPARENT);

			RECT rText {};
			GetClientRect(mhwnd, &rText);
			FillRect(hdc, &rText, (HBRUSH)(COLOR_WINDOW + 1));

			InflateRect(&rText, -GetSystemMetrics(SM_CXEDGE)*2, -GetSystemMetrics(SM_CYEDGE)*2);

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
	UpdateOpcodes();
}

void ATHistoryWindow::OnDebuggerEvent(ATDebugEvent eventId) {
	CheckDisasmMode();

	if (mpHistoryView)
		mpHistoryView->RefreshAll();
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

	vdfunction<void()> mpFnThemeUpdated;
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

	return ATUIDebuggerPane::WndProc(msg, wParam, lParam);
}

bool ATConsoleWindow::OnCreate() {
	if (!ATUIDebuggerPane::OnCreate())
		return false;

	mhwndLog = CreateWindowEx(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, _T(""), ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_VISIBLE|WS_CHILD, 0, 0, 0, 0, mhwnd, (HMENU)100, g_hInst, NULL);
	if (!mhwndLog)
		return false;

	mhwndPrompt = CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_VISIBLE|WS_CHILD|ES_READONLY, 0, 0, 0, 0, mhwnd, (HMENU)100, g_hInst, NULL);
	if (!mhwndPrompt)
		return false;

	mhwndEdit = CreateWindowEx(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, _T(""), WS_VISIBLE|WS_CHILD|ES_AUTOHSCROLL, 0, 0, 0, 0, mhwnd, (HMENU)100, g_hInst, NULL);
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

	ATUIDebuggerPane::OnDestroy();
}

void ATConsoleWindow::OnSize() {
	RECT r;

	if (GetClientRect(mhwnd, &r)) {
		int prw = 10 * g_monoFontCharWidth + 4*GetSystemMetrics(SM_CXEDGE);
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
	OnThemeUpdated();
}

void ATConsoleWindow::OnThemeUpdated() {
	// RichEdit50W tries to be cute and retransforms the font given in WM_SETFONT to the
	// current DPI. Unfortunately, this just makes a mess of the font size, so we have to
	// bypass it and send EM_SETCHARFORMAT instead to get the size we actually specified
	// to stick.

	const bool useDark = ATUIIsDarkThemeActive();

	CHARFORMAT2 cf = {};
	cf.cbSize = sizeof cf;
	cf.dwMask = CFM_SIZE;
	cf.yHeight = g_monoFontPtSizeTenths * 2;

	if (useDark) {
		cf.dwMask |= CFM_COLOR | CFM_BACKCOLOR | CFM_EFFECTS;
		cf.dwEffects = 0;
		cf.crTextColor = RGB(216, 216, 216);
		cf.crBackColor = RGB(32, 32, 32);
	}

	if (mhwndLog) {
		SendMessage(mhwndLog, WM_SETFONT, (WPARAM)g_monoFont, TRUE);
		SendMessage(mhwndLog, EM_SETCHARFORMAT, SCF_ALL | SCF_DEFAULT, (LPARAM)&cf);

		if (useDark)
			SendMessage(mhwndLog, EM_SETBKGNDCOLOR, FALSE, cf.crBackColor);
		else
			SendMessage(mhwndLog, EM_SETBKGNDCOLOR, TRUE, RGB(255, 255, 255));
	}

	if (mhwndEdit) {
		SendMessage(mhwndEdit, WM_SETFONT, (WPARAM)g_monoFont, TRUE);
		SendMessage(mhwndEdit, EM_SETCHARFORMAT, SCF_ALL | SCF_DEFAULT, (LPARAM)&cf);

		if (useDark)
			SendMessage(mhwndEdit, EM_SETBKGNDCOLOR, FALSE, cf.crBackColor);
		else
			SendMessage(mhwndEdit, EM_SETBKGNDCOLOR, TRUE, RGB(255, 255, 255));
	}

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
	SendMessageW(mhwndLog, EM_SETSEL, -1, -1);
	ShowEnd();

	mAppendBuffer.clear();
}

///////////////////////////////////////////////////////////////////////////

class ATMemoryWindow final : public ATUIDebuggerPane,
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
	void OnThemeUpdated();
	void OnPaint();
	void RemakeView(uint32 focusAddr);
	bool GetAddressFromPoint(int x, int y, uint32& addr) const;

	HWND	mhwndAddress;
	HMENU	mMenu;

	RECT	mTextArea;
	uint32	mViewStart;
	bool	mbViewValid;
	uint32	mCharWidth;
	uint32	mLineHeight;
	uintptr	mLastTarget;

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
	, mbViewValid(false)
	, mLineHeight(16)
	, mLastTarget(0)
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

		case WM_ERASEBKGND:
			return FALSE;

		case WM_PAINT:
			OnPaint();
			return 0;

		case WM_CONTEXTMENU:
			{
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);
				POINT pt = {x, y};
				ScreenToClient(mhwnd, &pt);

				uint32 addr;
				const bool addrValid = GetAddressFromPoint(pt.x, pt.y, addr);

				HMENU hSubMenu = GetSubMenu(mMenu, 0);

				VDEnableMenuItemByCommandW32(hSubMenu, ID_CONTEXT_TOGGLEREADBREAKPOINT, addrValid);
				VDEnableMenuItemByCommandW32(hSubMenu, ID_CONTEXT_TOGGLEWRITEBREAKPOINT, addrValid);

				UINT id = TrackPopupMenu(hSubMenu, TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD, x, y, 0, mhwnd, NULL);

				switch(id) {
					case ID_CONTEXT_TOGGLEREADBREAKPOINT:
						if (addrValid)
							ATGetDebugger()->ToggleAccessBreakpoint(addr, false);
						break;

					case ID_CONTEXT_TOGGLEWRITEBREAKPOINT:
						if (addrValid)
							ATGetDebugger()->ToggleAccessBreakpoint(addr, true);
						break;
				}
			}

			return 0;
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

void ATMemoryWindow::OnThemeUpdated() {
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

		const ATUIThemeColors& tc = ATUIGetThemeColors();

		COLORREF normalTextColor = VDSwizzleU32(tc.mContentFg) >> 8;
		COLORREF changedTextColor = ((normalTextColor & 0xFEFEFE) >> 1) | 0x0000FF;

		::SelectObject(hdc, g_monoFont);
		::SetTextAlign(hdc, TA_TOP | TA_LEFT);
		::SetBkMode(hdc, TRANSPARENT);
		::SetBkColor(hdc, VDSwizzleU32(tc.mContentBg) >> 8);

		int rowStart	= (ps.rcPaint.top - mTextArea.top) / mLineHeight;
		int rowEnd		= (ps.rcPaint.bottom - mTextArea.top + mLineHeight - 1) / mLineHeight;

		uint32 incMask = ATAddressGetSpaceSize(mViewStart) - 1;

		IATDebugger *debugger = ATGetDebugger();
		IATDebugTarget *target = debugger->GetTarget();

		uint32 addrBase = mViewStart & ~incMask;
		for(int rowIndex = rowStart; rowIndex < rowEnd; ++rowIndex) {
			uint32 addr = addrBase + ((mViewStart + (rowIndex << 4)) & incMask);

			mTempLine.sprintf("%s:", debugger->GetAddressText(addr, false).c_str());
			mTempLine2.clear();
			mTempLine2.resize(mTempLine.size(), ' ');

			uint32 changeMask = (rowIndex < (int)mChangedBits.size()) ? mChangedBits[rowIndex] : 0;
			for(int i=0; i<16; ++i) {
				uint32 baddr = addrBase + ((addr + i) & incMask);

				data[i] = target->DebugReadByte(baddr);

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
	bool changed = false;
	uint32 changeMask = 0xFFFFFFFFU;

	if (mViewStart != focusAddr || !mbViewValid) {
		mViewStart = focusAddr;
		mbViewValid = true;
		changed = true;
		changeMask = 0;
	}

	int rows = (int)mChangedBits.size();

	int limit = (int)mViewData.size();

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
		case kATAddressSpace_ROM:
			addrLen = 6;
			break;

		case kATAddressSpace_VBXE:
			addrLen = 7;
			break;

		case kATAddressSpace_EXTRAM:
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
	if (mLastTarget != (uintptr)state.mpDebugTarget) {
		mLastTarget = (uintptr)state.mpDebugTarget;

		mbViewValid = false;
	}

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

	VDFunctionThunkInfo	*mpListViewThunk;

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
				mpExpr = ATDebuggerParseExpression(VDTextWToA(expr).c_str(), ATGetDebuggerSymbolLookup(), ATGetDebugger()->GetExprOpts());
			} catch(const ATDebuggerExprParseException& ex) {
				mValueStr = L"<Evaluation error: ";
				mValueStr += VDTextAToW(ex.gets());
				mValueStr += L'>';
			}
		}

		bool Update() {
			if (!mpExpr)
				return false;

			ATDebugExpEvalContext ctx = ATGetDebugger()->GetEvalContext();

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
	SetWindowLongPtrW(mhwndList, GWLP_WNDPROC, (LONG_PTR)VDGetThunkFunction<WNDPROC>(mpListViewThunk));

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

	if (event->mpNewLabel && *event->mpNewLabel) {
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
		if (event->mpNewLabel && !isLast)
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
	void OnFontsUpdated();

	LRESULT DLAddrEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT PFAddrEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	HWND	mhwndDisplay;
	HWND	mhwndDLAddrCombo;
	HWND	mhwndPFAddrCombo;
	HMENU	mhmenu;
	int		mComboResizeInProgress;

	VDFunctionThunkInfo *mpThunkDLEditCombo;
	VDFunctionThunkInfo *mpThunkPFEditCombo;
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

	return ATUIDebuggerPane::WndProc(msg, wParam, lParam);
}

bool ATDebugDisplayWindow::OnCreate() {
	if (!ATUIDebuggerPane::OnCreate())
		return false;

	mhwndDLAddrCombo = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_VISIBLE|WS_CHILD|WS_CLIPSIBLINGS|CBS_DROPDOWN|CBS_HASSTRINGS|CBS_AUTOHSCROLL, 0, 0, 0, 0, mhwnd, (HMENU)102, g_hInst, NULL);
	if (mhwndDLAddrCombo) {
		SendMessageW(mhwndDLAddrCombo, WM_SETFONT, (WPARAM)g_propFont, TRUE);
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

	mhwndPFAddrCombo = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_VISIBLE|WS_CHILD|WS_CLIPSIBLINGS|CBS_DROPDOWN|CBS_HASSTRINGS|CBS_AUTOHSCROLL, 0, 0, 0, 0, mhwnd, (HMENU)103, g_hInst, NULL);
	if (mhwndPFAddrCombo) {
		SendMessageW(mhwndPFAddrCombo, WM_SETFONT, (WPARAM)g_propFont, TRUE);
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
		SendMessage(mhwndDLAddrCombo, WM_SETFONT, (WPARAM)g_propFont, TRUE);

	if (mhwndPFAddrCombo)
		SendMessage(mhwndPFAddrCombo, WM_SETFONT, (WPARAM)g_propFont, TRUE);

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

public:
	int AddRef() { return ATUIPane::AddRef(); }
	int Release() { return ATUIPane::Release(); }

public:
	void WriteASCII(const void *buf, size_t len) override;
	void WriteATASCII(const void *buf, size_t len) override;

protected:
	VDGUIHandle Create(uint32 exStyle, uint32 style, int x, int y, int cx, int cy, VDGUIHandle parent, int id);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) override;

	bool OnCreate() override;
	void OnDestroy() override;
	void OnSize() override;
	void OnFontsUpdated() override;
	void OnSetFocus() override;

	vdrefptr<IVDTextEditor> mpTextEditor;
	HWND	mhwndTextEditor;

	uint32		mLineBufIdx;
	uint8		mLineBuf[132];
};

ATPrinterOutputWindow::ATPrinterOutputWindow()
	: ATUIPane(kATUIPaneId_PrinterOutput, L"Printer Output")
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

		if (c == 0x0D)
			continue;

		if (c != 0x0A) {
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

	g_sim.SetPrinterOutput(this);
	return true;
}

void ATPrinterOutputWindow::OnDestroy() {
	g_sim.SetPrinterOutput(nullptr);

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

		IATDebugger *d = ATGetDebugger();
		d->SetEnabled(true);

		if (!ATRestorePaneLayout(NULL)) {
			ATActivateUIPane(kATUIPaneId_Console, !ATGetUIPane(kATUIPaneId_Console));
			ATActivateUIPane(kATUIPaneId_Registers, false);
		}

		d->ShowBannerOnce();
	}
}

void ATOpenConsole() {
	if (!g_uiDebuggerMode) {
		ATSavePaneLayout(NULL);
		g_uiDebuggerMode = true;

		IATDebugger *d = ATGetDebugger();
		d->SetEnabled(true);

		if (ATRestorePaneLayout(NULL)) {
			if (ATGetUIPane(kATUIPaneId_Console))
				ATActivateUIPane(kATUIPaneId_Console, true);
		} else {
			ATLoadDefaultPaneLayout();
		}

		d->ShowBannerOnce();
	}
}

void ATCloseConsole() {
	if (g_uiDebuggerMode) {
		ATGetDebugger()->SetEnabled(false);

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

VDZHFONT ATGetConsoleFontW32() {
	return g_monoFont;
}

int ATGetConsoleFontLineHeightW32() {
	return g_monoFontLineHeight;
}

VDZHFONT ATConsoleGetPropFontW32() {
	return g_propFont;
}

int ATConsoleGetPropFontLineHeightW32() {
	return g_propFontLineHeight;
}

///////////////////////////////////////////////////////////////////////////

namespace {
	ATNotifyList<const vdfunction<void()> *> g_consoleFontCallbacks;
}

void ATConsoleAddFontNotification(const vdfunction<void()> *callback) {
	g_consoleFontCallbacks.Add(callback);
}

void ATConsoleRemoveFontNotification(const vdfunction<void()> *callback) {
	g_consoleFontCallbacks.Remove(callback);
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

	g_consoleFontCallbacks.Notify([](const auto& cb) -> bool { (*cb)(); return false; });
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

bool ATConsoleConfirmScriptLoad() {
	ATUIGenericDialogOptions opts {};

	opts.mhParent = ATUIGetMainWindow();
	opts.mpTitle = L"Debugger script found";
	opts.mpMessage =
		L"Do you want to run the debugger script that is included with this image?\n"
		L"\n"
		L"Debugger scripts are powerful and should only be run for programs you are debugging and from sources that you trust."
		L"Automatic debugger script loading and confirmation settings can be toggled in the Debugger section of Configure System."
		;
	opts.mpIgnoreTag = "RunDebuggerScript";
	opts.mIconType = kATUIGenericIconType_Warning;
	opts.mResultMask = kATUIGenericResultMask_OKCancel;
	opts.mValidIgnoreMask = kATUIGenericResultMask_OKCancel;
	opts.mAspectLimit = 4.0f;

	bool wasIgnored = false;
	opts.mpCustomIgnoreFlag = &wasIgnored;

	const bool result = (ATUIShowGenericDialogAutoCenter(opts) == kATUIGenericResult_OK);

	if (wasIgnored) {
		ATGetDebugger()->SetScriptAutoLoadMode(result ? ATDebuggerScriptAutoLoadMode::Enabled : ATDebuggerScriptAutoLoadMode::Disabled);
	}

	return result;
}

void ATConsoleRequestFile(ATDebuggerRequestFileEvent& event) {
	if (event.mbSave)
		event.mPath = VDGetSaveFileName('dbgr', ATUIGetNewPopupOwner(), L"Select file to write for debugger command", L"All files\0*.*\0", nullptr);
	else
		event.mPath = VDGetLoadFileName('dbgr', ATUIGetNewPopupOwner(), L"Select file to read for debugger command", L"All files\0*.*\0", nullptr);
}

void ATInitUIPanes() {
	ATGetDebugger()->SetScriptAutoLoadConfirmFn(ATConsoleConfirmScriptLoad);
	ATGetDebugger()->SetOnRequestFile(ATConsoleRequestFile);

	VDLoadSystemLibraryW32("msftedit");

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

void ATConsoleOpenLogFile(const wchar_t *path) {
	ATConsoleCloseLogFile();

	vdautoptr<VDFileStream> fs(new VDFileStream(path, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways));
	vdautoptr<VDTextOutputStream> tos(new VDTextOutputStream(fs));

	g_pLogFile = fs.release();
	g_pLogOutput = tos.release();
}

void ATConsoleCloseLogFileNT() {
	vdsafedelete <<= g_pLogOutput;
	vdsafedelete <<= g_pLogFile;
}

void ATConsoleCloseLogFile() {
	try {
		if (g_pLogOutput)
			g_pLogOutput->Flush();

		if (g_pLogFile)
			g_pLogFile->close();
	} catch(...) {
		ATConsoleCloseLogFileNT();
		throw;
	}

	ATConsoleCloseLogFileNT();
}

void ATConsoleWrite(const char *s) {
	if (g_pConsoleWindow) {
		g_pConsoleWindow->Write(s);
		g_pConsoleWindow->ShowEnd();
	}

	if (g_pLogOutput) {
		for(;;) {
			const char *lbreak = strchr(s, '\n');

			if (!lbreak) {
				g_pLogOutput->Write(s);
				break;
			}

			g_pLogOutput->PutLine(s, (int)(lbreak - s));

			s = lbreak+1;
		}
	}
}

void ATConsolePrintf(const char *format, ...) {
	if (g_pConsoleWindow) {
		char buf[3072];
		va_list val;

		va_start(val, format);
		const unsigned result = (unsigned)vsnprintf(buf, vdcountof(buf), format, val);
		va_end(val);

		if (result < vdcountof(buf))
			ATConsoleWrite(buf);
	}
}

void ATConsoleTaggedPrintf(const char *format, ...) {
	if (g_pConsoleWindow) {
		ATAnticEmulator& antic = g_sim.GetAntic();
		char buf[3072];

		unsigned prefixLen = (unsigned)snprintf(buf, vdcountof(buf), "(%3d:%3d,%3d) ", antic.GetRawFrameCounter(), antic.GetBeamY(), antic.GetBeamX());
		if (prefixLen < vdcountof(buf)) {
			va_list val;

			va_start(val, format);
			const unsigned messageLen = (unsigned)vsnprintf(buf + prefixLen, vdcountof(buf) - prefixLen, format, val);
			va_end(val);

			if (messageLen < vdcountof(buf) - prefixLen)
				ATConsoleWrite(buf);
		}
	}
}

bool ATConsoleCheckBreak() {
	return GetAsyncKeyState(VK_CONTROL) < 0 && (GetAsyncKeyState(VK_CANCEL) < 0 || GetAsyncKeyState(VK_PAUSE) < 0 || GetAsyncKeyState('A' + 2) < 0);
}

void ATConsolePingBeamPosition(uint32 frame, uint32 vpos, uint32 hpos) {
	ATHistoryWindow *historyPane = static_cast<ATHistoryWindow *>(ATGetUIPane(kATUIPaneId_History));
	
	if (!historyPane)
		return;

	const auto& decoder = g_sim.GetTimestampDecoder();

	// note that we're deliberately using unsigned wrapping here, to avoid naughty signed conversions
	const uint32 cycle = decoder.mFrameTimestampBase + (frame - decoder.mFrameCountBase) * decoder.mCyclesPerFrame + 114 * vpos + hpos;

	if (historyPane->JumpToCycle(cycle))
		return;

	ATActivateUIPane(kATUIPaneId_History, true);
}
