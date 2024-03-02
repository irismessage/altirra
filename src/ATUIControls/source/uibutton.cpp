#include <stdafx.h>
#include <vd2/VDDisplay/textrenderer.h>
#include <at/atuicontrols/uibutton.h>
#include <at/atnativeui/theme.h>
#include <at/atui/uimanager.h>
#include <at/atui/uidrawingutils.h>

ATUIButton::ATUIButton()
	: mStockImageIdx(-1)
	, mbDepressed(false)
	, mbHeld(false)
	, mbToggleMode(false)
	, mbFrameEnabled(true)
	, mTextX(0)
	, mTextY(0)
	, mTextColor(0)
	, mTextColorDisabled(0x808080)
	, mActivatedEvent()
	, mPressedEvent()
{
	const auto& tc = ATUIGetThemeColors();
	mTextColor = tc.mButtonFg;
	mTextColorDisabled = tc.mDisabledFg;

	SetTouchMode(kATUITouchMode_Immediate);
	UpdateFillColor();
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
		InvalidateMeasure();
	}
}

void ATUIButton::SetTextColor(uint32 color) {
	if (mTextColor != color) {
		mTextColor = color;

		if (IsEnabled())
			Invalidate();
	}
}

void ATUIButton::SetTextColorDisabled(uint32 color) {
	if (mTextColorDisabled != color) {
		mTextColorDisabled = color;

		if (!IsEnabled())
			Invalidate();
	}
}

void ATUIButton::SetDepressed(bool depressed) {
	if (mbDepressed != depressed) {
		mbDepressed = depressed;

		UpdateFillColor();
		Invalidate();

		if (depressed) {
			if (mPressedEvent)
				mPressedEvent();
		} else {
			if (mActivatedEvent)
				mActivatedEvent();
		}
	}
}

void ATUIButton::SetToggleMode(bool enabled) {
	mbToggleMode = enabled;
}

void ATUIButton::SetFrameEnabled(bool enabled) {
	if (mbFrameEnabled != enabled) {
		mbFrameEnabled = enabled;

		UpdateFillColor();

		Relayout();
		Invalidate();
		InvalidateMeasure();
	}
}

void ATUIButton::OnMouseDownL(sint32 x, sint32 y) {
	if (!IsEnabled())
		return;

	SetHeld(true);
	CaptureCursor();
	Focus();
}

void ATUIButton::OnMouseUpL(sint32 x, sint32 y) {
	SetHeld(false);

	if (IsCursorCaptured())
		ReleaseCursor();
}

void ATUIButton::OnActionStart(uint32 id) {
	switch(id) {
		case kActionActivate:
			SetHeld(true);
			break;

		default:
			ATUIWidget::OnActionStart(id);
	}
}

void ATUIButton::OnActionStop(uint32 id) {
	switch(id) {
		case kActionActivate:
			SetHeld(false);
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

ATUIWidgetMetrics ATUIButton::OnMeasure() {
	ATUIWidgetMetrics m;

	if (mStockImageIdx >= 0) {
		ATUIStockImage& image = mpManager->GetStockImage((ATUIStockImageIdx)mStockImageIdx);

		m.mMinSize.w = image.mWidth;
		m.mMinSize.h = image.mHeight;
	} else if (mpFont) {
		VDDisplayFontMetrics fm;
		mpFont->GetMetrics(fm);

		m.mMinSize = mpFont->MeasureString(mText.data(), mText.size(), false);
		m.mMinSize.h = fm.mAscent + 2*fm.mDescent;
	}

	if (mbFrameEnabled) {
		m.mMinSize.w += 4;
		m.mMinSize.h += 4;
	}

	m.mDesiredSize = m.mMinSize;

	return m;
}

void ATUIButton::OnEnableChanged() {
	Invalidate();
}

void ATUIButton::OnSetFocus() {
	if (mbFrameEnabled) {
		UpdateFillColor();
		Invalidate();
	}
}

void ATUIButton::OnKillFocus() {
	if (mbFrameEnabled) {
		UpdateFillColor();
		Invalidate();
	}
}

void ATUIButton::SetHeld(bool held) {
	if (mbHeld == held)
		return;

	mbHeld = held;

	// We set this even if toggle mode isn't currently set so that toggle
	// mode can be toggled on the fly.
	if (held)
		mbToggleNextState = !mbDepressed;

	if (mHeldEvent)
		mHeldEvent(held);

	if (!mbToggleMode)
		SetDepressed(held);
	else if (held) {
		if (mbToggleNextState)
			SetDepressed(true);
	} else {
		if (!mbToggleNextState)
			SetDepressed(false);
	}
}

void ATUIButton::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	vdrect32 r(0, 0, w, h);

	if (mbFrameEnabled) {
		ATUIDraw3DRect(rdr, r, mbDepressed);
		r.left = 2;
		r.top = 2;
		r.right -= 2;
		r.bottom -= 2;
	}

	if (rdr.PushViewport(r, r.left, r.top)) {
		if (mStockImageIdx >= 0) {
			ATUIStockImage& image = mpManager->GetStockImage((ATUIStockImageIdx)mStockImageIdx);

			VDDisplayBlt blt;
			blt.mDestX = (r.width() - image.mWidth) >> 1;
			blt.mDestY = (r.height() - image.mHeight) >> 1;
			blt.mSrcX = 0;
			blt.mSrcY = 0;
			blt.mWidth = image.mWidth;
			blt.mHeight = image.mHeight;

			rdr.SetColorRGB(IsEnabled() ? mTextColor : mTextColorDisabled);
			rdr.MultiBlt(&blt, 1, image.mImageView, IVDDisplayRenderer::kBltMode_Color);
		} else if (mpFont) {
			VDDisplayTextRenderer *tr = rdr.GetTextRenderer();

			tr->SetFont(mpFont);
			tr->SetAlignment(VDDisplayTextRenderer::kAlignLeft, VDDisplayTextRenderer::kVertAlignTop);
			tr->SetColorRGB(IsEnabled() ? mTextColor : mTextColorDisabled);
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

		sint32 w = mArea.width();
		sint32 h = mArea.height();

		if (mbFrameEnabled) {
			w -= 4;
			h -= 4;
		}

		mTextX = (w - size.w) >> 1;
		mTextY = (h - m.mAscent) >> 1;

		Invalidate();
	}
}

void ATUIButton::UpdateFillColor() {
	const auto& tc = ATUIGetThemeColors();
	if (mbFrameEnabled)
		SetFillColor(mbDepressed ? tc.mButtonPushedBg : HasFocus() ? tc.mFocusedBg : tc.mButtonBg);
	else
		SetAlphaFillColor(0);
}
