#include "stdafx.h"
#include <vd2/system/math.h>
#include <vd2/system/refcount.h>
#include "uianchor.h"

class ATUITranslationAnchor : public vdrefcounted<IATUIAnchor> {
public:
	ATUITranslationAnchor(float fx, float fy) : mFractionX(fx), mFractionY(fy) {}

	virtual void *AsInterface(uint32 typeId) { return NULL; }

	virtual vdrect32 Position(const vdrect32& containerArea, const vdsize32& size);

protected:
	const float mFractionX;
	const float mFractionY;
};

vdrect32 ATUITranslationAnchor::Position(const vdrect32& containerArea, const vdsize32& size) {
	const sint32 dx = containerArea.width() - size.w;
	const sint32 dy = containerArea.height() - size.h;

	vdrect32 r(containerArea);

	if (dx > 0) {
		r.left = VDRoundToInt32(dx * mFractionX);
		r.right = r.left + size.w;
	}

	if (dy > 0) {
		r.top = VDRoundToInt32(dy * mFractionY);
		r.bottom = r.top + size.h;
	}

	return r;
}

void ATUICreateTranslationAnchor(float fractionX, float fractionY, IATUIAnchor **anchor) {
	IATUIAnchor *p = new ATUITranslationAnchor(fractionX, fractionY);

	p->AddRef();
	*anchor = p;
}
