//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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
#include <list>
#include <windows.h>
#include <richedit.h>
#include <vd2/system/filesys.h>
#include <vd2/system/math.h>
#include <vd2/Dita/services.h>
#include "Dialog.h"
#include "uiproxies.h"
#include "resource.h"
#include "options.h"

// This is actually deprecated in earlier SDKs (VS2005) and undeprecated
// in later ones (Win7). Interesting.
#ifndef DM_INTERLACED
#define DM_INTERLACED 0x00000002
#endif

#ifndef BCM_SETSHIELD
#define BCM_SETSHIELD	0x160C
#endif

void ATUIShowDialogSetFileAssociations(VDGUIHandle parent, bool allowElevation);
void ATUIShowDialogRemoveFileAssociations(VDGUIHandle parent, bool allowElevation);

///////////////////////////////////////////////////////////////////////////

class ATUIDialogFullScreenMode : public VDDialogFrameW32 {
public:
	struct ModeInfo {
		uint32 mWidth;
		uint32 mHeight;
		uint32 mRefresh;
	};

	ATUIDialogFullScreenMode();

	const ModeInfo& GetSelectedItem() const { return mSelectedMode; }
	void SetSelectedItem(const ModeInfo& modeInfo) { mSelectedMode = modeInfo; }

protected:
	bool OnLoaded();
	void OnDestroy();
	void OnDataExchange(bool write);
	void OnSelectedItemChanged(VDUIProxyListView *sender, int index);

	ModeInfo	mSelectedMode;

	VDUIProxyListView mList;
	VDDelegate mDelSelItemChanged;

	struct ModeItem : public vdrefcounted<IVDUIListViewVirtualItem>, public ModeInfo {
		ModeItem(const ModeInfo& modeInfo) : ModeInfo(modeInfo) {}

		void GetText(int subItem, VDStringW& s) const;
	};

	struct ModeInfoLess {
		bool operator()(const ModeInfo& x, const ModeInfo& y) const {
			if (x.mWidth != y.mWidth)
				return x.mWidth < y.mWidth;

			if (x.mHeight != y.mHeight)
				return x.mHeight < y.mHeight;

			return x.mRefresh < y.mRefresh;
		}
	};

	struct ModeInfoEqual {
		bool operator()(const ModeInfo& x, const ModeInfo& y) const {
			return x.mWidth == y.mWidth &&
				x.mHeight == y.mHeight &&
				x.mRefresh == y.mRefresh;
		}
	};

	struct ModeInfoMatch {
		ModeInfoMatch(const ModeInfo& mode) : mMode(mode) {}

		bool operator()(const ModeInfo& mode) const {
			return ModeInfoEqual()(mMode, mode);
		}

		const ModeInfo mMode;
	};
};

void ATUIDialogFullScreenMode::ModeItem::GetText(int subItem, VDStringW& s) const {
	switch(subItem) {
		case 0:
			s.sprintf(L"%ux%u", mWidth, mHeight);
			break;

		case 1:
			if (!mRefresh)
				s = L"Default";
			else
				s.sprintf(L"%uHz", mRefresh);
			break;
	}
}

ATUIDialogFullScreenMode::ATUIDialogFullScreenMode()
	: VDDialogFrameW32(IDD_OPTIONS_DISPLAY_MODE)
{
	memset(&mSelectedMode, 0, sizeof mSelectedMode);

	mList.OnItemSelectionChanged() += mDelSelItemChanged.Bind(this, &ATUIDialogFullScreenMode::OnSelectedItemChanged);
}

bool ATUIDialogFullScreenMode::OnLoaded() {
	AddProxy(&mList, IDC_MODE_LIST);
	mList.InsertColumn(0, L"Resolution", 0);
	mList.InsertColumn(1, L"Refresh rate", 0);
	mList.SetFullRowSelectEnabled(true);

	VDDialogFrameW32::OnLoaded();

	SetFocusToControl(IDC_LIST);
	return true;
}

void ATUIDialogFullScreenMode::OnDestroy() {
	mList.Clear();
}

void ATUIDialogFullScreenMode::OnDataExchange(bool write) {
	if (write) {
		ModeItem *p = static_cast<ModeItem *>(mList.GetSelectedVirtualItem());

		if (!p) {
			FailValidation(IDC_LIST);
			return;
		}

		mSelectedMode = *p;
	} else {
		struct {
			DEVMODE dm;
			char buf[1024];
		} devMode;

		vdfastvector<ModeInfo> modes;

		int modeIndex = 0;
		for(;; ++modeIndex) {
			devMode.dm.dmSize = sizeof(DEVMODE);
			devMode.dm.dmDriverExtra = sizeof devMode.buf;

			if (!EnumDisplaySettings(NULL, modeIndex, &devMode.dm))
				break;

			// throw out paletted modes
			if (devMode.dm.dmBitsPerPel < 15)
				continue;

			// throw out interlaced modes
			if (devMode.dm.dmDisplayFlags & DM_INTERLACED)
				continue;

			ModeInfo mode = { devMode.dm.dmPelsWidth, devMode.dm.dmPelsHeight, devMode.dm.dmDisplayFrequency };

			if (mode.mRefresh == 1)
				mode.mRefresh = 0;

			modes.push_back(mode);
		}

		std::sort(modes.begin(), modes.end(), ModeInfoLess());
		modes.erase(std::unique(modes.begin(), modes.end(), ModeInfoEqual()), modes.end());

		int selectedIndex = std::find_if(modes.begin(), modes.end(), ModeInfoMatch(mSelectedMode)) - modes.begin();
		if (selectedIndex >= (int)modes.size())
			selectedIndex = -1;

		for(vdfastvector<ModeInfo>::const_iterator it(modes.begin()), itEnd(modes.end());
			it != itEnd;
			++it)
		{
			const ModeInfo& modeInfo = *it;

			ModeItem *modeItem = new ModeItem(modeInfo);

			if (modeItem) {
				modeItem->AddRef();
				mList.InsertVirtualItem(-1, modeItem);
				modeItem->Release();
			}
		}

		mList.SetSelectedIndex(selectedIndex);
		mList.EnsureItemVisible(selectedIndex);
		mList.AutoSizeColumns();
		EnableControl(IDOK, selectedIndex >= 0);
	}
}

void ATUIDialogFullScreenMode::OnSelectedItemChanged(VDUIProxyListView *sender, int index) {
	EnableControl(IDOK, index >= 0);
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogOptionsPage : public VDDialogFrameW32 {
public:
	ATUIDialogOptionsPage(uint32 id, ATOptions& opts);
	virtual ~ATUIDialogOptionsPage();

	struct HelpEntry {
		uint32 mId;
		uint32 mLinkedId;
		vdrect32 mArea;
		VDStringW mLabel;
		VDStringW mText;
	};

	const HelpEntry *GetHelpEntry(const vdpoint32& pt) const;

protected:
	void AddHelpEntry(uint32 id, const wchar_t *label, const wchar_t *s);
	void LinkHelpEntry(uint32 id, uint32 linkedId);
	void ClearHelpEntries();

	ATOptions& mOptions;

	typedef std::list<HelpEntry> HelpEntries;
	HelpEntries mHelpEntries;
};

ATUIDialogOptionsPage::ATUIDialogOptionsPage(uint32 id, ATOptions& opts)
	: VDDialogFrameW32(id)
	, mOptions(opts)
{
}

ATUIDialogOptionsPage::~ATUIDialogOptionsPage() {
}

void ATUIDialogOptionsPage::AddHelpEntry(uint32 id, const wchar_t *label, const wchar_t *s) {
	mHelpEntries.push_back(HelpEntry());
	HelpEntry& e = mHelpEntries.back();
	e.mId = id;
	e.mLinkedId = 0;
	e.mArea = GetControlPos(id);
	e.mLabel = label;
	e.mText = s;
}

void ATUIDialogOptionsPage::LinkHelpEntry(uint32 id, uint32 linkedId) {
	mHelpEntries.push_back(HelpEntry());
	HelpEntry& e = mHelpEntries.back();
	e.mId = id;
	e.mLinkedId = linkedId;
	e.mArea = GetControlPos(id);
}

void ATUIDialogOptionsPage::ClearHelpEntries() {
	mHelpEntries.clear();
}

const ATUIDialogOptionsPage::HelpEntry *ATUIDialogOptionsPage::GetHelpEntry(const vdpoint32& pt) const {
	for(HelpEntries::const_iterator it = mHelpEntries.begin(), itEnd = mHelpEntries.end();
		it != itEnd;
		++it)
	{
		const HelpEntry& e = *it;

		if (e.mArea.contains(pt)) {
			if (e.mLinkedId) {
				for(it = mHelpEntries.begin(); it != itEnd; ++it) {
					if (it->mId == e.mLinkedId)
						return &*it;
				}

				return NULL;
			}

			return &e;
		}
	}

	return NULL;
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogOptionsPageDisplay : public ATUIDialogOptionsPage {
public:
	ATUIDialogOptionsPageDisplay(ATOptions& opts);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
};

ATUIDialogOptionsPageDisplay::ATUIDialogOptionsPageDisplay(ATOptions& opts)
	: ATUIDialogOptionsPage(IDD_OPTIONS_DISPLAY, opts)
{
}

bool ATUIDialogOptionsPageDisplay::OnLoaded() {
	AddHelpEntry(IDC_GRAPHICS_DDRAW, L"DirectDraw", L"Enable DirectDraw support. This is used if D3D9/OpenGL are disabled or not available.");
	AddHelpEntry(IDC_GRAPHICS_D3D9, L"Direct3D 9", L"Enable Direct3D 9 support. This is the best option for speed and quality, and also enables the filtering options.");
	AddHelpEntry(IDC_GRAPHICS_OPENGL, L"OpenGL", L"Enable OpenGL support. Direct3D 9 is a better option, but this is a reasonable fallback.");
	AddHelpEntry(IDC_FSMODE_DESKTOP, L"Full screen mode: Match desktop", L"Uses the desktop resolution for full screen mode. This avoids a mode switch.");
	AddHelpEntry(IDC_FSMODE_CUSTOM, L"Full screen mode: Custom", L"Use a specific video mode for full screen mode. Zero for refresh rate allows any rate.");
	LinkHelpEntry(IDC_FSMODE_WIDTH, IDC_FSMODE_CUSTOM);
	LinkHelpEntry(IDC_FSMODE_HEIGHT, IDC_FSMODE_CUSTOM);
	LinkHelpEntry(IDC_FSMODE_REFRESH, IDC_FSMODE_CUSTOM);
	LinkHelpEntry(IDC_FSMODE_BROWSE, IDC_FSMODE_CUSTOM);

	return ATUIDialogOptionsPage::OnLoaded();
}

void ATUIDialogOptionsPageDisplay::OnDataExchange(bool write) {
	ExchangeControlValueBoolCheckbox(write, IDC_GRAPHICS_DDRAW, mOptions.mbDisplayDDraw);
	ExchangeControlValueBoolCheckbox(write, IDC_GRAPHICS_D3D9, mOptions.mbDisplayD3D9);
	ExchangeControlValueBoolCheckbox(write, IDC_GRAPHICS_OPENGL, mOptions.mbDisplayOpenGL);

	if (write) {
		mOptions.mFullScreenWidth = 0;
		mOptions.mFullScreenHeight = 0;
		mOptions.mFullScreenRefreshRate = 0;

		if (IsButtonChecked(IDC_FSMODE_CUSTOM)) {
			VDStringW s;
			VDStringW t;

			if (GetControlText(IDC_FSMODE_WIDTH, s) && GetControlText(IDC_FSMODE_HEIGHT, t)) {
				mOptions.mFullScreenWidth = wcstoul(s.c_str(), NULL, 10);
				mOptions.mFullScreenHeight = wcstoul(t.c_str(), NULL, 10);

				if (GetControlText(IDC_FSMODE_REFRESH, s))
					mOptions.mFullScreenRefreshRate = wcstoul(s.c_str(), NULL, 10);
			}
		}
	} else {
		if (mOptions.mFullScreenWidth && mOptions.mFullScreenHeight) {
			CheckButton(IDC_FSMODE_DESKTOP, false);
			CheckButton(IDC_FSMODE_CUSTOM, true);
			SetControlTextF(IDC_FSMODE_WIDTH, L"%u", mOptions.mFullScreenWidth);
			SetControlTextF(IDC_FSMODE_HEIGHT, L"%u", mOptions.mFullScreenHeight);
			SetControlTextF(IDC_FSMODE_REFRESH, L"%u", mOptions.mFullScreenRefreshRate);
		} else {
			CheckButton(IDC_FSMODE_DESKTOP, true);
			CheckButton(IDC_FSMODE_CUSTOM, false);
			SetControlText(IDC_FSMODE_WIDTH, L"");
			SetControlText(IDC_FSMODE_HEIGHT, L"");
			SetControlText(IDC_FSMODE_REFRESH, L"");
		}
	}
}

bool ATUIDialogOptionsPageDisplay::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_FSMODE_BROWSE) {
		OnDataExchange(true);

		const ATUIDialogFullScreenMode::ModeInfo modeInfo = {
			mOptions.mFullScreenWidth,
			mOptions.mFullScreenHeight,
			mOptions.mFullScreenRefreshRate
		};

		ATUIDialogFullScreenMode dlg;
		dlg.SetSelectedItem(modeInfo);
		if (dlg.ShowDialog((VDGUIHandle)mhdlg)){
			const ATUIDialogFullScreenMode::ModeInfo& newModeInfo = dlg.GetSelectedItem();
			mOptions.mFullScreenWidth = newModeInfo.mWidth;
			mOptions.mFullScreenHeight = newModeInfo.mHeight;
			mOptions.mFullScreenRefreshRate = newModeInfo.mRefresh;
			OnDataExchange(false);
		}
		return true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogOptionsPageErrors : public ATUIDialogOptionsPage {
public:
	ATUIDialogOptionsPageErrors(ATOptions& opts);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
};

ATUIDialogOptionsPageErrors::ATUIDialogOptionsPageErrors(ATOptions& opts)
: ATUIDialogOptionsPage(IDD_OPTIONS_ERRORS, opts)
{
}

bool ATUIDialogOptionsPageErrors::OnLoaded() {
	AddHelpEntry(IDC_ERRORMODE_DIALOG, L"Error mode: Dialog", L"Display a dialog with recovery options when a program fails. This is the most user friendly mode and the default.");
	AddHelpEntry(IDC_ERRORMODE_DEBUG, L"Error mode: Debug", L"Open the debugger when a program fails. This is the most convenient mode when debugging. Note that this happens anyway if the debugger is already active");
	AddHelpEntry(IDC_ERRORMODE_PAUSE, L"Error mode: Pause", L"Pause the simulation when a program fails. There is no other visual feedback when this occurs.");
	AddHelpEntry(IDC_ERRORMODE_COLDRESET, L"Error mode: Cold reset", L"Immediately restart the simulator via virtual power-off/power-on when a program fails. This is best for unattended operation.");

	return ATUIDialogOptionsPage::OnLoaded();
}

void ATUIDialogOptionsPageErrors::OnDataExchange(bool write) {
	if (write) {
		if (IsButtonChecked(IDC_ERRORMODE_DIALOG))
			mOptions.mErrorMode = kATErrorMode_Dialog;
		else if (IsButtonChecked(IDC_ERRORMODE_DEBUG))
			mOptions.mErrorMode = kATErrorMode_Debug;
		else if (IsButtonChecked(IDC_ERRORMODE_PAUSE))
			mOptions.mErrorMode = kATErrorMode_Pause;
		else if (IsButtonChecked(IDC_ERRORMODE_COLDRESET))
			mOptions.mErrorMode = kATErrorMode_ColdReset;
	} else {
		switch(mOptions.mErrorMode) {
			case kATErrorMode_Dialog:
				CheckButton(IDC_ERRORMODE_DIALOG, true);
				break;
			case kATErrorMode_Debug:
				CheckButton(IDC_ERRORMODE_DEBUG, true);
				break;
			case kATErrorMode_Pause:
				CheckButton(IDC_ERRORMODE_PAUSE, true);
				break;
			case kATErrorMode_ColdReset:
				CheckButton(IDC_ERRORMODE_COLDRESET, true);
				break;
		}
	}
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogOptionsPageStartup : public ATUIDialogOptionsPage {
public:
	ATUIDialogOptionsPageStartup(ATOptions& opts);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
};

ATUIDialogOptionsPageStartup::ATUIDialogOptionsPageStartup(ATOptions& opts)
	: ATUIDialogOptionsPage(IDD_OPTIONS_STARTUP, opts)
{
}

bool ATUIDialogOptionsPageStartup::OnLoaded() {
	AddHelpEntry(IDC_SINGLE_INSTANCE, L"Reuse program instance", L"When enabled, launching the program will attempt to reuse an existing running instance instead of starting a new one (running under the same user).");

	return ATUIDialogOptionsPage::OnLoaded();
}

void ATUIDialogOptionsPageStartup::OnDataExchange(bool write) {
	ExchangeControlValueBoolCheckbox(write, IDC_SINGLE_INSTANCE, mOptions.mbSingleInstance);
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogOptionsPageFileAssoc : public ATUIDialogOptionsPage {
public:
	ATUIDialogOptionsPageFileAssoc(ATOptions& opts);

protected:
	bool OnLoaded();
	bool OnCommand(uint32 id, uint32 extcode);
};

ATUIDialogOptionsPageFileAssoc::ATUIDialogOptionsPageFileAssoc(ATOptions& opts)
	: ATUIDialogOptionsPage(IDD_OPTIONS_FILEASSOC, opts)
{
}

bool ATUIDialogOptionsPageFileAssoc::OnLoaded() {
	if (LOBYTE(LOWORD(GetVersion())) >= 6) {
		HWND hwndItem = GetDlgItem(mhdlg, IDC_SETFILEASSOC);

		if (hwndItem)
			SendMessage(hwndItem, BCM_SETSHIELD, 0, TRUE);

		hwndItem = GetDlgItem(mhdlg, IDC_REMOVEFILEASSOC);

		if (hwndItem)
			SendMessage(hwndItem, BCM_SETSHIELD, 0, TRUE);
	}	

	return ATUIDialogOptionsPage::OnLoaded();
}

bool ATUIDialogOptionsPageFileAssoc::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_SETFILEASSOC) {
		ATUIShowDialogSetFileAssociations((VDGUIHandle)mhdlg, true);
		return true;
	} else if (id == IDC_REMOVEFILEASSOC) {
		ATUIShowDialogRemoveFileAssociations((VDGUIHandle)mhdlg, true);
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogOptions : public VDDialogFrameW32 {
public:
	ATUIDialogOptions(ATOptions& opts);

protected:
	enum {
		kTimerID_Help = 10
	};

	bool OnLoaded();
	void OnDestroy();
	bool OnTimer(uint32 id);
	bool OnCommand(uint32 id, uint32 extcode);
	void SelectPage(int index);
	void AppendRTF(VDStringA& rtf, const wchar_t *text);

	int mSelectedPage;
	vdpoint32 mPagePos;
	uint32 mLastHelpId;
	HWND mhwndHelp;

	ATUIDialogOptionsPage *mpPages[4];

	ATOptions& mOptions;
};

ATUIDialogOptions::ATUIDialogOptions(ATOptions& opts)
	: VDDialogFrameW32(IDD_OPTIONS)
	, mOptions(opts) 
{
}

bool ATUIDialogOptions::OnLoaded() {
	mhwndHelp = GetDlgItem(mhdlg, IDC_HELP_INFO);
	if (mhwndHelp) {
		SendMessage(mhwndHelp, EM_SETBKGNDCOLOR, FALSE, GetSysColor(COLOR_3DFACE));
	}

	mLastHelpId = 0;

	SetPeriodicTimer(kTimerID_Help, 1000);

	ShowControl(IDC_PAGE_AREA, false);
	mPagePos = GetControlPos(IDC_PAGE_AREA).top_left();

	mpPages[0] = new ATUIDialogOptionsPageStartup(mOptions);
	mpPages[1] = new ATUIDialogOptionsPageDisplay(mOptions);
	mpPages[2] = new ATUIDialogOptionsPageErrors(mOptions);
	mpPages[3] = new ATUIDialogOptionsPageFileAssoc(mOptions);
	mSelectedPage = -1;

	LBAddString(IDC_PAGE_LIST, L"Startup");
	LBAddString(IDC_PAGE_LIST, L"Display");
	LBAddString(IDC_PAGE_LIST, L"Error Handling");
	LBAddString(IDC_PAGE_LIST, L"File Types");

	SelectPage(0);
	LBSetSelectedIndex(IDC_PAGE_LIST, 0);

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogOptions::OnDestroy() {
	for(size_t i = 0; i < sizeof(mpPages)/sizeof(mpPages[0]); ++i) {
		if (mpPages[i]) {
			ATUIDialogOptionsPage *page = mpPages[i];
			page->Sync(true);
			page->Destroy();
			delete page;
			mpPages[i] = NULL;
		}
	}
}

bool ATUIDialogOptions::OnTimer(uint32 id) {
	if (id == kTimerID_Help) {
		HWND hwndFocus = GetFocus();

		if (mhwndHelp && hwndFocus && mSelectedPage >= 0) {
			ATUIDialogOptionsPage *page = mpPages[mSelectedPage];

			RECT r;
			if (GetWindowRect(hwndFocus, &r)) {
				POINT pt = { (r.left + r.right) >> 1, (r.top + r.bottom) >> 1 };

				if (ScreenToClient(page->GetWindowHandle(), &pt)) {
					const ATUIDialogOptionsPage::HelpEntry *he = page->GetHelpEntry(vdpoint32(pt.x, pt.y));

					if (he && he->mId != mLastHelpId) {
						mLastHelpId = he->mId;

						VDStringA s;

						s = "{\\rtf {\\b ";
						AppendRTF(s, he->mLabel.c_str());
						s += "}\\par ";
						AppendRTF(s, he->mText.c_str());
						s += "}";

						SETTEXTEX stex;
						stex.flags = ST_DEFAULT;
						stex.codepage = CP_ACP;
						SendMessage(mhwndHelp, EM_SETTEXTEX, (WPARAM)&stex, (LPARAM)s.c_str());
					}
				}
			}
		}

		return true;
	}

	return false;
}

bool ATUIDialogOptions::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_PAGE_LIST) {
		SelectPage(LBGetSelectedIndex(IDC_PAGE_LIST));
		return true;
	}

	return false;
}

void ATUIDialogOptions::SelectPage(int index) {
	if (mSelectedPage == index)
		return;

	if (mSelectedPage >= 0) {
		ATUIDialogOptionsPage *page = mpPages[mSelectedPage];
		page->Sync(true);
		page->Destroy();
		mLastHelpId = 0;
		if (mhwndHelp)
			SetWindowText(mhwndHelp, _T(""));
	}

	mSelectedPage = index;

	if (mSelectedPage >= 0) {
		ATUIDialogOptionsPage *page = mpPages[mSelectedPage];

		if (page->Create((VDGUIHandle)mhdlg)) {
			page->SetPosition(mPagePos);
			page->Show();
		}
	}
}

void ATUIDialogOptions::AppendRTF(VDStringA& rtf, const wchar_t *text) {
	const VDStringA& texta = VDTextWToA(text);
	for(VDStringA::const_iterator it = texta.begin(), itEnd = texta.end();
		it != itEnd;
		++it)
	{
		const unsigned char c = *it;

		if (c < 0x20 || c > 0x80 || c == '{' || c == '}' || c == '\\')
			rtf.append_sprintf("\\'%02x", c);
		else
			rtf += c;
	}
}

///////////////////////////////////////////////////////////////////////////

void ATUIShowDialogOptions(VDGUIHandle hParent) {
	HMODULE hmodRE20 = LoadLibraryW(L"riched20");

	ATOptions opts(g_ATOptions);
	ATUIDialogOptions dlg(opts);

	if (dlg.ShowDialog(hParent)) {
		ATOptions prevOpts(g_ATOptions);
		g_ATOptions = opts;
		g_ATOptions.mbDirty = true;
		ATOptionsSave();
		ATOptionsRunUpdateCallbacks(&prevOpts);
	}

	if (hmodRE20)
		FreeLibrary(hmodRE20);
}
