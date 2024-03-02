#ifndef f_VD2_VDDISPLAY_COMPOSITOR_H
#define f_VD2_VDDISPLAY_COMPOSITOR_H

#include <vd2/system/refcount.h>

class IVDDisplayRenderer;

struct VDDisplayCompositeInfo {
	uint32 mWidth;
	uint32 mHeight;
};

class VDINTERFACE IVDDisplayCompositor : public IVDRefCount {
public:
	virtual void Composite(IVDDisplayRenderer& r, const VDDisplayCompositeInfo& compInfo) = 0;
};

#endif
