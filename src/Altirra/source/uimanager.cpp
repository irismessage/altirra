#include <stdafx.h>
#include <vd2/system/vdalloc.h>
#include <vd2/Kasumi/triblt.h>
#include <vd2/VDDisplay/font.h>
#include <vd2/VDDisplay/textrenderer.h>
#include <vd2/VDDisplay/renderersoft.h>
#include "uimanager.h"
#include "uicontainer.h"

///////////////////////////////////////////////////////////////////////////

ATUIManager::ATUIManager()
	: mpNativeDisplay(NULL)
	, mpMainWindow(NULL)
	, mpCursorWindow(NULL)
	, mpFocusWindow(NULL)
	, mbCursorCaptured(false)
	, mCursorImageId(kATUICursorImage_None)
	, mbInvalidated(false)
{
	for(size_t i=0; i<vdcountof(mpStockImages); ++i)
		mpStockImages[i] = NULL;
}

ATUIManager::~ATUIManager() {
	Shutdown();
}

void ATUIManager::Init(IATUINativeDisplay *natDisp) {
	mpNativeDisplay = natDisp;

	mpMainWindow = new ATUIContainer;
	mpMainWindow->AddRef();
	mpMainWindow->SetHitTransparent(true);
	mpMainWindow->SetParent(this, NULL);

	VDCreateDisplaySystemFont(20, false, "MS Shell Dlg", &mpThemeFonts[kATUIThemeFont_Default]);
	VDCreateDisplaySystemFont(14, false, "Lucida Console", &mpThemeFonts[kATUIThemeFont_MonoSmall]);
	VDCreateDisplaySystemFont(20, false, "Lucida Console", &mpThemeFonts[kATUIThemeFont_Mono]);
	VDCreateDisplaySystemFont(-11, false, "Tahoma", &mpThemeFonts[kATUIThemeFont_Tooltip]);
	VDCreateDisplaySystemFont(-11, true, "Tahoma", &mpThemeFonts[kATUIThemeFont_TooltipBold]);
	mpThemeFonts[kATUIThemeFont_Menu] = mpThemeFonts[kATUIThemeFont_Tooltip];
	mpThemeFonts[kATUIThemeFont_Menu]->AddRef();

	static const wchar_t kMagicChars[]={
		L'a',	// menu check
		L'h',	// menu radio
		L'8',	// menu arrow
	};

	VDDisplayFontMetrics menuFontMetrics;
	mpThemeFonts[kATUIThemeFont_Menu]->GetMetrics(menuFontMetrics);

	vdrefptr<IVDDisplayFont> marlett;
	VDCreateDisplaySystemFont(menuFontMetrics.mAscent + menuFontMetrics.mDescent + 2, false, "Marlett", ~marlett);

	for(int i=0; i<3; ++i) {
		vdfastvector<VDDisplayFontGlyphPlacement> placements;
		vdrect32 bounds;
		marlett->ShapeText(&kMagicChars[i], 1, placements, &bounds, NULL, NULL);

		ATUIStockImage *img1 = new ATUIStockImage;
		mpStockImages[i] = img1;
		img1->mBuffer.init(bounds.width(), bounds.height(), nsVDPixmap::kPixFormat_XRGB8888);
		img1->mWidth = bounds.width();
		img1->mHeight = bounds.height();
		img1->mOffsetX = bounds.left;
		img1->mOffsetY = bounds.top;

		VDDisplayRendererSoft rs;
		rs.Init();
		rs.Begin(img1->mBuffer);
		rs.SetColorRGB(0);
		rs.FillRect(0, 0, bounds.width(), bounds.height());
		VDDisplayTextRenderer& tr = *rs.GetTextRenderer();
		tr.SetColorRGB(0xFFFFFF);
		tr.SetFont(marlett);
		tr.SetAlignment(VDDisplayTextRenderer::kAlignLeft, VDDisplayTextRenderer::kVertAlignTop);
		tr.SetPosition(0, 0);
		tr.DrawTextSpan(&kMagicChars[i], 1);

		img1->mImageView.SetImage(img1->mBuffer, false);
	}
}

void ATUIManager::Shutdown() {
	vdsafedelete <<= mpStockImages;

	vdsaferelease <<= mpMainWindow;
	vdsaferelease <<= mpThemeFonts;

	mpNativeDisplay = NULL;
}

ATUIContainer *ATUIManager::GetMainWindow() const {
	return mpMainWindow;
}

void ATUIManager::Resize(sint32 w, sint32 h) {
	if (w > 0 && h > 0 && mpMainWindow)
		mpMainWindow->SetArea(vdrect32(0, 0, w, h));
}

void ATUIManager::SetFocusWindow(ATUIWidget *w) {
	if (mpFocusWindow == w)
		return;

	ATUIWidget *prevFocus = mpFocusWindow;
	mpFocusWindow = w;

	if (prevFocus)
		prevFocus->OnKillFocus();

	if (mpFocusWindow == w)
		w->OnSetFocus();
}

void ATUIManager::CaptureCursor(ATUIWidget *w) {
	if (w && mpCursorWindow != w) {
		if (mpCursorWindow)
			mpCursorWindow->OnMouseLeave();

		mpCursorWindow = w;
		UpdateCursorImage();
	}

	bool newCaptureState = (w != NULL);

	if (mbCursorCaptured != newCaptureState) {
		mbCursorCaptured = newCaptureState;

		if (mpNativeDisplay) {
			if (newCaptureState)
				mpNativeDisplay->CaptureCursor();
			else
				mpNativeDisplay->ReleaseCursor();
		}
	}
}

void ATUIManager::BeginModal(ATUIWidget *w) {
	if (!w || w->GetManager() != this) {
		VDASSERT(false);
		return;
	}

	if (!mpNativeDisplay)
		mpNativeDisplay->BeginModal();

	ModalEntry& me = mModalStack.push_back();
	me.mpPreviousModal = mpModalWindow;

	mpModalWindow = w;
}

void ATUIManager::EndModal() {
	VDASSERT(mpModalWindow);
	VDASSERT(!mModalStack.empty());

	const ModalEntry& me = mModalStack.back();

	mpModalWindow = me.mpPreviousModal;
	mModalStack.pop_back();

	if (mpNativeDisplay)
		mpNativeDisplay->EndModal();
}

bool ATUIManager::IsKeyDown(uint32 vk) {
	return mpNativeDisplay && mpNativeDisplay->IsKeyDown(vk);
}

bool ATUIManager::OnMouseMove(sint32 x, sint32 y) {
	if (!mpMainWindow)
		return false;

	if (!mbCursorCaptured) {
		if (!UpdateCursorWindow(x, y))
			return true;
	}

	if (!mpCursorWindow)
		return false;

	for(ATUIWidget *w = mpCursorWindow; w; w = w->GetParent()) {
		const vdrect32& area = w->GetArea();

		x -= area.left;
		y -= area.top;
	}

	mpCursorWindow->OnMouseMove(x, y);
	return true;
}

bool ATUIManager::OnMouseDownL(sint32 x, sint32 y) {
	if (!mpMainWindow)
		return false;

	if (!mbCursorCaptured) {
		if (!UpdateCursorWindow(x, y))
			return true;
	}

	if (!mpCursorWindow)
		return false;

	for(ATUIWidget *w = mpCursorWindow; w; w = w->GetParent()) {
		const vdrect32& area = w->GetArea();

		x -= area.left;
		y -= area.top;
	}

	mpCursorWindow->OnMouseDownL(x, y);
	return true;
}

bool ATUIManager::OnMouseUpL(sint32 x, sint32 y) {
	if (!mpMainWindow)
		return false;

	if (!mbCursorCaptured) {
		if (!UpdateCursorWindow(x, y))
			return true;
	}

	if (!mpCursorWindow)
		return false;

	for(ATUIWidget *w = mpCursorWindow; w; w = w->GetParent()) {
		const vdrect32& area = w->GetArea();

		x -= area.left;
		y -= area.top;
	}

	mpCursorWindow->OnMouseUpL(x, y);
	return true;
}

void ATUIManager::OnMouseLeave() {
	if (mpMainWindow)
		mpMainWindow->OnMouseLeave();
}

bool ATUIManager::OnKeyDown(uint32 vk) {
	for(ATUIWidget *w = mpFocusWindow; w; w = w->GetParent()) {
		if (w->OnKeyDown(vk))
			return true;
	}

	return false;
}

bool ATUIManager::OnKeyUp(uint32 vk) {
	for(ATUIWidget *w = mpFocusWindow; w; w = w->GetParent()) {
		if (w->OnKeyUp(vk))
			return true;
	}

	return false;
}

bool ATUIManager::OnChar(uint32 ch) {
	for(ATUIWidget *w = mpFocusWindow; w; w = w->GetParent()) {
		if (w->OnChar(ch))
			return true;
	}

	return false;
}

void ATUIManager::Attach(ATUIWidget *w) {
}

void ATUIManager::Detach(ATUIWidget *w) {
	VDASSERT(mpModalWindow != w);

	if (mpCursorWindow == w) {
		mpCursorWindow = NULL;
		mbCursorCaptured = false;
		UpdateCursorImage();
	}

	if (mpFocusWindow == w)
		mpFocusWindow = w->GetParent();

	for(ModalStack::iterator it = mModalStack.begin(), itEnd = mModalStack.end();
		it != itEnd;
		++it)
	{
		ModalEntry& ent = *it;

		if (ent.mpPreviousModal == w)
			ent.mpPreviousModal = w->GetParent();
	}
}

void ATUIManager::Invalidate(ATUIWidget *w) {
	if (!mbInvalidated) {
		mbInvalidated = true;

		if (mpNativeDisplay)
			mpNativeDisplay->Invalidate();
	}
}

void ATUIManager::Composite(IVDDisplayRenderer& r, const VDDisplayCompositeInfo& compInfo) {
	if (mpMainWindow)
		mpMainWindow->Draw(r);

	mbInvalidated = false;
}

void ATUIManager::UpdateCursorImage() {
	uint32 id = 0;

	for(ATUIWidget *w = mpCursorWindow; w; w = w->GetParent()) {
		id = w->GetCursorImage();

		if (id)
			break;
	}

	if (mCursorImageId != id)
		mCursorImageId = id;
}

bool ATUIManager::UpdateCursorWindow(sint32 x, sint32 y) {
	ATUIWidget *w = mpMainWindow->HitTest(vdpoint32(x, y));

	if (w != mpCursorWindow) {
		if (mpModalWindow && !mpModalWindow->IsSameOrAncestorOf(w))
			return false;

		if (mpCursorWindow)
			mpCursorWindow->OnMouseLeave();

		mpCursorWindow = w;
		UpdateCursorImage();
	}

	return true;
}
