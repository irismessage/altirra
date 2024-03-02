#ifndef f_AT2_UILABEL_H
#define f_AT2_UILABEL_H

#include <vd2/system/VDString.h>
#include <at/atui/uiwidget.h>

class IVDDisplayFont;

class ATUILabel : public ATUIWidget {
public:
	enum Align {
		kAlignLeft,
		kAlignCenter,
		kAlignRight
	};

	enum VAlign {
		kVAlignTop,
		kVAlignMiddle,
		kVAlignBottom
	};

	ATUILabel();

	void SetFont(IVDDisplayFont *font);
	void SetBoldFont(IVDDisplayFont *font);

	void ClearBorderColor();
	void SetBorderColor(uint32 c);

	void SetTextAlign(Align align);
	void SetTextVAlign(VAlign align);
	void SetTextColor(uint32 c);
	void SetTextOffset(sint32 x, sint32 y);

	void SetWordWrapEnabled(bool enabled);

	void SetText(const wchar_t *s);
	void SetTextF(const wchar_t *format, ...);

	void SetHTMLText(const wchar_t *s);

	void SetMinSizeText(const wchar_t *);

	void Clear();
	void AppendFormattedText(uint32 color, const wchar_t *s);
	void AppendFormattedTextF(uint32 color, const wchar_t *format, ...);

	void AutoSize() { AutoSize(mArea.left, mArea.top); }
	void AutoSize(int x, int y);

protected:
	void OnCreate() override;
	ATUIWidgetMetrics OnMeasure() override;
	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) override;

	void InvalidateForTextChange();
	void Reflow(sint32 w);

	static constexpr sint32 kUnlimitedSize = 0x1FFFFFFF;

	vdrefptr<IVDDisplayFont> mpFont;
	vdrefptr<IVDDisplayFont> mpBoldFont;
	VDStringW mText;
	VDStringW mTextF;
	Align mTextAlign;
	VAlign mTextVAlign;
	uint32 mTextColor;
	sint32 mTextX;
	sint32 mTextY;
	sint32 mBorderColor;
	vdsize32 mTextSize;
	sint32 mReflowLastWidth = -1;
	bool mbWordWrapEnabled = false;

	VDStringW mMinSizeText;
	vdsize32 mMinTextSize;

	struct Span {
		sint32 mX;
		sint32 mWidth;
		sint32 mFgColor;
		sint32 mBgColor;
		uint32 mStart;
		uint32 mChars;
		bool mbBold;
	};

	vdfastvector<Span> mSpans;

	struct Line {
		uint32 mSpanCount;
	};

	vdfastvector<Line> mLines;

	struct RenderLine {
		uint32 mSpanCount = 0;
		uint32 mAscent = 0;
		uint32 mDescent = 0;
		sint32 mWidth = 0;
	};

	vdfastvector<RenderLine> mRenderLines;
	vdfastvector<Span> mRenderSpans;
};

#endif
