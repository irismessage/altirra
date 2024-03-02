#ifndef f_AT2_UIMENULIST_H
#define f_AT2_UIMENULIST_H

#include <vd2/system/event.h>
#include <vd2/system/time.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/VDDisplay/font.h>
#include "uiwidget.h"

class IVDDisplayFont;
class ATUIMenu;

struct ATUIMenuItem {
	VDStringW mText;
	vdrefptr<ATUIMenu> mpSubMenu;
	uint32 mId;
	bool mbSeparator : 1;
	bool mbDisabled : 1;
	bool mbChecked : 1;
	bool mbRadioChecked : 1;

	ATUIMenuItem()
		: mId(0)
		, mbSeparator(false)
		, mbDisabled(false)
		, mbChecked(false)
		, mbRadioChecked(false)
	{
	}
};

VDMOVE_CAPABLE(ATUIMenuItem);

class ATUIMenu : public vdrefcount {
public:
	void AddItem(const ATUIMenuItem& item) { mItems.push_back(item); }
	void AddSeparator();

	void RemoveItems(uint32 start, uint32 n);
	void RemoveAllItems();

	uint32 GetItemCount() const { return mItems.size(); }
	ATUIMenuItem *GetItemByIndex(uint32 i) { return &mItems[i]; }
	const ATUIMenuItem *GetItemByIndex(uint32 i) const { return &mItems[i]; }
	ATUIMenuItem *GetItemById(uint32 id, bool recurse);
	const ATUIMenuItem *GetItemById(uint32 id, bool recurse) const;

protected:
	typedef vdvector<ATUIMenuItem> Items;
	Items mItems;
};

class ATUIMenuList : public ATUIWidget, public IVDTimerCallback {
public:
	ATUIMenuList();
	~ATUIMenuList();

	void SetFont(IVDDisplayFont *font);
	void SetPopup(bool popup) { mbPopup = popup; }
	void SetMenu(ATUIMenu *menu);

	int GetItemFromPoint(sint32 x, sint32 y) const;

	void AutoSize();
	void CloseMenu();

	virtual void OnMouseMove(sint32 x, sint32 y);
	virtual void OnMouseDownL(sint32 x, sint32 y);
	virtual void OnMouseLeave();

	virtual bool OnKeyDown(uint32 vk);
	virtual bool OnChar(uint32 vk);

	virtual void OnCreate();

	VDEvent<ATUIMenuList, uint32>& OnItemSelected() { return mItemSelectedEvent; }

public:
	virtual void TimerCallback();

protected:
	virtual void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h);

	virtual bool HandleMouseMove(sint32 x, sint32 y);
	virtual bool HandleMouseDownL(sint32 x, sint32 y, bool nested, uint32& itemSelected);

	void SetSelectedIndex(sint32 selIndex, bool immediate);
	void OpenSubMenu();
	void CloseSubMenu();

	void Reflow();

	vdrefptr<IVDDisplayFont> mpFont;
	sint32 mSelectedIndex;
	bool mbPopup;
	bool mbActive;
	vdsize32	mIdealSize;
	uint32		mTextSplitX;
	uint32		mLeftMargin;
	uint32		mRightMargin;

	ATUIMenuList *mpRootList;
	vdrefptr<ATUIMenu> mpMenu;

	struct ItemInfo {
		VDStringW mLeftText;
		VDStringW mRightText;
		sint32 mUnderX1;
		sint32 mUnderX2;
		sint32 mUnderY;
		sint32 mPos;
		sint32 mSize;
		bool mbSelectable : 1;
		bool mbSeparator : 1;
		bool mbPopup : 1;
		bool mbDisabled : 1;
		bool mbChecked : 1;
		bool mbRadioChecked : 1;
	};

	typedef vdvector<ItemInfo> MenuItems;
	MenuItems mMenuItems;

	vdrefptr<ATUIMenuList> mpSubMenu;

	VDEvent<ATUIMenuList, uint32> mItemSelectedEvent;

	VDLazyTimer mSubMenuTimer;
};

#endif
