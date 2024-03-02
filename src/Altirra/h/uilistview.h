#ifndef f_AT_UILISTVIEW_H
#define f_AT_UILISTVIEW_H

#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/VDDisplay/font.h>
#include "uiwidget.h"

struct ATUIListViewItem {
	VDStringW mText;
};

VDMOVE_CAPABLE(ATUIListViewItem);

class ATUIListView : public ATUIWidget {
public:
	ATUIListView();
	~ATUIListView();

	void AddItem(const wchar_t *text);
	void InsertItem(sint32 pos, const wchar_t *text);
	void RemoveItem(sint32 pos);
	void RemoveAllItems();

	void SetSelectedItem(sint32 idx);

public:
	virtual void OnMouseDownL(sint32 x, sint32 y);

	virtual bool OnKeyDown(uint32 vk);

	virtual void OnCreate();

protected:
	virtual void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h);

	sint32	mScrollY;
	sint32	mSelectedIndex;
	sint32	mItemHeight;

	uint32	mTextColor;
	uint32	mHighlightBackgroundColor;
	uint32	mHighlightTextColor;

	vdrefptr<IVDDisplayFont> mpFont;
	vdvector<ATUIListViewItem> mItems;
};

#endif
