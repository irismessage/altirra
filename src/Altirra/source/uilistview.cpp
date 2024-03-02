#include "stdafx.h"
#include <vd2/VDDisplay/textrenderer.h>
#include "uilistview.h"
#include "uimanager.h"

template<> void vdmove<ATUIListViewItem>(ATUIListViewItem& dst, ATUIListViewItem& src) {
	dst.mText.move_from(src.mText);
}

ATUIListView::ATUIListView()
	: mScrollY(0)
	, mSelectedIndex(-1)
	, mItemHeight(0)
	, mTextColor(0x000000)
	, mHighlightBackgroundColor(0x0A246A)
	, mHighlightTextColor(0xFFFFFF)
{
	SetFillColor(0xD4D0C8);
	SetCursorImage(kATUICursorImage_Arrow);

	AddItem(L"Foo");
	AddItem(L"Bar");
	AddItem(L"Baz");
}

ATUIListView::~ATUIListView() {
}

void ATUIListView::AddItem(const wchar_t *text) {
	InsertItem(0x7FFFFFFF, text);
	Invalidate();
}

void ATUIListView::InsertItem(sint32 pos, const wchar_t *text) {
	uint32 n = (uint32)mItems.size();

	if (pos < 0)
		pos = 0;
	else if ((uint32)pos > n)
		pos = n;

	ATUIListViewItem& item = *mItems.insert(mItems.begin() + pos, ATUIListViewItem());

	item.mText = text;

	if (mSelectedIndex >= pos)
		++mSelectedIndex;

	Invalidate();
}

void ATUIListView::RemoveItem(sint32 pos) {
	uint32 n = (uint32)mItems.size();
	if ((uint32)pos < n) {
		mItems.erase(mItems.begin() + pos);

		if (mSelectedIndex >= pos) {
			if (mSelectedIndex > pos)
				--mSelectedIndex;

			if ((uint32)mSelectedIndex >= n)
				mSelectedIndex = -1;
		}

		Invalidate();
	}
}

void ATUIListView::RemoveAllItems() {
	if (!mItems.empty()) {
		mItems.clear();
		mSelectedIndex = -1;
		Invalidate();
	}
}

void ATUIListView::SetSelectedItem(sint32 idx) {
	if (idx < 0)
		idx = -1;

	sint32 n = (sint32)mItems.size();
	if (idx >= n)
		idx = n - 1;

	if (mSelectedIndex != idx) {
		mSelectedIndex = idx;
		Invalidate();
	}
}

void ATUIListView::OnMouseDownL(sint32 x, sint32 y) {
	Focus();

	sint32 idx = (mScrollY + y) / (sint32)mItemHeight;

	if (idx >= (sint32)mItems.size())
		idx = -1;

	SetSelectedItem(idx);
}

bool ATUIListView::OnKeyDown(uint32 vk) {
	switch(vk) {
		case kATUIVK_Up:
			SetSelectedItem(mSelectedIndex > 0 ? mSelectedIndex - 1 : 0);
			return true;

		case kATUIVK_Down:
			SetSelectedItem(mSelectedIndex + 1);
			return true;

		case kATUIVK_Home:
			SetSelectedItem(0);
			return true;

		case kATUIVK_End:
			SetSelectedItem((sint32)mItems.size() - 1);
			return true;
	}

	return false;
}

void ATUIListView::OnCreate() {
	mpFont = mpManager->GetThemeFont(kATUIThemeFont_Default);

	VDDisplayFontMetrics metrics;
	mpFont->GetMetrics(metrics);
	mItemHeight = metrics.mAscent + metrics.mDescent + 4;
}

void ATUIListView::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	VDDisplayTextRenderer *tr = rdr.GetTextRenderer();

	uint32 n = (uint32)mItems.size();
	sint32 y = -mScrollY;

	tr->SetAlignment(VDDisplayTextRenderer::kAlignLeft, VDDisplayTextRenderer::kVertAlignTop);
	
	for(uint32 i=0; i<n; ++i) {
		const ATUIListViewItem& lv = mItems[i];
		uint32 textColor = mTextColor;

		if (mSelectedIndex == (sint32)i) {
			textColor = mHighlightTextColor;
			rdr.SetColorRGB(mHighlightBackgroundColor);
			rdr.FillRect(0, y, w, mItemHeight);
		}

		if (rdr.PushViewport(vdrect32(2, y+2, w-2, y+mItemHeight-2), 2, y+2)) {
			tr->SetColorRGB(textColor);
			tr->DrawTextLine(0, 0, lv.mText.c_str());

			rdr.PopViewport();
		}

		y += mItemHeight;
	}
}
