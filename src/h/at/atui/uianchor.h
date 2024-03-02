#ifndef f_AT_UIANCHOR_H
#define f_AT_UIANCHOR_H

#include <vd2/system/unknown.h>
#include <vd2/system/vectors.h>

struct ATUIWidgetMetrics;

class IATUIAnchor : public IVDRefUnknown {
public:
	virtual vdrect32 Position(const vdrect32& containerArea, const vdsize32& size, const ATUIWidgetMetrics& m) = 0;
};

class IATUIProportionAnchor : public IATUIAnchor {
public:
	enum : uint32 { kTypeID = 'uipa' };

	virtual vdrect32f GetArea() const = 0;
	virtual vdrect32 GetOffsets() const = 0;
	virtual vdfloat2 GetPivot() const = 0;
	virtual void SetArea(const vdrect32f& area) = 0;
	virtual void SetOffsets(const vdrect32& offsets) = 0;
	virtual void SetPivot(const vdfloat2& pt) = 0;
};

void ATUICreateTranslationAnchor(float fractionX, float fractionY, IATUIAnchor **anchor);
void ATUICreateProportionAnchor(const vdrect32f& area, IATUIAnchor **anchor);
void ATUICreateProportionAnchor(const vdrect32f& area, const vdrect32& offsets, IATUIAnchor **anchor);

#endif
