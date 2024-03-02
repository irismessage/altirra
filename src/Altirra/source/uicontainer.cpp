#include "stdafx.h"
#include "uicontainer.h"
#include "uimanager.h"

ATUIContainer::ATUIContainer() {
	mbFastClip = true;
	SetAlphaFillColor(0);
}

ATUIContainer::~ATUIContainer() {
	RemoveAllChildren();
}

void ATUIContainer::AddChild(ATUIWidget *w) {
	w->AddRef();
	w->SetParent(mpManager, this);

	mWidgets.push_back(w);
}

void ATUIContainer::RemoveChild(ATUIWidget *w) {
	for(Widgets::iterator it(mWidgets.begin()), itEnd(mWidgets.end());
		it != itEnd;
		++it)
	{
		if (*it == w) {
			mWidgets.erase(it);
			w->SetParent(NULL, NULL);
			w->Release();
		}
	}
}

void ATUIContainer::RemoveAllChildren() {
	while(!mWidgets.empty()) {
		ATUIWidget *w = mWidgets.back();

		mWidgets.pop_back();

		w->SetParent(NULL, NULL);
		w->Release();
	}
}

ATUIWidget *ATUIContainer::HitTest(vdpoint32 pt) {
	if (!mbVisible || !mArea.contains(pt))
		return NULL;

	pt.x -= mArea.left;
	pt.y -= mArea.top;

	for(Widgets::const_reverse_iterator it(mWidgets.rbegin()), itEnd(mWidgets.rend());
		it != itEnd;
		++it)
	{
		ATUIWidget *w = *it;

		ATUIWidget *r = w->HitTest(pt);
		if (r)
			return r;
	}

	return mbHitTransparent ? NULL : this;
}

void ATUIContainer::OnSize() {
	vdrect32 r(mArea);
	vdrect32 r2;

	r.translate(-r.left, -r.top);

	for(Widgets::const_reverse_iterator it(mWidgets.rbegin()), itEnd(mWidgets.rend());
		it != itEnd;
		++it)
	{
		ATUIWidget *w = *it;

		switch(w->GetDockMode()) {
			case kATUIDockMode_None:
				break;

			case kATUIDockMode_Left:
				r2 = r;
				r2.right = r2.left + w->GetArea().width();
				r.left += r2.width();
				w->SetArea(r2);
				break;

			case kATUIDockMode_Right:
				r2 = r;
				r2.left = r2.right - w->GetArea().width();
				r.right -= r2.width();
				w->SetArea(r2);
				break;

			case kATUIDockMode_Top:
				r2 = r;
				r2.bottom = r2.top + w->GetArea().height();
				r.top += r2.height();
				w->SetArea(r2);
				break;

			case kATUIDockMode_Bottom:
				r2 = r;
				r2.top = r2.bottom - w->GetArea().height();
				r.bottom -= r2.height();
				w->SetArea(r2);
				break;

			case kATUIDockMode_Fill:
				w->SetArea(r);
				break;
		}
	}
}

void ATUIContainer::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	for(Widgets::const_iterator it(mWidgets.begin()), itEnd(mWidgets.end());
		it != itEnd;
		++it)
	{
		ATUIWidget *w = *it;

		w->Draw(rdr);
	}
}
