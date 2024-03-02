#include "uberblit_resample_special.h"
#include "blt_spanutils.h"

///////////////////////////////////////////////////////////////////////////////

void VDPixmapGenResampleRow_d2_p0_lin_u8::Init(IVDPixmapGen *src, uint32 srcIndex) {
	InitSource(src, srcIndex);
	src->AddWindowRequest(0, 0);

	mWidth = (mSrcWidth + 1) >> 1;
}

void VDPixmapGenResampleRow_d2_p0_lin_u8::Start() {
	mpSrc->Start();
	StartWindow(mWidth);
}

void VDPixmapGenResampleRow_d2_p0_lin_u8::Compute(void *dst0, sint32 y) {
	const uint8 *src = (const uint8 *)mpSrc->GetRow(y, mSrcIndex);

	nsVDPixmapSpanUtils::horiz_compress2x_coaligned((uint8 *)dst0, src, mSrcWidth);
}

///////////////////////////////////////////////////////////////////////////////

void VDPixmapGenResampleRow_d4_p0_lin_u8::Init(IVDPixmapGen *src, uint32 srcIndex) {
	InitSource(src, srcIndex);
	src->AddWindowRequest(0, 0);

	mWidth = (mSrcWidth + 3) >> 2;
}

void VDPixmapGenResampleRow_d4_p0_lin_u8::Start() {
	mpSrc->Start();
	StartWindow(mWidth);
}

void VDPixmapGenResampleRow_d4_p0_lin_u8::Compute(void *dst0, sint32 y) {
	const uint8 *src = (const uint8 *)mpSrc->GetRow(y, mSrcIndex);

	nsVDPixmapSpanUtils::horiz_compress4x_coaligned((uint8 *)dst0, src, mSrcWidth);
}

///////////////////////////////////////////////////////////////////////////////

void VDPixmapGenResampleRow_x2_p0_lin_u8::Init(IVDPixmapGen *src, uint32 srcIndex) {
	InitSource(src, srcIndex);
	src->AddWindowRequest(0, 0);

	mWidth = mSrcWidth * 2;
}

void VDPixmapGenResampleRow_x2_p0_lin_u8::Start() {
	mpSrc->Start();
	StartWindow(mWidth);
}

void VDPixmapGenResampleRow_x2_p0_lin_u8::Compute(void *dst0, sint32 y) {
	const uint8 *src = (const uint8 *)mpSrc->GetRow(y, mSrcIndex);

	nsVDPixmapSpanUtils::horiz_expand2x_coaligned((uint8 *)dst0, src, mWidth);
}

///////////////////////////////////////////////////////////////////////////////

void VDPixmapGenResampleRow_x4_p0_lin_u8::Init(IVDPixmapGen *src, uint32 srcIndex) {
	InitSource(src, srcIndex);
	src->AddWindowRequest(0, 0);

	mWidth = mSrcWidth * 4;
}

void VDPixmapGenResampleRow_x4_p0_lin_u8::Start() {
	mpSrc->Start();
	StartWindow(mWidth);
}

void VDPixmapGenResampleRow_x4_p0_lin_u8::Compute(void *dst0, sint32 y) {
	const uint8 *src = (const uint8 *)mpSrc->GetRow(y, mSrcIndex);

	nsVDPixmapSpanUtils::horiz_expand4x_coaligned((uint8 *)dst0, src, mWidth);
}

///////////////////////////////////////////////////////////////////////////////

void VDPixmapGenResampleCol_x2_phalf_lin_u8::Init(IVDPixmapGen *src, uint32 srcIndex) {
	InitSource(src, srcIndex);
	src->AddWindowRequest(-2, 2);

	mHeight = (mSrcHeight + 1) >> 1;
}

void VDPixmapGenResampleCol_x2_phalf_lin_u8::Start() {
	mpSrc->Start();
	StartWindow(mWidth);
}

void VDPixmapGenResampleCol_x2_phalf_lin_u8::Compute(void *dst0, sint32 y) {
	sint32 y2 = y+y;
	const uint8 *src[4] = {
		(const uint8 *)mpSrc->GetRow(y2 > 0 ? y2-1 : 0, mSrcIndex),
		(const uint8 *)mpSrc->GetRow(y2  , mSrcIndex),
		(const uint8 *)mpSrc->GetRow(y2+1, mSrcIndex),
		(const uint8 *)mpSrc->GetRow(y2+2, mSrcIndex)
	};

	nsVDPixmapSpanUtils::vert_compress2x_centered((uint8 *)dst0, src, mWidth, 0);
}

///////////////////////////////////////////////////////////////////////////////

void VDPixmapGenResampleCol_x4_p1half_lin_u8::Init(IVDPixmapGen *src, uint32 srcIndex) {
	InitSource(src, srcIndex);
	src->AddWindowRequest(-4, 4);

	mHeight = (mSrcHeight + 2) >> 2;
}

void VDPixmapGenResampleCol_x4_p1half_lin_u8::Start() {
	mpSrc->Start();
	StartWindow(mWidth);
}

void VDPixmapGenResampleCol_x4_p1half_lin_u8::Compute(void *dst0, sint32 y) {
	sint32 y4 = y*4;
	const uint8 *src[8] = {
		(const uint8 *)mpSrc->GetRow(y4 > 2 ? y4-2 : 0, mSrcIndex),
		(const uint8 *)mpSrc->GetRow(y4 > 1 ? y4-1 : 0, mSrcIndex),
		(const uint8 *)mpSrc->GetRow(y4  , mSrcIndex),
		(const uint8 *)mpSrc->GetRow(y4+1, mSrcIndex),
		(const uint8 *)mpSrc->GetRow(y4+2, mSrcIndex),
		(const uint8 *)mpSrc->GetRow(y4+3, mSrcIndex),
		(const uint8 *)mpSrc->GetRow(y4+4, mSrcIndex),
		(const uint8 *)mpSrc->GetRow(y4+5, mSrcIndex)
	};

	nsVDPixmapSpanUtils::vert_compress4x_centered((uint8 *)dst0, src, mWidth, 0);
}

///////////////////////////////////////////////////////////////////////////////

void VDPixmapGenResampleCol_d2_pnqrtr_lin_u8::Init(IVDPixmapGen *src, uint32 srcIndex) {
	InitSource(src, srcIndex);
	src->AddWindowRequest(-1, 1);

	mHeight = mSrcHeight * 2;
}

void VDPixmapGenResampleCol_d2_pnqrtr_lin_u8::Start() {
	mpSrc->Start();
	StartWindow(mWidth);
}

void VDPixmapGenResampleCol_d2_pnqrtr_lin_u8::Compute(void *dst0, sint32 y) {
	sint32 y2 = (y - 1) >> 1;
	const uint8 *src[2] = {
		(const uint8 *)mpSrc->GetRow(y2, mSrcIndex),
		(const uint8 *)mpSrc->GetRow(y2+1, mSrcIndex),
	};

	nsVDPixmapSpanUtils::vert_expand2x_centered((uint8 *)dst0, src, mWidth, ~y << 7);
}

///////////////////////////////////////////////////////////////////////////////

void VDPixmapGenResampleCol_d4_pn38_lin_u8::Init(IVDPixmapGen *src, uint32 srcIndex) {
	InitSource(src, srcIndex);
	src->AddWindowRequest(-1, 1);

	mHeight = mSrcHeight * 4;
}

void VDPixmapGenResampleCol_d4_pn38_lin_u8::Start() {
	mpSrc->Start();
	StartWindow(mWidth);
}

void VDPixmapGenResampleCol_d4_pn38_lin_u8::Compute(void *dst0, sint32 y) {
	sint32 y2 = (y - 2) >> 1;
	const uint8 *src[2] = {
		(const uint8 *)mpSrc->GetRow(y2, mSrcIndex),
		(const uint8 *)mpSrc->GetRow(y2+1, mSrcIndex),
	};

	nsVDPixmapSpanUtils::vert_expand4x_centered((uint8 *)dst0, src, mWidth, y << 6);
}
