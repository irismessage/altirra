//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2010 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "stdafx.h"
#include <vd2/system/math.h>
#include <vd2/system/memory.h>
#include <vd2/system/VDString.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/text.h>
#include <vd2/VDDisplay/compositor.h>
#include <vd2/VDDisplay/renderer.h>
#include <vd2/VDDisplay/textrenderer.h>
#include "uirender.h"
#include "audiomonitor.h"
#include "slightsid.h"
#include "uiwidget.h"
#include "uilabel.h"
#include "uicontainer.h"
#include "uimanager.h"

namespace {
	void Shade(IVDDisplayRenderer& rdr, int x1, int y1, int dx, int dy) {
		if (rdr.GetCaps().mbSupportsAlphaBlending) {
			rdr.AlphaFillRect(x1, y1, dx, dy, 0x80000000);
		} else {
			rdr.SetColorRGB(0);
			rdr.FillRect(x1, y1, dx, dy);
		}
	}
}

///////////////////////////////////////////////////////////////////////////
class ATUIAudioStatusDisplay : public ATUIWidget {
public:
	void SetFont(IVDDisplayFont *font);
	void Update(const ATUIAudioStatus& status);
	void AutoSize();

protected:
	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h);

	void FormatLine(int idx, const ATUIAudioStatus& status);

	vdrefptr<IVDDisplayFont> mpFont;
	VDStringW mText;
	ATUIAudioStatus mAudioStatus;
};

void ATUIAudioStatusDisplay::SetFont(IVDDisplayFont *font) {
	mpFont = font;
}

void ATUIAudioStatusDisplay::Update(const ATUIAudioStatus& status) {
	mAudioStatus = status;

	Invalidate();
}

void ATUIAudioStatusDisplay::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	VDDisplayFontMetrics metrics;
	mpFont->GetMetrics(metrics);

	const int fonth = metrics.mAscent + metrics.mDescent;
	int y2 = 2;

	VDDisplayTextRenderer& tr = *rdr.GetTextRenderer();
	tr.SetFont(mpFont);
	tr.SetAlignment(VDDisplayTextRenderer::kAlignLeft, VDDisplayTextRenderer::kVertAlignTop);
	tr.SetColorRGB(0xffffff);

	for(int i=0; i<7; ++i) {
		FormatLine(i, mAudioStatus);
		tr.DrawTextLine(2, y2, mText.c_str());
		y2 += fonth;
	}
}

void ATUIAudioStatusDisplay::AutoSize() {
	if (!mpFont)
		return;

	// create a test structure with some big values in it
	ATUIAudioStatus testStatus = {};

	testStatus.mUnderflowCount = 9999;
	testStatus.mOverflowCount = 9999;
	testStatus.mDropCount = 9999;
	testStatus.mMeasuredMin = 999999;
	testStatus.mMeasuredMax = 999999;
	testStatus.mTargetMin = 999999;
	testStatus.mTargetMax = 999999;
	testStatus.mIncomingRate = 99999;
	testStatus.mExpectedRate = 99999;

	// loop over all the strings and compute max size
	sint32 w = 0;
	for(int i=0; i<7; ++i) {
		FormatLine(i, testStatus);

		w = std::max<sint32>(w, mpFont->MeasureString(mText.c_str(), mText.size(), false).w);
	}

	// resize me
	VDDisplayFontMetrics metrics;
	mpFont->GetMetrics(metrics);

	const int fonth = metrics.mAscent + metrics.mDescent;

	SetSize(vdsize32(4 + w, 4 + fonth * 7));
}

void ATUIAudioStatusDisplay::FormatLine(int idx, const ATUIAudioStatus& status) {
	switch(idx) {
	case 0:
		mText.sprintf(L"Underflow count: %d", status.mUnderflowCount);
		break;

	case 1:
		mText.sprintf(L"Overflow count: %d", status.mOverflowCount);
		break;

	case 2:
		mText.sprintf(L"Drop count: %d", status.mDropCount);
		break;

	case 3:
		mText.sprintf(L"Measured range: %5d-%5d", status.mMeasuredMin, status.mMeasuredMax);
		break;

	case 4:
		mText.sprintf(L"Target range: %5d-%5d", status.mTargetMin, status.mTargetMax);
		break;

	case 5:
		mText.sprintf(L"Incoming data rate: %.2f samples/sec", status.mIncomingRate);
		break;

	case 6:
		mText.sprintf(L"Expected data rate: %.2f samples/sec", status.mExpectedRate);
		break;
	}
}

///////////////////////////////////////////////////////////////////////////
class ATUIAudioDisplay : public ATUIWidget {
public:
	ATUIAudioDisplay();

	void SetAudioMonitor(ATAudioMonitor *mon);
	void SetSlightSID(ATSlightSIDEmulator *ss);

	void SetBigFont(IVDDisplayFont *font);
	void SetSmallFont(IVDDisplayFont *font);

	void AutoSize();
	void Update() { Invalidate(); }

protected:
	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h);
	void PaintSID(IVDDisplayRenderer& rdr, VDDisplayTextRenderer& tr, sint32 w, sint32 h);
	void PaintPOKEY(IVDDisplayRenderer& rdr, VDDisplayTextRenderer& tr, sint32 w, sint32 h);

	vdrefptr<IVDDisplayFont> mpBigFont;
	vdrefptr<IVDDisplayFont> mpSmallFont;

	int mBigFontW;
	int mBigFontH;
	int mSmallFontW;
	int mSmallFontH;

	ATAudioMonitor *mpAudioMonitor;
	ATSlightSIDEmulator *mpSlightSID;
};

ATUIAudioDisplay::ATUIAudioDisplay()
	: mBigFontW(0)
	, mBigFontH(0)
	, mSmallFontW(0)
	, mSmallFontH(0)
	, mpAudioMonitor(NULL)
	, mpSlightSID(NULL)
{
}

void ATUIAudioDisplay::SetAudioMonitor(ATAudioMonitor *mon) {
	mpAudioMonitor = mon;
}

void ATUIAudioDisplay::SetSlightSID(ATSlightSIDEmulator *ss) {
	mpSlightSID = ss;
}

void ATUIAudioDisplay::SetBigFont(IVDDisplayFont *font) {
	if (mpBigFont == font)
		return;

	mpBigFont = font;

	const vdsize32& size = font->MeasureString(L"0123456789", 10, false);

	mBigFontW = size.w / 10;
	mBigFontH = size.h;
}

void ATUIAudioDisplay::SetSmallFont(IVDDisplayFont *font) {
	if (mpSmallFont == font)
		return;

	mpSmallFont = font;

	const vdsize32& size = font->MeasureString(L"0123456789", 10, false);

	mSmallFontW = size.w / 10;
	mSmallFontH = size.h;
}

void ATUIAudioDisplay::AutoSize() {
	const int chanht = 5 + mBigFontH + mSmallFontH;

	SetArea(vdrect32(mArea.left, mArea.top, mArea.left + 320, mArea.top + chanht * 4));
}

void ATUIAudioDisplay::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	VDDisplayTextRenderer& tr = *rdr.GetTextRenderer();

	if (mpSlightSID)
		PaintSID(rdr, tr, w, h);
	else
		PaintPOKEY(rdr, tr, w, h);
}

void ATUIAudioDisplay::PaintSID(IVDDisplayRenderer& rdr, VDDisplayTextRenderer& tr, sint32 w, sint32 h) {
	const int fontw = mBigFontW;
	const int fonth = mBigFontH;
	const int fontsmw = mSmallFontW;
	const int fontsmh = mSmallFontH;

	const int chanht = 5 + fonth + fontsmh;

	const int x = 0;
	const int y = 0;

	const int x_freq = x + fontw * 8;
	const int x_note1 = x;
	const int x_note2 = x + fontsmw * 5;
	const int x_modes = x_freq + 4;
	const int x_duty = x_freq + 4;
	const int x_volbar = x_modes + 9*fontsmw;
	const int x_adsr = x_volbar + 5;

	wchar_t buf[128];

	const uint8 *regbase = mpSlightSID->GetRegisters();
	for(int ch=0; ch<3; ++ch) {
		const uint8 *chreg = regbase + 7*ch;
		const int chy = y + chanht*ch;
		const int chy_freq = chy;
		const int chy_modes = chy;
		const int chy_duty = chy + fontsmh;
		const int chy_note = chy + fonth;
		const int chy_adsr = chy + 1;
		const int chy_envelope = chy_adsr + fontsmh;
		const uint32 color = (ch != 2 || !(regbase[0x18] & 0x80)) ? 0xFFFFFF : 0x006e6e6e;

		const uint32 freq = chreg[0] + chreg[1]*256;
		//const float hz = (float)freq * (17897725.0f / 18.0f / 16777216.0f);
		const float hz = (float)freq * (985248.0f / 16777216.0f);
		swprintf(buf, 128, L"%.1f", hz);

		tr.SetFont(mpBigFont);
		tr.SetColorRGB(color);
		tr.SetAlignment(VDDisplayTextRenderer::kAlignRight, VDDisplayTextRenderer::kVertAlignTop);
		tr.DrawTextLine(x_freq, chy_freq, buf);

		buf[0] = chreg[4] & 0x80 ? 'N' : ' ';
		buf[1] = chreg[4] & 0x40 ? 'P' : ' ';
		buf[2] = chreg[4] & 0x20 ? 'S' : ' ';
		buf[3] = chreg[4] & 0x10 ? 'T' : ' ';
		buf[4] = chreg[4] & 0x08 ? 'E' : ' ';
		buf[5] = chreg[4] & 0x04 ? 'R' : ' ';
		buf[6] = chreg[4] & 0x02 ? 'S' : ' ';
		buf[7] = chreg[4] & 0x01 ? 'G' : ' ';
		buf[8] = 0;
		tr.SetAlignment(VDDisplayTextRenderer::kAlignLeft, VDDisplayTextRenderer::kVertAlignTop);
		tr.DrawTextLine(x_modes, chy_modes, buf);

		swprintf(buf, 128, L"%3.0f%%", (chreg[2] + (chreg[3] & 15)*256) * 100.0f / 4096.0f);
		tr.DrawTextLine(x_duty, chy_duty, buf);

		float midiNote = 69.0f + logf(hz + 0.0001f) * 17.312340490667560888319096172023f - 105.37631656229591524883618971458f;

		if (midiNote < 0)
			midiNote = 0;
		else if (midiNote > 140)
			midiNote = 140;

		int midiNoteInt = (int)(0.5f + midiNote);
		swprintf(buf, 128, L"%04X", freq);
		tr.DrawTextLine(x_note1, chy_note, buf);

		swprintf(buf, 128, L"%3u%+1.0f", midiNoteInt, (midiNote - midiNoteInt) * 10.0f);
		tr.DrawTextLine(x_note2, chy_note, buf);

		const int maxbarht = chanht - 2;

		int env = mpSlightSID->GetEnvelopeValue(ch);
		int ht = (env >> 4) * maxbarht / 15;

		int sustain = (chreg[6] >> 4);
		int sustainht = (sustain * maxbarht)/15;
		rdr.SetColorRGB(0x003b3b3b);
		rdr.FillRect(x_volbar, chy+chanht-1-sustainht, 2, sustainht);

		rdr.SetColorRGB(color);
		rdr.FillRect(x_volbar, chy+chanht-1-ht, 2, ht);

		int envmode = mpSlightSID->GetEnvelopeMode(ch);
		bool sustainMode = env <= sustain*17;

		int x2 = x_adsr;
		tr.SetColorRGB(envmode == 0 ? 0xFFFFFF : 0x007a4500);
		tr.DrawTextLine(x2, chy_adsr, L"A");
		x2 += mpSmallFont->MeasureString(L"A", 1, false).w;

		tr.SetColorRGB(envmode == 1 && !sustainMode ? 0xFFFFFF : 0x007a4500);
		tr.DrawTextLine(x2, chy_adsr, L"D");
		x2 += mpSmallFont->MeasureString(L"D", 1, false).w;

		tr.SetColorRGB(envmode == 1 && sustainMode ? 0xFFFFFF : 0x007a4500);
		tr.DrawTextLine(x2, chy_adsr, L"S");
		x2 += mpSmallFont->MeasureString(L"S", 1, false).w;

		tr.SetColorRGB(envmode == 2 ? 0xFFFFFF : 0x007a4500);
		tr.DrawTextLine(x2, chy_adsr, L"R");

		swprintf(buf, 128, L"%02X%02X", chreg[5], chreg[6]);
		tr.SetColorRGB(color);
		tr.DrawTextLine(x_adsr, chy_envelope, buf);
	}

	swprintf(buf, 128, L"%ls %ls %ls @ $%04X [%X] -> CH%lc%lc%lc"
		, regbase[24] & 0x10 ? L"LP" : L"  "
		, regbase[24] & 0x20 ? L"BP" : L"  "
		, regbase[24] & 0x40 ? L"HP" : L"  "
		, (regbase[21] & 7) + 8*regbase[22]
		, regbase[23] >> 4
		, regbase[23] & 0x01 ? L'1' : L' '
		, regbase[23] & 0x02 ? L'2' : L' '
		, regbase[23] & 0x04 ? L'3' : L' '
	);

	tr.SetColorRGB(0xFFFFFF);
	tr.DrawTextLine(x, y + chanht*3 + 6, buf);
}

void ATUIAudioDisplay::PaintPOKEY(IVDDisplayRenderer& rdr, VDDisplayTextRenderer& tr, sint32 w, sint32 h) {
	const int fontw = mBigFontW;
	const int fonth = mBigFontH;
	const int fontsmw = mSmallFontW;
	const int fontsmh = mSmallFontH;

	ATPokeyAudioLog *log;
	ATPokeyRegisterState *rstate;

	mpAudioMonitor->Update(&log, &rstate);

	uint8 audctl = rstate->mReg[8];

	int slowRate = audctl & 0x01 ? 114 : 28;
	int divisors[4];

	divisors[0] = (audctl & 0x40) ? (int)rstate->mReg[0] + 4 : ((int)rstate->mReg[0] + 1) * slowRate;

	divisors[1] = (audctl & 0x10)
		? (audctl & 0x40) ? rstate->mReg[0] + ((int)rstate->mReg[2] << 8) + 7 : (rstate->mReg[0] + ((int)rstate->mReg[2] << 8) + 1) * slowRate
		: ((int)rstate->mReg[2] + 1) * slowRate;

	divisors[2] = (audctl & 0x20) ? (int)rstate->mReg[4] + 4 : ((int)rstate->mReg[4] + 1) * slowRate;

	divisors[3] = (audctl & 0x08)
		? (audctl & 0x20) ? rstate->mReg[4] + ((int)rstate->mReg[6] << 8) + 7 : (rstate->mReg[4] + ((int)rstate->mReg[6] << 8) + 1) * slowRate
		: ((int)rstate->mReg[6] + 1) * slowRate;

	// layout
	const int chanht = 5 + fonth + fontsmh;

	const int x = 0;
	const int y = 0;

	const int x_link = x;
	const int x_clock = x + 2*fontsmw;
	const int x_highpass = x + 5*fontsmw + (fontsmw >> 1);
	const int x_mode = x + 7*fontsmw;
	const int x_noise = x + 9*fontsmw;
	const int x_waveform = x + std::max<int>(11*fontsmw, 8*fontw) + 4;

	const int chanw = (x_waveform - x) * 5;

	sint32 hstep = log->mRecordedCount ? ((chanw - x_waveform - 4) << 16) / log->mRecordedCount : 0;

	Shade(rdr, x, y, chanw, chanht * 4);

	wchar_t buf[128];
	for(int ch=0; ch<4; ++ch) {
		const int chy = y + chanht*ch;
		const int chanfreqy = chy + 4;
		const int chandetaily = chy + 4 + fonth + 1;

		// draw frequency
		swprintf(buf, 128, L"%.1f", 7159090.0f / 8.0f / divisors[ch]);

		tr.SetColorRGB(0xFFFFFF);
		tr.SetFont(mpBigFont);
		tr.SetAlignment(VDDisplayTextRenderer::kAlignRight, VDDisplayTextRenderer::kVertAlignTop);
		tr.DrawTextLine(x_waveform - 4, chanfreqy, buf);

		tr.SetAlignment(VDDisplayTextRenderer::kAlignLeft, VDDisplayTextRenderer::kVertAlignTop);
		tr.SetFont(mpSmallFont);

		// draw link/clock indicator
		if ((ch == 1 && (audctl & 0x10)) || (ch == 3 && (audctl & 0x08)))
			tr.DrawTextLine(x_link, chandetaily, L"16");
		else if ((ch == 0 && (audctl & 0x40)) || (ch == 2 && (audctl & 0x20)))
			tr.DrawTextLine(x_clock, chandetaily, L"1.79");
		else
			tr.DrawTextLine(x_clock, chandetaily, audctl & 1 ? L"15K" : L"64K");

		// draw high-pass indicator
		if ((ch == 0 && (audctl & 4)) || (ch == 1 && (audctl & 2)))
			tr.DrawTextLine(x_highpass, chandetaily, L"H");

		// draw mode indicator
		const uint8 ctl = rstate->mReg[ch*2 + 1];
		if (ctl & 0x10)
			tr.DrawTextLine(x_mode, chandetaily, L"V");
		else {
			tr.DrawTextLine(x_mode, chandetaily, (ctl & 0x80) ? L"L" : L"5");

			if (ctl & 0x20)
				tr.DrawTextLine(x_noise, chandetaily, L"T");
			else if (ctl & 0x40)
				tr.DrawTextLine(x_noise, chandetaily, L"4");
			else
				tr.DrawTextLine(x_noise, chandetaily, (audctl & 0x80) ? L"9" : L"17");
		}

		// draw volume indicator
		int vol = (ctl & 15) * (chanht - 3) / 15;

		rdr.SetColorRGB(0xFFFFFF);
		rdr.FillRect(x_waveform, chy + chanht - 1 - vol, 1, vol);

		const uint32 n = log->mRecordedCount;

		if (n >= 2) {
			uint32 hpos = 0x8000 + ((x_waveform + 2) << 16);
			int pybase = chy + chanht - 1;

			vdfastvector<vdpoint32> pts(n);

			for(uint32 pos = 0; pos < n; ++pos) {
				int px = hpos >> 16;
				int py = pybase - log->mpStates[pos].mChannelOutputs[ch] * (chanht - 3) / 15;

				pts[pos] = vdpoint32(px, py);

				hpos += hstep;
			}

			rdr.PolyLine(pts.data(), n - 1);
		}
	}

	mpAudioMonitor->Reset();
}

///////////////////////////////////////////////////////////////////////////
class ATUIRenderer : public vdrefcount, public IATUIRenderer {
public:
	ATUIRenderer();
	~ATUIRenderer();

	int AddRef() { return vdrefcount::AddRef(); }
	int Release() { return vdrefcount::Release(); }

	void SetStatusFlags(uint32 flags) { mStatusFlags |= flags; mStickyStatusFlags |= flags; }
	void ResetStatusFlags(uint32 flags) { mStatusFlags &= ~flags; }
	void PulseStatusFlags(uint32 flags) { mStickyStatusFlags |= flags; }

	void SetStatusCounter(uint32 index, uint32 value);
	void SetDiskMotorActivity(uint32 index, bool on);

	void SetHActivity(bool write);
	void SetPCLinkActivity(bool write);
	void SetIDEActivity(bool write, uint32 lba);
	void SetFlashWriteActivity();

	void SetCassetteIndicatorVisible(bool vis) { mbShowCassetteIndicator = vis; }
	void SetCassettePosition(float pos);

	void SetRecordingPosition();
	void SetRecordingPosition(float time, sint64 size);

	void SetModemConnection(const char *str);

	void SetLedStatus(uint8 ledMask);

	void ClearWatchedValue(int index);
	void SetWatchedValue(int index, uint32 value, int len);
	void SetAudioStatus(ATUIAudioStatus *status);
	void SetAudioMonitor(ATAudioMonitor *monitor);
	void SetSlightSID(ATSlightSIDEmulator *emu);

	void SetFpsIndicator(float fps);

	void SetHoverTip(int px, int py, const wchar_t *text);

	void SetPaused(bool paused);

	void SetUIManager(ATUIManager *m);

	void Relayout(int w, int h);
	void Update();

protected:
	void InvalidateLayout();

	void UpdateHostDeviceLabel();
	void UpdatePCLinkLabel();
	void UpdateHoverTipPos();

	uint32	mStatusFlags;
	uint32	mStickyStatusFlags;
	uint32	mStatusCounter[15];
	uint32	mDiskMotorFlags;
	float	mCassettePos;
	int		mRecordingPos;
	sint64	mRecordingSize;
	bool	mbShowCassetteIndicator;
	int		mShowCassetteIndicatorCounter;

	uint32	mHardDiskLBA;
	uint8	mHardDiskCounter;
	bool	mbHardDiskRead;
	bool	mbHardDiskWrite;

	uint8	mHReadCounter;
	uint8	mHWriteCounter;
	uint8	mPCLinkReadCounter;
	uint8	mPCLinkWriteCounter;
	uint8	mFlashWriteCounter;

	VDStringA	mModemConnection;

	uint8	mLedStatus;

	uint32	mWatchedValues[8];
	sint8	mWatchedValueLens[8];

	ATAudioMonitor	*mpAudioMonitor;
	ATSlightSIDEmulator *mpSlightSID;

	VDDisplaySubRenderCache mFpsRenderCache;
	float mFps;

	vdrefptr<IVDDisplayFont> mpSysFont;
	vdrefptr<IVDDisplayFont> mpSmallMonoSysFont;
	vdrefptr<IVDDisplayFont> mpSysMonoFont;
	vdrefptr<IVDDisplayFont> mpSysHoverTipFont;
	vdrefptr<IVDDisplayFont> mpSysBoldHoverTipFont;
	int mSysFontDigitWidth;
	int mSysFontDigitHeight;
	int mSysMonoFontHeight;

	sint32	mPrevLayoutWidth;
	sint32	mPrevLayoutHeight;

	vdrefptr<ATUILabel> mpDiskDriveIndicatorLabels[15];
	vdrefptr<ATUILabel> mpFpsLabel;
	vdrefptr<ATUILabel> mpModemConnectionLabel;
	vdrefptr<ATUILabel> mpWatchLabels[8];
	vdrefptr<ATUILabel> mpHardDiskDeviceLabel;
	vdrefptr<ATUILabel> mpRecordingLabel;
	vdrefptr<ATUILabel> mpFlashWriteLabel;
	vdrefptr<ATUILabel> mpHostDeviceLabel;
	vdrefptr<ATUILabel> mpPCLinkLabel;
	vdrefptr<ATUILabel> mpLedLabels[2];
	vdrefptr<ATUILabel> mpCassetteLabel;
	vdrefptr<ATUILabel> mpCassetteTimeLabel;
	vdrefptr<ATUILabel> mpPausedLabel;
	vdrefptr<ATUIAudioStatusDisplay> mpAudioStatusDisplay;
	vdrefptr<ATUIAudioDisplay> mpAudioDisplay;

	vdrefptr<ATUILabel> mpHoverTip;
	int mHoverTipX;
	int mHoverTipY;

	static const uint32 kDiskColors[8][2];
};

const uint32 ATUIRenderer::kDiskColors[8][2]={
	{ 0x91a100, 0xffff67 },
	{ 0xd37040, 0xffe7b7 },
	{ 0xd454cf, 0xffcbff },
	{ 0x9266ff, 0xffddff },
	{ 0x4796ec, 0xbeffff },
	{ 0x35ba61, 0xacffd8 },
	{ 0x6cb200, 0xe3ff6f },
	{ 0xbb860e, 0xfffd85 },
};

void ATCreateUIRenderer(IATUIRenderer **r) {
	*r = new ATUIRenderer;
	(*r)->AddRef();
}

ATUIRenderer::ATUIRenderer()
	: mStatusFlags(0)
	, mStickyStatusFlags(0)
	, mDiskMotorFlags(0)
	, mCassettePos(0)
	, mRecordingPos(-1)
	, mRecordingSize(-1)
	, mbShowCassetteIndicator(false)
	, mShowCassetteIndicatorCounter(0)
	, mHardDiskLBA(0)
	, mbHardDiskRead(false)
	, mbHardDiskWrite(false)
	, mHardDiskCounter(0)
	, mHReadCounter(0)
	, mHWriteCounter(0)
	, mPCLinkReadCounter(0)
	, mPCLinkWriteCounter(0)
	, mFlashWriteCounter(0)
	, mLedStatus(0)
	, mpAudioMonitor(NULL)
	, mpSlightSID(NULL)
	, mFps(-1.0f)
	, mPrevLayoutWidth(0)
	, mPrevLayoutHeight(0)
	, mSysFontDigitWidth(0)
	, mSysFontDigitHeight(0)
	, mHoverTipX(0)
	, mHoverTipY(0)
{
	for(int i=0; i<15; ++i) {
		mStatusCounter[i] = i+1;
	}

	for(int i=0; i<8; ++i)
		mWatchedValueLens[i] = -1;

	for(int i=0; i<15; ++i) {
		ATUILabel *label = new ATUILabel;
		
		mpDiskDriveIndicatorLabels[i] = label;

		label->SetTextColor(0);
		label->SetVisible(false);
		label->SetTextOffset(2, 1);
	}

	for(int i=0; i<8; ++i) {
		ATUILabel *label = new ATUILabel;
		
		mpWatchLabels[i] = label;

		label->SetFillColor(0);
		label->SetTextColor(0xFFFFFF);
		label->SetVisible(false);
		label->SetTextOffset(2, 1);
	}

	mpModemConnectionLabel = new ATUILabel;
	mpModemConnectionLabel->SetVisible(false);
	mpModemConnectionLabel->SetFillColor(0x1e00ac);
	mpModemConnectionLabel->SetTextColor(0x8458ff);

	mpFpsLabel = new ATUILabel;
	mpFpsLabel->SetVisible(false);
	mpFpsLabel->SetTextColor(0xFFFFFF);
	mpFpsLabel->SetFillColor(0);
	mpFpsLabel->SetTextOffset(2, 0);

	mpAudioStatusDisplay = new ATUIAudioStatusDisplay;
	mpAudioStatusDisplay->SetVisible(false);
	mpAudioStatusDisplay->SetAlphaFillColor(0x80000000);
	mpAudioStatusDisplay->AutoSize();

	mpAudioDisplay = new ATUIAudioDisplay;
	mpAudioDisplay->SetVisible(false);
	mpAudioDisplay->SetAlphaFillColor(0x80000000);
	mpAudioDisplay->SetSmallFont(mpSmallMonoSysFont);

	mpHardDiskDeviceLabel = new ATUILabel;
	mpHardDiskDeviceLabel->SetVisible(false);
	mpHardDiskDeviceLabel->SetFillColor(0x91d81d);
	mpHardDiskDeviceLabel->SetTextColor(0);

	mpRecordingLabel = new ATUILabel;
	mpRecordingLabel->SetVisible(false);
	mpRecordingLabel->SetFillColor(0x932f00);
	mpRecordingLabel->SetTextColor(0xffffff);

	mpFlashWriteLabel = new ATUILabel;
	mpFlashWriteLabel->SetVisible(false);
	mpFlashWriteLabel->SetFillColor(0xd37040);
	mpFlashWriteLabel->SetTextColor(0x000000);
	mpFlashWriteLabel->SetText(L"F");

	for(int i=0; i<2; ++i) {
		ATUILabel *label = new ATUILabel;
		
		mpLedLabels[i] = label;

		label->SetVisible(false);
		label->SetFillColor(0xffffff);
		label->SetTextColor(0xdd5d87);
		label->SetText(i ? L"2" : L"1");
	}

	mpPCLinkLabel = new ATUILabel;
	mpPCLinkLabel->SetVisible(false);
	mpPCLinkLabel->SetFillColor(0x007920);
	mpPCLinkLabel->SetTextColor(0x000000);

	mpHostDeviceLabel = new ATUILabel;
	mpHostDeviceLabel->SetVisible(false);
	mpHostDeviceLabel->SetFillColor(0x007920);
	mpHostDeviceLabel->SetTextColor(0x000000);

	mpCassetteLabel = new ATUILabel;
	mpCassetteLabel->SetVisible(false);
	mpCassetteLabel->SetFillColor(0x93e1ff);
	mpCassetteLabel->SetTextColor(0);
	mpCassetteLabel->SetText(L"C");

	mpCassetteTimeLabel = new ATUILabel;
	mpCassetteTimeLabel->SetVisible(false);
	mpCassetteTimeLabel->SetTextColor(0x0755ab);
	mpCassetteTimeLabel->SetFillColor(0);

	mpPausedLabel = new ATUILabel;
	mpPausedLabel->SetVisible(false);
	mpPausedLabel->SetFont(mpSysFont);
	mpPausedLabel->SetTextColor(0);
	mpPausedLabel->SetFillColor(0xd4d0c8);
	mpPausedLabel->SetText(L" Paused ");

	mpHoverTip = new ATUILabel;
	mpHoverTip->SetVisible(false);
	mpHoverTip->SetTextColor(0);
	mpHoverTip->SetFillColor(0xffffe1);
	mpHoverTip->SetBorderColor(0);
	mpHoverTip->SetTextOffset(2, 2);
}

ATUIRenderer::~ATUIRenderer() {
}

void ATUIRenderer::SetStatusCounter(uint32 index, uint32 value) {
	mStatusCounter[index] = value;
}

void ATUIRenderer::SetDiskMotorActivity(uint32 index, bool on) {
	if (on)
		mDiskMotorFlags |= (1 << index);
	else
		mDiskMotorFlags &= ~(1 << index);
}

void ATUIRenderer::SetHActivity(bool write) {
	bool update = false;

	if (write) {
		if (mHWriteCounter < 25)
			update = true;

		mHWriteCounter = 30;
	} else {
		if (mHReadCounter < 25)
			update = true;

		mHReadCounter = 30;
	}

	if (update)
		UpdateHostDeviceLabel();
}

void ATUIRenderer::SetPCLinkActivity(bool write) {
	bool update = false;

	if (write) {
		if (mPCLinkWriteCounter < 25)
			update = true;

		mPCLinkWriteCounter = 30;
	} else {
		if (mPCLinkReadCounter < 25)
			update = true;

		mPCLinkReadCounter = 30;
	}

	if (update)
		UpdatePCLinkLabel();
}

void ATUIRenderer::SetIDEActivity(bool write, uint32 lba) {
	if (mHardDiskLBA != lba) {
		mbHardDiskWrite = false;
		mbHardDiskRead = false;
	}

	mHardDiskCounter = 3;

	if (write)
		mbHardDiskWrite = true;
	else
		mbHardDiskRead = true;

	mHardDiskLBA = lba;

	mpHardDiskDeviceLabel->SetVisible(true);
	mpHardDiskDeviceLabel->SetTextF(L"%lc%u", mbHardDiskWrite ? L'W' : L'R', mHardDiskLBA);
	mpHardDiskDeviceLabel->AutoSize();
}

void ATUIRenderer::SetFlashWriteActivity() {
	mFlashWriteCounter = 20;

	mpFlashWriteLabel->SetVisible(true);
}

void ATUIRenderer::SetModemConnection(const char *str) {
	if (*str) {
		mpModemConnectionLabel->SetVisible(true);
		mpModemConnectionLabel->SetText(VDTextAToW(str).c_str());
		mpModemConnectionLabel->AutoSize();
	} else {
		mpModemConnectionLabel->SetVisible(false);
	}
}

void ATUIRenderer::SetRecordingPosition() {
	mRecordingPos = -1;
	mRecordingSize = -1;
	mpRecordingLabel->SetVisible(false);
}

void ATUIRenderer::SetRecordingPosition(float time, sint64 size) {
	int cpos = VDRoundToInt(time);
	int csize = (int)((size * 10) >> 20);

	if (mRecordingPos == cpos && mRecordingSize == csize)
		return;

	mRecordingPos = cpos;
	mRecordingSize = csize;

	int secs = cpos % 60;
	int mins = cpos / 60;
	int hours = mins / 60;
	mins %= 60;

	mpRecordingLabel->SetTextF(L"R%02u:%02u:%02u (%.1fM)", hours, mins, secs, (float)csize / 10.0f);
	mpRecordingLabel->AutoSize();
	mpRecordingLabel->SetVisible(true);
}

void ATUIRenderer::SetLedStatus(uint8 ledMask) {
	if (mLedStatus == ledMask)
		return;

	mLedStatus = ledMask;

	mpLedLabels[0]->SetVisible((ledMask & 1) != 0);
	mpLedLabels[1]->SetVisible((ledMask & 2) != 0);
}

void ATUIRenderer::SetCassettePosition(float pos) {
	if (mCassettePos == pos)
		return;

	mCassettePos = pos;

	int cpos = VDRoundToInt(mCassettePos);

	int secs = cpos % 60;
	int mins = cpos / 60;
	int hours = mins / 60;
	mins %= 60;

	mpCassetteTimeLabel->SetTextF(L"%02u:%02u:%02u", hours, mins, secs);
	mpCassetteTimeLabel->AutoSize();
}

void ATUIRenderer::ClearWatchedValue(int index) {
	if (index >= 0 && index < 8) {
		mWatchedValueLens[index] = -1;
		mpWatchLabels[index]->SetVisible(false);
	}
}

void ATUIRenderer::SetWatchedValue(int index, uint32 value, int len) {
	if (index >= 0 && index < 8) {
		mWatchedValues[index] = value;
		mWatchedValueLens[index] = len;
		mpWatchLabels[index]->SetVisible(true);
	}
}

void ATUIRenderer::SetAudioStatus(ATUIAudioStatus *status) {
	if (status) {
		mpAudioStatusDisplay->Update(*status);
		mpAudioStatusDisplay->SetVisible(true);
	} else {
		mpAudioStatusDisplay->SetVisible(false);
	}
}

void ATUIRenderer::SetAudioMonitor(ATAudioMonitor *monitor) {
	mpAudioMonitor = monitor;

	mpAudioDisplay->SetAudioMonitor(monitor);
	mpAudioDisplay->AutoSize();
	mpAudioDisplay->SetVisible(monitor != NULL);
	InvalidateLayout();
}

void ATUIRenderer::SetSlightSID(ATSlightSIDEmulator *emu) {
	mpSlightSID = emu;

	mpAudioDisplay->SetSlightSID(emu);
	mpAudioDisplay->AutoSize();
	InvalidateLayout();
}

void ATUIRenderer::SetFpsIndicator(float fps) {
	if (mFps != fps) {
		mFps = fps;

		if (fps < 0) {
			mpFpsLabel->SetVisible(false);
		} else {
			mpFpsLabel->SetVisible(true);
			mpFpsLabel->SetTextF(L"%.3f fps", fps);
			mpFpsLabel->AutoSize();
		}
	}
}

void ATUIRenderer::SetHoverTip(int px, int py, const wchar_t *text) {
	if (!text || !*text) {
		mpHoverTip->SetVisible(false);
	} else {
		mHoverTipX = px;
		mHoverTipY = py;

		mpHoverTip->SetHTMLText(text);
		mpHoverTip->AutoSize();

		mpHoverTip->SetVisible(true);
		UpdateHoverTipPos();
	}
}

void ATUIRenderer::SetPaused(bool paused) {
	mpPausedLabel->SetVisible(paused);
}

void ATUIRenderer::SetUIManager(ATUIManager *m) {
	if (m) {
		ATUIContainer *c = m->GetMainWindow();

		for(int i = 14; i >= 0; --i)
			c->AddChild(mpDiskDriveIndicatorLabels[i]);

		c->AddChild(mpFpsLabel);
		c->AddChild(mpCassetteLabel);
		c->AddChild(mpCassetteTimeLabel);

		for(int i=0; i<2; ++i)
			c->AddChild(mpLedLabels[i]);

		c->AddChild(mpHostDeviceLabel);
		c->AddChild(mpPCLinkLabel);
		c->AddChild(mpRecordingLabel);
		c->AddChild(mpHardDiskDeviceLabel);
		c->AddChild(mpFlashWriteLabel);
		c->AddChild(mpModemConnectionLabel);

		for(int i=0; i<8; ++i)
			c->AddChild(mpWatchLabels[i]);

		c->AddChild(mpAudioDisplay);
		c->AddChild(mpAudioStatusDisplay);
		c->AddChild(mpPausedLabel);
		c->AddChild(mpHoverTip);

		// update fonts
		mpSysFont = m->GetThemeFont(kATUIThemeFont_Header);
		mpSmallMonoSysFont = m->GetThemeFont(kATUIThemeFont_MonoSmall);
		mpSysMonoFont = m->GetThemeFont(kATUIThemeFont_Mono);
		mpSysHoverTipFont = m->GetThemeFont(kATUIThemeFont_Tooltip);
		mpSysBoldHoverTipFont = m->GetThemeFont(kATUIThemeFont_TooltipBold);

		if (mpSysFont) {
			vdsize32 digitSize = mpSysFont->MeasureString(L"0123456789", 10, false);
			mSysFontDigitWidth = digitSize.w / 10;
			mSysFontDigitHeight = digitSize.h;
		}

		mSysMonoFontHeight = 0;

		if (mpSysMonoFont) {
			VDDisplayFontMetrics metrics;
			mpSysMonoFont->GetMetrics(metrics);

			mSysMonoFontHeight = metrics.mAscent + metrics.mDescent;
		}

		for(int i=0; i<15; ++i)
			mpDiskDriveIndicatorLabels[i]->SetFont(mpSysFont);

		for(int i=0; i<8; ++i)
			mpWatchLabels[i]->SetFont(mpSysFont);

		mpModemConnectionLabel->SetFont(mpSysFont);
		mpFpsLabel->SetFont(mpSysFont);
		mpAudioStatusDisplay->SetFont(mpSysFont);
		mpAudioStatusDisplay->AutoSize();
		mpAudioDisplay->SetBigFont(mpSysFont);
		mpAudioDisplay->SetSmallFont(mpSmallMonoSysFont);
		mpHardDiskDeviceLabel->SetFont(mpSysFont);
		mpRecordingLabel->SetFont(mpSysFont);
		mpFlashWriteLabel->SetFont(mpSysFont);
		mpFlashWriteLabel->AutoSize();

		for(int i=0; i<2; ++i) {
			mpLedLabels[i]->SetFont(mpSysFont);
			mpLedLabels[i]->AutoSize();
		}

		mpPCLinkLabel->SetFont(mpSysFont);
		mpHostDeviceLabel->SetFont(mpSysFont);
		mpCassetteLabel->SetFont(mpSysFont);
		mpCassetteLabel->AutoSize();
		mpCassetteTimeLabel->SetFont(mpSysFont);
		mpPausedLabel->SetFont(mpSysFont);
		mpPausedLabel->AutoSize();
		mpHoverTip->SetFont(mpSysHoverTipFont);
		mpHoverTip->SetBoldFont(mpSysBoldHoverTipFont);

		// update layout
		InvalidateLayout();
	}
}

void ATUIRenderer::Update() {
	uint32 statusFlags = mStatusFlags | mStickyStatusFlags;
	mStickyStatusFlags = mStatusFlags;

	int x = mPrevLayoutWidth;
	int y = mPrevLayoutHeight - mSysFontDigitHeight;

	for(int i = 14; i >= 0; --i) {
		ATUILabel& label = *mpDiskDriveIndicatorLabels[i];
		const uint32 flag = (1 << i);

		if ((statusFlags | mDiskMotorFlags) & flag) {
			label.SetTextF(L"%u", mStatusCounter[i]);
			label.AutoSize(x, mPrevLayoutHeight - mSysFontDigitHeight);

			x -= label.GetArea().width();
			label.SetPosition(vdpoint32(x, y));

			label.SetFillColor(kDiskColors[i & 7][(statusFlags & flag) != 0]);
			label.SetVisible(true);
		} else {
			label.SetVisible(false);
			x -= mSysFontDigitWidth;
		}
	}

	if (statusFlags & 0x10000) {
		mpCassetteLabel->SetVisible(true);

		mShowCassetteIndicatorCounter = 60;
	} else {
		mpCassetteLabel->SetVisible(false);

		if (mbShowCassetteIndicator)
			mShowCassetteIndicatorCounter = 60;
	}

	if (mShowCassetteIndicatorCounter) {
		--mShowCassetteIndicatorCounter;

		mpCassetteTimeLabel->SetVisible(true);
	} else {
		mpCassetteTimeLabel->SetVisible(false);
	}

	// draw H: indicators
	bool updateH = false;

	if (mHReadCounter) {
		--mHReadCounter;

		if (mHReadCounter == 24)
			updateH = true;
		else if (!mHReadCounter && !mHWriteCounter)
			updateH = true;
	}

	if (mHWriteCounter) {
		--mHWriteCounter;

		if (mHWriteCounter == 24)
			updateH = true;
		else if (!mHWriteCounter && !mHReadCounter)
			updateH = true;
	}

	if (updateH)
		UpdateHostDeviceLabel();

	// draw PCLink indicators (same place as H:)
	if (mPCLinkReadCounter)
		--mPCLinkReadCounter;

	if (mPCLinkWriteCounter)
		--mPCLinkWriteCounter;

	// draw H: indicators
	if (mbHardDiskRead || mbHardDiskWrite) {
		if (!--mHardDiskCounter) {
			mbHardDiskRead = false;
			mbHardDiskWrite = false;
		}
	} else {
		mpHardDiskDeviceLabel->SetVisible(false);
	}

	// draw flash write counter
	if (mFlashWriteCounter) {
		if (!--mFlashWriteCounter)
			mpFlashWriteLabel->SetVisible(false);
	}

	// draw watched values
	for(int i=0; i<8; ++i) {
		int len = mWatchedValueLens[i];
		if (len < 0)
			continue;

		ATUILabel& label = *mpWatchLabels[i];

		switch(len) {
			case 0:
				label.SetTextF(L"%d", (int)mWatchedValues[i]);
				break;
			case 1:
				label.SetTextF(L"%02X", mWatchedValues[i]);
				break;
			case 2:
				label.SetTextF(L"%04X", mWatchedValues[i]);
				break;
		}

		label.AutoSize();
	}

	// draw audio monitor
	mpAudioDisplay->Update();
}

void ATUIRenderer::InvalidateLayout() {
	Relayout(mPrevLayoutWidth, mPrevLayoutHeight);
}

void ATUIRenderer::Relayout(int w, int h) {
	mPrevLayoutWidth = w;
	mPrevLayoutHeight = h;

	mpFpsLabel->SetPosition(vdpoint32(w - 10 * mSysFontDigitWidth, 10));
	mpModemConnectionLabel->SetPosition(vdpoint32(1, h - mSysFontDigitHeight * 2));
	mpAudioDisplay->SetPosition(vdpoint32(8, h - mpAudioDisplay->GetArea().height() - mSysFontDigitHeight * 4));

	for(int i=0; i<8; ++i) {
		ATUILabel& label = *mpWatchLabels[i];

		int y = h - 4*mSysFontDigitHeight - (7 - i)*mSysMonoFontHeight;

		label.SetPosition(vdpoint32(64, y));
	}

	int ystats = h - mSysFontDigitHeight;

	mpHardDiskDeviceLabel->SetPosition(vdpoint32(mSysFontDigitWidth * 36, ystats));
	mpRecordingLabel->SetPosition(vdpoint32(mSysFontDigitWidth * 27, ystats));
	mpFlashWriteLabel->SetPosition(vdpoint32(mSysFontDigitWidth * 47, ystats));
	mpPCLinkLabel->SetPosition(vdpoint32(mSysFontDigitWidth * 19, ystats));
	mpHostDeviceLabel->SetPosition(vdpoint32(mSysFontDigitWidth * 19, ystats));

	for(int i=0; i<2; ++i)
		mpLedLabels[i]->SetPosition(vdpoint32(mSysFontDigitWidth * (11+i), ystats));

	mpCassetteLabel->SetPosition(vdpoint32(0, ystats));
	mpCassetteTimeLabel->SetPosition(vdpoint32(mpCassetteLabel->GetArea().width(), ystats));

	mpAudioStatusDisplay->SetPosition(vdpoint32(16, 16));

	mpPausedLabel->SetPosition(vdpoint32((w - mpPausedLabel->GetArea().width()) >> 1, 64));

	UpdateHoverTipPos();
}

void ATUIRenderer::UpdateHostDeviceLabel() {
	if (!mHReadCounter && !mHWriteCounter) {
		mpHostDeviceLabel->SetVisible(false);
		return;
	}

	mpHostDeviceLabel->Clear();
	mpHostDeviceLabel->AppendFormattedText(0, L"H:");

	mpHostDeviceLabel->AppendFormattedText(
		mHReadCounter >= 25 ? 0xFFFFFF : mHReadCounter ? 0x000000 : 0x007920,
		L"R");
	mpHostDeviceLabel->AppendFormattedText(
		mHWriteCounter >= 25 ? 0xFFFFFF : mHWriteCounter ? 0x000000 : 0x007920,
		L"W");

	mpHostDeviceLabel->AutoSize();
	mpHostDeviceLabel->SetVisible(true);
}

void ATUIRenderer::UpdatePCLinkLabel() {
	if (!mPCLinkReadCounter && !mPCLinkWriteCounter) {
		mpPCLinkLabel->SetVisible(false);
		return;
	}

	mpPCLinkLabel->Clear();
	mpPCLinkLabel->AppendFormattedText(0, L"PCL:");

	mpPCLinkLabel->AppendFormattedText(
		mPCLinkReadCounter >= 25 ? 0xFFFFFF : mPCLinkReadCounter ? 0x000000 : 0x007920,
		L"R");
	mpPCLinkLabel->AppendFormattedText(
		mPCLinkWriteCounter >= 25 ? 0xFFFFFF : mPCLinkWriteCounter ? 0x000000 : 0x007920,
		L"W");

	mpPCLinkLabel->AutoSize();
	mpPCLinkLabel->SetVisible(true);
}

void ATUIRenderer::UpdateHoverTipPos() {
	if (mpHoverTip->IsVisible()) {
		const vdsize32 htsize = mpHoverTip->GetArea().size();

		int x = mHoverTipX;
		int y = mHoverTipY + 32;

		if (x + htsize.w > mPrevLayoutWidth)
			x = std::max<int>(0, mPrevLayoutWidth - htsize.w);

		if (y + htsize.h > mPrevLayoutHeight) {
			int y2 = y - 32 - htsize.h;

			if (y2 >= 0)
				y = y2;
		}

		mpHoverTip->SetPosition(vdpoint32(x, y));
	}
}
