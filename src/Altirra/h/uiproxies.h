#ifndef f_VD2_VDLIB_UIPROXIES_H
#define f_VD2_VDLIB_UIPROXIES_H

#include <vd2/system/event.h>
#include <vd2/system/refcount.h>
#include <vd2/system/unknown.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vectors.h>
#include <vd2/system/VDString.h>
#include <vd2/system/win32/miniwindows.h>

struct VDUIAccelerator;

class VDUIProxyControl : public vdlist_node {
public:
	VDUIProxyControl();

	VDZHWND GetHandle() const { return mhwnd; }

	virtual void Attach(VDZHWND hwnd);
	virtual void Detach();

	void SetArea(const vdrect32& r);

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

/////////////////////////////////////////////////////////////////////////////

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

/////////////////////////////////////////////////////////////////////////////

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

/////////////////////////////////////////////////////////////////////////////

class VDUIProxyTabControl : public VDUIProxyControl {
public:
	VDUIProxyTabControl();
	~VDUIProxyTabControl();

	void AddItem(const wchar_t *s);
	void DeleteItem(int index);

	vdsize32 GetControlSizeForContent(const vdsize32&) const;
	vdrect32 GetContentArea() const;

	int GetSelection() const;
	void SetSelection(int index);

	VDEvent<VDUIProxyTabControl, int>& OnSelectionChanged() {
		return mSelectionChanged;
	}

protected:
	VDZLRESULT On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam);

	VDEvent<VDUIProxyTabControl, int> mSelectionChanged;
};

/////////////////////////////////////////////////////////////////////////////

class VDUIProxyComboBoxControl : public VDUIProxyControl {
public:
	VDUIProxyComboBoxControl();
	~VDUIProxyComboBoxControl();

	void AddItem(const wchar_t *s);

	int GetSelection() const;
	void SetSelection(int index);

	VDEvent<VDUIProxyComboBoxControl, int>& OnSelectionChanged() {
		return mSelectionChanged;
	}

protected:
	VDZLRESULT On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam);

	VDEvent<VDUIProxyComboBoxControl, int> mSelectionChanged;
};

/////////////////////////////////////////////////////////////////////////////

class IVDUITreeViewVirtualItem : public IVDRefUnknown {
public:
	virtual void GetText(VDStringW& s) const = 0;
};

class VDUIProxyTreeViewControl : public VDUIProxyControl {
public:
	typedef uintptr NodeRef;

	static const NodeRef kNodeRoot;
	static const NodeRef kNodeFirst;
	static const NodeRef kNodeLast;

	VDUIProxyTreeViewControl();
	~VDUIProxyTreeViewControl();

	IVDUITreeViewVirtualItem *GetSelectedVirtualItem() const;

	void Clear();
	void DeleteItem(NodeRef ref);
	NodeRef AddItem(NodeRef parent, NodeRef insertAfter, const wchar_t *label);
	NodeRef AddVirtualItem(NodeRef parent, NodeRef insertAfter, IVDUITreeViewVirtualItem *item);

	void MakeNodeVisible(NodeRef node);
	void SelectNode(NodeRef node);
	void RefreshNode(NodeRef node);

	VDEvent<VDUIProxyTreeViewControl, int>& OnItemSelectionChanged() {
		return mEventItemSelectionChanged;
	}

	VDEvent<VDUIProxyTreeViewControl, bool *>& OnItemDoubleClicked() {
		return mEventItemDoubleClicked;
	}

protected:
	VDZLRESULT On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam);

	int			mNextTextIndex;
	VDStringW mTextW[3];
	VDStringA mTextA[3];

	VDEvent<VDUIProxyTreeViewControl, int> mEventItemSelectionChanged;
	VDEvent<VDUIProxyTreeViewControl, bool *> mEventItemDoubleClicked;
};

#endif
