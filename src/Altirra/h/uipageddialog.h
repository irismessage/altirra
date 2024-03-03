//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2018 Avery Lee
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

#ifndef f_AT_UIPAGEDDIALOG_H
#define f_AT_UIPAGEDDIALOG_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdstl_vectorview.h>
#include <vd2/system/win32/miniwindows.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/uiproxies.h>

class VDFunctionThunk;
class ATUIPagedDialog;

class ATUIContextualHelpProvider {
public:
	struct HelpEntry {
		uint32 mId;
		uint32 mLinkedId;
		vdrect32 mArea;
		VDStringW mLabel;
		VDStringW mText;
	};

	void LinkParent(vdfunction<vdrect32(uint32)> fn);
	void SetDynamicHelpProvider(vdfunction<const HelpEntry&(const HelpEntry&)> fn);

	const HelpEntry *GetHelpEntryByPoint(const vdpoint32& pt) const;
	const HelpEntry *GetHelpEntryById(uint32 id) const;

	void AddHelpEntry(uint32 id, const wchar_t *label, const wchar_t *s);
	void LinkHelpEntry(uint32 commonHelpId, uint32 linkedId);
	void ClearHelpEntries();

	vdfunction<vdrect32(uint32)> mpfnGetControlPos;
	vdfunction<const HelpEntry&(const HelpEntry&)> mpfnGetDynamicHelp;

	typedef vdvector<HelpEntry> HelpEntries;
	HelpEntries mHelpEntries;
};

class ATUIDialogPage : public VDDialogFrameW32 {
public:
	using VDDialogFrameW32::VDDialogFrameW32;

	ATUIDialogPage(uint32 id);
	virtual ~ATUIDialogPage() = default;

	void SetParentDialog(ATUIPagedDialog *parent) { mpParentPagedDialog = parent; }

	virtual void ExchangeOtherSettings(bool write);
	virtual const char *GetPageTag() const;

protected:
	friend class ATUIPagedDialog;

	void AddHelpEntry(uint32 id, const wchar_t *label, const wchar_t *s);
	void LinkHelpEntry(uint32 commonHelpId, uint32 linkedId);
	void ClearHelpEntries();

	ATUIContextualHelpProvider mHelpProvider;

	ATUIPagedDialog *mpParentPagedDialog = nullptr;
};

class ATUIContextualHelpDialog : public VDDialogFrameW32 {
public:
	using VDDialogFrameW32::VDDialogFrameW32;

	~ATUIContextualHelpDialog();

protected:
	enum {
		kTimerID_Help = 10
	};

	virtual VDZHWND GetHelpBaseWindow() const = 0;
	virtual ATUIContextualHelpProvider *GetHelpProvider() = 0;

	void OnPreLoaded() override;
	bool OnTimer(uint32 id) override;

	void CheckFocus();
	void CheckFocus(const vdpoint32& pt);
	void ShowHelp(const ATUIContextualHelpProvider::HelpEntry *he);
	void RefreshHelp(uint32 id);

	void AppendRTF(VDStringA& rtf, const wchar_t *text);

	void InstallMouseHook();
	void UninstallMouseHook();
	VDZLRESULT OnMouseEvent(int code, VDZWPARAM wParam, VDZLPARAM lParam);

	void InstallKeyboardHook();
	void UninstallKeyboardHook();
	VDZLRESULT OnKeyboardEvent(int code, VDZWPARAM wParam, VDZLPARAM lParam);

	uintptr mLastFocus {};
	uint32 mLastHelpId {};
	vdrect32 mLastMouseHelpRect { 0, 0, 0, 0 };

	VDFunctionThunkInfo *mpMouseFuncThunk = nullptr;
	void *mpMouseHook = nullptr;

	VDFunctionThunkInfo *mpKeyboardFuncThunk = nullptr;
	void *mpKeyboardHook = nullptr;

	VDUIProxyRichEditControl mHelpView;
};

class ATUIPagedDialog : public ATUIContextualHelpDialog {
public:
	ATUIPagedDialog(uint32 id);
	~ATUIPagedDialog();

	void SetInitialPage(int index);
	void SetInitialPageByName(const char *tag);
	int GetSelectedPage() const { return mSelectedPage; }

	void SwitchToPage(const char *tag);

protected:
	VDZHWND GetHelpBaseWindow() const override final;
	ATUIContextualHelpProvider *GetHelpProvider() override final;

protected:
	VDZINT_PTR DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam);
	bool OnLoaded() override;
	void OnDataExchange(bool write) override;
	void OnDestroy() override;
	void OnDpiChanged() override;
	bool OnPreTranslate(VDZMSG& msg) override;

	virtual void OnPopulatePages() = 0;

	void PushCategory(const wchar_t *name);
	void PopCategory();
	void AddPage(const wchar_t *name, vdautoptr<ATUIDialogPage> page);
	void SelectPage(int index);

	void OnTreeSelectedItemChanged();

	struct PageTreeItem;

	int mSelectedPage {};
	int mInitialPage {};
	VDStringA mInitialPageName;

	vdfastvector<ATUIDialogPage *> mPages;
	vdfastvector<PageTreeItem *> mPageTreeItems;
	vdfastvector<uintptr> mPageTreeCategories;

	VDUIProxyListBoxControl mPageListView;
	VDUIProxyTreeViewControl mPageTreeView;
	ATUINativeWindowProxy mPageAreaView;
};

#endif
