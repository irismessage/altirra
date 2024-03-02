#ifndef f_AT_UIANCHOR_H
#define f_AT_UIANCHOR_H

#include <vd2/system/unknown.h>
#include <vd2/system/vectors.h>

class IATUIAnchor : public IVDRefUnknown {
public:
	virtual vdrect32 Position(const vdrect32& containerArea, const vdsize32& size) = 0;
};

void ATUICreateTranslationAnchor(const vdsize32& size, float fractionX, float fractionY, IATUIAnchor **anchor);

#endif
