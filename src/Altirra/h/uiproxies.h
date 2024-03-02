#ifndef f_AT_UIPROXIES_H
#define f_AT_UIPROXIES_H

#include <vd2/system/event.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/win32/miniwindows.h>

struct VDUIAccelerator;

class VDUIProxyControl : public vdlist_node {
public:
	VDUIProxyControl();

	VDZHWND GetHandle() const { return mhwnd; }

	virtual void Attach(VDZHWND hwnd);
	virtual void Detach();

	virtual VDZLRESULT On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam);
	virtual VDZLRESULT On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam);

protected:
	VDZHWND	mhwnd;
};

class VDUIProxyMessageDispatcherW32 {
public:
	void AddControl(VDUIProxyControl *control);
	void RemoveControl(VDZHWND hwnd);
	void RemoveAllControls();

	VDZLRESULT Dispatch_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam);
	VDZLRESULT Dispatch_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam);

protected:
	size_t Hash(VDZHWND hwnd) const;
	VDUIProxyControl *GetControl(VDZHWND hwnd);

	enum { kHashTableSize = 31 };

	typedef vdlist<VDUIProxyControl> HashChain;
	HashChain mHashTable[kHashTableSize];
};

class IVDUIListViewVirtualItem : public IVDRefCount {
public:
	virtual void GetText(int subItem, VDStringW& s) const = 0;
};

class IVDUIListViewVirtualComparer {
public:
	virtual int Compare(IVDUIListViewVirtualItem *x, IVDUIListViewVirtualItem *y) = 0;
};

class VDUIProxyListView : public VDUIProxyControl {
public:
	VDUIProxyListView();

	void AutoSizeColumns();
	void Clear();
	void ClearExtraColumns();
	void DeleteItem(int index);
	int GetColumnCount() const;
	int GetItemCount() const;
	int GetSelectedIndex() const;
	void SetSelectedIndex(int index);
	void SetFullRowSelectEnabled(bool enabled);
	void SetItemCheckboxesEnabled(bool enabled);
	void EnsureItemVisible(int index);
	int GetVisibleTopIndex();
	void SetVisibleTopIndex(int index);
	IVDUIListViewVirtualItem *GetVirtualItem(int index) const;
	void InsertColumn(int index, const wchar_t *label, int width);
	int InsertItem(int item, const wchar_t *text);
	int InsertVirtualItem(int item, IVDUIListViewVirtualItem *lvvi);
	void RefreshItem(int item);
	void EditItemLabel(int item);
	void GetItemText(int item, VDStringW& s) const;
	void SetItemText(int item, int subitem, const wchar_t *text);

	bool IsItemChecked(int item);
	void SetItemChecked(int item, bool checked);

	void Sort(IVDUIListViewVirtualComparer& comparer);

	VDEvent<VDUIProxyListView, int>& OnColumnClicked() {
		return mEventColumnClicked;
	}

	VDEvent<VDUIProxyListView, int>& OnItemSelectionChanged() {
		return mEventItemSelectionChanged;
	}

	VDEvent<VDUIProxyListView, int>& OnItemDoubleClicked() {
		return mEventItemDoubleClicked;
	}

	VDEvent<VDUIProxyListView, int>& OnItemCheckedChanged() {
		return mEventItemCheckedChanged;
	}

	struct LabelEventData {
		int mIndex;
		const wchar_t *mpNewLabel;
	};

	VDEvent<VDUIProxyListView, LabelEventData>& OnItemLabelChanged() {
		return mEventItemLabelEdited;
	}

protected:
	static int VDZCALLBACK SortAdapter(VDZLPARAM x, VDZLPARAM y, VDZLPARAM cookie);
	VDZLRESULT On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam);

	int			mChangeNotificationLocks;
	int			mNextTextIndex;
	VDStringW	mTextW[3];
	VDStringA	mTextA[3];

	VDEvent<VDUIProxyListView, int> mEventColumnClicked;
	VDEvent<VDUIProxyListView, int> mEventItemSelectionChanged;
	VDEvent<VDUIProxyListView, int> mEventItemDoubleClicked;
	VDEvent<VDUIProxyListView, int> mEventItemCheckedChanged;
	VDEvent<VDUIProxyListView, LabelEventData> mEventItemLabelEdited;
};

class VDUIProxyHotKeyControl : public VDUIProxyControl {
public:
	VDUIProxyHotKeyControl();
	~VDUIProxyHotKeyControl();

	bool GetAccelerator(VDUIAccelerator& accel) const;
	void SetAccelerator(const VDUIAccelerator& accel);

	VDEvent<VDUIProxyHotKeyControl, VDUIAccelerator>& OnEventHotKeyChanged() {
		return mEventHotKeyChanged;
	}

protected:
	VDZLRESULT On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam);

	VDEvent<VDUIProxyHotKeyControl, VDUIAccelerator> mEventHotKeyChanged;
};

#endif
