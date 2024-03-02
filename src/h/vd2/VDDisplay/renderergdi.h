#ifndef f_VD2_VDDISPLAY_RENDERERGDI_H
#define f_VD2_VDDISPLAY_RENDERERGDI_H

#include <vd2/system/win32/miniwindows.h>
#include <vd2/VDDisplay/renderer.h>

class IVDDisplayRendererGDI : public IVDDisplayRenderer {
public:
	virtual ~IVDDisplayRendererGDI() = default;

	virtual bool Begin(VDZHDC hdc, sint32 w, sint32 h) = 0;
	virtual void End() = 0;

	virtual void FillTri(const vdpoint32 *pts) = 0;

	// This is a cheat to get around us not being able to support the generic
	// text rendering API properly yet with GDI.
	virtual void SetTextFont(VDZHFONT hfont) = 0;
	virtual void SetTextColorRGB(uint32 c) = 0;
	virtual void SetTextBkTransp() = 0;
	virtual void SetTextBkColorRGB(uint32 c) = 0;

	enum class TextAlign : uint8 { Left, Center, Right };
	enum class TextVAlign : uint8 { Top, Baseline, Bottom };

	virtual void SetTextAlignment(TextAlign align = TextAlign::Left, TextVAlign valign = TextVAlign::Top) = 0;
	virtual void DrawTextSpan(sint32 x, sint32 y, const wchar_t *text, uint32 numChars) = 0;
};

IVDDisplayRendererGDI *VDDisplayCreateRendererGDI();

#endif
