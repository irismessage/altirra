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
#include "font3x5.inl"
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
	void SetIDEActivity(bool write, uint32 lba);
	void SetFlashWriteActivity();

	void SetCassetteIndicatorVisible(bool vis) { mbShowCassetteIndicator = vis; }
	void SetCassettePosition(float pos);

	void SetRecordingPosition() { mRecordingPos = -1; }
	void SetRecordingPosition(float time) { mRecordingPos = time; }

	void SetModemConnection(const char *str) { mModemConnection = str; }

	void SetWatchedValue(int index, uint32 value, int len);
	void SetAudioStatus(ATUIAudioStatus *status);

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
	uint8	mFlashWriteCounter;

	VDStringA	mModemConnection;

	uint32	mWatchedValues[8];
	uint8	mWatchedValueLens[8];

	bool	mbShowAudioStatus;
	ATUIAudioStatus mAudioStatus;
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
	, mFlashWriteCounter(0)
	, mbShowAudioStatus(false)
{
	for(int i=0; i<15; ++i) {
		mStatusCounter[i] = i+1;
	}

	for(int i=0; i<8; ++i)
		mWatchedValueLens[i] = 0;
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

	const bool showAllIndicators = false;
	if (showAllIndicators) {
		statusFlags = 0x10000;
		mShowCassetteIndicatorCounter = 1;
		mRecordingPos = 10000;
		mHReadCounter = 4;
		mHWriteCounter = 4;
		mFlashWriteCounter = 30;
		mbHardDiskRead = true;
		mbHardDiskWrite = true;
		mHardDiskLBA = 1000000;
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

	// draw IDE indicators
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
		if (!len)
			continue;

		int y = pxdst.h - 6*12 + i*8;

		switch(len) {
			case 1:
				sprintf(buf, "%02X", mWatchedValues[i]);
				break;
			case 2:
				sprintf(buf, "%04X", mWatchedValues[i]);
				break;
		}

		DrawString8(pxdst, palette, 64, y, 0xFF, 0x00, buf);
	}
}
