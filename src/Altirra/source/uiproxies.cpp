#include "stdafx.h"
#include <windows.h>
#include <commctrl.h>

#include <vd2/system/w32assist.h>
#include <vd2/Dita/accel.h>
#include "uiproxies.h"

///////////////////////////////////////////////////////////////////////////////

VDUIProxyControl::VDUIProxyControl()
	: mhwnd(NULL)
{
}

void VDUIProxyControl::Attach(VDZHWND hwnd) {
	VDASSERT(IsWindow(hwnd));
	mhwnd = hwnd;
}

void VDUIProxyControl::Detach() {
	mhwnd = NULL;
}

void VDUIProxyControl::SetArea(const vdrect32& r) {
	if (mhwnd)
		SetWindowPos(mhwnd, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
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

void VDUIProxyMessageDispatcherW32::RemoveAllControls() {
	for(int i=0; i<kHashTableSize; ++i)
		mHashTable[i].clear();
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

void VDUIProxyListView::AutoSizeColumns() {
	const int colCount = GetColumnCount();

	for(int col=0; col<colCount; ++col) {
		SendMessage(mhwnd, LVM_SETCOLUMNWIDTH, col, LVSCW_AUTOSIZE_USEHEADER);
		const int hdrWidth = SendMessage(mhwnd, LVM_GETCOLUMNWIDTH, col, 0);

		SendMessage(mhwnd, LVM_SETCOLUMNWIDTH, col, LVSCW_AUTOSIZE);
		const int dataWidth = SendMessage(mhwnd, LVM_GETCOLUMNWIDTH, col, 0);

		if (dataWidth < hdrWidth)
			SendMessage(mhwnd, LVM_SETCOLUMNWIDTH, col, hdrWidth);
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
}

void VDUIProxyListView::DeleteItem(int index) {
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

void VDUIProxyListView::SetFullRowSelectEnabled(bool enabled) {
	ListView_SetExtendedListViewStyleEx(mhwnd, LVS_EX_FULLROWSELECT, enabled ? LVS_EX_FULLROWSELECT : 0);
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

IVDUIListViewVirtualItem *VDUIProxyListView::GetVirtualItem(int index) const {
	if (index < 0)
		return NULL;

	if (VDIsWindowsNT()) {
		LVITEMW itemw={};
		itemw.mask = LVIF_PARAM;
		itemw.iItem = index;
		itemw.iSubItem = 0;
		if (SendMessage(mhwnd, LVM_GETITEMA, 0, (LPARAM)&itemw))
			return (IVDUIListViewVirtualItem *)itemw.lParam;
	} else {
		LVITEMA itema={};
		itema.mask = LVIF_PARAM;
		itema.iItem = index;
		itema.iSubItem = 0;
		if (SendMessage(mhwnd, LVM_GETITEMW, 0, (LPARAM)&itema))
			return (IVDUIListViewVirtualItem *)itema.lParam;
	}

	return NULL;
}

void VDUIProxyListView::InsertColumn(int index, const wchar_t *label, int width) {
	if (VDIsWindowsNT()) {
		LVCOLUMNW colw = {};

		colw.mask		= LVCF_FMT | LVCF_TEXT | LVCF_WIDTH;
		colw.fmt		= LVCFMT_LEFT;
		colw.cx			= width;
		colw.pszText	= (LPWSTR)label;

		SendMessageW(mhwnd, LVM_INSERTCOLUMNW, (WPARAM)index, (LPARAM)&colw);
	} else {
		LVCOLUMNA cola = {};
		VDStringA labela(VDTextWToA(label));

		cola.mask		= LVCF_FMT | LVCF_TEXT | LVCF_WIDTH;
		cola.fmt		= LVCFMT_LEFT;
		cola.cx			= width;
		cola.pszText	= (LPSTR)labela.c_str();

		SendMessageA(mhwnd, LVM_INSERTCOLUMNA, (WPARAM)index, (LPARAM)&cola);
	}
}

int VDUIProxyListView::InsertItem(int item, const wchar_t *text) {
	if (item < 0)
		item = 0x7FFFFFFF;

	if (VDIsWindowsNT()) {
		LVITEMW itemw = {};

		itemw.mask		= LVIF_TEXT;
		itemw.iItem		= item;
		itemw.pszText	= (LPWSTR)text;

		return (int)SendMessageW(mhwnd, LVM_INSERTITEMW, 0, (LPARAM)&itemw);
	} else {
		LVITEMA itema = {};
		VDStringA texta(VDTextWToA(text));

		itema.mask		= LVIF_TEXT;
		itema.iItem		= item;
		itema.pszText	= (LPSTR)texta.c_str();

		return (int)SendMessageA(mhwnd, LVM_INSERTITEMA, 0, (LPARAM)&itema);
	}
}

int VDUIProxyListView::InsertVirtualItem(int item, IVDUIListViewVirtualItem *lvvi) {
	int index;

	if (item < 0)
		item = 0x7FFFFFFF;

	++mChangeNotificationLocks;
	if (VDIsWindowsNT()) {
		LVITEMW itemw = {};

		itemw.mask		= LVIF_TEXT | LVIF_PARAM;
		itemw.iItem		= item;
		itemw.pszText	= LPSTR_TEXTCALLBACKW;
		itemw.lParam	= (LPARAM)lvvi;

		index = (int)SendMessageW(mhwnd, LVM_INSERTITEMW, 0, (LPARAM)&itemw);
	} else {
		LVITEMA itema = {};

		itema.mask		= LVIF_TEXT | LVIF_PARAM;
		itema.iItem		= item;
		itema.pszText	= LPSTR_TEXTCALLBACKA;
		itema.lParam	= (LPARAM)lvvi;

		index = (int)SendMessageA(mhwnd, LVM_INSERTITEMA, 0, (LPARAM)&itema);
	}
	--mChangeNotificationLocks;

	if (index >= 0)
		lvvi->AddRef();

	return index;
}

void VDUIProxyListView::RefreshItem(int item) {
	SendMessage(mhwnd, LVM_REDRAWITEMS, item, item);
}

void VDUIProxyListView::EditItemLabel(int item) {
	ListView_EditLabel(mhwnd, item);
}

void VDUIProxyListView::GetItemText(int item, VDStringW& s) const {
	if (VDIsWindowsNT()) {
		LVITEMW itemw;
		wchar_t buf[512];

		itemw.iSubItem = 0;
		itemw.cchTextMax = 511;
		itemw.pszText = buf;
		buf[0] = 0;
		SendMessageW(mhwnd, LVM_GETITEMTEXTW, item, (LPARAM)&itemw);

		s = buf;
	} else {
		LVITEMA itema;
		char buf[512];

		itema.iSubItem = 0;
		itema.cchTextMax = 511;
		itema.pszText = buf;
		buf[0] = 0;
		SendMessageW(mhwnd, LVM_GETITEMTEXTA, item, (LPARAM)&itema);

		s = VDTextAToW(buf);
	}
}

void VDUIProxyListView::SetItemText(int item, int subitem, const wchar_t *text) {
	if (VDIsWindowsNT()) {
		LVITEMW itemw = {};

		itemw.mask		= LVIF_TEXT;
		itemw.iItem		= item;
		itemw.iSubItem	= subitem;
		itemw.pszText	= (LPWSTR)text;

		SendMessageW(mhwnd, LVM_SETITEMW, 0, (LPARAM)&itemw);
	} else {
		LVITEMA itema = {};
		VDStringA texta(VDTextWToA(text));

		itema.mask		= LVIF_TEXT;
		itema.iItem		= item;
		itema.iSubItem	= subitem;
		itema.pszText	= (LPSTR)texta.c_str();

		SendMessageA(mhwnd, LVM_SETITEMA, 0, (LPARAM)&itema);
	}
}

bool VDUIProxyListView::IsItemChecked(int item) {
	return ListView_GetCheckState(mhwnd, item) != 0;
}

void VDUIProxyListView::SetItemChecked(int item, bool checked) {
	ListView_SetCheckState(mhwnd, item, checked);
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

				mTextW[0].clear();
				lvvi->GetText(dispa->item.iSubItem, mTextW[0]);
				mTextA[mNextTextIndex] = VDTextWToA(mTextW[0]);
				dispa->item.pszText = (LPSTR)mTextA[mNextTextIndex].c_str();

				if (++mNextTextIndex >= 3)
					mNextTextIndex = 0;
			}
			break;

		case LVN_GETDISPINFOW:
			{
				NMLVDISPINFOW *dispw = (NMLVDISPINFOW *)hdr;
				IVDUIListViewVirtualItem *lvvi = (IVDUIListViewVirtualItem *)dispw->item.lParam;

				mTextW[mNextTextIndex].clear();
				lvvi->GetText(dispw->item.iSubItem, mTextW[mNextTextIndex]);
				dispw->item.pszText = (LPWSTR)mTextW[mNextTextIndex].c_str();

				if (++mNextTextIndex >= 3)
					mNextTextIndex = 0;
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
					LabelEventData data = {
						di->item.iItem,
						label.c_str()
					};

					mEventItemLabelEdited.Raise(this, data);
				}
			}
			return TRUE;

		case LVN_ENDLABELEDITW:
			{
				const NMLVDISPINFOW *di = (const NMLVDISPINFOW *)hdr;

				if (di->item.pszText) {
					LabelEventData data = {
						di->item.iItem,
						di->item.pszText
					};

					mEventItemLabelEdited.Raise(this, data);
				}
			}
			return TRUE;

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

	uint32 v = SendMessage(mhwnd, HKM_GETHOTKEY, 0, 0);

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
	if (VDIsWindowsNT()) {
		TCITEMW tciw = { TCIF_TEXT };

		tciw.pszText = (LPWSTR)s;

		SendMessageW(mhwnd, TCM_INSERTITEMW, n, (LPARAM)&tciw);
	} else {
		TCITEMA tcia = { TCIF_TEXT };
		VDStringA sa(VDTextWToA(s));

		tcia.pszText = (LPSTR)sa.c_str();

		SendMessageA(mhwnd, TCM_INSERTITEMA, n, (LPARAM)&tcia);
	}
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

VDUIProxyComboBoxControl::VDUIProxyComboBoxControl() {
}

VDUIProxyComboBoxControl::~VDUIProxyComboBoxControl() {
}

void VDUIProxyComboBoxControl::AddItem(const wchar_t *s) {
	if (!mhwnd)
		return;

	if (VDIsWindowsNT())
		SendMessageW(mhwnd, CB_ADDSTRING, 0, (LPARAM)s);
	else
		SendMessageA(mhwnd, CB_ADDSTRING, 0, (LPARAM)VDTextWToA(s).c_str());
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
