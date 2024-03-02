#include "stdafx.h"
#include <vd2/VDDisplay/textrenderer.h>
#include "uibutton.h"
#include "uimanager.h"
#include "uidrawingutils.h"

ATUIButton::ATUIButton()
	: mStockImageIdx(-1)
	, mbDepressed(false)
	, mTextX(0)
	, mTextY(0)
	, mActivatedEvent()
	, mPressedEvent()
{
	SetFillColor(0xD4D0C8);
	BindAction(kATUIVK_Space, kActionActivate);
	BindAction(kATUIVK_Return, kActionActivate);
}

ATUIButton::~ATUIButton() {
}

void ATUIButton::SetStockImage(sint32 idx) {
	if (mStockImageIdx == idx)
		return;

	mStockImageIdx = idx;
	Invalidate();
}

void ATUIButton::SetText(const wchar_t *s) {
	if (mText != s) {
		mText = s;

		Relayout();
		Invalidate();
	}
}

void ATUIButton::SetDepressed(bool depressed) {
	if (mbDepressed != depressed) {
		mbDepressed = depressed;

		Invalidate();

		if (depressed) {
			if (mPressedEvent)
				mPressedEvent(this);
		} else {
			if (mActivatedEvent)
				mActivatedEvent(this);
		}
	}
}

void ATUIButton::OnMouseDownL(sint32 x, sint32 y) {
	SetDepressed(true);
	CaptureCursor();
	Focus();
}

void ATUIButton::OnMouseUpL(sint32 x, sint32 y) {
	if (mbDepressed) {
		ReleaseCursor();
		SetDepressed(false);
	}
}

void ATUIButton::OnActionStart(uint32 id) {
	switch(id) {
		case kActionActivate:
			SetDepressed(true);
			break;

		default:
			ATUIWidget::OnActionStart(id);
	}
}

void ATUIButton::OnActionStop(uint32 id) {
	switch(id) {
		case kActionActivate:
			SetDepressed(false);
			break;

		default:
			ATUIWidget::OnActionStop(id);
	}
}

void ATUIButton::OnCreate() {
	mpFont = mpManager->GetThemeFont(kATUIThemeFont_Default);

	Relayout();
}

void ATUIButton::OnSize() {
	Relayout();
}

void ATUIButton::OnSetFocus() {
	SetFillColor(0xA0C0FF);
	Invalidate();
}

void ATUIButton::OnKillFocus() {
	SetFillColor(0xD4D0C8);
	Invalidate();
}

void ATUIButton::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	ATUIDraw3DRect(rdr, vdrect32(0, 0, w, h), mbDepressed);

	if (mpFont && rdr.PushViewport(vdrect32(2, 2, w-2, h-2), 2, 2)) {
		if (mStockImageIdx >= 0) {
			ATUIStockImage& image = mpManager->GetStockImage((ATUIStockImageIdx)mStockImageIdx);

			VDDisplayBlt blt;
			blt.mDestX = ((w-4) - image.mWidth) >> 1;
			blt.mDestY = ((h-4) - image.mHeight) >> 1;
			blt.mSrcX = 0;
			blt.mSrcY = 0;
			blt.mWidth = image.mWidth;
			blt.mHeight = image.mHeight;

			rdr.SetColorRGB(0);
			rdr.MultiBlt(&blt, 1, image.mImageView, IVDDisplayRenderer::kBltMode_Color);
		} else {
			VDDisplayTextRenderer *tr = rdr.GetTextRenderer();

			tr->SetFont(mpFont);
			tr->SetAlignment(VDDisplayTextRenderer::kAlignLeft, VDDisplayTextRenderer::kVertAlignTop);
			tr->SetColorRGB(0);
			tr->DrawTextLine(mTextX, mTextY, mText.c_str());
		}

		rdr.PopViewport();
	}
}

void ATUIButton::Relayout() {
	if (mpFont) {
		vdsize32 size = mpFont->MeasureString(mText.data(), mText.size(), false);

		VDDisplayFontMetrics m;
		mpFont->GetMetrics(m);

		mTextX = ((mArea.width() - 4) - size.w) >> 1;
		mTextY = ((mArea.height() - 4) - m.mAscent) >> 1;

		Invalidate();
	}
}
