#include "stdafx.h"
#include <vd2/VDDisplay/textrenderer.h>
#include "uiwidget.h"
#include "uianchor.h"
#include "uimanager.h"
#include "uicontainer.h"
#include "uidrawingutils.h"

ATUIWidget::ATUIWidget()
	: mpManager(NULL)
	, mpParent(NULL)
	, mArea(0, 0, 0, 0)
	, mClientArea(0, 0, 0, 0)
	, mFillColor(0xFF000000)
	, mCursorImage(0)
	, mDockMode(kATUIDockMode_None)
	, mFrameMode(kATUIFrameMode_None)
	, mpAnchor(NULL)
	, mInstanceId(0)
	, mOwnerId(0)
	, mbVisible(true)
	, mbFastClip(false)
	, mbHitTransparent(false)
{
}

ATUIWidget::~ATUIWidget() {
	vdsaferelease <<= mpAnchor;
}

void ATUIWidget::Destroy() {
	if (mpParent)
		mpParent->RemoveChild(this);
}

void ATUIWidget::Focus() {
	if (mpManager)
		mpManager->SetActiveWindow(this);
}

ATUIWidget *ATUIWidget::GetOwner() const {
	if (!mOwnerId || !mpManager)
		return NULL;

	return mpManager->GetWindowByInstance(mOwnerId);
}

void ATUIWidget::SetOwner(ATUIWidget *w) {
	mOwnerId = w->GetInstanceId();
}

ATUIWidget *ATUIWidget::GetParentOrOwner() const {
	ATUIWidget *w = GetOwner();

	if (!w)
		w = GetParent();

	return w;
}

bool ATUIWidget::HasFocus() const {
	return mpManager && mpManager->GetFocusWindow() == this;
}

bool ATUIWidget::HasCursor() const {
	return mpManager && mpManager->GetCursorWindow() == this;
}

bool ATUIWidget::IsCursorCaptured() const {
	return mpManager && mpManager->GetCursorCaptureWindow() == this;
}

void ATUIWidget::CaptureCursor(bool motionMode, bool constrained) {
	if (mpManager)
		mpManager->CaptureCursor(this, motionMode, constrained);
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

void ATUIWidget::SetFrameMode(ATUIFrameMode frameMode) {
	if (mFrameMode != frameMode) {
		mFrameMode = frameMode;

		RecomputeClientArea();
	}
}

void ATUIWidget::SetCursorImage(uint32 id) {
	if (mCursorImage == id)
		return;

	mCursorImage = id;

	if (mpManager)
		mpManager->UpdateCursorImage(this);
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

		// We only need to invalidate on a change of size, not position. Otherwise, we only need to
		// invalidate the parent.
		if (mArea.size() != r2.size())
			Invalidate();
		else if (mpParent && mbVisible)
			mpParent->Invalidate();

		mArea = r2;

		RecomputeClientArea();
	}
}

vdrect32 ATUIWidget::ComputeWindowSize(const vdrect32& clientArea) const {
	vdrect32 r(clientArea);

	if (mFrameMode) {
		r.left -= 2;
		r.top -= 2;
		r.right += 2;
		r.bottom += 2;
	}

	return r;
}

void ATUIWidget::SetDockMode(ATUIDockMode mode) {
	if (mDockMode == mode)
		return;

	mDockMode = mode;

	if (mpParent)
		mpParent->InvalidateLayout();
}

void ATUIWidget::SetAnchor(IATUIAnchor *anchor) {
	if (mpAnchor == anchor)
		return;

	if (anchor)
		anchor->AddRef();

	if (mpAnchor)
		mpAnchor->Release();

	mpAnchor = anchor;

	if (mpParent)
		mpParent->InvalidateLayout();
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

	return false;
}

ATUIWidget *ATUIWidget::HitTest(vdpoint32 pt) {
	return mbVisible && !mbHitTransparent && mArea.contains(pt) ? this : NULL;
}

bool ATUIWidget::TranslateScreenPtToClientPt(vdpoint32 spt, vdpoint32& cpt) {
	sint32 x = spt.x;
	sint32 y = spt.y;

	for(ATUIWidget *w = this; w; w = w->GetParent()) {
		x -= w->mArea.left;
		y -= w->mArea.top;
		x -= w->mClientArea.left;
		y -= w->mClientArea.top;
	}

	return TranslateWindowPtToClientPt(vdpoint32(x, y), cpt);
}

bool ATUIWidget::TranslateWindowPtToClientPt(vdpoint32 wpt, vdpoint32& cpt) {
	cpt.x = wpt.x - mClientArea.left;
	cpt.y = wpt.y - mClientArea.top;

	return mClientArea.contains(wpt);
}

vdpoint32 ATUIWidget::TranslateClientPtToScreenPt(vdpoint32 cpt) {
	for(ATUIWidget *w = this; w; w = w->GetParent()) {
		cpt.x += w->mClientArea.left;
		cpt.y += w->mClientArea.top;
		cpt.x += w->mArea.left;
		cpt.y += w->mArea.top;
	}

	return cpt;
}

void ATUIWidget::UnbindAllActions() {
	mActionMap.clear();
}

void ATUIWidget::BindAction(const ATUITriggerBinding& binding) {
	mActionMap.push_back(binding);
}

void ATUIWidget::BindAction(uint32 vk, uint32 action, uint32 mod, uint32 instanceid) {
	ATUITriggerBinding binding;
	binding.mVk = vk;
	binding.mModMask = ATUITriggerBinding::kModAll;
	binding.mModVal = mod;
	binding.mAction = action;
	binding.mTargetInstanceId = instanceid;

	BindAction(binding);
}

const ATUITriggerBinding *ATUIWidget::FindAction(uint32 vk, uint32 extvk, uint32 mods) const {
	for(ActionMap::const_iterator it(mActionMap.begin()), itEnd(mActionMap.end());
		it != itEnd;
		++it)
	{
		const ATUITriggerBinding& binding = *it;

		if (binding.mVk == vk || binding.mVk == extvk) {
			if (!((mods ^ binding.mModVal) & binding.mModMask))
				return &binding;
		}
	}

	return NULL;
}

void ATUIWidget::OnMouseRelativeMove(sint32 x, sint32 y) {
}

void ATUIWidget::OnMouseMove(sint32 x, sint32 y) {
}

void ATUIWidget::OnMouseDownL(sint32 x, sint32 y) {
}

void ATUIWidget::OnMouseDblClkL(sint32 x, sint32 y) {
	OnMouseDownL(x, y);
}

void ATUIWidget::OnMouseUpL(sint32 x, sint32 y) {
}

void ATUIWidget::OnMouseDown(sint32 x, sint32 y, uint32 vk, bool dblclk) {
	if (vk == kATUIVK_LButton) {
		if (dblclk)
			OnMouseDblClkL(x, y);
		else
			OnMouseDownL(x, y);
	}
}

void ATUIWidget::OnMouseUp(sint32 x, sint32 y, uint32 vk) {
	if (vk == kATUIVK_LButton)
		OnMouseUpL(x, y);
}

void ATUIWidget::OnMouseWheel(sint32 x, sint32 y, float delta) {
}

void ATUIWidget::OnMouseLeave() {
}

void ATUIWidget::OnMouseHover(sint32 x, sint32 y) {
}

bool ATUIWidget::OnContextMenu(const vdpoint32 *pt) {
	return false;
}

bool ATUIWidget::OnKeyDown(const ATUIKeyEvent& event) {
	uint32 mods = 0;

	if (mpManager->IsKeyDown(kATUIVK_Control))
		mods += ATUITriggerBinding::kModCtrl;

	if (mpManager->IsKeyDown(kATUIVK_Shift))
		mods += ATUITriggerBinding::kModShift;

	if (mpManager->IsKeyDown(kATUIVK_Alt))
		mods += ATUITriggerBinding::kModAlt;

	const ATUITriggerBinding *binding = FindAction(event.mVirtKey, event.mExtendedVirtKey, mods);
	if (binding) {
		mpManager->BeginAction(this, *binding);
		return true;
	}

	return false;
}

bool ATUIWidget::OnKeyUp(const ATUIKeyEvent& event) {
	return false;
}

bool ATUIWidget::OnChar(const ATUICharEvent& event) {
	return false;
}

void ATUIWidget::OnActionStart(uint32 id) {
	if (id == kActionFocus)
		Focus();
}

void ATUIWidget::OnActionRepeat(uint32 trid) {
}

void ATUIWidget::OnActionStop(uint32 trid) {
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

void ATUIWidget::OnCaptureLost() {
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

	bool drawInner = true;
	bool framed = false;

	if (mFrameMode) {
		vdrect32 frameRect(0, 0, mArea.width(), mArea.height());

		switch(mFrameMode) {
			case kATUIFrameMode_Raised:
				ATUIDraw3DRect(*sr, frameRect, false);
				break;
			case kATUIFrameMode_Sunken:
				ATUIDraw3DRect(*sr, frameRect, true);
				break;
			case kATUIFrameMode_SunkenThin:
				ATUIDrawThin3DRect(*sr, frameRect, false);
				break;
			case kATUIFrameMode_RaisedEdge:
				ATUIDrawThin3DRect(*sr, frameRect, false);
				++frameRect.left;
				++frameRect.top;
				--frameRect.right;
				--frameRect.bottom;
				ATUIDrawThin3DRect(*sr, frameRect, true);
				break;
		}

		drawInner = sr->PushViewport(mClientArea, mClientArea.left, mClientArea.top);
		framed = true;
	}

	if (drawInner) {
		if (mFillColor >= 0x01000000) {
			if (sr == &rdr && sr->GetCaps().mbSupportsAlphaBlending && mFillColor < 0xFF000000) {
				sr->AlphaFillRect(0, 0, mArea.width(), mArea.height(), mFillColor);
			} else {
				sr->SetColorRGB(mFillColor);
				sr->FillRect(0, 0, mArea.width(), mArea.height());
			}
		}

		sr->SetColorRGB(0);

		Paint(*sr, mClientArea.width(), mClientArea.height());

		if (framed)
			sr->PopViewport();
	}

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

void ATUIWidget::RecomputeClientArea() {
	vdrect32 clientArea(0, 0, mArea.width(), mArea.height());

	if (mFrameMode) {
		if (mFrameMode == kATUIFrameMode_SunkenThin) {
			++clientArea.left;
			++clientArea.top;
			--clientArea.right;
			--clientArea.bottom;
		} else {
			clientArea.left += 2;
			clientArea.top += 2;
			clientArea.right -= 2;
			clientArea.bottom -= 2;
		}

		if (clientArea.empty())
			clientArea.set(0, 0, 0, 0);
	}

	if (mClientArea != clientArea) {
		mClientArea = clientArea;

		OnSize();
	}
}
