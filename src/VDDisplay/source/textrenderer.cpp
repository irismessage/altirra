#include "stdafx.h"
#include <vd2/Kasumi/pixmap.h>
#include <vd2/system/memory.h>
#include <vd2/VDDisplay/textrenderer.h>

VDDisplayTextRenderer::VDDisplayTextRenderer()
	: mpRenderer(NULL)
	, mGeneration(1)
	, mColor(0)
{
}

VDDisplayTextRenderer::~VDDisplayTextRenderer() {
}

void VDDisplayTextRenderer::Init(IVDDisplayRenderer *renderer, uint32 cachew, uint32 cacheh) {
	mCacheImage.init(cachew, cacheh, nsVDPixmap::kPixFormat_XRGB8888);

	VDMemset32Rect(mCacheImage.data, mCacheImage.pitch, 0, cachew, cacheh);

	mCacheImageView.SetImage(mCacheImage, false);

	mpRenderer = renderer;
	mWidth = cachew;
	mHeight = cacheh;

	Discard();
}

void VDDisplayTextRenderer::Begin() {
	mAlignment = kAlignLeft;
	mVertAlign = kVertAlignBaseline;
	mColor = 0;
	mDrawX = 0;
	mDrawY = 0;
}

void VDDisplayTextRenderer::End() {
	SetFont(NULL);
}

void VDDisplayTextRenderer::SetFont(IVDDisplayFont *font) {
	mpFont = font;
}

void VDDisplayTextRenderer::SetColorRGB(uint32 c) {
	mColor = c;
}

void VDDisplayTextRenderer::SetAlignment(Alignment align, VertAlign valign) {
	mAlignment = align;
	mVertAlign = valign;
}

void VDDisplayTextRenderer::SetPosition(int x, int y) {
	mDrawX = x;
	mDrawY = y;
}

void VDDisplayTextRenderer::DrawTextLine(int x, int y, const wchar_t *text) {
	SetPosition(x, y);
	DrawTextSpan(text, wcslen(text));
}

void VDDisplayTextRenderer::DrawTextSpan(const wchar_t *text, uint32 numChars) {
	if (!mpFont)
		return;

	mBlts.clear();

	mGlyphPlacements.clear();

	vdrect32 bounds;
	vdpoint32 nextPos;
	mpFont->ShapeText(text, numChars, mGlyphPlacements, &bounds, NULL, &nextPos);

	sint32 x = mDrawX;
	sint32 y = mDrawY;

	mDrawX += nextPos.x;
	mDrawY += nextPos.y;

	switch(mAlignment) {
		case kAlignLeft:
			break;

		case kAlignCenter:
			x -= bounds.width() >> 1;
			break;

		case kAlignRight:
			x -= bounds.width();
			break;
	}

	switch(mVertAlign) {
		case kVertAlignTop:
			y -= bounds.top;
			break;

		case kVertAlignBaseline:
			break;

		case kVertAlignBottom:
			y -= bounds.bottom;
			break;
	}

	DrawPrearrangedText(x, y, mGlyphPlacements.data(), (uint32)mGlyphPlacements.size());
}

void VDDisplayTextRenderer::DrawPrearrangedText(int x, int y, const VDDisplayFontGlyphPlacement *glyphPlacements, uint32 n) {
	mpRenderer->SetColorRGB(mColor);

	mBlts.clear();
	for(uint32 i=0; i<n; ++i) {
		const HashNode *node;

		for(;;) {
			node = PrepareGlyph(mpFont, (uint32)glyphPlacements[i].mGlyphIndex);

			if (node)
				break;

			if (mBlts.empty())
				goto failed;

			mpRenderer->MultiColorBlt(mBlts.data(), (uint32)mBlts.size(), mCacheImageView);
			Discard();

			mBlts.clear();
		}

		VDDisplayBlt& blt = mBlts.push_back();

		blt.mDestX = x + glyphPlacements[i].mX;
		blt.mDestY = y + glyphPlacements[i].mY;
		blt.mSrcX = node->mX;
		blt.mSrcY = node->mY;
		blt.mWidth = node->mWidth;
		blt.mHeight = node->mHeight;

failed:
		;
	}

	mpRenderer->MultiColorBlt(mBlts.data(), (uint32)mBlts.size(), mCacheImageView);
}

void VDDisplayTextRenderer::Discard() {
	++mGeneration;
	VDMemsetPointer(mpHashTable, NULL, 64);
	mHashNodeAllocator.Clear();
	mX = 0;
	mY = 0;
	mLineHeight = 0;

	VDMemset32Rect(mCacheImage.data, mCacheImage.pitch, 0, mCacheImage.w, mCacheImage.h);
}

const VDDisplayTextRenderer::HashNode *VDDisplayTextRenderer::PrepareGlyph(IVDDisplayFont *font, uint32 glyphIndex) {
	HashNode *node = mpHashTable[glyphIndex & 0x3f];
	uintptr fontId = (uintptr)font;

	for(; node; node = node->mpNext) {
		if (node->mGlyphIndex == glyphIndex && node->mFontId == fontId)
			break;
	}

	if (!node) {
		VDDisplayFontGlyphMetrics metrics;
		font->GetGlyphMetrics(glyphIndex, metrics);

		uint32 w = metrics.mWidth;
		uint32 h = metrics.mHeight;

		node = Allocate(fontId, glyphIndex, w, h);
		if (!node)
			return NULL;

		node->mAdvance = metrics.mAdvance;
		node->mDx = metrics.mX;
		node->mDy = metrics.mY;

		VDPixmap px = {};
		px.format = nsVDPixmap::kPixFormat_XRGB8888;
		px.data = (char *)mCacheImage.data + mCacheImage.pitch * node->mY + sizeof(uint32)*node->mX;
		px.pitch = mCacheImage.pitch;
		px.w = node->mWidth;
		px.h = node->mHeight;

		font->GetGlyphImage(glyphIndex, px);

		mCacheImageView.Invalidate();
	}

	return node;
}

VDDisplayTextRenderer::HashNode *VDDisplayTextRenderer::Allocate(uintptr fontId, uint32 c, uint32 w, uint32 h) {
	if (mX + w > mWidth) {
		if (mY + mLineHeight + h + 1 > mHeight)
			return NULL;
		
		mX = 0;
		mY += mLineHeight + 1;
		mLineHeight = 0;
	}

	HashNode *node = mHashNodeAllocator.Allocate<HashNode>();
	if (!node)
		return NULL;

	node->mpNext = mpHashTable[c & 0x3f];
	mpHashTable[c & 0x3f] = node;
	node->mX = mX;
	node->mY = mY;
	node->mWidth = w;
	node->mHeight = h;
	node->mGlyphIndex = c;
	node->mFontId = fontId;

	mX += w + 1;
	if (mLineHeight < h)
		mLineHeight = h;

	return node;
}
