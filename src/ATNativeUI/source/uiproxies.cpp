#include <stdafx.h>
#include <windows.h>
#include <richedit.h>
#include <commctrl.h>

#include <vd2/system/w32assist.h>
#include <vd2/system/thunk.h>
#include <vd2/system/vdstl.h>
#include <vd2/Dita/accel.h>
#include <at/atnativeui/uiproxies.h>

///////////////////////////////////////////////////////////////////////////////

VDUIProxyControl::VDUIProxyControl()
	: mhwnd(NULL)
	, mRedrawInhibitCount(0)
{
}

void VDUIProxyControl::Attach(VDZHWND hwnd) {
	VDASSERT(IsWindow(hwnd));
	mhwnd = hwnd;
}

void VDUIProxyControl::Detach() {
	mhwnd = NULL;
}

vdrect32 VDUIProxyControl::GetArea() const {
	if (mhwnd) {
		RECT r;
		if (GetWindowRect(mhwnd, &r)) {
			HWND hwndParent = GetAncestor(mhwnd, GA_PARENT);

			if (hwndParent && MapWindowPoints(NULL, hwndParent, (LPPOINT)&r, 2))
				return vdrect32(r.left, r.top, r.right, r.bottom);
		}
	}

	return vdrect32(0, 0, 0, 0);
}

void VDUIProxyControl::SetArea(const vdrect32& r) {
	if (mhwnd)
		SetWindowPos(mhwnd, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void VDUIProxyControl::SetEnabled(bool enabled) {
	if (mhwnd)
		EnableWindow(mhwnd, enabled);
}

void VDUIProxyControl::SetRedraw(bool redraw) {
	if (redraw) {
		if (!--mRedrawInhibitCount) {
			if (mhwnd)
				SendMessage(mhwnd, WM_SETREDRAW, TRUE, 0);
		}
	} else {
		if (!mRedrawInhibitCount++) {
			if (mhwnd)
				SendMessage(mhwnd, WM_SETREDRAW, FALSE, 0);
		}
	}
}

VDZLRESULT VDUIProxyControl::On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	return 0;
}

VDZLRESULT VDUIProxyControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

void VDUIProxyMessageDispatcherW32::AddControl(VDUIProxyControl *control) {
	VDZHWND hwnd = control->GetHandle();
	size_t hc = Hash(hwnd);

	mHashTable[hc].push_back(control);
}

void VDUIProxyMessageDispatcherW32::RemoveControl(VDZHWND hwnd) {
	size_t hc = Hash(hwnd);
	HashChain& hchain = mHashTable[hc];

	HashChain::iterator it(hchain.begin()), itEnd(hchain.end());
	for(; it != itEnd; ++it) {
		VDUIProxyControl *control = *it;

		if (control->GetHandle() == hwnd) {
			hchain.erase(control);
			break;
		}
	}

}

void VDUIProxyMessageDispatcherW32::RemoveAllControls(bool detach) {
	for(int i=0; i<kHashTableSize; ++i) {
		HashChain& hchain = mHashTable[i];

		if (detach) {
			HashChain::iterator it(hchain.begin()), itEnd(hchain.end());
			for(; it != itEnd; ++it) {
				VDUIProxyControl *control = *it;

				control->Detach();
			}
		}

		hchain.clear();
	}
}

VDZLRESULT VDUIProxyMessageDispatcherW32::Dispatch_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	VDUIProxyControl *control = GetControl((HWND)lParam);

	if (control)
		return control->On_WM_COMMAND(wParam, lParam);

	return 0;
}

VDZLRESULT VDUIProxyMessageDispatcherW32::Dispatch_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	const NMHDR *hdr = (const NMHDR *)lParam;
	VDUIProxyControl *control = GetControl(hdr->hwndFrom);

	if (control)
		return control->On_WM_NOTIFY(wParam, lParam);

	return 0;
}

size_t VDUIProxyMessageDispatcherW32::Hash(VDZHWND hwnd) const {
	return (size_t)hwnd % (size_t)kHashTableSize;
}

VDUIProxyControl *VDUIProxyMessageDispatcherW32::GetControl(VDZHWND hwnd) {
	size_t hc = Hash(hwnd);
	HashChain& hchain = mHashTable[hc];

	HashChain::iterator it(hchain.begin()), itEnd(hchain.end());
	for(; it != itEnd; ++it) {
		VDUIProxyControl *control = *it;

		if (control->GetHandle() == hwnd)
			return control;
	}

	return NULL;
}

///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////

VDUIProxyListView::VDUIProxyListView()
	: mChangeNotificationLocks(0)
	, mNextTextIndex(0)
{
}

void VDUIProxyListView::Detach() {
	Clear();
	VDUIProxyControl::Detach();
}

void VDUIProxyListView::AutoSizeColumns(bool expandlast) {
	const int colCount = GetColumnCount();

	int colCacheCount = (int)mColumnWidthCache.size();
	while(colCacheCount < colCount) {
		SendMessage(mhwnd, LVM_SETCOLUMNWIDTH, colCacheCount, LVSCW_AUTOSIZE_USEHEADER);
		mColumnWidthCache.push_back((int)SendMessage(mhwnd, LVM_GETCOLUMNWIDTH, colCacheCount, 0));
		++colCacheCount;
	}

	int totalWidth = 0;
	for(int col=0; col<colCount; ++col) {
		const int hdrWidth = mColumnWidthCache[col];

		SendMessage(mhwnd, LVM_SETCOLUMNWIDTH, col, LVSCW_AUTOSIZE);
		int dataWidth = (int)SendMessage(mhwnd, LVM_GETCOLUMNWIDTH, col, 0);

		if (dataWidth < hdrWidth)
			dataWidth = hdrWidth;

		if (expandlast && col == colCount-1) {
			RECT r;
			if (GetClientRect(mhwnd, &r)) {
				int extraWidth = r.right - totalWidth;

				if (dataWidth < extraWidth)
					dataWidth = extraWidth;
			}
		}

		SendMessage(mhwnd, LVM_SETCOLUMNWIDTH, col, dataWidth);

		totalWidth += dataWidth;
	}
}

void VDUIProxyListView::Clear() {
	if (mhwnd)
		SendMessage(mhwnd, LVM_DELETEALLITEMS, 0, 0);
}

void VDUIProxyListView::ClearExtraColumns() {
	if (!mhwnd)
		return;

	uint32 n = GetColumnCount();
	for(uint32 i=n; i > 1; --i)
		ListView_DeleteColumn(mhwnd, i - 1);

	if (!mColumnWidthCache.empty())
		mColumnWidthCache.resize(1);
}

void VDUIProxyListView::DeleteItem(int index) {
	if (index >= 0)
		SendMessage(mhwnd, LVM_DELETEITEM, index, 0);
}

int VDUIProxyListView::GetColumnCount() const {
	HWND hwndHeader = (HWND)SendMessage(mhwnd, LVM_GETHEADER, 0, 0);
	if (!hwndHeader)
		return 0;

	return (int)SendMessage(hwndHeader, HDM_GETITEMCOUNT, 0, 0);
}

int VDUIProxyListView::GetItemCount() const {
	return (int)SendMessage(mhwnd, LVM_GETITEMCOUNT, 0, 0);
}

int VDUIProxyListView::GetSelectedIndex() const {
	return ListView_GetNextItem(mhwnd, -1, LVNI_SELECTED);
}

void VDUIProxyListView::SetSelectedIndex(int index) {
	ListView_SetItemState(mhwnd, index, LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
}

IVDUIListViewVirtualItem *VDUIProxyListView::GetSelectedItem() const {
	int idx = GetSelectedIndex();

	if (idx < 0)
		return NULL;

	return GetVirtualItem(idx);
}

void VDUIProxyListView::GetSelectedIndices(vdfastvector<int>& indices) const {
	int idx = -1;

	indices.clear();
	for(;;) {
		idx = ListView_GetNextItem(mhwnd, idx, LVNI_SELECTED);
		if (idx < 0)
			return;

		indices.push_back(idx);
	}
}

void VDUIProxyListView::SetFullRowSelectEnabled(bool enabled) {
	ListView_SetExtendedListViewStyleEx(mhwnd, LVS_EX_FULLROWSELECT, enabled ? LVS_EX_FULLROWSELECT : 0);
}

void VDUIProxyListView::SetGridLinesEnabled(bool enabled) {
	ListView_SetExtendedListViewStyleEx(mhwnd, LVS_EX_GRIDLINES, enabled ? LVS_EX_GRIDLINES : 0);
}

void VDUIProxyListView::SetItemCheckboxesEnabled(bool enabled) {
	ListView_SetExtendedListViewStyleEx(mhwnd, LVS_EX_CHECKBOXES, enabled ? LVS_EX_CHECKBOXES : 0);
}

void VDUIProxyListView::EnsureItemVisible(int index) {
	ListView_EnsureVisible(mhwnd, index, FALSE);
}

int VDUIProxyListView::GetVisibleTopIndex() {
	return ListView_GetTopIndex(mhwnd);
}

void VDUIProxyListView::SetVisibleTopIndex(int index) {
	int n = ListView_GetItemCount(mhwnd);
	if (n > 0) {
		ListView_EnsureVisible(mhwnd, n - 1, FALSE);
		ListView_EnsureVisible(mhwnd, index, FALSE);
	}
}

IVDUIListViewVirtualItem *VDUIProxyListView::GetSelectedVirtualItem() const {
	int index = GetSelectedIndex();
	if (index < 0)
		return NULL;

	return GetVirtualItem(index);
}

IVDUIListViewVirtualItem *VDUIProxyListView::GetVirtualItem(int index) const {
	if (index < 0)
		return NULL;

	LVITEMW itemw={};
	itemw.mask = LVIF_PARAM;
	itemw.iItem = index;
	itemw.iSubItem = 0;
	if (SendMessage(mhwnd, LVM_GETITEMW, 0, (LPARAM)&itemw))
		return (IVDUIListViewVirtualItem *)itemw.lParam;

	return NULL;
}

void VDUIProxyListView::InsertColumn(int index, const wchar_t *label, int width, bool rightAligned) {
	VDASSERT(index || !rightAligned);

	LVCOLUMNW colw = {};

	colw.mask		= LVCF_FMT | LVCF_TEXT | LVCF_WIDTH;
	colw.fmt		= rightAligned ? LVCFMT_RIGHT : LVCFMT_LEFT;
	colw.cx			= width;
	colw.pszText	= (LPWSTR)label;

	SendMessageW(mhwnd, LVM_INSERTCOLUMNW, (WPARAM)index, (LPARAM)&colw);
}

int VDUIProxyListView::InsertItem(int item, const wchar_t *text) {
	if (item < 0)
		item = 0x7FFFFFFF;

	LVITEMW itemw = {};

	itemw.mask		= LVIF_TEXT;
	itemw.iItem		= item;
	itemw.pszText	= (LPWSTR)text;

	return (int)SendMessageW(mhwnd, LVM_INSERTITEMW, 0, (LPARAM)&itemw);
}

int VDUIProxyListView::InsertVirtualItem(int item, IVDUIListViewVirtualItem *lvvi) {
	int index;

	if (item < 0)
		item = 0x7FFFFFFF;

	++mChangeNotificationLocks;

	LVITEMW itemw = {};

	itemw.mask		= LVIF_TEXT | LVIF_PARAM;
	itemw.iItem		= item;
	itemw.pszText	= LPSTR_TEXTCALLBACKW;
	itemw.lParam	= (LPARAM)lvvi;

	index = (int)SendMessageW(mhwnd, LVM_INSERTITEMW, 0, (LPARAM)&itemw);

	--mChangeNotificationLocks;

	if (index >= 0)
		lvvi->AddRef();

	return index;
}

void VDUIProxyListView::RefreshItem(int item) {
	SendMessage(mhwnd, LVM_REDRAWITEMS, item, item);
}

void VDUIProxyListView::RefreshAllItems() {
	int n = GetItemCount();

	if (n)
		SendMessage(mhwnd, LVM_REDRAWITEMS, 0, n - 1);
}

void VDUIProxyListView::EditItemLabel(int item) {
	ListView_EditLabel(mhwnd, item);
}

void VDUIProxyListView::GetItemText(int item, VDStringW& s) const {
	LVITEMW itemw;
	wchar_t buf[512];

	itemw.iSubItem = 0;
	itemw.cchTextMax = 511;
	itemw.pszText = buf;
	buf[0] = 0;
	SendMessageW(mhwnd, LVM_GETITEMTEXTW, item, (LPARAM)&itemw);

	s = buf;
}

void VDUIProxyListView::SetItemText(int item, int subitem, const wchar_t *text) {
	LVITEMW itemw = {};

	itemw.mask		= LVIF_TEXT;
	itemw.iItem		= item;
	itemw.iSubItem	= subitem;
	itemw.pszText	= (LPWSTR)text;

	SendMessageW(mhwnd, LVM_SETITEMW, 0, (LPARAM)&itemw);
}

bool VDUIProxyListView::IsItemChecked(int item) {
	return ListView_GetCheckState(mhwnd, item) != 0;
}

void VDUIProxyListView::SetItemChecked(int item, bool checked) {
	ListView_SetCheckState(mhwnd, item, checked);
}

void VDUIProxyListView::SetItemCheckedVisible(int item, bool checked) {
	if (!mhwnd)
		return;

	ListView_SetItemState(mhwnd, item, INDEXTOSTATEIMAGEMASK(0), LVIS_STATEIMAGEMASK);
}

void VDUIProxyListView::SetItemImage(int item, uint32 imageIndex) {
	LVITEMW itemw = {};

	itemw.mask		= LVIF_IMAGE;
	itemw.iItem		= item;
	itemw.iSubItem	= 0;
	itemw.iImage	= imageIndex;

	SendMessageW(mhwnd, LVM_SETITEMW, 0, (LPARAM)&itemw);
}

bool VDUIProxyListView::GetItemScreenRect(int item, vdrect32& r) const {
	r.set(0, 0, 0, 0);

	if (!mhwnd)
		return false;

	RECT nr = {LVIR_BOUNDS};
	if (!SendMessage(mhwnd, LVM_GETITEMRECT, (WPARAM)item, (LPARAM)&nr))
		return false;

	MapWindowPoints(mhwnd, NULL, (LPPOINT)&nr, 2);

	r.set(nr.left, nr.top, nr.right, nr.bottom);
	return true;
}

void VDUIProxyListView::Sort(IVDUIListViewVirtualComparer& comparer) {
	ListView_SortItems(mhwnd, SortAdapter, (LPARAM)&comparer);
}

int VDZCALLBACK VDUIProxyListView::SortAdapter(LPARAM x, LPARAM y, LPARAM cookie) {
	return ((IVDUIListViewVirtualComparer *)cookie)->Compare((IVDUIListViewVirtualItem *)x, (IVDUIListViewVirtualItem *)y);
}

VDZLRESULT VDUIProxyListView::On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	const NMHDR *hdr = (const NMHDR *)lParam;

	switch(hdr->code) {
		case LVN_GETDISPINFOA:
			{
				NMLVDISPINFOA *dispa = (NMLVDISPINFOA *)hdr;
				IVDUIListViewVirtualItem *lvvi = (IVDUIListViewVirtualItem *)dispa->item.lParam;

				if (dispa->item.mask & LVIF_TEXT) {
					mTextW[0].clear();
					if (lvvi)
						lvvi->GetText(dispa->item.iSubItem, mTextW[0]);
					mTextA[mNextTextIndex] = VDTextWToA(mTextW[0]);
					dispa->item.pszText = (LPSTR)mTextA[mNextTextIndex].c_str();

					if (++mNextTextIndex >= 3)
						mNextTextIndex = 0;
				}
			}
			break;

		case LVN_GETDISPINFOW:
			{
				NMLVDISPINFOW *dispw = (NMLVDISPINFOW *)hdr;
				IVDUIListViewVirtualItem *lvvi = (IVDUIListViewVirtualItem *)dispw->item.lParam;

				if (dispw->item.mask & LVIF_TEXT) {
					mTextW[mNextTextIndex].clear();
					if (lvvi)
						lvvi->GetText(dispw->item.iSubItem, mTextW[mNextTextIndex]);
					dispw->item.pszText = (LPWSTR)mTextW[mNextTextIndex].c_str();

					if (++mNextTextIndex >= 3)
						mNextTextIndex = 0;
				}
			}
			break;

		case LVN_DELETEITEM:
			{
				const NMLISTVIEW *nmlv = (const NMLISTVIEW *)hdr;
				IVDUIListViewVirtualItem *lvvi = (IVDUIListViewVirtualItem *)nmlv->lParam;

				if (lvvi)
					lvvi->Release();
			}
			break;

		case LVN_COLUMNCLICK:
			{
				const NMLISTVIEW *nmlv = (const NMLISTVIEW *)hdr;

				mEventColumnClicked.Raise(this, nmlv->iSubItem);
			}
			break;

		case LVN_ITEMCHANGING:
			if (!mChangeNotificationLocks) {
				const NMLISTVIEW *nmlv = (const NMLISTVIEW *)hdr;

				if (nmlv->uChanged & LVIF_STATE) {
					uint32 deltaState = nmlv->uOldState ^ nmlv->uNewState;

					if (deltaState & LVIS_STATEIMAGEMASK) {
						VDASSERT(nmlv->iItem >= 0);

						CheckedChangingEvent event;
						event.mIndex = nmlv->iItem;
						event.mbNewVisible = (nmlv->uNewState & LVIS_STATEIMAGEMASK) != 0;
						event.mbNewChecked = (nmlv->uNewState & 0x2000) != 0;
						event.mbAllowChange = true;
						mEventItemCheckedChanging.Raise(this, &event);

						if (!event.mbAllowChange)
							return TRUE;
					}
				}
			}
			break;

		case LVN_ITEMCHANGED:
			if (!mChangeNotificationLocks) {
				const NMLISTVIEW *nmlv = (const NMLISTVIEW *)hdr;

				if (nmlv->uChanged & LVIF_STATE) {
					uint32 deltaState = nmlv->uOldState ^ nmlv->uNewState;

					if (deltaState & LVIS_SELECTED) {
						int selIndex = ListView_GetNextItem(mhwnd, -1, LVNI_ALL | LVNI_SELECTED);

						mEventItemSelectionChanged.Raise(this, selIndex);
					}

					if (deltaState & LVIS_STATEIMAGEMASK) {
						VDASSERT(nmlv->iItem >= 0);
						mEventItemCheckedChanged.Raise(this, nmlv->iItem);
					}
				}
			}
			break;

		case LVN_ENDLABELEDITA:
			{
				const NMLVDISPINFOA *di = (const NMLVDISPINFOA *)hdr;
				if (di->item.pszText) {
					const VDStringW label(VDTextAToW(di->item.pszText));
					LabelChangedEvent event = {
						true,
						di->item.iItem,
						label.c_str()
					};

					mEventItemLabelEdited.Raise(this, &event);

					if (!event.mbAllowEdit)
						return FALSE;
				}
			}
			return TRUE;

		case LVN_ENDLABELEDITW:
			{
				const NMLVDISPINFOW *di = (const NMLVDISPINFOW *)hdr;

				LabelChangedEvent event = {
					true,
					di->item.iItem,
					di->item.pszText
				};

				mEventItemLabelEdited.Raise(this, &event);

				if (!event.mbAllowEdit)
					return FALSE;
			}
			return TRUE;

		case LVN_BEGINDRAG:
			{
				const NMLISTVIEW *nmlv = (const NMLISTVIEW *)hdr;

				mEventItemBeginDrag.Raise(this, nmlv->iItem);
			}
			return 0;

		case LVN_BEGINRDRAG:
			{
				const NMLISTVIEW *nmlv = (const NMLISTVIEW *)hdr;

				mEventItemBeginRDrag.Raise(this, nmlv->iItem);
			}
			return 0;

		case NM_RCLICK:
			{
				const NMITEMACTIVATE *nmia = (const NMITEMACTIVATE *)hdr;

				ContextMenuEvent event;
				event.mIndex = nmia->iItem;

				POINT pt = nmia->ptAction;
				ClientToScreen(mhwnd, &pt);
				event.mX = pt.x;
				event.mY = pt.y;
				mEventItemContextMenu.Raise(this, event);
			}
			return 0;

		case NM_DBLCLK:
			{
				int selIndex = ListView_GetNextItem(mhwnd, -1, LVNI_ALL | LVNI_SELECTED);

				mEventItemDoubleClicked.Raise(this, selIndex);
			}
			return 0;
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////

VDUIProxyHotKeyControl::VDUIProxyHotKeyControl() {
}

VDUIProxyHotKeyControl::~VDUIProxyHotKeyControl() {
}

bool VDUIProxyHotKeyControl::GetAccelerator(VDUIAccelerator& accel) const {
	if (!mhwnd)
		return false;

	uint32 v = (uint32)SendMessage(mhwnd, HKM_GETHOTKEY, 0, 0);

	accel.mVirtKey = (uint8)v;
	accel.mModifiers = 0;
	
	const uint8 mods = (uint8)(v >> 8);
	if (mods & HOTKEYF_SHIFT)
		accel.mModifiers |= VDUIAccelerator::kModShift;

	if (mods & HOTKEYF_CONTROL)
		accel.mModifiers |= VDUIAccelerator::kModCtrl;

	if (mods & HOTKEYF_ALT)
		accel.mModifiers |= VDUIAccelerator::kModAlt;

	if (mods & HOTKEYF_EXT)
		accel.mModifiers |= VDUIAccelerator::kModExtended;

	return true;
}

void VDUIProxyHotKeyControl::SetAccelerator(const VDUIAccelerator& accel) {
	uint32 mods = 0;

	if (accel.mModifiers & VDUIAccelerator::kModShift)
		mods |= HOTKEYF_SHIFT;

	if (accel.mModifiers & VDUIAccelerator::kModCtrl)
		mods |= HOTKEYF_CONTROL;

	if (accel.mModifiers & VDUIAccelerator::kModAlt)
		mods |= HOTKEYF_ALT;

	if (accel.mModifiers & VDUIAccelerator::kModExtended)
		mods |= HOTKEYF_EXT;

	SendMessage(mhwnd, HKM_SETHOTKEY, accel.mVirtKey + (mods << 8), 0);
}

VDZLRESULT VDUIProxyHotKeyControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (HIWORD(wParam) == EN_CHANGE) {
		VDUIAccelerator accel;
		GetAccelerator(accel);
		mEventHotKeyChanged.Raise(this, accel);
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////

VDUIProxyTabControl::VDUIProxyTabControl() {
}

VDUIProxyTabControl::~VDUIProxyTabControl() {
}

void VDUIProxyTabControl::AddItem(const wchar_t *s) {
	if (!mhwnd)
		return;

	int n = TabCtrl_GetItemCount(mhwnd);
	TCITEMW tciw = { TCIF_TEXT };

	tciw.pszText = (LPWSTR)s;

	SendMessageW(mhwnd, TCM_INSERTITEMW, n, (LPARAM)&tciw);
}

void VDUIProxyTabControl::DeleteItem(int index) {
	if (mhwnd)
		SendMessage(mhwnd, TCM_DELETEITEM, index, 0);
}

vdsize32 VDUIProxyTabControl::GetControlSizeForContent(const vdsize32& sz) const {
	if (!mhwnd)
		return vdsize32(0, 0);

	RECT r = { 0, 0, sz.w, sz.h };
	TabCtrl_AdjustRect(mhwnd, TRUE, &r);

	return vdsize32(r.right - r.left, r.bottom - r.top);
}

vdrect32 VDUIProxyTabControl::GetContentArea() const {
	if (!mhwnd)
		return vdrect32(0, 0, 0, 0);

	RECT r = {0};
	GetWindowRect(mhwnd, &r);

	HWND hwndParent = GetParent(mhwnd);
	if (hwndParent)
		MapWindowPoints(NULL, hwndParent, (LPPOINT)&r, 2);

	TabCtrl_AdjustRect(mhwnd, FALSE, &r);

	return vdrect32(r.left, r.top, r.right, r.bottom);
}

int VDUIProxyTabControl::GetSelection() const {
	if (!mhwnd)
		return -1;

	return (int)SendMessage(mhwnd, TCM_GETCURSEL, 0, 0);
}

void VDUIProxyTabControl::SetSelection(int index) {
	if (mhwnd)
		SendMessage(mhwnd, TCM_SETCURSEL, index, 0);
}

VDZLRESULT VDUIProxyTabControl::On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (((const NMHDR *)lParam)->code == TCN_SELCHANGE) {
		mSelectionChanged.Raise(this, GetSelection());
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////

VDUIProxyListBoxControl::VDUIProxyListBoxControl() {
}

VDUIProxyListBoxControl::~VDUIProxyListBoxControl() {
}

void VDUIProxyListBoxControl::Clear() {
	if (!mhwnd)
		return;

	SendMessage(mhwnd, LB_RESETCONTENT, 0, 0);
}

int VDUIProxyListBoxControl::AddItem(const wchar_t *s, uintptr_t cookie) {
	if (!mhwnd)
		return -1;

	int idx;
	idx = (int)SendMessageW(mhwnd, LB_ADDSTRING, 0, (LPARAM)s);

	if (idx >= 0)
		SendMessage(mhwnd, LB_SETITEMDATA, idx, (LPARAM)cookie);

	return idx;
}

void VDUIProxyListBoxControl::SetItemText(int index, const wchar_t *s) {
	if (!mhwnd || index < 0)
		return;

	int n = (int)SendMessage(mhwnd, LB_GETCOUNT, 0, 0);
	if (index >= n)
		return;

	uintptr_t itemData = (uintptr_t)SendMessage(mhwnd, LB_GETITEMDATA, index, 0);
	bool selected = (GetSelection() == index);

	++mSuppressNotificationCount;
	SendMessage(mhwnd, LB_DELETESTRING, index, 0);
	int newIdx = (int)SendMessageW(mhwnd, LB_INSERTSTRING, 0, (LPARAM)s);
	if (newIdx >= 0) {
		SendMessage(mhwnd, LB_SETITEMDATA, newIdx, (LPARAM)itemData);

		if (selected)
			SetSelection(newIdx);
	}
	--mSuppressNotificationCount;
}

uintptr VDUIProxyListBoxControl::GetItemData(int index) const {
	if (index < 0 || !mhwnd)
		return 0;

	return SendMessage(mhwnd, LB_GETITEMDATA, index, 0);
}

int VDUIProxyListBoxControl::GetSelection() const {
	if (!mhwnd)
		return -1;

	return (int)SendMessage(mhwnd, LB_GETCURSEL, 0, 0);
}

void VDUIProxyListBoxControl::SetSelection(int index) {
	if (mhwnd)
		SendMessage(mhwnd, LB_SETCURSEL, index, 0);
}

void VDUIProxyListBoxControl::MakeSelectionVisible() {
	if (!mhwnd)
		return;

	int idx = (int)SendMessage(mhwnd, LB_GETCURSEL, 0, 0);

	if (idx >= 0)
		SendMessage(mhwnd, LB_SETCURSEL, idx, 0);
}

void VDUIProxyListBoxControl::SetTabStops(const int *units, uint32 n) {
	if (!mhwnd)
		return;

	vdfastvector<INT> v(n);

	for(uint32 i=0; i<n; ++i)
		v[i] = units[i];

	SendMessage(mhwnd, LB_SETTABSTOPS, n, (LPARAM)v.data());
}

VDZLRESULT VDUIProxyListBoxControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (mSuppressNotificationCount)
		return 0;

	if (HIWORD(wParam) == LBN_SELCHANGE) {
		mSelectionChanged.Raise(this, GetSelection());
	} else if (HIWORD(wParam) == LBN_DBLCLK) {
		int sel = GetSelection();

		if (sel >= 0)
			mEventItemDoubleClicked.Raise(this, GetSelection());
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////

VDUIProxyComboBoxControl::VDUIProxyComboBoxControl() {
}

VDUIProxyComboBoxControl::~VDUIProxyComboBoxControl() {
}

void VDUIProxyComboBoxControl::AddItem(const wchar_t *s) {
	if (!mhwnd)
		return;

	SendMessageW(mhwnd, CB_ADDSTRING, 0, (LPARAM)s);
}

int VDUIProxyComboBoxControl::GetSelection() const {
	if (!mhwnd)
		return -1;

	return (int)SendMessage(mhwnd, CB_GETCURSEL, 0, 0);
}

void VDUIProxyComboBoxControl::SetSelection(int index) {
	if (mhwnd)
		SendMessage(mhwnd, CB_SETCURSEL, index, 0);
}

VDZLRESULT VDUIProxyComboBoxControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (HIWORD(wParam) == CBN_SELCHANGE) {
		mSelectionChanged.Raise(this, GetSelection());
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////

const VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::kNodeRoot = (NodeRef)TVI_ROOT;
const VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::kNodeFirst = (NodeRef)TVI_FIRST;
const VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::kNodeLast = (NodeRef)TVI_LAST;

VDUIProxyTreeViewControl::VDUIProxyTreeViewControl()
	: mNextTextIndex(0)
	, mpEditWndProcThunk(NULL)
	, mhfontBold(NULL)
	, mbCreatedBoldFont(false)
{
}

VDUIProxyTreeViewControl::~VDUIProxyTreeViewControl() {
	if (mpEditWndProcThunk) {
		SendMessageW(mhwnd, TVM_ENDEDITLABELNOW, TRUE, 0);

		VDDestroyFunctionThunk(mpEditWndProcThunk);
		mpEditWndProcThunk = NULL;
	}
}

IVDUITreeViewVirtualItem *VDUIProxyTreeViewControl::GetSelectedVirtualItem() const {
	if (!mhwnd)
		return NULL;

	HTREEITEM hti = TreeView_GetSelection(mhwnd);

	if (!hti)
		return NULL;

	TVITEMW itemw = {0};

	itemw.mask = LVIF_PARAM;
	itemw.hItem = hti;

	SendMessageW(mhwnd, TVM_GETITEMW, 0, (LPARAM)&itemw);
	return (IVDUITreeViewVirtualItem *)itemw.lParam;
}

IVDUITreeViewVirtualItem *VDUIProxyTreeViewControl::GetVirtualItem(NodeRef ref) const {
	if (!mhwnd)
		return NULL;

	TVITEMW itemw = {0};

	itemw.mask = LVIF_PARAM;
	itemw.hItem = (HTREEITEM)ref;

	SendMessageW(mhwnd, TVM_GETITEMW, 0, (LPARAM)&itemw);

	return (IVDUITreeViewVirtualItem *)itemw.lParam;
}

void VDUIProxyTreeViewControl::Clear() {
	if (mhwnd) {
		TreeView_DeleteAllItems(mhwnd);
	}
}

void VDUIProxyTreeViewControl::DeleteItem(NodeRef ref) {
	if (mhwnd) {
		TreeView_DeleteItem(mhwnd, (HTREEITEM)ref);
	}
}

VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::AddItem(NodeRef parent, NodeRef insertAfter, const wchar_t *label) {
	if (!mhwnd)
		return NULL;

	TVINSERTSTRUCTW isw = { 0 };

	isw.hParent = (HTREEITEM)parent;
	isw.hInsertAfter = (HTREEITEM)insertAfter;
	isw.item.mask = TVIF_TEXT | TVIF_PARAM;
	isw.item.pszText = (LPWSTR)label;
	isw.item.lParam = NULL;

	return (NodeRef)SendMessageW(mhwnd, TVM_INSERTITEMW, 0, (LPARAM)&isw);
}

VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::AddVirtualItem(NodeRef parent, NodeRef insertAfter, IVDUITreeViewVirtualItem *item) {
	if (!mhwnd)
		return NULL;

	HTREEITEM hti;

	TVINSERTSTRUCTW isw = { 0 };

	isw.hParent = (HTREEITEM)parent;
	isw.hInsertAfter = (HTREEITEM)insertAfter;
	isw.item.mask = TVIF_PARAM | TVIF_TEXT;
	isw.item.lParam = (LPARAM)item;
	isw.item.pszText = LPSTR_TEXTCALLBACKW;

	hti = (HTREEITEM)SendMessageW(mhwnd, TVM_INSERTITEMW, 0, (LPARAM)&isw);

	if (hti) {
		if (parent != kNodeRoot) {
			TreeView_Expand(mhwnd, (HTREEITEM)parent, TVE_EXPAND);
		}

		item->AddRef();
	}

	return (NodeRef)hti;
}

void VDUIProxyTreeViewControl::MakeNodeVisible(NodeRef node) {
	if (mhwnd) {
		TreeView_EnsureVisible(mhwnd, (HTREEITEM)node);
	}
}

void VDUIProxyTreeViewControl::SelectNode(NodeRef node) {
	if (mhwnd) {
		TreeView_SelectItem(mhwnd, (HTREEITEM)node);
	}
}

void VDUIProxyTreeViewControl::RefreshNode(NodeRef node) {
	VDASSERT(node);

	if (mhwnd) {
		TVITEMW itemw = {0};

		itemw.mask = LVIF_PARAM;
		itemw.hItem = (HTREEITEM)node;

		SendMessageW(mhwnd, TVM_GETITEMW, 0, (LPARAM)&itemw);

		if (itemw.lParam) {
			itemw.mask = LVIF_TEXT;
			itemw.pszText = LPSTR_TEXTCALLBACKW;
			SendMessageW(mhwnd, TVM_SETITEMW, 0, (LPARAM)&itemw);
		}
	}
}

void VDUIProxyTreeViewControl::ExpandNode(NodeRef node, bool expanded) {
	VDASSERT(node);

	if (!mhwnd)
		return;

	TreeView_Expand(mhwnd, (HTREEITEM)node, expanded ? TVE_EXPAND : TVE_COLLAPSE);
}

void VDUIProxyTreeViewControl::EditNodeLabel(NodeRef node) {
	if (!mhwnd)
		return;

	::SetFocus(mhwnd);
	::SendMessageW(mhwnd, TVM_EDITLABELW, 0, (LPARAM)(HTREEITEM)node);
}

namespace {
	int CALLBACK TreeNodeCompareFn(LPARAM node1, LPARAM node2, LPARAM comparer) {
		if (!node1)
			return node2 ? -1 : 0;

		if (!node2)
			return 1;

		return ((IVDUITreeViewVirtualItemComparer *)comparer)->Compare(
			*(IVDUITreeViewVirtualItem *)node1,
			*(IVDUITreeViewVirtualItem *)node2);
	}
}

bool VDUIProxyTreeViewControl::HasChildren(NodeRef parent) const {
	return mhwnd && TreeView_GetChild(mhwnd, parent) != NULL;
}

void VDUIProxyTreeViewControl::EnumChildren(NodeRef parent, const vdfunction<void(IVDUITreeViewVirtualItem *)>& callback) {
	if (!mhwnd)
		return;

	TVITEMW itemw = {0};
	itemw.mask = LVIF_PARAM;

	HTREEITEM hti = TreeView_GetChild(mhwnd, parent);
	while(hti) {
		itemw.hItem = hti;
		if (SendMessageW(mhwnd, TVM_GETITEMW, 0, (LPARAM)&itemw))
			callback((IVDUITreeViewVirtualItem *)itemw.lParam);

		hti = TreeView_GetNextSibling(mhwnd, hti);
	}
}

void VDUIProxyTreeViewControl::EnumChildrenRecursive(NodeRef parent, const vdfunction<void(IVDUITreeViewVirtualItem *)>& callback) {
	if (!mhwnd)
		return;

	HTREEITEM current = TreeView_GetChild(mhwnd, (HTREEITEM)parent);
	if (!current)
		return;

	vdfastvector<HTREEITEM> traversalStack;

	TVITEMW itemw = {0};
	itemw.mask = LVIF_PARAM;
	for(;;) {
		while(current) {
			itemw.hItem = current;
			if (SendMessageW(mhwnd, TVM_GETITEMW, 0, (LPARAM)&itemw))
				callback((IVDUITreeViewVirtualItem *)itemw.lParam);

			HTREEITEM firstChild = TreeView_GetChild(mhwnd, current);
			if (firstChild)
				traversalStack.push_back(firstChild);

			current = TreeView_GetNextSibling(mhwnd, current);
		}

		if (traversalStack.empty())
			break;

		current = traversalStack.back();
		traversalStack.pop_back();
	}
}

void VDUIProxyTreeViewControl::SortChildren(NodeRef parent, IVDUITreeViewVirtualItemComparer& comparer) {
	if (!mhwnd)
		return;

	TVSORTCB scb;
	scb.hParent = (HTREEITEM)parent;
	scb.lParam = (LPARAM)&comparer;
	scb.lpfnCompare = TreeNodeCompareFn;

	SendMessageW(mhwnd, TVM_SORTCHILDRENCB, 0, (LPARAM)&scb);
}

void VDUIProxyTreeViewControl::SetOnBeginDrag(const vdfunction<void(const BeginDragEvent& event)>& fn) {
	mpOnBeginDrag = fn;
}

VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::FindDropTarget() const {
	POINT pt;
	if (!mhwnd || !GetCursorPos(&pt))
		return NULL;

	if (!ScreenToClient(mhwnd, &pt))
		return NULL;

	TVHITTESTINFO hti = {};
	hti.pt = pt;

	return (NodeRef)TreeView_HitTest(mhwnd, &hti);
}

void VDUIProxyTreeViewControl::SetDropTargetHighlight(NodeRef item) {
	if (mhwnd) {
		TreeView_SelectDropTarget(mhwnd, item);
	}
}

void VDUIProxyTreeViewControl::Attach(VDZHWND hwnd) {
	VDUIProxyControl::Attach(hwnd);

	if (mhwnd)
		SendMessageW(mhwnd, CCM_SETVERSION, 6, 0);
}

void VDUIProxyTreeViewControl::Detach() {
	if (mhfontBold) {
		::DeleteObject(mhfontBold);
		mhfontBold = NULL;
		mbCreatedBoldFont = false;
	}

	EnumChildrenRecursive(kNodeRoot, [](IVDUITreeViewVirtualItem *vi) { vi->Release(); });
}

VDZLRESULT VDUIProxyTreeViewControl::On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	const NMHDR *hdr = (const NMHDR *)lParam;

	switch(hdr->code) {
		case TVN_GETDISPINFOA:
			{
				NMTVDISPINFOA *dispa = (NMTVDISPINFOA *)hdr;
				IVDUITreeViewVirtualItem *lvvi = (IVDUITreeViewVirtualItem *)dispa->item.lParam;

				mTextW[0].clear();
				lvvi->GetText(mTextW[0]);
				mTextA[mNextTextIndex] = VDTextWToA(mTextW[0]);
				dispa->item.pszText = (LPSTR)mTextA[mNextTextIndex].c_str();

				if (++mNextTextIndex >= 3)
					mNextTextIndex = 0;
			}
			break;

		case TVN_GETDISPINFOW:
			{
				NMTVDISPINFOW *dispw = (NMTVDISPINFOW *)hdr;
				IVDUITreeViewVirtualItem *lvvi = (IVDUITreeViewVirtualItem *)dispw->item.lParam;

				mTextW[mNextTextIndex].clear();
				lvvi->GetText(mTextW[mNextTextIndex]);
				dispw->item.pszText = (LPWSTR)mTextW[mNextTextIndex].c_str();

				if (++mNextTextIndex >= 3)
					mNextTextIndex = 0;
			}
			break;

		case TVN_DELETEITEMA:
			{
				const NMTREEVIEWA *nmtv = (const NMTREEVIEWA *)hdr;
				IVDUITreeViewVirtualItem *lvvi = (IVDUITreeViewVirtualItem *)nmtv->itemOld.lParam;

				if (lvvi)
					lvvi->Release();
			}
			break;

		case TVN_DELETEITEMW:
			{
				const NMTREEVIEWW *nmtv = (const NMTREEVIEWW *)hdr;
				IVDUITreeViewVirtualItem *lvvi = (IVDUITreeViewVirtualItem *)nmtv->itemOld.lParam;

				if (lvvi)
					lvvi->Release();
			}
			break;

		case TVN_SELCHANGEDA:
		case TVN_SELCHANGEDW:
			mEventItemSelectionChanged.Raise(this, 0);
			break;

		case TVN_BEGINLABELEDITA:
			{
				const NMTVDISPINFOA& dispInfo = *(const NMTVDISPINFOA *)hdr;
				BeginEditEvent event;

				event.mNode = (NodeRef)dispInfo.item.hItem;
				event.mpItem = (IVDUITreeViewVirtualItem *)dispInfo.item.lParam;
				event.mbAllowEdit = true;
				event.mbOverrideText = false;

				mEventItemBeginEdit.Raise(this, &event);

				if (event.mbAllowEdit) {
					HWND hwndEdit = (HWND)SendMessageA(mhwnd, TVM_GETEDITCONTROL, 0, 0);

					if (hwndEdit) {
						if (!mpEditWndProcThunk)
							mpEditWndProcThunk = VDCreateFunctionThunkFromMethod(this, &VDUIProxyTreeViewControl::FixLabelEditWndProcA, true);

						if (mpEditWndProcThunk) {
							mPrevEditWndProc = (void *)(WNDPROC)GetWindowLongPtrA(hwndEdit, GWLP_WNDPROC);

							if (mPrevEditWndProc)
								SetWindowLongPtrA(hwndEdit, GWLP_WNDPROC, (LONG_PTR)mpEditWndProcThunk);
						}

						if (event.mbOverrideText)
							VDSetWindowTextW32(hwndEdit, event.mOverrideText.c_str());
					}
				}

				return !event.mbAllowEdit;
			}

		case TVN_BEGINLABELEDITW:
			{
				const NMTVDISPINFOW& dispInfo = *(const NMTVDISPINFOW *)hdr;
				BeginEditEvent event;

				event.mNode = (NodeRef)dispInfo.item.hItem;
				event.mpItem = (IVDUITreeViewVirtualItem *)dispInfo.item.lParam;
				event.mbAllowEdit = true;
				event.mbOverrideText = false;

				mEventItemBeginEdit.Raise(this, &event);

				if (event.mbAllowEdit) {
					HWND hwndEdit = (HWND)SendMessageA(mhwnd, TVM_GETEDITCONTROL, 0, 0);

					if (hwndEdit) {
						if (!mpEditWndProcThunk)
							mpEditWndProcThunk = VDCreateFunctionThunkFromMethod(this, &VDUIProxyTreeViewControl::FixLabelEditWndProcW, true);

						if (mpEditWndProcThunk) {
							mPrevEditWndProc = (WNDPROC)GetWindowLongPtrW(hwndEdit, GWLP_WNDPROC);

							if (mPrevEditWndProc)
								SetWindowLongPtrW(hwndEdit, GWLP_WNDPROC, (LONG_PTR)mpEditWndProcThunk);
						}

						if (event.mbOverrideText)
							VDSetWindowTextW32(hwndEdit, event.mOverrideText.c_str());
					}
				}

				return !event.mbAllowEdit;
			}

		case TVN_ENDLABELEDITA:
			{
				const NMTVDISPINFOA& dispInfo = *(const NMTVDISPINFOA *)hdr;

				if (dispInfo.item.pszText) {
					EndEditEvent event;

					event.mNode = (NodeRef)dispInfo.item.hItem;
					event.mpItem = (IVDUITreeViewVirtualItem *)dispInfo.item.lParam;

					const VDStringW& text = VDTextAToW(dispInfo.item.pszText);
					event.mpNewText = text.c_str();

					mEventItemEndEdit.Raise(this, &event);
				}
			}
			break;

		case TVN_ENDLABELEDITW:
			{
				const NMTVDISPINFOW& dispInfo = *(const NMTVDISPINFOW *)hdr;

				if (dispInfo.item.pszText) {
					EndEditEvent event;

					event.mNode = (NodeRef)dispInfo.item.hItem;
					event.mpItem = (IVDUITreeViewVirtualItem *)dispInfo.item.lParam;
					event.mpNewText = dispInfo.item.pszText;

					mEventItemEndEdit.Raise(this, &event);
				}
			}
			break;

		case TVN_BEGINDRAGA:
			{
				const NMTREEVIEWA& info = *(const NMTREEVIEWA *)hdr;
				BeginDragEvent event = {};

				event.mNode = (NodeRef)info.itemNew.hItem;
				event.mpItem = (IVDUITreeViewVirtualItem *)info.itemNew.lParam;
				event.mPos = vdpoint32(info.ptDrag.x, info.ptDrag.y);

				if (mpOnBeginDrag)
					mpOnBeginDrag(event);
			}
			break;

		case TVN_BEGINDRAGW:
			{
				const NMTREEVIEWW& info = *(const NMTREEVIEWW *)hdr;
				BeginDragEvent event = {};

				event.mNode = (NodeRef)info.itemNew.hItem;
				event.mpItem = (IVDUITreeViewVirtualItem *)info.itemNew.lParam;
				event.mPos = vdpoint32(info.ptDrag.x, info.ptDrag.y);

				if (mpOnBeginDrag)
					mpOnBeginDrag(event);
			}
			break;

		case NM_DBLCLK:
			{
				bool handled = false;
				mEventItemDoubleClicked.Raise(this, &handled);
				return handled;
			}

		case NM_CUSTOMDRAW:
			{
				NMTVCUSTOMDRAW& cd = *(LPNMTVCUSTOMDRAW)lParam;

				if (!mEventItemGetDisplayAttributes.IsEmpty()) {
					if (cd.nmcd.dwDrawStage == CDDS_PREPAINT) {
						return CDRF_NOTIFYITEMDRAW;
					} else if (cd.nmcd.dwDrawStage == CDDS_ITEMPREPAINT && cd.nmcd.lItemlParam) {
						GetDispAttrEvent event;
						event.mpItem = (IVDUITreeViewVirtualItem *)cd.nmcd.lItemlParam;
						event.mbIsBold = false;

						mEventItemGetDisplayAttributes.Raise(this, &event);

						if (event.mbIsBold) {
							if (!mhfontBold) {
								HFONT hfont = (HFONT)::GetCurrentObject(cd.nmcd.hdc, OBJ_FONT);

								if (hfont) {
									LOGFONTW lfw = {0};
									if (::GetObject(hfont, sizeof lfw, &lfw)) {
										lfw.lfWeight = FW_BOLD;

										mhfontBold = ::CreateFontIndirectW(&lfw);
									}
								}

								mbCreatedBoldFont = true;
							}

							if (mhfontBold) {
								::SelectObject(cd.nmcd.hdc, mhfontBold);
								return CDRF_NEWFONT;
							}
						}
					}
				}
			}
			return CDRF_DODEFAULT;
	}

	return 0;
}

VDZLRESULT VDUIProxyTreeViewControl::FixLabelEditWndProcA(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	switch(msg) {
		case WM_GETDLGCODE:
			return DLGC_WANTALLKEYS;
	}

	return ::CallWindowProcA((WNDPROC)mPrevEditWndProc, hwnd, msg, wParam, lParam);
}

VDZLRESULT VDUIProxyTreeViewControl::FixLabelEditWndProcW(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	switch(msg) {
		case WM_GETDLGCODE:
			return DLGC_WANTALLKEYS;
	}

	return ::CallWindowProcW((WNDPROC)mPrevEditWndProc, hwnd, msg, wParam, lParam);
}

/////////////////////////////////////////////////////////////////////////////

VDUIProxyEditControl::VDUIProxyEditControl() {
}

VDUIProxyEditControl::~VDUIProxyEditControl() {
}

VDZLRESULT VDUIProxyEditControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (HIWORD(wParam) == EN_CHANGE) {
		if (mpOnTextChanged)
			mpOnTextChanged(this);
	}

	return 0;
}

VDStringW VDUIProxyEditControl::GetText() const {
	if (!mhwnd)
		return VDStringW();

	return VDGetWindowTextW32(mhwnd);
}

void VDUIProxyEditControl::SetText(const wchar_t *s) {
	if (mhwnd)
		::SetWindowText(mhwnd, s);
}

void VDUIProxyEditControl::SetOnTextChanged(vdfunction<void(VDUIProxyEditControl *)> fn) {
	mpOnTextChanged = std::move(fn);
}

/////////////////////////////////////////////////////////////////////////////

VDUIProxyRichEditControl::VDUIProxyRichEditControl() {
}

VDUIProxyRichEditControl::~VDUIProxyRichEditControl() {
}

void VDUIProxyRichEditControl::AppendEscapedRTF(VDStringA& buf, const wchar_t *text) {
	const VDStringA& texta = VDTextWToA(text);
	for(VDStringA::const_iterator it = texta.begin(), itEnd = texta.end();
		it != itEnd;
		++it)
	{
		const unsigned char c = *it;

		if (c < 0x20 || c > 0x80 || c == '{' || c == '}' || c == '\\')
			buf.append_sprintf("\\'%02x", c);
		else if (c == '\n')
			buf += "\\par ";
		else
			buf += c;
	}
}

void VDUIProxyRichEditControl::EnsureCaretVisible() {
	if (mhwnd)
		::SendMessage(mhwnd, EM_SCROLLCARET, 0, 0);
}

void VDUIProxyRichEditControl::SetCaretPos(int lineIndex, int charIndex) {
	if (!mhwnd)
		return;

	int lineStart = (int)::SendMessage(mhwnd, EM_LINEINDEX, (WPARAM)lineIndex, 0);

	if (lineStart >= 0)
		lineStart += charIndex;

	CHARRANGE cr = { lineStart, lineStart };
	::SendMessage(mhwnd, EM_EXSETSEL, 0, (LPARAM)&cr);
}

void VDUIProxyRichEditControl::SetText(const wchar_t *s) {
	if (mhwnd)
		::SetWindowText(mhwnd, s);
}

void VDUIProxyRichEditControl::SetTextRTF(const char *s) {
	if (!mhwnd)
		return;

	SETTEXTEX stex;
	stex.flags = ST_DEFAULT;
	stex.codepage = CP_ACP;
	SendMessageA(mhwnd, EM_SETTEXTEX, (WPARAM)&stex, (LPARAM)s);
}

void VDUIProxyRichEditControl::SetReadOnlyBackground() {
	if (mhwnd)
		SendMessage(mhwnd, EM_SETBKGNDCOLOR, FALSE, GetSysColor(COLOR_3DFACE));
}

void VDUIProxyRichEditControl::SetOnTextChanged(vdfunction<void()> fn) {
	mpOnTextChanged = std::move(fn);
}

VDZLRESULT VDUIProxyRichEditControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (HIWORD(wParam) == EN_CHANGE) {
		if (mpOnTextChanged)
			mpOnTextChanged();
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////

VDUIProxyButtonControl::VDUIProxyButtonControl() {
}

VDUIProxyButtonControl::~VDUIProxyButtonControl() {
}

bool VDUIProxyButtonControl::GetChecked() const {
	return mhwnd && SendMessage(mhwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void VDUIProxyButtonControl::SetChecked(bool enable) {
	if (mhwnd)
		SendMessage(mhwnd, BM_SETCHECK, enable ? BST_CHECKED : BST_UNCHECKED, 0);
}

VDZLRESULT VDUIProxyButtonControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (HIWORD(wParam) == BN_CLICKED) {
		if (mpOnClicked)
			mpOnClicked(this);
	}

	return 0;
}

void VDUIProxyButtonControl::SetOnClicked(vdfunction<void(VDUIProxyButtonControl *)> fn) {
	mpOnClicked = std::move(fn);
}
