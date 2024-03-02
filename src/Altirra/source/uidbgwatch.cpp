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
#include <commctrl.h>
#include <vd2/system/thunk.h>
#include <vd2/system/w32assist.h>
#include "debuggerexp.h"
#include "uidbgwatch.h"

struct ATWatchWindow::WatchItem : public vdrefcounted<IVDUIListViewVirtualItem> {
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

ATWatchWindow::ATWatchWindow(uint32 id)
	: ATUIDebuggerPaneWindow(id, L"")
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

void *ATWatchWindow::AsInterface(uint32 iid) {
	if (iid == IATUIDebuggerWatchPane::kTypeID)
		return static_cast<IATUIDebuggerWatchPane *>(this);

	return ATUIDebuggerPaneWindow::AsInterface(iid);
}

void ATWatchWindow::AddWatch(const char *expr) {
	vdrefptr p{new WatchItem};
	p->SetExpr(VDTextU8ToW(VDStringSpanA(expr)).c_str());
	p->Update();
	int idx = mListView.InsertVirtualItem(mListView.GetItemCount() - 1, p);
	if (idx >= 0) {
		mListView.SetSelectedIndex(idx);
		mListView.AutoSizeColumns();
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

	return ATUIDebuggerPaneWindow::WndProc(msg, wParam, lParam);
}

bool ATWatchWindow::OnCreate() {
	if (!ATUIDebuggerPaneWindow::OnCreate())
		return false;

	mhwndList = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_VISIBLE | WS_CHILD | LVS_SHOWSELALWAYS | LVS_REPORT | LVS_ALIGNLEFT | LVS_EDITLABELS | LVS_NOSORTHEADER | LVS_SINGLESEL, 0, 0, 0, 0, mhwnd, (HMENU)101, VDGetLocalModuleHandleW32(), NULL);
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
	ATUIDebuggerPaneWindow::OnDestroy();
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

						if (idx >= 0 && idx < mListView.GetItemCount() - 1) {
							mListView.DeleteItem(idx);
							mListView.SetSelectedIndex(idx);
						}
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

void ATUIDebuggerRegisterWatchPane() {
	ATRegisterUIPaneClass(kATUIPaneId_WatchN, ATUIPaneClassFactory<ATWatchWindow, ATUIPane>);
}
