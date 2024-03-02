#include "stdafx.h"
#include <vd2/VDDisplay/textrenderer.h>
#include "uilabel.h"
#include <at/atui/uimanager.h>

ATUILabel::ATUILabel()
	: mTextAlign(kAlignLeft)
	, mTextColor(0)
	, mTextX(0)
	, mTextY(0)
	, mBorderColor(-1)
	, mTextSize(0, 0)
	, mbReflowPending(false)
{
	SetFillColor(0xD4D0C8);
}

void ATUILabel::SetFont(IVDDisplayFont *font) {
	if (mpFont != font) {
		mpFont = font;

		Invalidate();
	}
}

void ATUILabel::SetBoldFont(IVDDisplayFont *font) {
	if (mpBoldFont != font) {
		mpBoldFont = font;

		Invalidate();
	}
}

void ATUILabel::ClearBorderColor() {
	if (mBorderColor >= 0) {
		mBorderColor = -1;
		Invalidate();
	}
}

void ATUILabel::SetBorderColor(uint32 c) {
	if ((uint32)mBorderColor != c) {
		mBorderColor = c;
		Invalidate();
	}
}

void ATUILabel::SetTextAlign(Align align) {
	if (mTextAlign != align) {
		mTextAlign = align;
		Invalidate();
	}
}

void ATUILabel::SetTextColor(uint32 c) {
	if (mTextColor != c) {
		mTextColor = c;

		for(auto& span : mSpans)
			span.mColor = mTextColor;

		Invalidate();
	}
}

void ATUILabel::SetTextOffset(sint32 x, sint32 y) {
	if (mTextX != x || mTextY != y) {
		mTextX = x;
		mTextY = y;

		Invalidate();
	}
}

void ATUILabel::SetText(const wchar_t *s) {
	if (mText == s && !mSpans.empty())
		return;

	mText = s;

	mSpans.clear();
	mLines.clear();

	const wchar_t *t = s;
	for(;;) {
		const wchar_t *breakpt = wcschr(t, '\n');

		Span& span = mSpans.push_back();
		span.mbBold = false;
		span.mStart = t - s;
		span.mChars = breakpt ? breakpt - t : wcslen(t);
		span.mColor = mTextColor;

		Line& line = mLines.push_back();
		line.mSpanCount = 1;

		if (!breakpt)
			break;

		t = breakpt + 1;
	}

	mbReflowPending = true;
	Invalidate();
}

void ATUILabel::SetTextF(const wchar_t *format, ...) {
	mTextF.clear();

	va_list val;
	va_start(val, format);
	mTextF.append_vsprintf(format, val);
	va_end(val);

	SetText(mTextF.c_str());
}

void ATUILabel::SetHTMLText(const wchar_t *s) {
	Clear();

	// We only parse a very simple form of HTML here, for now:
	//
	//	<b>...</b>
	//	&amp;
	//	&gt;
	//	&lt;
	//
	// Obviously, this is not meant to be a fully compliant HTML/XHTML/XML
	// parser.

	Span span = {};
	span.mColor = mTextColor;
	span.mStart = 0;
	span.mChars = 0;

	mLines.resize(1);
	mLines.back().mSpanCount = 0;

	while(*s) {
		if (*s == '<') {
			++s;

			while(*s == L' ')
				++s;

			bool close = false;
			bool open = false;
			if (*s == L'/') {
				++s;
				close = true;
			} else
				open = true;

			const wchar_t *tagNameStart = s;

			while(iswalnum(*s))
				++s;

			const VDStringSpanW tagName(tagNameStart, s);

			// parse out attributes here... if we supported any yet.

			// check for self-closed tag
			while(*s == L' ')
				++s;

			if (*s == L'/')
				close = true;

			while(*s == L' ')
				++s;

			if (*s == L'>')
				++s;

			// process the tag/element
			if (tagName == L"b") {
				if (open != close) {
					if (span.mChars) {
						mSpans.push_back(span);
						++mLines.back().mSpanCount;
						span.mChars = 0;
					}

					span.mbBold = open;
					span.mStart = (uint32)mText.size();
				}
			}

			continue;
		}

		wchar_t c = *s++;
		if (c == '&') {
			const wchar_t *esc1 = s;

			while(*s) {
				if (*s++ == ';')
					break;
			}

			const VDStringSpanW escName(esc1, s);

			c = 0;
			if (escName == L"amp;")
				c = L'&';
			else if (escName == L"gt;")
				c = L'>';
			else if (escName == L"lt;")
				c = L'<';

			if (!c)
				continue;
		}

		if (c == '\n') {
			// We push a span even if it's empty before ending the line.
			// This is necessary to set the correct line height even if the line
			// is empty.
			mSpans.push_back(span);
			span.mChars = 0;
			span.mStart = (uint32)mText.size();
			++mLines.back().mSpanCount;

			mLines.push_back();
			mLines.back().mSpanCount = 0;
		} else {
			mText += c;
			++span.mChars;
		}
	}

	if (span.mChars) {
		mSpans.push_back(span);
		++mLines.back().mSpanCount;
	}

	mbReflowPending = true;
	Invalidate();
}

void ATUILabel::Clear() {
	if (!mText.empty() || !mSpans.empty()) {
		mText.clear();
		mSpans.clear();
		mLines.clear();

		mbReflowPending = true;
		Invalidate();
	}
}

void ATUILabel::AppendFormattedText(uint32 color, const wchar_t *s) {
	if (!*s)
		return;

	if (mLines.empty()) {
		Line& line = mLines.push_back();
		line.mSpanCount = 0;
	}

	Span& span = mSpans.push_back();

	span.mColor = color;
	span.mStart = mText.size();
	span.mChars = wcslen(s);
	span.mbBold = false;

	++mLines.back().mSpanCount;

	mText.append(s);

	mbReflowPending = true;
	Invalidate();
}

void ATUILabel::AppendFormattedTextF(uint32 color, const wchar_t *format, ...) {
	mTextF.clear();

	va_list val;
	va_start(val, format);
	mTextF.append_vsprintf(format, val);
	va_end(val);

	AppendFormattedText(color, mTextF.c_str());
}

void ATUILabel::AutoSize(int x, int y) {
	if (!mpFont)
		return;

	if (mbReflowPending)
		Reflow();

	vdrect32 r = ComputeWindowSize(vdrect32(0, 0, mTextSize.w + mTextX * 2, mTextSize.h + mTextY * 2));

	r.translate(x - r.left, y - r.top);
	SetArea(r);
}

void ATUILabel::OnCreate() {
	mpFont = mpManager->GetThemeFont(kATUIThemeFont_Default);
}

void ATUILabel::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	if (mbReflowPending)
		Reflow();

	if (mBorderColor >= 0) {
		rdr.SetColorRGB((uint32)mBorderColor);

		const vdpoint32 pts[5] = {
			vdpoint32(0, 0),
			vdpoint32(0, h-1),
			vdpoint32(w-1, h-1),
			vdpoint32(w-1, 0),
			vdpoint32(0, 0),
		};

		rdr.PolyLine(pts, 4);
	}

	if (mLines.empty())
		return;

	VDDisplayTextRenderer& tr = *rdr.GetTextRenderer();
	tr.SetFont(mpFont);

	tr.SetColorRGB(mTextColor);
	tr.SetAlignment(VDDisplayTextRenderer::kAlignLeft, VDDisplayTextRenderer::kVertAlignBaseline);

	const Line *line = mLines.data();
	uint32 lineSpanCount = line->mSpanCount;

	const wchar_t *const s = mText.c_str();
	for(vdfastvector<Span>::const_iterator it(mSpans.begin()), itEnd(mSpans.end());
		it != itEnd;
		++it)
	{
		const Span& span = *it;

		while(!lineSpanCount--) {
			++line;

			lineSpanCount = line->mSpanCount;
		}

		sint32 lineX = ((w - line->mWidth - mTextX*2) * (sint32)mTextAlign + 1) >> 1;

		if (span.mChars) {
			tr.SetFont(span.mbBold ? mpBoldFont : mpFont);
			tr.SetColorRGB(span.mColor);
			tr.SetPosition(lineX + span.mX + mTextX, span.mY + mTextY);
			tr.DrawTextSpan(s + span.mStart, span.mChars);
		}
	}
}

void ATUILabel::Reflow() {
	mbReflowPending = false;

	const wchar_t *const s = mText.c_str();

	Span *spans = mSpans.data();

	vdfastvector<VDDisplayFontGlyphPlacement> glyphPlacements;
	mTextSize.w = 0;
	mTextSize.h = 0;

	for(vdfastvector<Line>::iterator itLine = mLines.begin(), itLineEnd = mLines.end();
		itLine != itLineEnd;
		++itLine)
	{
		Line& line = *itLine;
		sint32 x = 0;
		sint32 ascent = 0;
		sint32 descent = 0;

		for(uint32 i=0; i<line.mSpanCount; ++i) {
			Span& span = spans[i];
			IVDDisplayFont *font = (span.mbBold ? mpBoldFont : mpFont);

			span.mX = x;

			if (font) {
				glyphPlacements.clear();

				vdrect32 cellBounds;
				vdpoint32 nextPos;

				font->ShapeText(s + span.mStart, span.mChars, glyphPlacements, &cellBounds, NULL, &nextPos);

				ascent = std::max<sint32>(ascent, -cellBounds.top);
				descent = std::max<sint32>(descent, cellBounds.bottom);

				x += nextPos.x;
			}
		}

		for(uint32 i=0; i<line.mSpanCount; ++i) {
			Span& span = spans[i];

			span.mY = mTextSize.h + ascent;
		}

		line.mAscent = ascent;
		line.mDescent = descent;
		line.mWidth = x;

		mTextSize.w = std::max<sint32>(mTextSize.w, x);
		mTextSize.h += ascent + descent;

		spans += line.mSpanCount;
	}
}
