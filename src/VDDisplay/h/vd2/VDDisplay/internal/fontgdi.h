#ifndef f_VD2_VDDISPLAY_FONTGDI_H
#define f_VD2_VDDISPLAY_FONTGDI_H

#include <vd2/system/unknown.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vectors.h>
#include <vd2/system/vdstl.h>
#include <vd2/VDDisplay/font.h>
#include <windows.h>
#include <mlang.h>

struct IMLangFontLink2;

class IVDDisplayFontGDI : public IVDRefUnknown {
public:
	enum { kTypeID = 'gdif' };

	virtual HFONT GetHFONT() = 0;
};

class VDDisplayFontGDI : public vdrefcounted<IVDDisplayFont>, public IVDDisplayFontGDI {
	VDDisplayFontGDI(const VDDisplayFontGDI&);
	VDDisplayFontGDI& operator=(const VDDisplayFontGDI&);
public:
	VDDisplayFontGDI();
	~VDDisplayFontGDI();

	int AddRef();
	int Release();
	void *AsInterface(uint32 iid);

	bool Init(HFONT font);
	void Shutdown();

	HFONT GetHFONT() { return mhfont; }

	void GetMetrics(VDDisplayFontMetrics& metrics);

	void ShapeText(const wchar_t *s, uint32 n, vdfastvector<VDDisplayFontGlyphPlacement>& glyphPlacements, vdrect32 *cellBounds, vdrect32 *glyphBounds, vdpoint32 *nextPos);

	void GetGlyphMetrics(uint32 c, VDDisplayFontGlyphMetrics& metrics);
	bool GetGlyphImage(uint32 c, const VDPixmap& dst);

	vdsize32 MeasureString(const wchar_t *s, uint32 n, bool includeOverhangs);
	vdsize32 FitString(const wchar_t *s, uint32 n, uint32 maxWidth, uint32 *count);

protected:
	bool RenderGlyph(uint32 c, const VDPixmap *dst, VDDisplayFontGlyphMetrics *dstMetrics);

	HFONT mhfont;
	HDC mhdc;
	HBITMAP mhbm;
	HGDIOBJ mhbmOld;
	void *mpvBits;
	uint32 mBitmapMargin;
	uint32 mBitmapWidth;
	TEXTMETRICA mMetrics;

	vdfastvector<uint32> mGlyphImageBuffer;
	typedef vdhashmap<uint32, VDDisplayFontGlyphMetrics> GlyphMetricsCache;
	GlyphMetricsCache mGlyphMetricsCache;
};

#endif
