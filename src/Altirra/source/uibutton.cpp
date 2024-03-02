#include "stdafx.h"
#include <vd2/VDDisplay/textrenderer.h>
#include "uibutton.h"
#include "uimanager.h"

namespace {
	void DrawBevel(IVDDisplayRenderer& rdr, const vdrect32& r, uint32 tlColor, uint32 brColor) {
		vdpoint32 pts[5] = {
			vdpoint32(r.right-1, r.top),
			vdpoint32(r.left, r.top),
			vdpoint32(r.left, r.bottom-1),
			vdpoint32(r.right-1, r.bottom-1),
			vdpoint32(r.right-1, r.top),
		};

		rdr.SetColorRGB(tlColor);
		rdr.PolyLine(pts, 2);
		rdr.SetColorRGB(brColor);
		rdr.PolyLine(pts+2, 2);
	}

	void DrawThin3DRect(IVDDisplayRenderer& rdr, const vdrect32& r, bool depressed) {
		DrawBevel(rdr, vdrect32(r.left+1, r.top+1, r.right-1, r.bottom-1), depressed ? 0x404040 : 0xFFFFFF, depressed ? 0xFFFFFF : 0x404040);
	}

	void Draw3DRect(IVDDisplayRenderer& rdr, const vdrect32& r, bool depressed) {
		DrawBevel(rdr, r, depressed ? 0x404040 : 0xD4D0C8, depressed ? 0xD4D0C8 : 0x404040);
		DrawBevel(rdr, vdrect32(r.left+1, r.top+1, r.right-1, r.bottom-1), depressed ? 0x404040 : 0xFFFFFF, depressed ? 0xFFFFFF : 0x404040);
	}
}

ATUIButton::ATUIButton()
	: mbDepressed(false)
	, mTextX(0)
	, mTextY(0)
{
	SetFillColor(0xD4D0C8);
}

ATUIButton::~ATUIButton() {
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
	}
}

void ATUIButton::OnMouseDownL(sint32 x, sint32 y) {
	SetDepressed(true);
	CaptureCursor();
	Focus();
}

void ATUIButton::OnMouseUpL(sint32 x, sint32 y) {
	ReleaseCursor();
	SetDepressed(false);
}

bool ATUIButton::OnKeyDown(uint32 vk) {
	switch(vk) {
		case kATUIVK_Return:
			SetDepressed(true);
			return true;
	}

	return false;
}

bool ATUIButton::OnKeyUp(uint32 vk) {
	switch(vk) {
		case kATUIVK_Return:
			SetDepressed(false);
			return true;
	}

	return false;
}

void ATUIButton::OnCreate() {
	mpFont = mpManager->GetThemeFont(kATUIThemeFont_Default);

	Relayout();
}

void ATUIButton::OnSize() {
	Relayout();
}

void ATUIButton::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	Draw3DRect(rdr, vdrect32(0, 0, w, h), mbDepressed);

	if (mpFont && rdr.PushViewport(vdrect32(2, 2, w-2, h-2), 2, 2)) {
		VDDisplayTextRenderer *tr = rdr.GetTextRenderer();

		tr->SetFont(mpFont);
		tr->SetAlignment(VDDisplayTextRenderer::kAlignLeft, VDDisplayTextRenderer::kVertAlignTop);
		tr->SetColorRGB(0);
		tr->DrawTextLine(mTextX, mTextY, mText.c_str());

		rdr.PopViewport();
	}
}

void ATUIButton::Relayout() {
	if (mpFont) {
		vdsize32 size = mpFont->MeasureString(mText.data(), mText.size(), false);

		mTextX = ((mArea.width() - 4) - size.w) >> 1;
		mTextY = ((mArea.height() - 4) - size.h) >> 1;

		Invalidate();
	}
}
