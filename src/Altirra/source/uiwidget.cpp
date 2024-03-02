#include "stdafx.h"
#include <vd2/VDDisplay/textrenderer.h>
#include "uiwidget.h"
#include "uimanager.h"
#include "uicontainer.h"

ATUIWidget::ATUIWidget()
	: mpManager(NULL)
	, mpParent(NULL)
	, mArea(0, 0, 0, 0)
	, mFillColor(0xFF000000)
	, mCursorImage(0)
	, mDockMode(kATUIDockMode_None)
	, mbVisible(true)
	, mbFastClip(false)
	, mbHitTransparent(false)
{
}

ATUIWidget::~ATUIWidget() {
}

void ATUIWidget::Destroy() {
	if (mpParent)
		mpParent->RemoveChild(this);
}

void ATUIWidget::Focus() {
	if (mpManager)
		mpManager->SetFocusWindow(this);
}

bool ATUIWidget::IsCursorCaptured() const {
	return mpManager && mpManager->GetCursorCaptureWindow() == this;
}

void ATUIWidget::CaptureCursor() {
	if (mpManager)
		mpManager->CaptureCursor(this);
}

void ATUIWidget::ReleaseCursor() {
	if (mpManager)
		mpManager->CaptureCursor(NULL);
}

void ATUIWidget::SetFillColor(uint32 color) {
	SetAlphaFillColor(color | 0xFF000000);
}

void ATUIWidget::SetAlphaFillColor(uint32 color) {
	if (mFillColor != color) {
		mFillColor = color;
		Invalidate();
	}
}

void ATUIWidget::SetPosition(const vdpoint32& pt) {
	vdrect32 r(mArea);
	r.translate(pt.x - r.left, pt.y - r.top);

	SetArea(r);
}

void ATUIWidget::SetSize(const vdsize32& sz) {
	vdrect32 r(mArea);

	r.resize(sz.w, sz.h);
	SetArea(r);
}

void ATUIWidget::SetArea(const vdrect32& r) {
	if (mArea != r) {
		vdrect32 r2 = r;
		if (r2.right < r2.left)
			r2.right = r2.left;

		if (r2.bottom < r2.top)
			r2.bottom = r2.top;

		// We only need to invalidate on a change of size, not position.
		if (mArea.size() != r2.size()) {
			Invalidate();
			OnSize();
		}

		mArea = r2;
	}
}

void ATUIWidget::SetDockMode(ATUIDockMode mode) {
	mDockMode = mode;
}

void ATUIWidget::SetVisible(bool visible) {
	if (mbVisible == visible)
		return;

	if (mbVisible && mpManager)
		mpManager->Invalidate(this);

	mbVisible = visible;

	if (visible)
		Invalidate();
}

bool ATUIWidget::IsSameOrAncestorOf(ATUIWidget *w) const {
	while(w) {
		if (w == this)
			return true;

		w = w->GetParent();
	}

	return true;
}

ATUIWidget *ATUIWidget::HitTest(vdpoint32 pt) {
	return mbVisible && !mbHitTransparent && mArea.contains(pt) ? this : NULL;
}

void ATUIWidget::OnMouseMove(sint32 x, sint32 y) {
}

void ATUIWidget::OnMouseDownL(sint32 x, sint32 y) {
}

void ATUIWidget::OnMouseUpL(sint32 x, sint32 y) {
}

void ATUIWidget::OnMouseLeave() {
}

bool ATUIWidget::OnKeyDown(uint32 vk) {
	return false;
}

bool ATUIWidget::OnKeyUp(uint32 vk) {
	return false;
}

bool ATUIWidget::OnChar(uint32 vk) {
	return false;
}

void ATUIWidget::OnCreate() {
}

void ATUIWidget::OnDestroy() {
}

void ATUIWidget::OnSize() {
}

void ATUIWidget::OnKillFocus() {
}

void ATUIWidget::OnSetFocus() {
}

void ATUIWidget::Draw(IVDDisplayRenderer& rdr) {
	if (!mbVisible)
		return;

	IVDDisplayRenderer *sr = &rdr;
	
	if (mbFastClip) {
		if (!rdr.PushViewport(mArea, mArea.left, mArea.top))
			return;
	} else {
		sr = rdr.BeginSubRender(mArea, mRenderCache);

		if (!sr)
			return;
	}

	if (sr == &rdr && sr->GetCaps().mbSupportsAlphaBlending && mFillColor < 0xFF000000) {
		sr->AlphaFillRect(0, 0, mArea.width(), mArea.height(), mFillColor);
	} else if (mFillColor >= 0x01000000) {
		sr->SetColorRGB(mFillColor);
		sr->FillRect(0, 0, mArea.width(), mArea.height());
	}

	sr->SetColorRGB(0);

	Paint(*sr, mArea.width(), mArea.height());

	if (mbFastClip)
		rdr.PopViewport();
	else
		rdr.EndSubRender();
}

void ATUIWidget::Invalidate() {
	if (mbVisible) {
		mRenderCache.Invalidate();

		if (mpManager)
			mpManager->Invalidate(this);
	}
}

void ATUIWidget::SetParent(ATUIManager *mgr, ATUIContainer *parent) {
	if (mpManager) {
		OnDestroy();
		mpManager->Detach(this);

		if (mbVisible && !mArea.empty())
			mpManager->Invalidate(this);
	}

	mpManager = mgr;
	mpParent = parent;

	if (mgr) {
		mgr->Attach(this);
		OnCreate();

		if (mbVisible && !mArea.empty())
			mgr->Invalidate(this);
	}
}
