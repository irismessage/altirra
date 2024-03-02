#ifndef f_VD2_VDDISPLAY_BICUBIC_H
#define f_VD2_VDDISPLAY_BICUBIC_H

#include <vd2/system/vdtypes.h>

void VDDisplayCreateBicubicTexture(uint32 *dst, int w, int srcw);
void VDDisplayCreateBicubicTexture(uint32 *dst, int dstw, int srcw, float outx, float outw);

#endif	// f_VD2_VDDISPLAY_BICUBIC_H
