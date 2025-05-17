//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include "stdafx.h"
#include <windows.h>
#include <windowsx.h>
#include <vd2/system/binary.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/color.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/file.h>
#include <vd2/system/vdstl_vectorview.h>
#include <vd2/system/zip.h>
#include <vd2/Dita/services.h>
#include <at/atcore/atascii.h>
#include <at/atnativeui/canvas_win32.h>
#include <at/atnativeui/uinativewindow.h>
#include "oshelper.h"
#include "printeroutput.h"
#include "printerttfencoder.h"
#include "resource.h"
#include "simulator.h"
#include "texteditor.h"
#include "uidbgprinteroutput.h"

extern ATSimulator g_sim;

///////////////////////////////////////////////////////////////////////////////

class ATUIPrinterGraphicalOutputWindow final : public ATUINativeWindow {
public:
	ATUIPrinterGraphicalOutputWindow();

	void AttachToOutput(ATPrinterGraphicalOutput& output);

	void Clear();

	void ResetView();
	void SaveAsPNG(float dpi);
	void SaveAsPDF();

private:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) override;
	LRESULT OnNCCalcSize(WPARAM wParam, LPARAM lParam);
	void OnDestroy();
	void OnSize();
	void OnKeyDown(uint32 vk, uint32 flags);
	void OnMouseMove(int x, int y);
	void OnMouseDownL(int x, int y);
	void OnMouseUpL(int x, int y);
	void OnMouseWheel(int x, int y, float delta);
	void OnCaptureChanged();
	bool OnGesture(WPARAM wParam, LPARAM lParam);
	void OnPaint();
	void OnDpiChanged(int dpi);

	void UpdateViewOrigin();
	void ScrollByPixels(int dx, int dy);
	void SetZoom(float clicks);
	void SetZoom(float clicks, const vdpoint32& centerPt);
	void UpdateZoom(const vdpoint32 *centerPt);

	void ForceFullInvalidation();
	void ProcessPendingInvalidation();
	void InvalidatePaperArea(float x1, float y1, float x2, float y2);

	vdrect32f GetDocumentBounds() const;

	struct ViewTransform {
		float mOriginX = 0;
		float mOriginY = 0;
		float mPixelsPerMM = 1;
		float mMMPerPixel = 1;
	};

	bool Render(const ViewTransform& viewTransform, sint32 x, sint32 y, uint32 w, uint32 h, bool force);
	void RenderTrapezoid(const sint32 subSpans[2][8], uint32 penIndex, bool rgb);
	void RenderTrapezoidRGB_Scalar(const sint32 subSpans[2][8], uint32 penIndex);

#if defined(VD_CPU_X64) || defined(VD_CPU_X86)
	void RenderTrapezoidRGB_SSE2(const sint32 subSpans[2][8], uint32 penIndex);
#elif defined(VD_CPU_ARM64)
	void RenderTrapezoidRGB_NEON(const sint32 subSpans[2][8], uint32 penIndex);
#endif

	void Downsample8x8(uint32 *dst, const uint8 *src, size_t w, bool rgb);
	void Downsample8x8_Scalar(uint32 *dst, const uint8 *src, size_t w);
#if defined(VD_CPU_X64)
	void Downsample8x8_POPCNT64(uint32 *dst, const uint8 *src, size_t w);
#elif defined(VD_CPU_X86)
	void Downsample8x8_POPCNT32(uint32 *dst, const uint8 *src, size_t w);
#elif defined(VD_CPU_ARM64)
	void Downsample8x8_NEON(uint32 *dst, const uint8 *src, size_t w);
#endif

	void DownsampleRGB8x8_Scalar(uint32 *dst, const uint8 *src, size_t w);
#if defined(VD_CPU_X64)
	void DownsampleRGB8x8_POPCNT64(uint32 *dst, const uint8 *src, size_t w);
#elif defined(VD_CPU_X86)
	void DownsampleRGB8x8_POPCNT32(uint32 *dst, const uint8 *src, size_t w);
#elif defined(VD_CPU_ARM64)
	void DownsampleRGB8x8_NEON(uint32 *dst, const uint8 *src, size_t w);
#endif

	float mPageWidthMM = 0;
	float mPageVBorderMM = 0;
	float mDotRadiusMM = 0;

	float mViewOriginX = 0;
	float mViewOriginY = 0;
	float mViewCenterX = 0;
	float mViewCenterY = 0;
	float mViewPixelsPerMM = 1.0f;
	float mViewMMPerPixel = 1.0f;
	sint32 mViewWidthPixels = 0;
	sint32 mViewHeightPixels = 0;
	int mViewDpi = 96;

	bool mbDragging = false;
	sint32 mDragLastX = 0;
	sint32 mDragLastY = 0;
	float mWheelAccum = 0;

	bool mbInGesture = false;
	bool mbFirstGestureEvent = false;
	vdpoint32 mGestureOrigin;
	float mGestureZoomOrigin = 0;

	static constexpr float kZoomMin = -10.0f;
	static constexpr float kZoomMax = 25.0f;
	float mZoomClicks = 0;

	bool mbInvalidationPending = false;

	using RenderDot = ATPrinterGraphicalOutput::RenderDot;
	vdfastvector<RenderDot> mDotCullBuffer[4];

	using RenderVector = ATPrinterGraphicalOutput::RenderVector;
	vdfastvector<RenderVector> mVectorCullBuffer;

	ATPrinterGraphicalOutput *mpOutput = nullptr;
	ATUICanvasW32 mCanvas;
	vdfastvector<uint32> mFrameBuffer;
	vdfastvector<uint8> mABuffer;

	vdstructex<RGNDATA> mRegionData;

	uint8 mPopCnt8[256];
	uint32 mGammaTable[65];
	uint8 mPenDithers[4][3][8] {};

	static constexpr float kBlackLevel = 0.00f;
	static constexpr float kWhiteLevel = 1.00f;
	static constexpr uint8 kPenColors[4][3] {
		{ 0x00, 0x00, 0x00 },	// black
		{ 0x18, 0x1F, 0xF0 },	// blue
		{ 0x0B, 0x9C, 0x2F },	// green
		{ 0xC9, 0x1B, 0x12 },	// red
	};
};

ATUIPrinterGraphicalOutputWindow::ATUIPrinterGraphicalOutputWindow() {
	for(int i=0; i<256; ++i) {
		uint8 v = i;

		v -= (v >> 1) & 0x55;
		v = (v & 0x33) + ((v & 0xCC) >> 2);

		mPopCnt8[i] = (v & 0x0F) + (v >> 4);
	}

	static constexpr float kRange = kWhiteLevel - kBlackLevel;
	for(int i=0; i<=64; ++i) {
		float luma = kWhiteLevel - kRange * (float)i / 64.0f;
		uint32 v = VDColorRGB(vdfloat32x3::set(luma, luma, luma)).LinearToSRGB().ToRGB8();

		mGammaTable[i] = v;
	}

	static constexpr uint8 kDitherPattern[] {
		64,  1, 57, 37, 43, 44, 22, 11,
		60, 30, 15, 62, 31, 54, 27, 52,
		26, 13, 63, 38, 19, 48, 24, 12,
		 6,  3, 56, 28, 14,  7, 58, 29,
		55, 34, 17, 49, 33, 41, 45, 47,
		46, 23, 50, 25, 53, 35, 40, 20,
		10,  5, 59, 36, 18,  9, 61, 39,
		42, 21, 51, 32, 16,  8,  4,  2
	};

	static_assert(vdcountof(kDitherPattern) == 64);

	memset(mPenDithers, 0, sizeof mPenDithers);

	int offset = 0;
	for(int penIndex = 0; penIndex < 4; ++penIndex) {
		for(int ch=0; ch<3; ++ch) {
			uint8 gammaValue = kPenColors[penIndex][ch];
			uint8 linearValue = gammaValue*gammaValue / 1001;

			for(int i=0; i<64; ++i) {
				if (kDitherPattern[(i + offset) & 63] > linearValue)
					mPenDithers[penIndex][ch][i >> 3] |= (1 << (i & 7));
			}

			offset += 7;
		}
	}
}

void ATUIPrinterGraphicalOutputWindow::AttachToOutput(ATPrinterGraphicalOutput& output) {
	const ATPrinterGraphicsSpec& spec = output.GetGraphicsSpec();

	mpOutput = &output;
	mpOutput->SetOnInvalidation(
		[this] {
			if (!mbInvalidationPending) {
				mbInvalidationPending = true;

				::PostMessage(mhwnd, WM_USER + 100, 0, 0);
			}
		}
	);

	mPageWidthMM = spec.mPageWidthMM;
	mPageVBorderMM = spec.mPageVBorderMM;
	mDotRadiusMM = spec.mDotRadiusMM;

	ResetView();
}

void ATUIPrinterGraphicalOutputWindow::Clear() {
	mViewCenterY = 0;

	UpdateViewOrigin();

	if (mpOutput)
		mpOutput->Clear();
}

void ATUIPrinterGraphicalOutputWindow::ResetView() {
	mZoomClicks = -1000.0f;
	SetZoom(0);

	mViewCenterX = mPageWidthMM * 0.5f;
	mViewCenterY = mPageVBorderMM;

	UpdateViewOrigin();

	ForceFullInvalidation();
}

void ATUIPrinterGraphicalOutputWindow::SaveAsPNG(float dpi) {
	const VDStringW& fn = VDGetSaveFileName("PrinterSaveAsPNG"_vdtypeid, (VDGUIHandle)mhwnd, L"Save PNG image", L"PNG image\0*.png\0", L"png");
	if (fn.empty())
		return;

	// get bounds in document space
	vdrect32f documentBounds = GetDocumentBounds();

	// compute pixel bounds
	const float mmToInches = 1.0f / 25.4f;
	const sint32 w = std::max<sint32>(1, (sint32)ceilf(documentBounds.width() * mmToInches * dpi));
	const sint32 h = std::max<sint32>(1, (sint32)ceilf(documentBounds.height() * mmToInches * dpi));

	// render whole screen
	ViewTransform vt;
	vt.mOriginX = documentBounds.left;
	vt.mOriginY = documentBounds.top;
	vt.mPixelsPerMM = mmToInches * dpi;
	vt.mMMPerPixel = 1.0f / vt.mPixelsPerMM;
	Render(vt, 0, 0, w, h, true);

	// save as PNG
	VDPixmap px {};
	px.w = w;
	px.h = h;
	px.data = &mFrameBuffer[w * (h - 1)];
	px.pitch = -w*4;
	px.format = nsVDPixmap::kPixFormat_XRGB8888;

	ATSaveFrame(px, fn.c_str());

	// clear out large buffers
	vdfastvector<uint32> fb;
	fb.swap(mFrameBuffer);

	vdfastvector<uint8> ab;
	ab.swap(mABuffer);
}

void ATUIPrinterGraphicalOutputWindow::SaveAsPDF() {
	const VDStringW& fn = VDGetSaveFileName("PrinterSaveAsPDF"_vdtypeid, (VDGUIHandle)mhwnd, L"Save PDF", L"PDF document\0*.pdf\0", L"pdf");
	if (fn.empty())
		return;

	// ZLIB header (Deflate 32K Normal)
	static constexpr uint8 kZLIBHeader[] { 0x78, 0x9C };

	VDFileStream fileOut(fn.c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kDenyAll);
	VDTextOutputStream textOut(&fileOut);

	vdfastvector<uint32> objectOffsets;

	textOut.PutLine("%PDF-1.4");
	textOut.PutLine("%\x80\x80\x80\x80");

	objectOffsets.push_back((uint32)textOut.Pos());
	textOut.PutLine("1 0 obj");
	textOut.PutLine("<< /Type /Catalog");
	textOut.PutLine("/Pages 2 0 R");
	textOut.PutLine(">>");
	textOut.PutLine("endobj");

	// reserve entry for pages object
	objectOffsets.push_back(0);

	// build TrueType font
	const auto& spec = mpOutput->GetGraphicsSpec();
	const int ttfDotsPerLine = std::min<int>(spec.mNumPins, 7);
	const float ttfAscentMM = spec.mDotRadiusMM*2 + spec.mVerticalDotPitchMM * (ttfDotsPerLine - 1);
	const float ttfUnitsPerMM = 1000.0f / ttfAscentMM;
	const int ttfDotRadius = (int)(0.5f + mDotRadiusMM * ttfUnitsPerMM);
	const int ttfDotVRange = 1000 - 2*ttfDotRadius;

	vdautoptr<ATTrueTypeEncoder> ttf(new ATTrueTypeEncoder);

	ttf->SetDefaultAdvanceWidth(ttfDotRadius * 2);

	// Create missing char
	ttf->BeginSimpleGlyph();
	ttf->EndSimpleGlyph();

	// Create space
	ttf->MapCharacter(0x20, ttf->BeginSimpleGlyph());
	ttf->EndSimpleGlyph();

	// Create initial dot
	//
	// TrueType uses quadratic splines with clockwise orientation in a bottom-up coordinate
	// system. We use 8 on-curve points at octants and round it off with another 8 off-curve
	// points. For a unit circle, the length of an octagonal segment is:
	//
	//  hypot(1-sqrt(2)/2, sqrt(2)/2) = sqrt(2 - sqrt(2)) = 0.765367
	//
	// The law of cosines then gives the distance for the control point:
	//
	//  c^2 = a^2 + b^2 - 2ab cos theta    (theta = 22.5d, a=b)
	//  2 - sqrt(2) = 2*a^2 * (1 - cos(135d))
	//  2 - sqrt(2) = 2*a^2 * (1 + sqrt(2)/2)
	//  a^2 = [2 - sqrt(2)] / [2 + sqrt(2)]
	//  a = sqrt(2) - 1

	auto dotGlyph = ttf->BeginSimpleGlyph();
	ttf->MapCharacter(0x21, dotGlyph);

	{
		const int r = ttfDotRadius;
		const int r2 = r*2;
		const int k = (ttfDotRadius * 724) / 1024;	// sqrt(2)/2
		const int c = (ttfDotRadius * 424) / 1024;	// sqrt(2) - 1

		ttf->AddGlyphPoint(r,   0,   true );
		ttf->AddGlyphPoint(r-c, 0,   false);
		ttf->AddGlyphPoint(r-k, r-k, true );
		ttf->AddGlyphPoint(0,   r-c, false);
		ttf->AddGlyphPoint(0,   r,   true );
		ttf->AddGlyphPoint(0,   r+c, false);
		ttf->AddGlyphPoint(r-k, r+k, true );
		ttf->AddGlyphPoint(r-c, r2,  false);
		ttf->AddGlyphPoint(r,   r2,  true );
		ttf->AddGlyphPoint(r+c, r2,  false);
		ttf->AddGlyphPoint(r+k, r+k, true );
		ttf->AddGlyphPoint(r2,  r+c, false);
		ttf->AddGlyphPoint(r2,  r,   true );
		ttf->AddGlyphPoint(r2,  r-c, false);
		ttf->AddGlyphPoint(r+k, r-k, true );
		ttf->AddGlyphPoint(r+c, 0,   false);
		ttf->EndContour();
	}

	ttf->EndSimpleGlyph();

	// map 0x20-0x7F
	for(int i=2; i<96; ++i) {
		ttf->MapCharacter(0x20+i, ttf->BeginCompositeGlyph());

		for(int j=0; j<7; ++j) {
			if (i & (1<<j))
				ttf->AddGlyphReference(dotGlyph, 0, (ttfDotVRange*j) / 6);
		}
		ttf->EndCompositeGlyph();
	}

	// Map 0xC0-0xDF. We avoid 0x80-0xBF because of character mapping problems in 0x80-0x9F and the altspace
	// at 0xA0. PDF is supposed to allow symbolic fonts to be mapped 1:1, but this doesn't seem to work in Acrobat
	// despite working in the browser viewers, and it's difficult to satisfy both PDF's symbol font character
	// mappings and FontValidator's requirements. Therefore we avoid the issue and just map to Latin-1 characters.
	for(int i=96; i<128; ++i) {
		ttf->MapCharacter(0x60+i, ttf->BeginCompositeGlyph());

		for(int j=0; j<7; ++j) {
			if (i & (1<<j))
				ttf->AddGlyphReference(dotGlyph, 0, (ttfDotVRange*j) / 6);
		}
		ttf->EndCompositeGlyph();
	}

	ttf->SetName(ATTrueTypeName::Copyright, "None - autogenerated");
	ttf->SetName(ATTrueTypeName::FontFamily, "Altirra Print");
	ttf->SetName(ATTrueTypeName::FontSubfamily, "Normal");
	ttf->SetName(ATTrueTypeName::FullFontName, "Altirra Print Normal");
	ttf->SetName(ATTrueTypeName::UniqueFontIdentifier, "Altirra Print Normal");
	ttf->SetName(ATTrueTypeName::Version, "Version 1.0");

	const vdspan<const uint8> fontData = ttf->Finalize();

	// add entry for embedded font (3)
	objectOffsets.push_back((uint32)textOut.Pos());
	textOut.FormatLine("%u 0 obj", (uint32)objectOffsets.size());

	textOut.PutLine(
		"<< /Type /Font "
		"/Subtype /TrueType "
		"/BaseFont /AAAAAA+Print "
		"/FirstChar 32 "
		"/LastChar 32 "
		"/Widths ["
	);

	textOut.FormatLine(" %d", 2*ttfDotRadius);

	textOut.PutLine(
		"] "
		"/FontDescriptor 4 0 R >>");

	textOut.PutLine("endobj");

	// add entry for embedded font descriptor (4)
	objectOffsets.push_back((uint32)textOut.Pos());
	textOut.FormatLine("%u 0 obj", (uint32)objectOffsets.size());

	textOut.FormatLine(
		"<< /Type /FontDescriptor "
		"/FontName /AAAAAA+Print "
		"/Flags 5 "
		"/FontBBox [0 -24 %d 1000] "
		"/ItalicAngle 0 "
		"/Ascent 1000 "
		"/Descent -24 "
		"/CapHeight 1000 "
		"/StemV 80 "
		"/MissingWidth %d "
		"/FontFile2 5 0 R >>",
		2 * ttfDotRadius,
		2 * ttfDotRadius
	);

	textOut.PutLine("endobj");

	// add entry for embedded font (5)
	objectOffsets.push_back((uint32)textOut.Pos());

	textOut.FormatLine("%u 0 obj", (uint32)objectOffsets.size());

	{
		VDMemoryBufferStream bufs;

		bufs.Write(kZLIBHeader, 2);

		uint32 adler32;

		{
			VDDeflateStream defs(bufs, VDDeflateChecksumMode::Adler32);
			defs.SetCompressionLevel(VDDeflateCompressionLevel::Quick);
			defs.Write(fontData.data(), fontData.size());
			defs.Finalize();
			adler32 = defs.Adler32();
		}

		uint32 v = VDToBEU32(adler32);
		bufs.Write(&v, 4);

		unsigned clen = (unsigned)bufs.Length();
		textOut.FormatLine("<</Length %u/Length1 %u/Filter/FlateDecode>>", clen, (unsigned)fontData.size());

		textOut.PutLine("stream");

		textOut.Write((const char *)bufs.GetBuffer().data(), clen);

		textOut.PutLine();
		textOut.PutLine("endstream");
	}

	textOut.PutLine("endobj");

	// render pages
	uint32 basePageObj = (uint32)objectOffsets.size() + 1;

	static constexpr float mmToPoints = 72.0f / 25.4f;
	const float pageWidthMM = mPageWidthMM;
	const float pageHeightMM = 11.0f * 25.4f;
	const float headHeightMM = spec.mDotRadiusMM * 2 + spec.mVerticalDotPitchMM * (float)(spec.mNumPins - 1);
	const int numPages = std::max(1, (int)ceilf(mpOutput->GetDocumentBounds().bottom / pageHeightMM));

	const float lineToBaselineAdjustMM = spec.mbBit0Top ? headHeightMM - spec.mDotRadiusMM : spec.mDotRadiusMM;

	for(int page = 0; page < numPages; ++page) {
		objectOffsets.push_back((uint32)textOut.Pos());
		textOut.FormatLine("%u 0 obj", (uint32)objectOffsets.size());
		textOut.PutLine("<< /Type /Page");
		textOut.PutLine("/Parent 2 0 R");
		textOut.PutLine("/Resources <<");
		textOut.PutLine(" /Font << /Print 3 0 R >>");
		textOut.PutLine(">>");
		textOut.FormatLine("/Contents %u 0 R", (uint32)objectOffsets.size() + 1);
		textOut.PutLine(">>");
		textOut.PutLine("endobj");

		objectOffsets.push_back((uint32)textOut.Pos());
		textOut.FormatLine("%u 0 obj", (uint32)objectOffsets.size());

		VDStringA s;
		
		s.sprintf("%.3f g", powf(kBlackLevel, 1.0f/2.2f));

		if (mpOutput) {
			ATPrinterGraphicalOutput::CullInfo cullInfo {};
			vdrect32f pageRect(0.0f, pageHeightMM * (float)page, pageWidthMM, pageHeightMM * (float)(page + 1));

			if (mpOutput->PreCull(cullInfo, pageRect)) {
				const float mmToUnits = 10000.0f / 25.4f;

				vdfastvector<ATPrinterGraphicalOutput::RenderColumn> cols;
				float lineY = 0;
				while(mpOutput->ExtractNextLine(cols, lineY, cullInfo, pageRect)) {
					// if the head orientation is top-down, reverse the bit pattern to bottom-up
					if (spec.mbBit0Top) {
						int shift = 32 - spec.mNumPins;
						for(ATPrinterGraphicalOutput::RenderColumn& col : cols) {
							uint32 v = col.mPins;

							v = ((v & 0x55555555) << 1) + ((v >> 1) & 0x55555555);
							v = ((v & 0x33333333) << 2) + ((v >> 2) & 0x33333333);
							v = ((v & 0x0F0F0F0F) << 4) + ((v >> 4) & 0x0F0F0F0F);
							v = ((v & 0x00FF00FF) << 8) + ((v >> 8) & 0x00FF00FF);
							v = (v >> 16) + (v << 16);

							col.mPins = v >> shift;
						}
					}

					// encode 10000 units / 1" per tile
					const float unitsPerPoint = 10000.0f / 72.0f;
					const float pointsPerUnit = 1.0f / unitsPerPoint;

					// PDF by default scales the font's em size to 1 unit high, so
					// we need to scale by the desired height of the em square -- which
					// is 1.024 times the height in 10000th of an inch.
					const float fontSize = ttfAscentMM * mmToUnits * 1024.0f / 1000.0f;

					s.append_sprintf(
						" q %.10f 0 0 %.10f 0 0 cm /Print %.2f Tf"
						, pointsPerUnit
						, pointsPerUnit
						, fontSize
					);

					// The font only accommodates up to 7 pins, so if there are more than 7
					// pins in the head, we may need to do more than one band.
					const int numBands = (spec.mNumPins + 6) / 7;

					for(int band = 0; band < numBands; ++band) {
						// Find the first column that has anything in the band. We do this to detect if a band
						// is empty so we can skip the band.
						const int bandPinShift = 7 * band;
						vdspan bandColumns(cols);
						uint32 bandPins = 0x7F << bandPinShift;

						while(!bandColumns.empty()) {
							if (bandColumns.front().mPins & bandPins)
								break;

							bandColumns = bandColumns.subspan(1);
						}

						if (bandColumns.empty())
							continue;

						// Set the line origin and begin the text object. X is easy as it's just the left
						// edge, but Y needs to be adjusted. In PDF, it needs to be set to the baseline,
						// but it's the center of the first dot in the printer output.
						const int fxx0 = VDRoundToInt32((cols[0].mX - pageRect.left) * mmToUnits);
						const int fxy0 = VDRoundToInt32((pageRect.bottom - (lineY + lineToBaselineAdjustMM - spec.mVerticalDotPitchMM * bandPinShift)) * mmToUnits);

						// Sort the columns in the font by ascending X position so we have the smallest
						// delta X offsets.
						std::sort(cols.begin(), cols.end(),
							[](const auto& a, const auto& b) {
								return a.mX < b.mX;
							}
						);

						// begin text object, update text transform, and begin array for TJ command
						s.append_sprintf(" BT %d %d Td [", fxx0, fxy0);

						const float dotWidthMils = 2 * ttfDotRadius;
						const float mmToMils = 1000.0f * mmToUnits / fontSize;
						float xoff = (-pageRect.left * mmToUnits - fxx0) * 1000.0f / fontSize;
						for(const auto& col : bandColumns) {
							uint32 pins = (col.mPins >> bandPinShift) & 0x7F;

							if (pins) {
								const int dx = VDRoundToInt32(col.mX * mmToMils + xoff);

								// apply vertical offset if needed
								if (dx)
									s.append_sprintf("%d", -dx);

								// print pins using character
								s.append_sprintf("<%02X>", pins >= 0x60 ? 0x60 + pins : 0x20 + pins);

								// update X offset tracking based on advance width and applied adjustment
								xoff -= dotWidthMils + (float)dx;
							}
						}

						// print text and end text object
						s += "] TJ ET";
					}

					// end of tile - pop transform
					s += " Q";
				}
			}

			vdfastvector<ATPrinterGraphicalOutput::RenderVector> rvectors;
			mpOutput->ExtractVectors(rvectors, pageRect);

			if (!rvectors.empty()) {
				const float dotRadiusPts = mDotRadiusMM * mmToPoints;
				uint32 lastColorIndex = ~UINT32_C(0);

				// push graphics state, set round end cap
				s.append_sprintf(" q 1 J %.2f w", dotRadiusPts * 2.0f);

				for(const auto& rv : rvectors) {
					if (lastColorIndex != rv.mColorIndex) {
						lastColorIndex = rv.mColorIndex;

						const uint8 (&rgb)[3] = kPenColors[rv.mColorIndex];
						s.append_sprintf(
							" %.2f %.2f %.2f RG"
							, (float)rgb[0] / 255.0f
							, (float)rgb[1] / 255.0f
							, (float)rgb[2] / 255.0f
						);
					}

					const vdfloat2 v1 = vdfloat2 { rv.mX1 - pageRect.left, pageRect.bottom - rv.mY1 } * mmToPoints;
					const vdfloat2 v2 = vdfloat2 { rv.mX2 - pageRect.left, pageRect.bottom - rv.mY2 } * mmToPoints;

					s.append_sprintf(
						" %.2f %.2f m %.2f %.2f l S"
						, v1.x, v1.y
						, v2.x, v2.y
					);
				}

				s += " Q";
			}
		}

		VDMemoryBufferStream bufs;

		bufs.Write(kZLIBHeader, 2);

		uint32 adler32;

		{
			VDDeflateStream defs(bufs, VDDeflateChecksumMode::Adler32);
			defs.SetCompressionLevel(VDDeflateCompressionLevel::Quick);
			defs.Write(s.data(), s.size());
			defs.Finalize();
			adler32 = defs.Adler32();
		}

		uint32 v = VDToBEU32(adler32);
		bufs.Write(&v, 4);

		textOut.FormatLine("<< /Length %u /Filter /FlateDecode >>", (unsigned)bufs.Length());
		textOut.PutLine("stream");

		const auto bufData = bufs.GetBuffer();
		textOut.Write((const char *)bufData.data(), (int)bufData.size());

		textOut.PutLine();
		textOut.PutLine("endstream");
		textOut.PutLine("endobj");
	}

	// write pages table
	objectOffsets[1] = (uint32)textOut.Pos();
	textOut.PutLine("2 0 obj");
	textOut.PutLine("<< /Type /Pages");
	textOut.PutLine("/Kids [");

	for(int i=0; i<numPages; ++i)
		textOut.FormatLine("%u 0 R", basePageObj + i*2);

	textOut.PutLine("]");
	textOut.FormatLine("/Count %u", numPages);
	textOut.FormatLine("/MediaBox [0 0 %f %f]", mPageWidthMM * mmToPoints, pageHeightMM * mmToPoints);
	textOut.PutLine(">>");
	textOut.PutLine("endobj");

	// write cross-reference table
	uint32 xrefPos = (uint32)textOut.Pos();

	textOut.PutLine("xref");
	textOut.FormatLine("0 %u", (unsigned)objectOffsets.size() + 1);
	textOut.PutLine("0000000000 65535 f");

	for(uint32 offset : objectOffsets)
		textOut.FormatLine("%010u 00000 n", offset);

	textOut.PutLine("trailer");
	textOut.PutLine("<< /Root 1 0 R");
	textOut.FormatLine("/Size %u", (unsigned)(objectOffsets.size() + 1));
	textOut.PutLine(">> startxref");
	textOut.FormatLine("%u", xrefPos);
	textOut.PutLine("%%EOF");
}

LRESULT ATUIPrinterGraphicalOutputWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			mCanvas.Init(mhwnd);
			SetTouchMode(kATUITouchMode_2DPanSmooth);
			break;

		case WM_DESTROY:
			OnDestroy();
			break;

		case WM_SIZE:
			OnSize();
			return 0;

		case WM_NCCALCSIZE:
			return OnNCCalcSize(wParam, lParam);

		case WM_KEYDOWN:
			OnKeyDown(wParam, lParam);
			return 0;

		case WM_MOUSEMOVE:
			OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			Focus();
			OnMouseDownL(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;

		case WM_LBUTTONUP:
			OnMouseUpL(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;

		case WM_MOUSEWHEEL:
			OnMouseWheel(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA);
			return 0;

		case WM_CAPTURECHANGED:
			OnCaptureChanged();
			return 0;

		case WM_PAINT:
			OnPaint();
			return 0;

		case WM_ERASEBKGND:
			return 0;

		case WM_GESTURE:
			if (OnGesture(wParam, lParam))
				return 0;
			break;

		case WM_DPICHANGED:
		case ATWM_INHERIT_DPICHANGED:
			OnDpiChanged(wParam);
			break;

		case WM_USER + 100:
			ProcessPendingInvalidation();
			return 0;
	}

	return ATUINativeWindow::WndProc(msg, wParam, lParam);
}

LRESULT ATUIPrinterGraphicalOutputWindow::OnNCCalcSize(WPARAM wParam, LPARAM lParam) {
	if (!wParam)
		return DefWindowProc(mhwnd, WM_NCCALCSIZE, wParam, lParam);

	NCCALCSIZE_PARAMS& params = *(NCCALCSIZE_PARAMS *)lParam;

	// ask DWP to compute the new client size
	DefWindowProc(mhwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&params.rgrc[0]);

	// compute the source and destination client rect sizes
	sint32 neww = params.rgrc[0].right  - params.rgrc[0].left;
	sint32 newh = params.rgrc[0].bottom - params.rgrc[0].top;
	sint32 oldw = params.rgrc[2].right  - params.rgrc[2].left;
	sint32 oldh = params.rgrc[2].bottom - params.rgrc[2].top;

	// compute the difference in center offsets
	sint32 newcx = neww / 2;
	sint32 newcy = newh / 2;
	sint32 oldcx = oldw / 2;
	sint32 oldcy = oldh / 2;

	// compute the size to copy
	sint32 savecx = std::min<sint32>(oldcx, newcx);
	sint32 savecy = std::min<sint32>(oldcy, newcy);
	sint32 savew = std::min<sint32>(oldw, neww);
	sint32 saveh = std::min<sint32>(oldh, newh);

	// set source and dest copy rects
	params.rgrc[1].left   = params.rgrc[0].left + newcx - savecx;
	params.rgrc[1].top    = params.rgrc[0].top  + newcy - savecy;
	params.rgrc[1].right  = params.rgrc[1].left + savew;
	params.rgrc[1].bottom = params.rgrc[1].top  + saveh;

	params.rgrc[2].left   = params.rgrc[2].left + oldcx - savecx;
	params.rgrc[2].top    = params.rgrc[2].top  + oldcy - savecy;
	params.rgrc[2].right  = params.rgrc[2].left + savew;
	params.rgrc[2].bottom = params.rgrc[2].top  + saveh;

	return WVR_VALIDRECTS;
}

void ATUIPrinterGraphicalOutputWindow::OnDestroy() {
	if (mpOutput) {
		mpOutput->SetOnInvalidation(nullptr);
		mpOutput = nullptr;
	}
}

void ATUIPrinterGraphicalOutputWindow::OnSize() {
	auto [w, h] = GetClientSize();

	if (mViewWidthPixels != w || mViewHeightPixels != h) {
		mViewWidthPixels = w;
		mViewHeightPixels = h;

		// Update the view origin based on the center and new size. We don't redraw
		// here as that's already done by the NCCALCSIZE correction.
		UpdateViewOrigin();
	}
}

void ATUIPrinterGraphicalOutputWindow::OnKeyDown(uint32 vk, uint32 flags) {
	const bool ctrl = GetKeyState(VK_CONTROL) < 0;

	switch(vk) {
		case VK_ESCAPE:
			if (ATGetUIPane(kATUIPaneId_Display))
				ATActivateUIPane(kATUIPaneId_Display, true);
			break;

		case VK_LEFT:
			ScrollByPixels(ctrl ? 1 : 100, 0);
			break;

		case VK_RIGHT:
			ScrollByPixels(ctrl ? -1 : -100, 0);
			break;

		case VK_UP:
			ScrollByPixels(0, ctrl ? 1 : 100);
			break;

		case VK_DOWN:
			ScrollByPixels(0, ctrl ? -1 : -100);
			break;

		case VK_PRIOR:
			ScrollByPixels(0, mViewHeightPixels);
			break;

		case VK_NEXT:
			ScrollByPixels(0, -mViewHeightPixels);
			break;

		case VK_OEM_PLUS:
			if (ctrl)
				SetZoom(mZoomClicks + 1.0f);
			break;

		case VK_OEM_MINUS:
			if (ctrl)
				SetZoom(mZoomClicks - 1.0f);
			break;

#ifdef ATNRELEASE
		case 'B':
			if (GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) < 0) {
				const auto start = VDGetPreciseTick();
				int iterations = 0;

				for(uint32 tick = VDGetCurrentTick(); VDGetCurrentTick() - tick < 20000;) {
					for(int i=0; i<10; ++i) {
						InvalidateRect(mhwnd, nullptr, FALSE);
						UpdateWindow(mhwnd);
					}

					iterations += 10;
				}
				
				const auto end = VDGetPreciseTick();
				const double elapsed = (double)(end - start) * VDGetPreciseSecondsPerTick();
				VDDEBUG2("%d iterations in %.2fms (%.2fms/iteration)\n", iterations, elapsed * 1000.0, elapsed * 1000.0 / (double)iterations);

				PostQuitMessage(0);
			}
			break;
#endif
	}
}

void ATUIPrinterGraphicalOutputWindow::OnMouseMove(int x, int y) {
	if (mbDragging) {
		int dx = x - mDragLastX;
		int dy = y - mDragLastY;
		mDragLastX = x;
		mDragLastY = y;

		ScrollByPixels(dx, dy);
	}
}

void ATUIPrinterGraphicalOutputWindow::OnMouseDownL(int x, int y) {
	mbDragging = true;
	mDragLastX = x;
	mDragLastY = y;
	::SetCapture(mhwnd);
}

void ATUIPrinterGraphicalOutputWindow::OnMouseUpL(int x, int y) {
	if (mbDragging) {
		mbDragging = false;
		::ReleaseCapture();
	}
}

void ATUIPrinterGraphicalOutputWindow::OnMouseWheel(int x, int y, float delta) {
	mWheelAccum += delta;

	const int clicks = (int)floorf(mWheelAccum + 0.5f);

	if (clicks) {
		const vdpoint32& cpt = TransformScreenToClient(vdpoint32(x, y));
		SetZoom(mZoomClicks + (float)clicks, cpt);

		mWheelAccum -= (int)clicks;
	}
}

void ATUIPrinterGraphicalOutputWindow::OnCaptureChanged() {
	mbDragging = false;
}

bool ATUIPrinterGraphicalOutputWindow::OnGesture(WPARAM wParam, LPARAM lParam) {
	GESTUREINFO gestureInfo {sizeof(GESTUREINFO)};

	const BOOL haveInfo = GetGestureInfo((HGESTUREINFO)lParam, &gestureInfo);
	if (!haveInfo)
		return false;

	CloseGestureInfoHandle((HGESTUREINFO)lParam);

	switch(gestureInfo.dwID) {
		case GID_BEGIN:
			mbInGesture = true;
			mbFirstGestureEvent = true;
			break;

		case GID_END:
			mbInGesture = false;
			mbFirstGestureEvent = false;
			break;

		default:
			if (mbInGesture) {
				vdpoint32 pt(gestureInfo.ptsLocation.x, gestureInfo.ptsLocation.y);

				switch(gestureInfo.dwID) {
					case GID_PAN:
						if (!mbFirstGestureEvent)
							ScrollByPixels(pt.x - mGestureOrigin.x, pt.y - mGestureOrigin.y);

						mGestureOrigin = pt;
						break;

					case GID_ZOOM:
						if (float distance = (float)gestureInfo.ullArguments; mbFirstGestureEvent) {
							mGestureZoomOrigin = distance;
						} else {
							if (distance > mGestureZoomOrigin * 1.10f) {
								mGestureZoomOrigin *= 1.1487f;
								SetZoom(mZoomClicks + 1.0f);
							} else if (distance < mGestureZoomOrigin / 1.10f) {
								mGestureZoomOrigin /= 1.1487f;
								SetZoom(mZoomClicks - 1.0f);
							}
						}
						break;
				}

				mbFirstGestureEvent = false;
			}
			break;
	}

	return false;
}

void ATUIPrinterGraphicalOutputWindow::OnPaint() {
	PAINTSTRUCT ps;
	if (HDC hdc = mCanvas.BeginDirect(ps, false)) {
		const sint32 w = ps.rcPaint.right - ps.rcPaint.left;
		const sint32 h = ps.rcPaint.bottom - ps.rcPaint.top;

		if (w > 0 && h > 0) {
			RECT *paintRects = &ps.rcPaint;
			size_t numPaintRects = 1;

			HRGN hrgnUpdate = ::CreateRectRgn(0, 0, 0, 0);
			if (hrgnUpdate) {
				if (::GetRandomRgn(hdc, hrgnUpdate, SYSRGN) > 0) {
					DWORD numBytes = ::GetRegionData(hrgnUpdate, 0, nullptr);

					if (numBytes) {
						if (mRegionData.size() < numBytes)
							mRegionData.resize(numBytes);

						if (::GetRegionData(hrgnUpdate, mRegionData.size(), mRegionData.data())) {
							paintRects = (RECT *)mRegionData->Buffer;
							numPaintRects = mRegionData->rdh.nCount;

							// convert all rects from screen to client coords
							for(size_t i = 0; i < numPaintRects; ++i) {
								RECT& r = paintRects[i];

								const auto& cr = TransformScreenToClient(vdrect32(r.left, r.top, r.right, r.bottom));

								r.left = cr.left;
								r.top = cr.top;
								r.right = cr.right;
								r.bottom = cr.bottom;
							}
						}
					}
				}
				::DeleteObject(hrgnUpdate);
			}

			while(numPaintRects--) {
				RECT paintRect = *paintRects++;

				// convert left and right to document coordinates
				const float viewfx1 = mViewOriginX + (float)paintRect.left * mViewMMPerPixel;
				const float viewfx2 = mViewOriginX + (float)paintRect.right * mViewMMPerPixel;

				// if we are full left/right, just clear to background color
				SetBkColor(hdc, 0x808080);
				SetBkMode(hdc, OPAQUE);

				if (viewfx2 <= 0 || viewfx1 >= mPageWidthMM) {
					ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &paintRect, L"", 0, nullptr);
				} else {
					// compute clipped page width within paint bounds, now that we know we won't
					// massively overflow
					sint32 paintx1 = paintRect.left;
					sint32 paintx2 = paintRect.right;
					sint32 painty1 = paintRect.top;
					sint32 painty2 = paintRect.bottom;

					// check for clipping at top of page
					const float viewfy1 = mViewOriginY + (float)paintRect.top * mViewMMPerPixel;
					if (viewfy1 < 0) {
						painty1 = (sint32)ceilf(-mViewOriginY * mViewPixelsPerMM - 0.5f / 8.0f);

						RECT rTop = paintRect;
						rTop.left = paintx1;
						rTop.right = paintx2;
						rTop.bottom = painty1;

						ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &rTop, L"", 0, nullptr);

						paintRect.top = painty1;
					}

					if (viewfx1 < 0) {
						paintx1 = (sint32)ceilf(-mViewOriginX * mViewPixelsPerMM - 0.5f / 8.0f);

						RECT rLeft  = paintRect;
						rLeft.right = paintx1;

						ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &rLeft, L"", 0, nullptr);
					}

					if (viewfx2 > mPageWidthMM) {
						paintx2 = (sint32)ceilf((mPageWidthMM - mViewOriginX) * mViewPixelsPerMM - 7.5f / 8.0f);

						RECT rRight  = paintRect;
						rRight.left = paintx2;

						ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &rRight, L"", 0, nullptr);
					}

					const sint32 paintw = paintx2 - paintx1;
					const sint32 painth = painty2 - painty1;

					if (paintw > 0 && painth > 0) {
						ViewTransform viewTransform;
						viewTransform.mOriginX = mViewOriginX;
						viewTransform.mOriginY = mViewOriginY;
						viewTransform.mMMPerPixel = mViewMMPerPixel;
						viewTransform.mPixelsPerMM = mViewPixelsPerMM;

						if (Render(viewTransform, paintx1, painty1, paintw, painth, false)) {
							BITMAPINFO bi {};
							bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
							bi.bmiHeader.biWidth = paintw;
							bi.bmiHeader.biHeight = painth;
							bi.bmiHeader.biPlanes = 1;
							bi.bmiHeader.biBitCount = 32;
							bi.bmiHeader.biCompression = BI_RGB;
							bi.bmiHeader.biSizeImage = paintw * h * 4;

							SetDIBitsToDevice(hdc, paintx1, painty1, paintw, painth, 0, 0, 0, painth, mFrameBuffer.data(), &bi, DIB_RGB_COLORS);
						} else {
							SetBkColor(hdc, VDSwizzleU32(mGammaTable[0]) >> 8);

							const RECT r { paintx1, painty1, paintx2, painty2 };

							ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &r, L"", 0, nullptr);
						}
					}
				}
			}
		}

		mCanvas.EndDirect(ps);
	}
}

void ATUIPrinterGraphicalOutputWindow::OnDpiChanged(int dpi) {
	if (dpi > 0 && dpi < 10000) {
		if (mViewDpi != dpi)
			UpdateZoom(nullptr);
	}
}

void ATUIPrinterGraphicalOutputWindow::UpdateViewOrigin() {
	mViewOriginX = mViewCenterX - (float)(mViewWidthPixels  / 2) * mViewMMPerPixel;
	mViewOriginY = mViewCenterY - (float)(mViewHeightPixels / 2) * mViewMMPerPixel;
}

void ATUIPrinterGraphicalOutputWindow::ScrollByPixels(int dx, int dy) {
	if (dx || dy) {
		mViewCenterX -= mViewMMPerPixel * dx;
		mViewCenterY -= mViewMMPerPixel * dy;
		UpdateViewOrigin();

		mCanvas.Scroll(dx, dy);
	}
}

void ATUIPrinterGraphicalOutputWindow::SetZoom(float clicks) {
	SetZoom(clicks, vdpoint32(mViewWidthPixels / 2, mViewHeightPixels / 2));
}

void ATUIPrinterGraphicalOutputWindow::SetZoom(float clicks, const vdpoint32& centerPt) {
	float newZoom = std::clamp(clicks, kZoomMin, kZoomMax);

	if (fabsf(mZoomClicks - newZoom) < 0.0001f)
		return;

	mZoomClicks = newZoom;

	UpdateZoom(&centerPt);
}

void ATUIPrinterGraphicalOutputWindow::UpdateZoom(const vdpoint32* centerPt) {
	float basePixelsPerMM = (float)mViewDpi / 25.4f;
	float newPixelsPerMM = basePixelsPerMM * powf(2.0f, (float)mZoomClicks / 5.0f);

	if (fabsf(mViewPixelsPerMM - newPixelsPerMM) > 1e-3f*std::max(mViewPixelsPerMM, newPixelsPerMM)) {
		const float pivotDX = centerPt ? centerPt->x - mViewWidthPixels / 2 : 0.0f;
		const float pivotDY = centerPt ? centerPt->y - mViewHeightPixels / 2 : 0.0f;

		float anchorX = mViewCenterX + mViewMMPerPixel * pivotDX;
		float anchorY = mViewCenterY + mViewMMPerPixel * pivotDY;

		mViewPixelsPerMM = newPixelsPerMM;
		mViewMMPerPixel = 1.0f / mViewPixelsPerMM;

		mViewCenterX = anchorX - mViewMMPerPixel * pivotDX;
		mViewCenterY = anchorY - mViewMMPerPixel * pivotDY;
		UpdateViewOrigin();

		ForceFullInvalidation();
	}
}

void ATUIPrinterGraphicalOutputWindow::ForceFullInvalidation() {
	InvalidateRect(mhwnd, nullptr, false);
	if (mpOutput) {
		bool all;
		vdrect32f r;
		mpOutput->ExtractInvalidationRect(all, r);
	}
}

void ATUIPrinterGraphicalOutputWindow::ProcessPendingInvalidation() {
	mbInvalidationPending = false;

	if (mpOutput) {
		bool invAll;
		vdrect32f invRect;

		if (mpOutput->ExtractInvalidationRect(invAll, invRect)) {
			if (invAll)
				InvalidateRect(mhwnd, nullptr, false);
			else
				InvalidatePaperArea(invRect.left, invRect.top, invRect.right, invRect.bottom);
		}
	}
}

void ATUIPrinterGraphicalOutputWindow::InvalidatePaperArea(float x1, float y1, float x2, float y2) {
	if (x1 >= x2 || y1 >= y2)
		return;

	x1 -= mViewOriginX;
	x2 -= mViewOriginX;
	y1 -= mViewOriginY;
	y2 -= mViewOriginY;

	x1 = x1 * mViewPixelsPerMM;
	y1 = y1 * mViewPixelsPerMM;
	x2 = x2 * mViewPixelsPerMM;
	y2 = y2 * mViewPixelsPerMM;

	if (x1 < 0)
		x1 = 0;

	if (y1 < 0)
		y1 = 0;

	if (x2 > (float)mViewWidthPixels)
		x2 = (float)mViewWidthPixels;

	if (y2 > (float)mViewHeightPixels)
		y2 = (float)mViewHeightPixels;

	if (x1 >= x2 || y1 >= y2)
		return;

	RECT r {
		(LONG)ceilf(x1 - 0.5f - 3.5f / 8.0f),
		(LONG)ceilf(y1 - 0.5f - 3.5f / 8.0f),
		(LONG)ceilf(x2 - 0.5f + 3.5f / 8.0f),
		(LONG)ceilf(y2 - 0.5f + 3.5f / 8.0f)
	};

	if (r.left < r.right && r.top < r.bottom)
		InvalidateRect(mhwnd, &r, FALSE);
}

vdrect32f ATUIPrinterGraphicalOutputWindow::GetDocumentBounds() const {
	if (mpOutput)
		return mpOutput->GetDocumentBounds();
	else
		return vdrect32f(0, 0, 0, 0);
}

bool ATUIPrinterGraphicalOutputWindow::Render(const ViewTransform& viewTransform, sint32 x, sint32 y, uint32 w, uint32 h, bool force) {
	// compute pixel bounding box
	const sint32 viewix1 = x;
	const sint32 viewiy1 = y;
	const sint32 viewix2 = x + w;
	const sint32 viewiy2 = y + h;
	const float viewfx1 = (float)viewix1;
	const float viewfy1 = (float)viewiy1;
	const float viewfx2 = (float)viewix2;
	const float viewfy2 = (float)viewiy2;

	// compute document space bounding box
	const float viewDocX1 = viewTransform.mOriginX + viewfx1 * viewTransform.mMMPerPixel;
	const float viewDocY1 = viewTransform.mOriginY + viewfy1 * viewTransform.mMMPerPixel;
	const float viewDocX2 = viewTransform.mOriginX + viewfx2 * viewTransform.mMMPerPixel;
	const float viewDocY2 = viewTransform.mOriginY + viewfy2 * viewTransform.mMMPerPixel;

	// pre-cull line range to entire vertical range
	const bool hasVectors = mpOutput && mpOutput->HasVectors();

	ATPrinterGraphicalOutput::CullInfo cullInfo;

	if (mpOutput) {
		if (!mpOutput->PreCull(cullInfo, vdrect32f(viewDocX1, viewDocY1, viewDocX2, viewDocY2)) && !force && !hasVectors)
			return false;
	}

	// clear framebuffer
	mFrameBuffer.resize(w * h);
	std::fill(mFrameBuffer.begin(), mFrameBuffer.end(), 0xFFFFFFFF);

	// initialize antialiasing buffer
	const size_t abufferSize = w * (hasVectors ? 24 : 8);

	if (mABuffer.size() < abufferSize)
		mABuffer.resize(abufferSize);

	// render one scan line at a time
	const float viewSubPixelsPerMM = viewTransform.mPixelsPerMM * 8.0f;
	const float rowCenterToFirstSubRowOffset = viewTransform.mMMPerPixel * (-3.5f / 8.0f);
	const float subRowStep = viewTransform.mMMPerPixel / 8.0f;
	const float dotRadius = mDotRadiusMM;
	const float dotRadiusSq = dotRadius * dotRadius;
	const sint32 subw = w * 8;

	alignas(16) static constexpr float kDither[8] {
		(0.0f - 3.5f) / 8.0f,
		(4.0f - 3.5f) / 8.0f,
		(2.0f - 3.5f) / 8.0f,
		(6.0f - 3.5f) / 8.0f,
		(1.0f - 3.5f) / 8.0f,
		(5.0f - 3.5f) / 8.0f,
		(3.0f - 3.5f) / 8.0f,
		(7.0f - 3.5f) / 8.0f,
	};

	vdfloat32x4 subRowYOffsets_a = vdfloat32x4::set(kDither[0] + 0.0f, kDither[1] + 1.0f, kDither[2] + 2.0f, kDither[3] + 3.0f) * subRowStep + rowCenterToFirstSubRowOffset;
	vdfloat32x4 subRowYOffsets_b = vdfloat32x4::set(kDither[4] + 4.0f, kDither[5] + 5.0f, kDither[6] + 6.0f, kDither[7] + 7.0f) * subRowStep + rowCenterToFirstSubRowOffset;

	for(uint32 yoff = 0; yoff < h; ++yoff) {
		const float docRowYC = viewDocY1 + ((float)yoff + 0.5f) * viewTransform.mMMPerPixel;
		const float docRowYD = 0.5f * viewTransform.mMMPerPixel;

		// pre-cull lines to scan line vertical range
		const float docRowY1 = docRowYC - docRowYD;
		const float docRowY2 = docRowYC + docRowYD;

		// pre-cull dots to scan line
		for(auto& dcb : mDotCullBuffer)
			dcb.clear();

		mVectorCullBuffer.clear();

		if (mpOutput) {
			const vdrect32f cullRect { viewDocX1 - viewTransform.mMMPerPixel * 0.5f, docRowY1, viewDocX2 + viewTransform.mMMPerPixel * 0.5f, docRowY2 };
			mpOutput->ExtractNextLineDots(mDotCullBuffer[0], cullInfo, cullRect);

			if (hasVectors) {
				vdrect32f dotCullRect {
					cullRect.left - mDotRadiusMM,
					cullRect.top - mDotRadiusMM,
					cullRect.right + mDotRadiusMM,
					cullRect.bottom + mDotRadiusMM
				};

				mpOutput->ExtractVectors(mVectorCullBuffer, cullRect);

				for (const RenderVector& v : mVectorCullBuffer) {
					auto& dcb = mDotCullBuffer[v.mColorIndex];
					if (dotCullRect.contains(vdpoint32f { v.mX1, v.mY1 }))
						dcb.emplace_back(v.mX1, v.mY1);

					if (dotCullRect.contains(vdpoint32f { v.mX2, v.mY2 }))
						dcb.emplace_back(v.mX2, v.mY2);
				}
			}
		}

		// clear the antialiasing buffer
		uint8 *abuf = mABuffer.data();
		memset(abuf, 0, abufferSize);

		// render all dots
		vdfloat32x4 ditherx_a = vdfloat32x4::set(kDither[0], kDither[1], kDither[2], kDither[3]);
		vdfloat32x4 ditherx_b = vdfloat32x4::set(kDither[4], kDither[5], kDither[6], kDither[7]);

		const float docRowYDPlusDotR = docRowYD + dotRadius;
		const float subwf = (float)subw;
		for(int i=0; i<4; ++i) {
			for(const RenderDot& VDRESTRICT dot : mDotCullBuffer[i]) {
				float dy = dot.mY - docRowYC;

				// vertical cull to row
				if (fabsf(dy) >= docRowYDPlusDotR)
					continue;

				// process one subrow at a time
				sint32 subSpans[2][8];

				vdfloat32x4 z = vdfloat32x4::zero();
				vdfloat32x4 vsubwf = vdfloat32x4::set1(subwf);
				vdfloat32x4 dy2_a = dy - subRowYOffsets_a;
				vdfloat32x4 dy2_b = dy - subRowYOffsets_b;
				vdfloat32x4 dx_a = sqrt(max(dotRadiusSq - dy2_a*dy2_a, z)) * viewSubPixelsPerMM;
				vdfloat32x4 dx_b = sqrt(max(dotRadiusSq - dy2_b*dy2_b, z)) * viewSubPixelsPerMM;

				// compute x range in document space
				const float xc = (dot.mX - viewDocX1) * viewSubPixelsPerMM;
				const vdfloat32x4 xc_a = xc + ditherx_a;
				const vdfloat32x4 xc_b = xc + ditherx_b;
				const vdfloat32x4 x1_a = min(max(xc_a - dx_a, z), vsubwf);
				const vdfloat32x4 x1_b = min(max(xc_b - dx_b, z), vsubwf);
				const vdfloat32x4 x2_a = min(max(xc_a + dx_a, z), vsubwf);
				const vdfloat32x4 x2_b = min(max(xc_b + dx_b, z), vsubwf);

				// convert x range to subpixels
				const vdint32x4 ix1_a = ceilint(x1_a);
				const vdint32x4 ix1_b = ceilint(x1_b);
				const vdint32x4 ix2_a = ceilint(x2_a);
				const vdint32x4 ix2_b = ceilint(x2_b);

				// horizontally clip
				storeu(&subSpans[0][0], ix1_a);
				storeu(&subSpans[0][4], ix1_b);
				storeu(&subSpans[1][0], ix2_a);
				storeu(&subSpans[1][4], ix2_b);

				RenderTrapezoid(subSpans, i, hasVectors);
			}
		}

		// render all vectors
		for(const RenderVector& v : mVectorCullBuffer) {
			// sort points so the first point is on top
			const float x1 = v.mY1 < v.mY2 ? v.mX1 : v.mX2;
			const float y1 = v.mY1 < v.mY2 ? v.mY1 : v.mY2;
			const float x2 = v.mY1 < v.mY2 ? v.mX2 : v.mX1;
			const float y2 = v.mY1 < v.mY2 ? v.mY2 : v.mY1;

			// compute perpendicular vector (points left)
			const float dx = x2 - x1;
			const float dy = y2 - y1;
			const float l2 = dx*dx + dy*dy;
			if (l2 < 1e-4f)
				continue;

			const float ps = l2 > 1e-6f ? mDotRadiusMM / sqrtf(l2) : 0.0f;
			const float xp = -dy * ps;
			const float yp = dx * ps;

			// init subrow edges
			const vdfloat32x4 vsubwf = vdfloat32x4::set1((float)subw);

			vdfloat32x4 subSpansF[2][2];
			sint32 subSpans[2][8];

			for(int i=0; i<2; ++i) {
				subSpansF[0][i] = vdfloat32x4::zero();
				subSpansF[1][i] = vsubwf;
			}

			// compute edges in counterclockwise order
			struct Edge {
				float x;
				float y;
				float dx;
				float dy;
			} edges[4] {
				{ x1 + xp, y1 + yp, dx, dy },
				{ x1 - xp, y1 - yp, -dx, -dy },
				{ x2, y2, dy, -dx },
				{ x1, y1, -dy, dx },
			};

			// render descending edges on left and ascending edges on right
			vdfloat32x4 suby_a = docRowYC + subRowYOffsets_a;
			vdfloat32x4 suby_b = docRowYC + subRowYOffsets_b;

			for(const Edge& edge : edges) {
				// check if edge is horizontal
				if (fabsf(edge.dy) < 1e-4f) {
					vdfloat32x4 vedgey = vdfloat32x4::set1(edge.y);

					// check orientation
					if (edge.dx < 0) {
						// edge going left -- inward facing down
						subSpansF[1][0] = min(subSpansF[1][0], nonzeromask(vsubwf, cmpge(suby_a, vedgey)));
						subSpansF[1][1] = min(subSpansF[1][1], nonzeromask(vsubwf, cmpge(suby_b, vedgey)));
					} else {
						// edge going right -- inward facing up
						subSpansF[1][0] = min(subSpansF[1][0], nonzeromask(vsubwf, cmpgt(vedgey, suby_a)));
						subSpansF[1][1] = min(subSpansF[1][1], nonzeromask(vsubwf, cmpgt(vedgey, suby_b)));
					}
				} else {
					const float edgeSlope = edge.dx / edge.dy;
					const float subxinc = edgeSlope * subRowStep * viewSubPixelsPerMM;
					const float subx0 = ((edge.x - viewDocX1) + (suby_a.x() - edge.y) * edgeSlope) * viewSubPixelsPerMM - 0.5f;

					if (edge.dy > 0) {
						subSpansF[0][0] = max(subSpansF[0][0], subx0 + subxinc*vdfloat32x4::set(0, 1, 2, 3));
						subSpansF[0][1] = max(subSpansF[0][1], subx0 + subxinc*vdfloat32x4::set(4, 5, 6, 7));
					} else {
						subSpansF[1][0] = min(subSpansF[1][0], subx0 + subxinc*vdfloat32x4::set(0, 1, 2, 3));
						subSpansF[1][1] = min(subSpansF[1][1], subx0 + subxinc*vdfloat32x4::set(4, 5, 6, 7));
					}
				}
			}

			// convert subspan edges to integer subpixel coordinates
			storeu(&subSpans[0][0], ceilint(subSpansF[0][0] + ditherx_a));
			storeu(&subSpans[0][4], ceilint(subSpansF[0][1] + ditherx_b));
			storeu(&subSpans[1][0], ceilint(subSpansF[1][0] + ditherx_a));
			storeu(&subSpans[1][4], ceilint(subSpansF[1][1] + ditherx_b));

			RenderTrapezoid(subSpans, v.mColorIndex, true);
		}

		// render antialiasing buffer row to framebuffer
		Downsample8x8(&mFrameBuffer[w * (h - 1 - yoff)], abuf, w, hasVectors);
	}

	return true;
}

void ATUIPrinterGraphicalOutputWindow::RenderTrapezoid(const sint32 subSpans[2][8], uint32 penIndex, bool rgb) {
	uint8 *VDRESTRICT abuf = mABuffer.data();

	if (rgb) {
#if defined(VD_CPU_X64) || defined(VD_CPU_X86)
		if (SSE2_enabled)
			return RenderTrapezoidRGB_SSE2(subSpans, penIndex);

		return RenderTrapezoidRGB_Scalar(subSpans, penIndex);
#elif defined(VD_CPU_ARM64)
		return RenderTrapezoidRGB_NEON(subSpans, penIndex);
#endif

	} else {
		for(uint32 subRow = 0; subRow < 8; ++subRow) {
			const sint32 csubx1 = subSpans[0][subRow];
			const sint32 csubx2 = subSpans[1][subRow];

			if (csubx1 >= csubx2)
				continue;

			// draw bits
			const uint32 ucsubx1 = (uint32)csubx1;
			const uint32 ucsubx2 = (uint32)csubx2;
			const uint32 maskx1 = ucsubx1 >> 3;
			const uint32 maskx2 = (ucsubx2 - 1) >> 3;
			const uint8 mask1 = 0xFF >> (ucsubx1 & 7);
			const uint8 mask2 = 0xFF << ((8 - ucsubx2) & 7);
			uint8 *__restrict dst = abuf + subRow + maskx1*8;

			if (maskx1 == maskx2) {
				*dst |= mask1 & mask2;
			} else {
				*dst |= mask1;
				dst += 8;

				for(uint32 x = maskx1 + 1; x < maskx2; ++x) {
					*dst = 0xFF;
					dst += 8;
				}

				*dst |= mask2;
			}
		}
	}
}

void ATUIPrinterGraphicalOutputWindow::RenderTrapezoidRGB_Scalar(const sint32 subSpans[2][8], uint32 penIndex) {
	uint8 *VDRESTRICT abuf = mABuffer.data();
	const auto *VDRESTRICT penDithers = mPenDithers[penIndex];

	for(uint32 subRow = 0; subRow < 8; ++subRow) {
		const sint32 csubx1 = subSpans[0][subRow];
		const sint32 csubx2 = subSpans[1][subRow];

		if (csubx1 >= csubx2)
			continue;

		const uint8 rdither = penDithers[0][subRow];
		const uint8 gdither = penDithers[1][subRow];
		const uint8 bdither = penDithers[2][subRow];

		// draw bits
		const uint32 ucsubx1 = (uint32)csubx1;
		const uint32 ucsubx2 = (uint32)csubx2;
		const uint32 maskx1 = ucsubx1 >> 3;
		const uint32 maskx2 = (ucsubx2 - 1) >> 3;
		const uint8 mask1 = 0xFF >> (ucsubx1 & 7);
		const uint8 mask2 = 0xFF << ((8 - ucsubx2) & 7);
		uint8 *VDRESTRICT dst = abuf + subRow + maskx1*24;

		if (maskx1 == maskx2) {
			const uint8 mask = mask1 & mask2;

			dst[0] |= rdither & mask;
			dst[8] |= gdither & mask;
			dst[16] |= bdither & mask;
		} else {
			dst[0] |= rdither & mask1;
			dst[8] |= gdither & mask1;
			dst[16] |= bdither & mask1;
			dst += 24;

			for(uint32 x = maskx1 + 1; x < maskx2; ++x) {
				dst[0] |= rdither;
				dst[8] |= gdither;
				dst[16] |= bdither;
				dst += 24;
			}

			dst[0] |= rdither & mask2;
			dst[8] |= gdither & mask2;
			dst[16] |= bdither & mask2;
		}
	}
}

#if defined(VD_CPU_X64) || defined(VD_CPU_X86)
void ATUIPrinterGraphicalOutputWindow::RenderTrapezoidRGB_SSE2(const sint32 subSpans[2][8], uint32 penIndex) {
#if defined(VD_CPU_X64)
	const uint64 rdither = VDReadUnalignedU64(mPenDithers[penIndex][0]);
	const uint64 gdither = VDReadUnalignedU64(mPenDithers[penIndex][1]);
	const uint64 bdither = VDReadUnalignedU64(mPenDithers[penIndex][2]);
#else
	const uint64 rdither1 = VDReadUnalignedU32(&mPenDithers[penIndex][0][0]);
	const uint64 rdither2 = VDReadUnalignedU32(&mPenDithers[penIndex][0][4]);
	const uint64 gdither1 = VDReadUnalignedU32(&mPenDithers[penIndex][1][0]);
	const uint64 gdither2 = VDReadUnalignedU32(&mPenDithers[penIndex][1][4]);
	const uint64 bdither1 = VDReadUnalignedU32(&mPenDithers[penIndex][2][0]);
	const uint64 bdither2 = VDReadUnalignedU32(&mPenDithers[penIndex][2][4]);
#endif

	// compute min/max of non-empty scans
	sint32 minSubX1 = 0x7FFFFFFF;
	sint32 maxSubX2 = 0;

	for(int i=0; i<8; ++i) {
		sint32 subx1 = subSpans[0][i];
		sint32 subx2 = subSpans[1][i];

		if (subx1 < subx2) {
			if (minSubX1 > subx1)
				minSubX1 = subx1;

			if (maxSubX2 < subx2)
				maxSubX2 = subx2;
		}
	}

	if (minSubX1 >= maxSubX2)
		return;

	// expand to byte boundaries
	minSubX1 &= ~(sint32)7;
	maxSubX2 = (maxSubX2 + 7) & ~(sint32)7;

	// load subscan ranges
	__m128i subX1A = _mm_loadu_si128((const __m128i *)&subSpans[0][0]);
	__m128i subX1B = _mm_loadu_si128((const __m128i *)&subSpans[0][4]);
	__m128i subX2A = _mm_loadu_si128((const __m128i *)&subSpans[1][0]);
	__m128i subX2B = _mm_loadu_si128((const __m128i *)&subSpans[1][4]);

	// rasterize blocks
	uint8 *__restrict dst = mABuffer.data() + (ptrdiff_t)minSubX1*3;

	for(sint32 blockStart = minSubX1; blockStart < maxSubX2; blockStart += 128) {
		// compute block-relative bounds with -128 bias
		__m128i blockPos = _mm_set1_epi32(blockStart + 128);
		__m128i blockSubX1A = _mm_sub_epi32(subX1A, blockPos);
		__m128i blockSubX1B = _mm_sub_epi32(subX1B, blockPos);
		__m128i blockSubX2A = _mm_sub_epi32(subX2A, blockPos);
		__m128i blockSubX2B = _mm_sub_epi32(subX2B, blockPos);

		// compress to signed bytes with saturation
		__m128i blockSubX1 = _mm_packs_epi32(blockSubX1A, blockSubX1B);
		__m128i blockSubX2 = _mm_packs_epi32(blockSubX2A, blockSubX2B);
		__m128i blockSubX12b = _mm_packs_epi16(blockSubX1, blockSubX2);

		// double up bytes
		__m128i blockSubX1bb = _mm_unpacklo_epi8(blockSubX12b, blockSubX12b);
		__m128i blockSubX2bb = _mm_unpackhi_epi8(blockSubX12b, blockSubX12b);

		// set up 64 ranges
		__m128i blockSubX1bbl = _mm_unpacklo_epi16(blockSubX1bb, blockSubX1bb);
		__m128i blockSubX1bbh = _mm_unpackhi_epi16(blockSubX1bb, blockSubX1bb);
		__m128i blockSubX2bbl = _mm_unpacklo_epi16(blockSubX2bb, blockSubX2bb);
		__m128i blockSubX2bbh = _mm_unpackhi_epi16(blockSubX2bb, blockSubX2bb);

		__m128i row01_x1 = _mm_shuffle_epi32(blockSubX1bbl, 0b0'01010000);
		__m128i row01_x2 = _mm_shuffle_epi32(blockSubX2bbl, 0b0'01010000);
		__m128i row23_x1 = _mm_shuffle_epi32(blockSubX1bbl, 0b0'11111010);
		__m128i row23_x2 = _mm_shuffle_epi32(blockSubX2bbl, 0b0'11111010);
		__m128i row45_x1 = _mm_shuffle_epi32(blockSubX1bbh, 0b0'01010000);
		__m128i row45_x2 = _mm_shuffle_epi32(blockSubX2bbh, 0b0'01010000);
		__m128i row67_x1 = _mm_shuffle_epi32(blockSubX1bbh, 0b0'11111010);
		__m128i row67_x2 = _mm_shuffle_epi32(blockSubX2bbh, 0b0'11111010);

		// set up bit position counter
		__m128i pos = _mm_set_epi8(-127, -126, -125, -124, -123, -122, -121, -120, -127, -126, -125, -124, -123, -122, -121, -120);
		__m128i posinc = _mm_set1_epi8(8);

		size_t byteCnt = std::min<size_t>(maxSubX2 - blockStart, 128) >> 3;
		while(byteCnt--) {
			// compute (pos >= x1 && pos < x2) as (!((pos+1) > x2) && (pos+1) > x1) to bit mask
#if defined(VD_CPU_X64)
			const uint64 mask
				=  (uint64)_mm_movemask_epi8(_mm_andnot_si128(_mm_cmpgt_epi8(pos, row01_x2), _mm_cmpgt_epi8(pos, row01_x1)))
				+ ((uint64)_mm_movemask_epi8(_mm_andnot_si128(_mm_cmpgt_epi8(pos, row23_x2), _mm_cmpgt_epi8(pos, row23_x1))) << 16)
				+ ((uint64)_mm_movemask_epi8(_mm_andnot_si128(_mm_cmpgt_epi8(pos, row45_x2), _mm_cmpgt_epi8(pos, row45_x1))) << 32)
				+ ((uint64)_mm_movemask_epi8(_mm_andnot_si128(_mm_cmpgt_epi8(pos, row67_x2), _mm_cmpgt_epi8(pos, row67_x1))) << 48);

			VDWriteUnalignedU64(dst +  0, VDReadUnalignedU64(dst +  0) | (mask & rdither));
			VDWriteUnalignedU64(dst +  8, VDReadUnalignedU64(dst +  8) | (mask & gdither));
			VDWriteUnalignedU64(dst + 16, VDReadUnalignedU64(dst + 16) | (mask & bdither));
#else
			const uint32 mask1
				=  (uint32)_mm_movemask_epi8(_mm_andnot_si128(_mm_cmpgt_epi8(pos, row01_x2), _mm_cmpgt_epi8(pos, row01_x1)))
				+ ((uint32)_mm_movemask_epi8(_mm_andnot_si128(_mm_cmpgt_epi8(pos, row23_x2), _mm_cmpgt_epi8(pos, row23_x1))) << 16);
			const uint32 mask2
				=  (uint32)_mm_movemask_epi8(_mm_andnot_si128(_mm_cmpgt_epi8(pos, row45_x2), _mm_cmpgt_epi8(pos, row45_x1)))
				+ ((uint32)_mm_movemask_epi8(_mm_andnot_si128(_mm_cmpgt_epi8(pos, row67_x2), _mm_cmpgt_epi8(pos, row67_x1))) << 16);

			VDWriteUnalignedU32(dst +  0, VDReadUnalignedU32(dst +  0) | (mask1 & rdither1));
			VDWriteUnalignedU32(dst +  4, VDReadUnalignedU32(dst +  4) | (mask2 & rdither2));
			VDWriteUnalignedU32(dst +  8, VDReadUnalignedU32(dst +  8) | (mask1 & gdither1));
			VDWriteUnalignedU32(dst + 12, VDReadUnalignedU32(dst + 12) | (mask2 & gdither2));
			VDWriteUnalignedU32(dst + 16, VDReadUnalignedU32(dst + 16) | (mask1 & bdither1));
			VDWriteUnalignedU32(dst + 20, VDReadUnalignedU32(dst + 20) | (mask2 & bdither2));
#endif

			pos = _mm_adds_epi8(pos, posinc);
			dst += 24;
		}
	}
}
#endif

#if defined(VD_CPU_ARM64)
void ATUIPrinterGraphicalOutputWindow::RenderTrapezoidRGB_NEON(const sint32 subSpans[2][8], uint32 penIndex) {
	const uint8x8_t rdither = vld1_u8(mPenDithers[penIndex][0]);
	const uint8x8_t gdither = vld1_u8(mPenDithers[penIndex][1]);
	const uint8x8_t bdither = vld1_u8(mPenDithers[penIndex][2]);

	// load subscan ranges
	const uint32x4_t subX1A = vreinterpretq_u32_s32(vld1q_s32(&subSpans[0][0]));
	const uint32x4_t subX1B = vreinterpretq_u32_s32(vld1q_s32(&subSpans[0][4]));
	const uint32x4_t subX2A = vreinterpretq_u32_s32(vld1q_s32(&subSpans[1][0]));
	const uint32x4_t subX2B = vreinterpretq_u32_s32(vld1q_s32(&subSpans[1][4]));

	// compute mask of valid ranges
	const uint32x4_t validMaskA = vcltq_u32(subX1A, subX2A);
	const uint32x4_t validMaskB = vcltq_u32(subX1B, subX2B);

	// compute min/max of non-empty scans
	uint32 minSubX1 = vminvq_u32(vminq_u32(vornq_u32(subX1A, validMaskA), vornq_u32(subX1B, validMaskB)));
	uint32 maxSubX2 = vmaxvq_u32(vmaxq_u32(vandq_u32(subX2A, validMaskA), vandq_u32(subX2B, validMaskB)));

	if (minSubX1 >= maxSubX2)
		return;

	// expand to byte boundaries
	minSubX1 &= ~(uint32)7;
	maxSubX2 = (maxSubX2 + 7) & ~(uint32)7;

	// load bit packing mask
	alignas(8) static constexpr uint8 kBitMask[] { 1, 2, 4, 8, 16, 32, 64, 128 };
	const uint8x8_t bitMask = vld1_u8(kBitMask);

	// rasterize blocks
	uint8 *VDRESTRICT dst = mABuffer.data() + (ptrdiff_t)minSubX1*3;

	for(uint32 blockStart = minSubX1; blockStart < maxSubX2; blockStart += 128) {
		// compute block-relative bounds with -128 bias
		uint32x4_t blockPos = vmovq_n_u32(blockStart);
		uint32x4_t blockSubX1A = vqsubq_u32(subX1A, blockPos);
		uint32x4_t blockSubX1B = vqsubq_u32(subX1B, blockPos);
		uint32x4_t blockSubX2A = vqsubq_u32(subX2A, blockPos);
		uint32x4_t blockSubX2B = vqsubq_u32(subX2B, blockPos);

		// compress to unsigned bytes with saturation
		uint16x4_t zu16 = vmov_n_u16(0);
		uint8x8_t blockSubX1 = vqmovn_u16(vcombine_u16(vqmovn_u32(blockSubX1A), vqmovn_u32(blockSubX1B)));
		uint8x8_t blockSubX2 = vqmovn_u16(vcombine_u16(vqmovn_u32(blockSubX2A), vqmovn_u32(blockSubX2B)));

		// broadcast 8x
		uint8x8_t row0_x1 = vdup_lane_u8(blockSubX1, 0);
		uint8x8_t row1_x1 = vdup_lane_u8(blockSubX1, 1);
		uint8x8_t row2_x1 = vdup_lane_u8(blockSubX1, 2);
		uint8x8_t row3_x1 = vdup_lane_u8(blockSubX1, 3);
		uint8x8_t row4_x1 = vdup_lane_u8(blockSubX1, 4);
		uint8x8_t row5_x1 = vdup_lane_u8(blockSubX1, 5);
		uint8x8_t row6_x1 = vdup_lane_u8(blockSubX1, 6);
		uint8x8_t row7_x1 = vdup_lane_u8(blockSubX1, 7);
		uint8x8_t row0_x2 = vdup_lane_u8(blockSubX2, 0);
		uint8x8_t row1_x2 = vdup_lane_u8(blockSubX2, 1);
		uint8x8_t row2_x2 = vdup_lane_u8(blockSubX2, 2);
		uint8x8_t row3_x2 = vdup_lane_u8(blockSubX2, 3);
		uint8x8_t row4_x2 = vdup_lane_u8(blockSubX2, 4);
		uint8x8_t row5_x2 = vdup_lane_u8(blockSubX2, 5);
		uint8x8_t row6_x2 = vdup_lane_u8(blockSubX2, 6);
		uint8x8_t row7_x2 = vdup_lane_u8(blockSubX2, 7);

		// set up bit position counter
		static constexpr uint8_t kInitialBitPos[] { 0, 1, 2, 3, 4, 5, 6, 7 };
		uint8x8_t pos = vld1_u8(kInitialBitPos);
		uint8x8_t posinc = vmov_n_u8(8);

		size_t byteCnt = std::min<size_t>(maxSubX2 - blockStart, 128) >> 3;
		while(byteCnt--) {
			// compute (pos >= x1 && pos < x2)
			uint8x8_t mask;

			mask = vmov_n_u8(vaddv_u8(vand_u8(vand_u8(vcge_u8(pos, row0_x1), vclt_u8(pos, row0_x2)), bitMask)));
			mask = vset_lane_u8(vaddv_u8(vand_u8(vand_u8(vcge_u8(pos, row1_x1), vclt_u8(pos, row1_x2)), bitMask)), mask, 1);
			mask = vset_lane_u8(vaddv_u8(vand_u8(vand_u8(vcge_u8(pos, row2_x1), vclt_u8(pos, row2_x2)), bitMask)), mask, 2);
			mask = vset_lane_u8(vaddv_u8(vand_u8(vand_u8(vcge_u8(pos, row3_x1), vclt_u8(pos, row3_x2)), bitMask)), mask, 3);
			mask = vset_lane_u8(vaddv_u8(vand_u8(vand_u8(vcge_u8(pos, row4_x1), vclt_u8(pos, row4_x2)), bitMask)), mask, 4);
			mask = vset_lane_u8(vaddv_u8(vand_u8(vand_u8(vcge_u8(pos, row5_x1), vclt_u8(pos, row5_x2)), bitMask)), mask, 5);
			mask = vset_lane_u8(vaddv_u8(vand_u8(vand_u8(vcge_u8(pos, row6_x1), vclt_u8(pos, row6_x2)), bitMask)), mask, 6);
			mask = vset_lane_u8(vaddv_u8(vand_u8(vand_u8(vcge_u8(pos, row7_x1), vclt_u8(pos, row7_x2)), bitMask)), mask, 7);

			vst1_u8(dst +  0, vorr_u8(vld1_u8(dst +  0), vand_u8(mask, rdither)));
			vst1_u8(dst +  8, vorr_u8(vld1_u8(dst +  8), vand_u8(mask, gdither)));
			vst1_u8(dst + 16, vorr_u8(vld1_u8(dst + 16), vand_u8(mask, bdither)));

			pos = vqadd_u8(pos, posinc);
			dst += 24;
		}
	}
}
#endif

void ATUIPrinterGraphicalOutputWindow::Downsample8x8(uint32 *dst, const uint8 *src, size_t w, bool rgb) {
	if (rgb) {
#ifdef VD_CPU_X64
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_POPCNT))
			return DownsampleRGB8x8_POPCNT64(dst, src, w);
#endif

#ifdef VD_CPU_X86
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_POPCNT))
			return DownsampleRGB8x8_POPCNT32(dst, src, w);
#endif

#ifdef VD_CPU_ARM64
		return DownsampleRGB8x8_NEON(dst, src, w);
#else
		DownsampleRGB8x8_Scalar(dst, src, w);
#endif
	} else {
#ifdef VD_CPU_X64
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_POPCNT))
			return Downsample8x8_POPCNT64(dst, src, w);
#endif

#ifdef VD_CPU_X86
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_POPCNT))
			return Downsample8x8_POPCNT32(dst, src, w);
#endif

#ifdef VD_CPU_ARM64
		return Downsample8x8_NEON(dst, src, w);
#else
		Downsample8x8_Scalar(dst, src, w);
#endif
	}
}

void ATUIPrinterGraphicalOutputWindow::Downsample8x8_Scalar(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const uint32 level
			= mPopCnt8[asrc[0]]
			+ mPopCnt8[asrc[1]]
			+ mPopCnt8[asrc[2]]
			+ mPopCnt8[asrc[3]]
			+ mPopCnt8[asrc[4]]
			+ mPopCnt8[asrc[5]]
			+ mPopCnt8[asrc[6]]
			+ mPopCnt8[asrc[7]];
		asrc += 8;

		// convert coverage to sRGB color
		*fbdst++ = mGammaTable[level];
	}
}

#ifdef VD_CPU_X64
VD_CPU_TARGET("popcnt")
void ATUIPrinterGraphicalOutputWindow::Downsample8x8_POPCNT64(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const size_t level = _mm_popcnt_u64(VDReadUnalignedU64(asrc));
		asrc += 8;

		// convert coverage to sRGB color
		*fbdst++ = mGammaTable[level];
	}
}
#endif

#ifdef VD_CPU_X86
void ATUIPrinterGraphicalOutputWindow::Downsample8x8_POPCNT32(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const size_t level = _mm_popcnt_u32(VDReadUnalignedU32(asrc))
			+ _mm_popcnt_u32(VDReadUnalignedU32(asrc + 4));
		asrc += 8;

		// convert coverage to sRGB color
		*fbdst++ = mGammaTable[level];
	}
}
#endif

#ifdef VD_CPU_ARM64
void ATUIPrinterGraphicalOutputWindow::Downsample8x8_NEON(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const uint32 level = vaddv_u8(vcnt_u8(vld1_u8(asrc)));
		asrc += 8;

		// convert coverage to sRGB color
		*fbdst++ = mGammaTable[level];
	}
}
#endif

void ATUIPrinterGraphicalOutputWindow::DownsampleRGB8x8_Scalar(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const uint32 rlevel
			= mPopCnt8[asrc[0]]
			+ mPopCnt8[asrc[1]]
			+ mPopCnt8[asrc[2]]
			+ mPopCnt8[asrc[3]]
			+ mPopCnt8[asrc[4]]
			+ mPopCnt8[asrc[5]]
			+ mPopCnt8[asrc[6]]
			+ mPopCnt8[asrc[7]];

		const uint32 glevel
			= mPopCnt8[asrc[8]]
			+ mPopCnt8[asrc[9]]
			+ mPopCnt8[asrc[10]]
			+ mPopCnt8[asrc[11]]
			+ mPopCnt8[asrc[12]]
			+ mPopCnt8[asrc[13]]
			+ mPopCnt8[asrc[14]]
			+ mPopCnt8[asrc[15]];

		const uint32 blevel
			= mPopCnt8[asrc[16]]
			+ mPopCnt8[asrc[17]]
			+ mPopCnt8[asrc[18]]
			+ mPopCnt8[asrc[19]]
			+ mPopCnt8[asrc[20]]
			+ mPopCnt8[asrc[21]]
			+ mPopCnt8[asrc[22]]
			+ mPopCnt8[asrc[23]];

		asrc += 24;

		// convert coverage to sRGB color
		*fbdst++
			= (mGammaTable[rlevel] & 0xFF0000)
			+ (mGammaTable[glevel] & 0x00FF00)
			+ (mGammaTable[blevel] & 0x0000FF)
			;
	}
}

#ifdef VD_CPU_X64
VD_CPU_TARGET("popcnt")
void ATUIPrinterGraphicalOutputWindow::DownsampleRGB8x8_POPCNT64(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const size_t rlevel = _mm_popcnt_u64(VDReadUnalignedU64(asrc));
		const size_t glevel = _mm_popcnt_u64(VDReadUnalignedU64(asrc+8));
		const size_t blevel = _mm_popcnt_u64(VDReadUnalignedU64(asrc+16));
		asrc += 24;

		// convert coverage to sRGB color
		*fbdst++
			= (mGammaTable[rlevel] & 0xFF0000)
			+ (mGammaTable[glevel] & 0x00FF00)
			+ (mGammaTable[blevel] & 0x0000FF)
			;
	}
}
#endif

#ifdef VD_CPU_X86
void ATUIPrinterGraphicalOutputWindow::DownsampleRGB8x8_POPCNT32(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const size_t rlevel = _mm_popcnt_u32(VDReadUnalignedU32(asrc))
							+ _mm_popcnt_u32(VDReadUnalignedU32(asrc+4));
		const size_t glevel = _mm_popcnt_u32(VDReadUnalignedU32(asrc+8))
							+ _mm_popcnt_u32(VDReadUnalignedU32(asrc+12));
		const size_t blevel = _mm_popcnt_u32(VDReadUnalignedU32(asrc+16))
							+ _mm_popcnt_u32(VDReadUnalignedU32(asrc+20));
		asrc += 24;

		// convert coverage to sRGB color
		*fbdst++
			= (mGammaTable[rlevel] & 0xFF0000)
			+ (mGammaTable[glevel] & 0x00FF00)
			+ (mGammaTable[blevel] & 0x0000FF)
			;
	}
}
#endif

#ifdef VD_CPU_ARM64
void ATUIPrinterGraphicalOutputWindow::DownsampleRGB8x8_NEON(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const uint32 rlevel = vaddv_u8(vcnt_u8(vld1_u8(asrc)));
		const uint32 glevel = vaddv_u8(vcnt_u8(vld1_u8(asrc+8)));
		const uint32 blevel = vaddv_u8(vcnt_u8(vld1_u8(asrc+16)));
		asrc += 24;

		// convert coverage to sRGB color
		*fbdst++
			= (mGammaTable[rlevel] & 0xFF0000)
			+ (mGammaTable[glevel] & 0x00FF00)
			+ (mGammaTable[blevel] & 0x0000FF)
			;
	}
}
#endif

///////////////////////////////////////////////////////////////////////////////

ATPrinterOutputWindow::ATPrinterOutputWindow()
	: ATUIPaneWindow(kATUIPaneId_PrinterOutput, L"Printer Output")
	, mhwndTextEditor(NULL)
	, mLineBufIdx(0)
{
	mPreferredDockCode = kATContainerDockBottom;

	mAddedOutputFn = [this](ATPrinterOutput& output) { OnAddedOutput(output); };
	mRemovingOutputFn = [this](ATPrinterOutput& output) { OnRemovingOutput(output); };
	mAddedGraphicalOutputFn = [this](ATPrinterGraphicalOutput& output) { OnAddedGraphicalOutput(output); };
	mRemovingGraphicalOutputFn = [this](ATPrinterGraphicalOutput& output) { OnRemovingGraphicalOutput(output); };
}

ATPrinterOutputWindow::~ATPrinterOutputWindow() {
}

LRESULT ATPrinterOutputWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_SIZE:
			OnSize();
			break;

		case WM_CONTEXTMENU:
			try {
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);

				HMENU menu0 = LoadMenu(NULL, mpGraphicWindow ? MAKEINTRESOURCE(IDR_PRINTER_GRAPHIC_CONTEXT_MENU) : MAKEINTRESOURCE(IDR_PRINTER_CONTEXT_MENU));
				if (menu0) {
					HMENU menu = GetSubMenu(menu0, 0);
					BOOL cmd = 0;

					if (x == -1 && y == -1) {
						const vdpoint32& pt = mpGraphicWindow
							? mpGraphicWindow->TransformClientToScreen(vdpoint32(0, 0))
							: mpTextEditor->GetScreenPosForContextMenu();
						
						x = pt.x;
						y = pt.y;
					} else {
						POINT pt = {x, y};

						if (!mpGraphicWindow && ScreenToClient(mhwndTextEditor, &pt))
							mpTextEditor->SetCursorPixelPos(pt.x, pt.y);
					}

					cmd = TrackPopupMenu(menu, TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD, x, y, 0, mhwnd, NULL);

					DestroyMenu(menu0);

					switch(cmd) {
						case ID_CONTEXT_CLEAR:
							if (mpGraphicWindow)
								mpGraphicWindow->Clear();
							else if (mpTextEditor) {
								mpTextEditor->Clear();
								mLastTextOffset = 0;

								if (mpTextOutput) {
									mpTextOutput->Clear();
									mpTextOutput->Revalidate();
								}
							}
							break;

						case ID_CONTEXT_RESETVIEW:
							if (mpGraphicWindow)
								mpGraphicWindow->ResetView();
							break;

						case ID_SAVEAS_PNGIMAGE96:
							if (mpGraphicWindow)
								mpGraphicWindow->SaveAsPNG(96.0f);
							break;

						case ID_SAVEAS_PNGIMAGE300:
							if (mpGraphicWindow)
								mpGraphicWindow->SaveAsPNG(300.0f);
							break;

						case ID_SAVEAS_PDF:
							if (mpGraphicWindow)
								mpGraphicWindow->SaveAsPDF();
							break;
					}
				}
			} catch(const VDException& ex) {
				VDDialogFrameW32::ShowError((VDGUIHandle)mhwnd, ex.wc_str(), nullptr);
			}
			break;
	}

	return ATUIPaneWindow::WndProc(msg, wParam, lParam);
}

bool ATPrinterOutputWindow::OnCreate() {
	if (!ATUIPaneWindow::OnCreate())
		return false;

	if (!VDCreateTextEditor(~mpTextEditor))
		return false;

	mhwndTextEditor = (HWND)mpTextEditor->Create(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd, 100);

	OnFontsUpdated();

	mpTextEditor->SetReadOnly(true);

	OnSize();

	mpOutputMgr = static_cast<ATPrinterOutputManager *>(&g_sim.GetPrinterOutputManager());
	mpOutputMgr->OnAddedOutput.Add(&mAddedOutputFn);
	mpOutputMgr->OnRemovingOutput.Add(&mRemovingOutputFn);
	mpOutputMgr->OnAddedGraphicalOutput.Add(&mAddedGraphicalOutputFn);
	mpOutputMgr->OnRemovingGraphicalOutput.Add(&mRemovingGraphicalOutputFn);

	AttachToAnyOutput();

	return true;
}

void ATPrinterOutputWindow::OnDestroy() {
	DetachFromTextOutput();
	DetachFromGraphicsOutput();

	if (mpOutputMgr) {
		mpOutputMgr->OnAddedOutput.Remove(&mAddedOutputFn);
		mpOutputMgr->OnRemovingOutput.Remove(&mRemovingOutputFn);
		mpOutputMgr->OnAddedGraphicalOutput.Remove(&mAddedGraphicalOutputFn);
		mpOutputMgr->OnRemovingGraphicalOutput.Remove(&mRemovingGraphicalOutputFn);
		mpOutputMgr = nullptr;
	}

	ATUIPaneWindow::OnDestroy();
}

void ATPrinterOutputWindow::OnSize() {
	if (mpGraphicWindow) {
		mpGraphicWindow->SetArea(GetClientArea());
	} else if (mhwndTextEditor) {
		ATUINativeWindowProxy proxy(mhwndTextEditor);

		proxy.SetArea(GetClientArea());
	}
}

void ATPrinterOutputWindow::OnFontsUpdated() {
	if (mhwndTextEditor)
		SendMessage(mhwndTextEditor, WM_SETFONT, (WPARAM)ATGetConsoleFontW32(), TRUE);
}

void ATPrinterOutputWindow::OnSetFocus() {
	if (mpGraphicWindow)
		mpGraphicWindow->Focus();
	else
		::SetFocus(mhwndTextEditor);
}

void ATPrinterOutputWindow::OnAddedOutput(ATPrinterOutput& output) {
	if (!mpTextOutput && !mpGraphicsOutput)
		AttachToAnyOutput();
}

void ATPrinterOutputWindow::OnRemovingOutput(ATPrinterOutput& output) {
	if (&output == mpTextOutput) {
		DetachFromTextOutput();

		AttachToAnyOutput();
	}
}

void ATPrinterOutputWindow::OnAddedGraphicalOutput(ATPrinterGraphicalOutput& output) {
	if (!mpGraphicsOutput)
		AttachToAnyOutput();
}

void ATPrinterOutputWindow::OnRemovingGraphicalOutput(ATPrinterGraphicalOutput& output) {
	if (&output == mpGraphicsOutput) {
		DetachFromGraphicsOutput();

		AttachToAnyOutput();
	}
}

void ATPrinterOutputWindow::AttachToAnyOutput() {
	if (mpOutputMgr) {
		if (mpOutputMgr->GetGraphicalOutputCount() > 0)
			AttachToGraphicsOutput(mpOutputMgr->GetGraphicalOutput(0));
		else if (mpOutputMgr->GetOutputCount() > 0)
			AttachToTextOutput(mpOutputMgr->GetOutput(0));
	}
}

void ATPrinterOutputWindow::AttachToTextOutput(ATPrinterOutput& output) {
	DetachFromGraphicsOutput();

	mLastTextOffset = 0;
	if (mpTextEditor)
		mpTextEditor->Clear();

	mpTextOutput = &output;
	mpTextOutput->SetOnInvalidation(
		[this] {
			UpdateTextOutput();
		}
	);

	UpdateTextOutput();
}

void ATPrinterOutputWindow::DetachFromTextOutput() {
	if (mpTextOutput) {
		mpTextOutput->SetOnInvalidation(nullptr);
		mpTextOutput = nullptr;

		if (mpTextEditor)
			mpTextEditor->Clear();
	}
}

void ATPrinterOutputWindow::UpdateTextOutput() {
	if (mpTextOutput) {
		const size_t offset = mpTextOutput->GetLength();

		if (offset > mLastTextOffset) {
			if (mpTextEditor)
				mpTextEditor->Append(mpTextOutput->GetTextPointer(mLastTextOffset));

			mLastTextOffset = offset;
		}

		mpTextOutput->Revalidate();
	}
}

void ATPrinterOutputWindow::AttachToGraphicsOutput(ATPrinterGraphicalOutput& output) {
	DetachFromTextOutput();
	DetachFromGraphicsOutput();

	mpGraphicsOutput = &output;

	mpTextEditor->Clear();

	ATUINativeWindowProxy proxy(mhwndTextEditor);
	proxy.Hide();

	mpGraphicWindow = new ATUIPrinterGraphicalOutputWindow;
	mpGraphicWindow->CreateChild(mhdlg, 101, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE | WS_TABSTOP);

	OnSize();

	mpGraphicWindow->AttachToOutput(output);
}

void ATPrinterOutputWindow::DetachFromGraphicsOutput() {
	if (mpGraphicsOutput) {
		mpGraphicsOutput->SetOnInvalidation(nullptr);
		mpGraphicsOutput = nullptr;
	}

	if (mpGraphicWindow) {
		mpGraphicWindow->Destroy();
		mpGraphicWindow = nullptr;

		// the text window was not resized while the graphic window existed, so fix that now
		OnSize();

		mpTextEditor->Clear();

		ATUINativeWindowProxy proxy(mhwndTextEditor);
		proxy.Show();
	}
}

////////////////////////////////////////////////////////////////////////////////

void ATUIDebuggerRegisterPrinterOutputPane() {
	ATRegisterUIPaneType(kATUIPaneId_PrinterOutput, VDRefCountObjectFactory<ATPrinterOutputWindow, ATUIPane>);
}
