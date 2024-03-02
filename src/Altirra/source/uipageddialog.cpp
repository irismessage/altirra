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

#include "stdafx.h"
#include <windows.h>
#include <vd2/system/thunk.h>
#include "uipageddialog.h"
#include "resource.h"

const ATUIDialogPage::HelpEntry *ATUIDialogPage::GetHelpEntry(const vdpoint32& pt) const {
	for(const HelpEntry& e : mHelpEntries) {
		if (!e.mArea.contains(pt))
			continue;

		if (e.mLinkedId) {
			for (const HelpEntry& e2 : mHelpEntries) {
				if (e2.mId == e.mLinkedId)
					return &e2;
			}

			return nullptr;
		}

		return &e;
	}

	return nullptr;
}

void ATUIDialogPage::ExchangeOtherSettings(bool write) {
}

void ATUIDialogPage::AddHelpEntry(uint32 id, const wchar_t *label, const wchar_t *s) {
	mHelpEntries.push_back(HelpEntry());
	HelpEntry& e = mHelpEntries.back();
	e.mId = id;
	e.mLinkedId = 0;
	e.mArea = GetControlPos(id);
	e.mLabel = label;
	e.mText = s;
}

void ATUIDialogPage::LinkHelpEntry(uint32 id, uint32 linkedId) {
	mHelpEntries.push_back(HelpEntry());
	HelpEntry& e = mHelpEntries.back();
	e.mId = id;
	e.mLinkedId = linkedId;
	e.mArea = GetControlPos(id);
}

void ATUIDialogPage::ClearHelpEntries() {
	mHelpEntries.clear();
}

///////////////////////////////////////////////////////////////////////////

struct ATUIPagedDialog::PageTreeItem final : public vdrefcounted<IVDUITreeViewVirtualItem> {
	void *AsInterface(uint32 iid) override {
		return nullptr;
	}

	void GetText(VDStringW& s) const override {
		s = mLabel;
	}

	VDStringW mLabel;
	int mPage;

	uintptr mNode;
};

ATUIPagedDialog::ATUIPagedDialog(uint32 id)
	: VDDialogFrameW32(id)
{
	mPageListView.SetOnSelectionChanged([this](int v) { SelectPage(v); });
	mPageTreeView.SetOnItemSelectionChanged([this] { OnTreeSelectedItemChanged(); });
}

ATUIPagedDialog::~ATUIPagedDialog() {
	UninstallMouseHook();
}

void ATUIPagedDialog::SetInitialPage(int index) {
	mInitialPage = index;
}

bool ATUIPagedDialog::OnLoaded() {
	AddProxy(&mHelpView, IDC_HELP_INFO);
	AddProxy(&mPageListView, IDC_PAGE_LIST);
	AddProxy(&mPageTreeView, IDC_PAGE_TREE);

	mPageAreaView = GetControl(IDC_PAGE_AREA);

	mHelpView.SetReadOnlyBackground();

	mLastHelpId = 0;

	SetPeriodicTimer(kTimerID_Help, 1000);

	mPageAreaView.Hide();

	mSelectedPage = -1;
	
	OnPopulatePages();

	OnDataExchange(false);

	SelectPage(mInitialPage);

	if (mPageListView.IsValid())
		mPageListView.SetSelection(mInitialPage);

	if (mPageTreeView.IsValid() && (unsigned)mInitialPage < mPageTreeItems.size())
		mPageTreeView.SelectNode(mPageTreeItems[mInitialPage]->mNode);

	InstallMouseHook();
	return VDDialogFrameW32::OnLoaded();
}

void ATUIPagedDialog::OnDataExchange(bool write) {
	for(auto *page : mPages) {
		page->Sync(true);
		page->ExchangeOtherSettings(write);
	}
}

void ATUIPagedDialog::OnDestroy() {
	UninstallMouseHook();

	for(auto *pti : mPageTreeItems) {
		pti->Release();
	}

	mPageTreeItems.clear();

	for(auto *page : mPages) {
		page->Destroy();
		delete page;
	}

	mPages.clear();
}

bool ATUIPagedDialog::OnTimer(uint32 id) {
	if (id != kTimerID_Help)
		return false;

	POINT pt {};
	GetCursorPos(&pt);

	CheckFocus({ pt.x, pt.y });
	return true;
}

void ATUIPagedDialog::OnDpiChanged() {
	for (auto *page : mPages) {
		page->UpdateChildDpi();
	}
}

void ATUIPagedDialog::CheckFocus(const vdpoint32& pt) {
	if (mSelectedPage < 0 || mLastHelpRect.contains(pt))
		return;

	// check if there is another window in the way, particularly a combo popup; bypass checks if so
	HWND hwndCursorOwner = ChildWindowFromPointEx(GetDesktopWindow(), POINT { pt.x, pt.y }, CWP_SKIPINVISIBLE);
	if (hwndCursorOwner && hwndCursorOwner != mhwnd)
		return;

	ATUIDialogPage *page = mPages[mSelectedPage];

	const vdpoint32 pagePt = page->TransformScreenToClient(pt);

	const ATUIDialogPage::HelpEntry *he = page->GetHelpEntry(pagePt);
	if (he && he->mId != mLastHelpId) {
		mLastHelpId = he->mId;
		mLastHelpRect = page->TransformClientToScreen(he->mArea);

		VDStringA s;

		s = "{\\rtf{\\fonttbl{\\f0\\fnil\\fcharset0 MS Shell Dlg;}}\\f0\\sa90\\fs16{\\b ";
		AppendRTF(s, he->mLabel.c_str());
		s += "}\\par ";
		AppendRTF(s, he->mText.c_str());
		s += "}";

		mHelpView.SetTextRTF(s.c_str());
	}
}

void ATUIPagedDialog::PushCategory(const wchar_t *name) {
	if (!mPageTreeView.IsValid())
		return;

	vdrefptr<PageTreeItem> pti(new PageTreeItem);
	pti->mLabel = name;
	pti->mPage = -1;
	const uintptr parentNode = mPageTreeCategories.empty() ? mPageTreeView.kNodeRoot : mPageTreeCategories.back();
	pti->mNode = mPageTreeView.AddVirtualItem(parentNode, mPageTreeView.kNodeLast, pti);

	mPageTreeCategories.push_back(pti->mNode);
}

void ATUIPagedDialog::PopCategory() {
	if (!mPageTreeView.IsValid())
		return;

	mPageTreeView.ExpandNode(mPageTreeCategories.back(), true);
	mPageTreeCategories.pop_back();
}

void ATUIPagedDialog::AddPage(const wchar_t *name, vdautoptr<ATUIDialogPage> page) {
	if (mPageListView.IsValid())
		mPageListView.AddItem(name);

	if (mPageTreeView.IsValid()) {
		vdrefptr<PageTreeItem> pti(new PageTreeItem);
		pti->mLabel = name;
		pti->mPage = (int)mPages.size();

		const uintptr parentNode = mPageTreeCategories.empty() ? mPageTreeView.kNodeRoot : mPageTreeCategories.back();
		pti->mNode = mPageTreeView.AddVirtualItem(parentNode, mPageTreeView.kNodeLast, pti);

		mPageTreeItems.push_back(pti);
		pti.release();
	}

	mPages.push_back(page);
	page.release();
}

void ATUIPagedDialog::SelectPage(int index) {
	if (mSelectedPage == index)
		return;

	if (mSelectedPage >= 0) {
		ATUIDialogPage *page = mPages[mSelectedPage];
		page->Sync(true);

		mResizer.Remove(page->GetWindowHandle());
		page->Destroy();
		mLastHelpId = 0;
		mLastHelpRect = { 0, 0, 0, 0 };
		mHelpView.SetText(L"");
	}

	mSelectedPage = index;

	if (mSelectedPage >= 0) {
		ATUIDialogPage *page = mPages[mSelectedPage];

		if (page->Create((VDGUIHandle)mhdlg)) {
			const auto& r = mPageAreaView.GetArea();

			page->SetArea(r, false);
			mResizer.AddAlias(page->GetWindowHandle(), mPageAreaView.GetWindowHandle(), mResizer.kTL);
			page->Show();
		}
	}
}

void ATUIPagedDialog::AppendRTF(VDStringA& rtf, const wchar_t *text) {
	const VDStringA& texta = VDTextWToA(text);
	for (VDStringA::const_iterator it = texta.begin(), itEnd = texta.end();
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

void ATUIPagedDialog::InstallMouseHook() {
	if (!mpMouseFuncThunk) {
		mpMouseFuncThunk = VDCreateFunctionThunkFromMethod(this, &ATUIPagedDialog::OnMouseEvent, true);

		if (!mpMouseFuncThunk)
			return;
	}

	if (!mpMouseHook)
		mpMouseHook = SetWindowsHookEx(WH_MOUSE, VDGetThunkFunction<HOOKPROC>(mpMouseFuncThunk), nullptr, GetCurrentThreadId());
}

void ATUIPagedDialog::UninstallMouseHook() {
	if (mpMouseHook) {
		UnhookWindowsHookEx((HHOOK)mpMouseHook);
		mpMouseHook = nullptr;
	}

	if (mpMouseFuncThunk) {
		VDDestroyFunctionThunk(mpMouseFuncThunk);
		mpMouseFuncThunk = nullptr;
	}
}

VDZLRESULT ATUIPagedDialog::OnMouseEvent(int code, VDZWPARAM wParam, VDZLPARAM lParam) {
	if (code == HC_ACTION && wParam == WM_MOUSEMOVE) {
		const MOUSEHOOKSTRUCT& mhs = *(const MOUSEHOOKSTRUCT *)lParam;

		CheckFocus({ mhs.pt.x, mhs.pt.y });
	}

	return CallNextHookEx((HHOOK)mpMouseHook, code, wParam, lParam);
}

void ATUIPagedDialog::OnTreeSelectedItemChanged() {
	PageTreeItem *pti = mPageTreeView.GetSelectedVirtualItem<PageTreeItem>();

	if (pti && pti->mPage >= 0)
		SelectPage(pti->mPage);
}
