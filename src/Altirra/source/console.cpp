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
#include <malloc.h>
#include <stdio.h>
#include <vd2/Dita/services.h>
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/strutil.h>
#include <vd2/system/thread.h>
#include <vd2/system/thunk.h>
#include <vd2/system/refcount.h>
#include <vd2/system/registry.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/w32assist.h>
#include <at/atui/constants.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/genericdialog.h>
#include <at/atnativeui/uiframe.h>
#include <at/atnativeui/uiproxies.h>
#include <at/atcore/address.h>
#include <at/atcore/wraptime.h>
#include "console.h"
#include "uiaccessors.h"
#include "uicommondialogs.h"
#include "uidbgpane.h"
#include "texteditor.h"
#include "simulator.h"
#include "debugger.h"
#include "debuggersettings.h"
#include "resource.h"
#include "disasm.h"
#include "oshelper.h"

extern HINSTANCE g_hInst;
extern HWND g_hwnd;

extern ATSimulator g_sim;
extern ATContainerWindow *g_pMainWindow;
extern IATUIDebuggerConsoleWindow *g_pConsoleWindow;

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

	ATDebuggerSourceFileInfo sourceFileInfo;
	if (!lookup->GetSourceFilePath(moduleId, lineInfo.mFileId, sourceFileInfo) && lineInfo.mLine)
		return false;

	IATSourceWindow *w = ATOpenSourceWindow(sourceFileInfo);
	if (!w)
		return false;

	w->FocusOnLine(lineInfo.mLine - 1);
	return true;
}

///////////////////////////////////////////////////////////////////////////

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

VDZHMENU ATUIGetSourceContextMenuW32() {
	return GetSubMenu(g_hmenuSrcContext, 0);
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

void ATConsoleGetCharMetrics(int& charWidth, int& lineHeight) {
	charWidth = g_monoFontCharWidth;
	lineHeight = g_monoFontLineHeight;
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

bool ATConsoleConfirmScriptLoad() {
	ATUIGenericDialogOptions opts {};

	opts.mhParent = ATUIGetNewPopupOwner();
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

	extern void ATUIDebuggerRegisterDisassemblyPane();
	ATUIDebuggerRegisterDisassemblyPane();

	extern void ATUIDebuggerRegisterMemoryPane();
	ATUIDebuggerRegisterMemoryPane();

	extern void ATUIDebuggerRegisterSourcePane();
	ATUIDebuggerRegisterSourcePane();

	extern void ATUIDebuggerRegisterConsolePane();
	ATUIDebuggerRegisterConsolePane();

	extern void ATUIDebuggerRegisterDebugDisplayPane();
	ATUIDebuggerRegisterDebugDisplayPane();
	
	extern void ATUIDebuggerRegisterHistoryPane();
	ATUIDebuggerRegisterHistoryPane();

	extern void ATUIDebuggerRegisterWatchPane();
	ATUIDebuggerRegisterWatchPane();

	extern void ATUIDebuggerRegisterRegistersPane();
	ATUIDebuggerRegisterRegistersPane();

	extern void ATUIDebuggerRegisterCallStackPane();
	ATUIDebuggerRegisterCallStackPane();

	extern void ATUIDebuggerRegisterPrinterOutputPane();
	ATUIDebuggerRegisterPrinterOutputPane();

	extern void ATUIDebuggerRegisterBreakpointsPane();
	ATUIDebuggerRegisterBreakpointsPane();

	extern void ATUIDebuggerRegisterTargetsPane();
	ATUIDebuggerRegisterTargetsPane();

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
				s.append_sprintf(&",%x"[s.back() == '{' ? 1 : 0], id);

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
						HWND hwndPane = uipane->AsNativeWindow().GetHandleW32();
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
		HWND hwndFrame = activePane->AsNativeWindow().GetHandleW32();
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

		HWND hwndPane = pane->AsNativeWindow().GetHandleW32();
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

				HWND hwndPane = uipane->AsNativeWindow().GetHandleW32();
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

			// SetWindowPlacement() is documented as ensuring that windows are not off-screen, but
			// this functionality does not work for windows with WS_EX_TOOLWINDOW, so we have to
			// fix it ourselves.
			HMONITOR hmon = ::MonitorFromWindow(hwndFrame, MONITOR_DEFAULTTONULL);
			if (!hmon) {
				hmon = ::MonitorFromWindow(hwndFrame, MONITOR_DEFAULTTONEAREST);

				if (hmon) {
					MONITORINFO mi { sizeof(MONITORINFO) };
					TITLEBARINFO tbi { sizeof(TITLEBARINFO) };
					RECT r {};

					if (GetMonitorInfo(hmon, &mi) && GetTitleBarInfo(hwndFrame, &tbi) && GetWindowRect(hwndFrame, &r)) {
						// move the window so that the center of the title bar is within the
						// work area of the monitor

						const int tbx = (tbi.rcTitleBar.left + tbi.rcTitleBar.right) >> 1;
						const int tby = (tbi.rcTitleBar.top + tbi.rcTitleBar.bottom) >> 1;
						const int tbx2 = std::clamp<int>(tbx, mi.rcWork.left, mi.rcWork.right);
						const int tby2 = std::clamp<int>(tby, mi.rcWork.top, mi.rcWork.bottom);

						SetWindowPos(hwndFrame, nullptr, r.left + (tbx2 - tbx), r.top + (tby2 - tby), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
					}
				}
			}

			::ShowWindow(hwndFrame, SW_SHOWNOACTIVATE);
		}
	}

	if (activePaneId && !frames[activePaneId]) {
		activePane = ATGetUIPane(activePaneId);
		if (activePane) {
			HWND hwndPane = activePane->AsNativeWindow().GetHandleW32();
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
	IATUIDebuggerHistoryPane *historyPane = vdpoly_cast<IATUIDebuggerHistoryPane *>(ATGetUIPane(kATUIPaneId_History));
	
	if (!historyPane)
		return;

	const auto& decoder = g_sim.GetTimestampDecoder();

	// note that we're deliberately using unsigned wrapping here, to avoid naughty signed conversions
	const uint32 cycle = decoder.mFrameTimestampBase + (frame - decoder.mFrameCountBase) * decoder.mCyclesPerFrame + 114 * vpos + hpos;

	if (historyPane->JumpToCycle(cycle))
		return;

	ATActivateUIPane(kATUIPaneId_History, true);
}
