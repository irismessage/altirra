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
#include <vd2/system/VDString.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/text.h>
#include "uirender.h"
#include "audiomonitor.h"
#include "font3x5.inl"
#include "font3x5p.inl"
#include "font5x8p.inl"

namespace {
	void DrawString8(const VDPixmap& px, const uint32 *palette, int x, int y, uint8 forecolor, uint8 backcolor, const char *str) {
		uint32 fc = forecolor;
		uint32 bc = backcolor;
		if (px.format == nsVDPixmap::kPixFormat_XRGB8888) {
			fc = palette[fc];
			bc = palette[bc];
		}

		VDPixmapDrawText(px, &g_ATFont5x8p_FontInfo, x, y, fc, bc, str);
	}

	void DrawString5(const VDPixmap& px, const uint32 *palette, int x, int y, uint8 forecolor, uint8 backcolor, const char *str) {
		uint32 fc = forecolor;
		uint32 bc = backcolor;
		if (px.format == nsVDPixmap::kPixFormat_XRGB8888) {
			fc = palette[fc];
			bc = palette[bc];
		}

		VDPixmapDrawText(px, &g_ATFont3x5p_FontInfo, x, y, fc, bc, str);
	}

	void Shade(const VDPixmap& px, int x1, int y1, int dx, int dy) {
		int x2 = x1 + dx;
		int y2 = y1 + dy;

		if (x1 < 0)
			x1 = 0;

		if (y1 < 0)
			y1 = 0;

		if (x2 > (int)px.w)
			x2 = (int)px.w;

		if (y2 > (int)px.h)
			y2 = (int)px.h;

		if (x2 <= x1 || y2 <= y1)
			return;

		bool px32 = false;

		if (px.format == nsVDPixmap::kPixFormat_XRGB8888)
			px32 = true;
		else if (px.format != nsVDPixmap::kPixFormat_Pal8)
			return;

		dx = x2 - x1;
		dy = y2 - y1;

		char *dst0 = (char *)px.data + px.pitch * y1 + (px32 ? x1 * 4 : x1);

		do {
			int dx2 = dx;

			if (px32) {
				uint32 *dst = (uint32 *)dst0;

				do {
					*dst = (*dst & 0xfcfcfc) >> 2;
					++dst;
				} while(--dx2);
			} else {
				uint8 *dst = (uint8 *)dst0;

				do {
					uint8 c = *dst;

					c = (c & 0xf0) + ((c & 0x0c) >> 2);

					*dst++ = c;
				} while(--dx2);
			}

			dst0 += px.pitch;
		} while(--dy);
	}

	void FillRect(const VDPixmap& px, const uint32 *palette, uint8 color, int x1, int y1, int dx, int dy) {
		int x2 = x1 + dx;
		int y2 = y1 + dy;

		if (x1 < 0)
			x1 = 0;

		if (y1 < 0)
			y1 = 0;

		if (x2 > (int)px.w)
			x2 = (int)px.w;

		if (y2 > (int)px.h)
			y2 = (int)px.h;

		if (x2 <= x1 || y2 <= y1)
			return;

		bool px32 = false;
		uint32 c32;

		if (px.format == nsVDPixmap::kPixFormat_XRGB8888) {
			px32 = true;
			c32 = palette[color];
		} else if (px.format != nsVDPixmap::kPixFormat_Pal8)
			return;

		dx = x2 - x1;
		dy = y2 - y1;

		char *dst0 = (char *)px.data + px.pitch * y1 + (px32 ? x1 * 4 : x1);

		do {
			int dx2 = dx;

			if (px32) {
				uint32 *dst = (uint32 *)dst0;

				do {
					*dst++ = c32;
				} while(--dx2);
			} else {
				uint8 *dst = (uint8 *)dst0;

				do {
					*dst++ = color;
				} while(--dx2);
			}

			dst0 += px.pitch;
		} while(--dy);
	}
}

class ATUIRenderer : public vdrefcounted<IATUIRenderer> {
public:
	ATUIRenderer();
	~ATUIRenderer();

	void SetStatusFlags(uint32 flags) { mStatusFlags |= flags; mStickyStatusFlags |= flags; }
	void ResetStatusFlags(uint32 flags) { mStatusFlags &= ~flags; }
	void PulseStatusFlags(uint32 flags) { mStickyStatusFlags |= flags; }

	void SetStatusCounter(uint32 index, uint32 value);

	void SetHActivity(bool write);
	void SetPCLinkActivity(bool write);
	void SetIDEActivity(bool write, uint32 lba);
	void SetFlashWriteActivity();

	void SetCassetteIndicatorVisible(bool vis) { mbShowCassetteIndicator = vis; }
	void SetCassettePosition(float pos);

	void SetRecordingPosition() { mRecordingPos = -1; }
	void SetRecordingPosition(float time) { mRecordingPos = time; }

	void SetModemConnection(const char *str) { mModemConnection = str; }

	void ClearWatchedValue(int index);
	void SetWatchedValue(int index, uint32 value, int len);
	void SetAudioStatus(ATUIAudioStatus *status);
	void SetAudioMonitor(ATAudioMonitor *monitor);

	void Render(const VDPixmap& px, const uint32 *palette);

protected:
	uint32	mStatusFlags;
	uint32	mStickyStatusFlags;
	uint32	mStatusCounter[15];
	float	mCassettePos;
	float	mRecordingPos;
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

	uint32	mWatchedValues[8];
	sint8	mWatchedValueLens[8];

	bool	mbShowAudioStatus;
	ATUIAudioStatus mAudioStatus;

	ATAudioMonitor	*mpAudioMonitor;
};

void ATCreateUIRenderer(IATUIRenderer **r) {
	*r = new ATUIRenderer;
	(*r)->AddRef();
}

ATUIRenderer::ATUIRenderer()
	: mStatusFlags(0)
	, mStickyStatusFlags(0)
	, mCassettePos(0)
	, mRecordingPos(-1)
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
	, mbShowAudioStatus(false)
	, mpAudioMonitor(NULL)
{
	for(int i=0; i<15; ++i) {
		mStatusCounter[i] = i+1;
	}

	for(int i=0; i<8; ++i)
		mWatchedValueLens[i] = -1;
}

ATUIRenderer::~ATUIRenderer() {
}

void ATUIRenderer::SetStatusCounter(uint32 index, uint32 value) {
	mStatusCounter[index] = value;
}

void ATUIRenderer::SetHActivity(bool write) {
	if (write)
		mHWriteCounter = 30;
	else
		mHReadCounter = 30;
}

void ATUIRenderer::SetPCLinkActivity(bool write) {
	if (write)
		mPCLinkWriteCounter = 30;
	else
		mPCLinkReadCounter = 30;
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
}

void ATUIRenderer::SetFlashWriteActivity() {
	mFlashWriteCounter = 20;
}

void ATUIRenderer::SetCassettePosition(float pos) {
	mCassettePos = pos;
}

void ATUIRenderer::ClearWatchedValue(int index) {
	if (index >= 0 && index < 8)
		mWatchedValueLens[index] = -1;
}

void ATUIRenderer::SetWatchedValue(int index, uint32 value, int len) {
	if (index >= 0 && index < 8) {
		mWatchedValues[index] = value;
		mWatchedValueLens[index] = len;
	}
}

void ATUIRenderer::SetAudioStatus(ATUIAudioStatus *status) {
	if (status) {
		mAudioStatus = *status;
		mbShowAudioStatus = true;
	} else {
		mbShowAudioStatus = false;
	}
}

void ATUIRenderer::SetAudioMonitor(ATAudioMonitor *monitor) {
	mpAudioMonitor = monitor;
}

void ATUIRenderer::Render(const VDPixmap& pxdst, const uint32 *palette) {
	uint32 statusFlags = mStatusFlags | mStickyStatusFlags;
	mStickyStatusFlags = mStatusFlags;

	int x = pxdst.w;
	int y = pxdst.h - 9;

	char buf[128];

	if (mbShowAudioStatus) {
		sprintf(buf, "Underflow count: %d ", mAudioStatus.mUnderflowCount);
		DrawString8(pxdst, palette, 16, 16, 0x0F, 0x00, buf);
		sprintf(buf, "Overflow count: %d ", mAudioStatus.mOverflowCount);
		DrawString8(pxdst, palette, 16, 24, 0x0F, 0x00, buf);
		sprintf(buf, "Drop count: %d ", mAudioStatus.mDropCount);
		DrawString8(pxdst, palette, 16, 32, 0x0F, 0x00, buf);
		sprintf(buf, "Measured range: %5d-%5d ", mAudioStatus.mMeasuredMin, mAudioStatus.mMeasuredMax);
		DrawString8(pxdst, palette, 16, 40, 0x0F, 0x00, buf);
		sprintf(buf, "Target range: %5d-%5d ", mAudioStatus.mTargetMin, mAudioStatus.mTargetMax);
		DrawString8(pxdst, palette, 16, 48, 0x0F, 0x00, buf);
		sprintf(buf, "Incoming data rate: %.2f samples/sec", mAudioStatus.mIncomingRate);
		DrawString8(pxdst, palette, 16, 56, 0x0F, 0x00, buf);
		sprintf(buf, "Expected data rate: %.2f samples/sec", mAudioStatus.mExpectedRate);
		DrawString8(pxdst, palette, 16, 64, 0x0F, 0x00, buf);
	}

	const bool showAllIndicators = false;
	if (showAllIndicators) {
		statusFlags = 0x10000;
		mShowCassetteIndicatorCounter = 1;
		mRecordingPos = 10000;
		mHReadCounter = 4;
		mHWriteCounter = 4;
		mPCLinkReadCounter = 4;
		mPCLinkWriteCounter = 4;
		mFlashWriteCounter = 30;
		mbHardDiskRead = true;
		mbHardDiskWrite = true;
		mHardDiskLBA = 1000000;
		mModemConnection = "Connected to 192.168.0.1:8000";

		for(int i=0; i<15; ++i)
			mStatusCounter[i] = i + 1;

		statusFlags = 0x7fff;
	}

	for(int i = 14; i >= 0; --i) {
		if (statusFlags & (1 << i)) {
			sprintf(buf, "%u", mStatusCounter[i]);

			x -= 5 * strlen(buf) + 1;

			DrawString8(pxdst, palette, x, y, 0, 0x1F + (i << 5), buf);
		} else {
			x -= 5;
		}
	}

	if (statusFlags & 0x10000) {
		DrawString8(pxdst, palette, 0, y, 0, 0x9F, "C");
		mShowCassetteIndicatorCounter = 60;
	} else if (mbShowCassetteIndicator)
		mShowCassetteIndicatorCounter = 60;

	if (mShowCassetteIndicatorCounter) {
		--mShowCassetteIndicatorCounter;

		int cpos = VDRoundToInt(mCassettePos);

		int secs = cpos % 60;
		int mins = cpos / 60;
		int hours = mins / 60;
		mins %= 60;

		sprintf(buf, "%02u:%02u:%02u", hours, mins, secs);
		DrawString8(pxdst, palette, 7, y, 0x94, 0, buf);
	}

	// draw H: indicators
	if (mHReadCounter || mHWriteCounter) {
		DrawString8(pxdst, palette, 48, y, 0x00, 0xB4, "H:  ");

		if (mHReadCounter) {
			--mHReadCounter;
			DrawString8(pxdst, palette, 58, y, mHReadCounter >= 25 ? 0xFF : 0x00, 0xB4, "R");
		}

		if (mHWriteCounter) {
			--mHWriteCounter;
			DrawString8(pxdst, palette, 64, y, mHWriteCounter >= 25 ? 0xFF : 0x00, 0xB4, "W");
		}
	}

	// draw PCLink indicators (same place as H:)
	if (mPCLinkReadCounter || mPCLinkWriteCounter) {
		DrawString8(pxdst, palette, 48, y, 0x00, 0xB4, "PCL:  ");

		if (mPCLinkReadCounter) {
			--mPCLinkReadCounter;
			DrawString8(pxdst, palette, 70, y, mHReadCounter >= 25 ? 0xFF : 0x00, 0xB4, "R");
		}

		if (mPCLinkWriteCounter) {
			--mPCLinkWriteCounter;
			DrawString8(pxdst, palette, 76, y, mHWriteCounter >= 25 ? 0xFF : 0x00, 0xB4, "W");
		}
	}

	// draw recording position
	if (mRecordingPos > 0) {
		int cpos = VDRoundToInt(mRecordingPos);

		int secs = cpos % 60;
		int mins = cpos / 60;
		int hours = mins / 60;
		mins %= 60;

		sprintf(buf, "R%02u:%02u:%02u", hours, mins, secs);
		DrawString8(pxdst, palette, 72, y, 0x0f, 0x34, buf);
	}

	// draw H: indicators
	if (mbHardDiskRead || mbHardDiskWrite) {
		sprintf(buf, "%c%u", mbHardDiskWrite ? 'W' : 'R', mHardDiskLBA);
		DrawString8(pxdst, palette, 116, y, 0x00, 0xDC, buf);

		if (!--mHardDiskCounter) {
			mbHardDiskRead = false;
			mbHardDiskWrite = false;
		}
	}

	// draw flash write counter
	if (mFlashWriteCounter) {
		--mFlashWriteCounter;

		DrawString8(pxdst, palette, 180, y, 0x00, 0x38, "F");
	}

	// draw modem connection
	if (!mModemConnection.empty()) {
		DrawString8(pxdst, palette, 1, y - 10, 0x78, 0x70, mModemConnection.c_str());
	}

	// draw watched values
	for(int i=0; i<8; ++i) {
		int len = mWatchedValueLens[i];
		if (len < 0)
			continue;

		int y = pxdst.h - 6*12 + i*8;

		switch(len) {
			case 0:
				sprintf(buf, "%d", (int)mWatchedValues[i]);
				break;
			case 1:
				sprintf(buf, "%02X", mWatchedValues[i]);
				break;
			case 2:
				sprintf(buf, "%04X", mWatchedValues[i]);
				break;
		}

		DrawString8(pxdst, palette, 64, y, 0xFF, 0x00, buf);
	}

	// draw audio monitor
	if (mpAudioMonitor) {
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

		const int x = 8;
		const int y = pxdst.h - 128;
		sint32 hstep = log->mRecordedCount ? (80 << 16) / log->mRecordedCount : 0;

		Shade(pxdst, x, y, 130, 72);

		for(int ch=0; ch<4; ++ch) {
			int chy = y + 18*ch;
			sprintf(buf, "%7.1f", 7159090.0f / 8.0f / divisors[ch]);

			DrawString8(pxdst, palette, x, chy + 4, 0xFF, 0x00, buf);

			// draw link/clock indicator
			if ((ch == 1 && (audctl & 0x10)) || (ch == 3 && (audctl & 0x08)))
				DrawString5(pxdst, palette, x, chy + 13, 0xFF, 0x00, "16");
			else if ((ch == 0 && (audctl & 0x40)) || (ch == 2 && (audctl & 0x20)))
				DrawString5(pxdst, palette, x + 8, chy + 13, 0xFF, 0x00, "1.79");
			else
				DrawString5(pxdst, palette, x + 8, chy + 13, 0xFF, 0x00, audctl & 1 ? "15K" : "64K");

			// draw high-pass indicator
			if ((ch == 0 && (audctl & 4)) || (ch == 1 && (audctl & 2)))
				DrawString5(pxdst, palette, x + 22, chy + 13, 0xFF, 0x00, "H");

			// draw mode indicator
			const uint8 ctl = rstate->mReg[ch*2 + 1];
			if (ctl & 0x10)
				DrawString5(pxdst, palette, x + 30, chy + 13, 0xFF, 0x00, "V");
			else {
				DrawString5(pxdst, palette, x + 30, chy + 13, 0xFF, 0x00, (ctl & 0x80) ? "L" : "5");

				if (ctl & 0x20)
					DrawString5(pxdst, palette, x + 36, chy + 13, 0xFF, 0x00, "T");
				else if (ctl & 0x40)
					DrawString5(pxdst, palette, x + 36, chy + 13, 0xFF, 0x00, "4");
				else
					DrawString5(pxdst, palette, x + 36, chy + 13, 0xFF, 0x00, (audctl & 0x80) ? "9" : "17");
			}

			// draw volume indicator
			int vol = ctl & 15;
			FillRect(pxdst, palette, 0xFF, x + 48, chy + 18 - vol, 1, vol);

			if (log->mRecordedCount) {
				uint32 hpos = 0x8000 + ((x + 50) << 16);
				int pybase = chy + 17;

				for(uint32 pos = 0; pos < log->mRecordedCount; ++pos) {
					int px = hpos >> 16;
					int py = pybase - log->mpStates[pos].mChannelOutputs[ch];

					if (px < pxdst.w && py < pxdst.h) {
						if (pxdst.format == nsVDPixmap::kPixFormat_XRGB8888) {
							uint32 *ppx = (uint32 *)((char *)pxdst.data + pxdst.pitch * py) + px;

							*ppx = 0xFFFFFF;
						} else if (pxdst.format == nsVDPixmap::kPixFormat_Pal8) {
							((uint8 *)pxdst.data)[pxdst.pitch * py + px] = 0xFF;
						}
					}

					hpos += hstep;
				}
			}
		}

		mpAudioMonitor->Reset();
	}
}
