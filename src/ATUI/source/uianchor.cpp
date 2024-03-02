#include <stdafx.h>
#include <vd2/system/math.h>
#include <vd2/system/refcount.h>
#include <at/atui/uianchor.h>

class ATUITranslationAnchor : public vdrefcounted<IATUIAnchor> {
public:
	ATUITranslationAnchor(float fx, float fy) : mFractionX(fx), mFractionY(fy) {}

	virtual void *AsInterface(uint32 typeId) { return NULL; }

	virtual vdrect32 Position(const vdrect32& containerArea, const vdsize32& size, const ATUIWidgetMetrics& m);

protected:
	const float mFractionX;
	const float mFractionY;
};

vdrect32 ATUITranslationAnchor::Position(const vdrect32& containerArea, const vdsize32& size, const ATUIWidgetMetrics& m) {
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

class ATUIProportionAnchor : public vdrefcounted<IATUIProportionAnchor> {
public:
	ATUIProportionAnchor(const vdrect32f& area, const vdrect32& offsets) : mArea(area), mOffsets(offsets) {}

public:
	vdrect32f GetArea() const override;
	vdrect32 GetOffsets() const override;
	vdfloat2 GetPivot() const override;
	void SetArea(const vdrect32f& area) override;
	void SetOffsets(const vdrect32& offset) override;
	void SetPivot(const vdfloat2& pt) override;

public:
	void *AsInterface(uint32 typeId) override;

public:
	vdrect32 Position(const vdrect32& containerArea, const vdsize32& size, const ATUIWidgetMetrics& m) override;


protected:
	vdrect32f mArea;
	vdrect32 mOffsets;
	vdfloat2 mPivot;
};

void *ATUIProportionAnchor::AsInterface(uint32 typeId) {
	if (typeId == IATUIProportionAnchor::kTypeID)
		return static_cast<IATUIProportionAnchor *>(this);

	return nullptr;
}

vdrect32f ATUIProportionAnchor::GetArea() const {
	return mArea;
}

vdrect32 ATUIProportionAnchor::GetOffsets() const {
	return mOffsets;
}

vdfloat2 ATUIProportionAnchor::GetPivot() const {
	return mPivot;
}

void ATUIProportionAnchor::SetArea(const vdrect32f& area) {
	mArea = area;
}

void ATUIProportionAnchor::SetOffsets(const vdrect32& offset) {
	mOffsets = offset;
}

void ATUIProportionAnchor::SetPivot(const vdfloat2& pivot) {
	mPivot = pivot;
}

vdrect32 ATUIProportionAnchor::Position(const vdrect32& containerArea, const vdsize32& size, const ATUIWidgetMetrics& m) {
	const float w = (float)containerArea.width();
	const float h = (float)containerArea.height();

	vdrect32 r;
	r.left = VDRoundToInt32(w * mArea.left) + mOffsets.left;
	r.top = VDRoundToInt32(h * mArea.top) + mOffsets.top;
	r.right = std::max<sint32>(r.left, VDRoundToInt32(w * mArea.right) + mOffsets.right);
	r.bottom = std::max<sint32>(r.top, VDRoundToInt32(h * mArea.bottom) + mOffsets.bottom);

	return r;
}

void ATUICreateProportionAnchor(const vdrect32f& area, IATUIAnchor **anchor) {
	IATUIAnchor *p = new ATUIProportionAnchor(area, vdrect32(0, 0, 0, 0));

	p->AddRef();
	*anchor = p;
}

void ATUICreateProportionAnchor(const vdrect32f& area, const vdrect32& offsets, IATUIAnchor **anchor) {
	IATUIAnchor *p = new ATUIProportionAnchor(area, offsets);

	p->AddRef();
	*anchor = p;
}
