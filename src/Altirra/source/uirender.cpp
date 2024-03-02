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
#include <vd2/Kasumi/pixmap.h>
#include "uirender.h"

namespace {
	static const uint8 kCharData[15][7][4]={
#define X 0xFF
		{
			X,X,X,X,
			X,X,0,X,
			X,0,X,0,
			X,0,X,0,
			X,0,X,0,
			X,X,0,X,
			X,X,X,X,
		},
		{
			X,X,X,X,
			X,X,0,X,
			X,0,0,X,
			X,X,0,X,
			X,X,0,X,
			X,0,0,0,
			X,X,X,X,
		},
		{
			X,X,X,X,
			X,0,0,X,
			X,X,X,0,
			X,X,0,X,
			X,0,X,X,
			X,0,0,0,
			X,X,X,X,
		},
		{
			X,X,X,X,
			X,0,0,X,
			X,X,X,0,
			X,X,0,X,
			X,X,X,0,
			X,0,0,X,
			X,X,X,X,
		},
		{
			X,X,X,X,
			X,0,X,0,
			X,0,X,0,
			X,0,0,0,
			X,X,X,0,
			X,X,X,0,
			X,X,X,X,
		},
		{
			X,X,X,X,
			X,0,0,0,
			X,0,X,X,
			X,0,0,X,
			X,X,X,0,
			X,0,0,X,
			X,X,X,X,
		},
		{
			X,X,X,X,
			X,X,0,0,
			X,0,X,X,
			X,0,0,X,
			X,0,X,0,
			X,X,0,X,
			X,X,X,X,
		},
		{
			X,X,X,X,
			X,0,0,0,
			X,X,X,0,
			X,X,0,X,
			X,X,0,X,
			X,X,0,X,
			X,X,X,X,
		},
		{
			X,X,X,X,
			X,X,0,X,
			X,0,X,0,
			X,X,0,X,
			X,0,X,0,
			X,X,0,X,
			X,X,X,X,
		},
		{
			X,X,X,X,
			X,X,0,X,
			X,0,X,0,
			X,X,0,0,
			X,X,X,0,
			X,0,0,X,
			X,X,X,X,
		},
		{
			X,X,X,X,
			X,X,0,0,
			X,0,X,X,
			X,0,X,X,
			X,0,X,X,
			X,X,0,0,
			X,X,X,X,
		},
		{
			X,X,X,X,
			X,0,0,X,
			X,0,X,0,
			X,0,0,X,
			X,0,X,0,
			X,0,X,0,
			X,X,X,X,
		},
		{
			X,X,X,X,
			X,X,X,X,
			X,X,0,X,
			X,X,X,X,
			X,X,0,X,
			X,X,X,X,
			X,X,X,X,
		},
		{
			X,X,X,X,
			X,0,X,0,
			X,0,X,0,
			X,0,0,0,
			X,0,0,0,
			X,0,X,0,
			X,X,X,X,
		},
		{
			X,X,X,X,
			X,0,X,0,
			X,0,X,0,
			X,0,0,0,
			X,0,X,0,
			X,0,X,0,
			X,X,X,X,
		},
#undef X
	};

	void DrawString8(const VDPixmap& px, const uint32 *palette, int x, int y, uint8 forecolor, uint8 backcolor, const char *str) {
		size_t len = strlen(str);
		int w = px.w;
		int h = px.h;

		if (x >= w || y >= h || y <= -7)
			return;

		int y1 = y < 0 ? -y : 0;
		int y2 = y > h - 7 ? h - y : 7;
		uint8 xorcolor = forecolor ^ backcolor;

		for(;;) {
			const uint8 *data = NULL;

			if (!len) {
				if (x < 0 || x >= w)
					break;

				uint8 *dst = (uint8 *)px.data + px.pitch * (y + y1);

				if (px.format == nsVDPixmap::kPixFormat_XRGB8888) {
					for(int yi = y1; yi < y2; ++yi) {
						((uint32 *)dst)[x] = palette[backcolor];
						dst += px.pitch;
					}
				} else {
					for(int yi = y1; yi < y2; ++yi) {
						dst[x] = backcolor;
						dst += px.pitch;
					}
				}

				break;
			}

			--len;
			const char c = *str++;

			if (c >= '0' && c <= '9')
				data = kCharData[c - '0'][0];
			else if (c == 'C')
				data = kCharData[10][0];
			else if (c == 'R')
				data = kCharData[11][0];
			else if (c == ':')
				data = kCharData[12][0];
			else if (c == 'W')
				data = kCharData[13][0];
			else if (c == 'H')
				data = kCharData[14][0];

			if (data) {
				int x1 = 0;
				int x2 = 4;

				if (x < 0)
					x1 = -x;

				if (x > w - 4)
					x2 = w - x;

				const uint8 *src = data + 5 * y1;
				uint8 *dst = (uint8 *)px.data + px.pitch * (y + y1);

				if (px.format == nsVDPixmap::kPixFormat_XRGB8888) {
					for(int yi = y1; yi < y2; ++yi) {
						uint32 *dstrow = (uint32 *)dst;

						for(int xi = x1; xi < x2; ++xi) {
							dstrow[x + xi] = palette[forecolor ^ (src[xi] & xorcolor)];
						}

						src += 4;
						dst += px.pitch;
					}
				} else {
					for(int yi = y1; yi < y2; ++yi) {
						for(int xi = x1; xi < x2; ++xi) {
							dst[x + xi] = forecolor ^ (src[xi] & xorcolor);
						}

						src += 4;
						dst += px.pitch;
					}
				}
			}

			x += 4;
		} 
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

	void SetCassetteIndicatorVisible(bool vis) { mbShowCassetteIndicator = vis; }
	void SetCassettePosition(float pos);

	void SetRecordingPosition() { mRecordingPos = -1; }
	void SetRecordingPosition(float time) { mRecordingPos = time; }

	void Render(const VDPixmap& px, const uint32 *palette);

protected:
	uint32	mStatusFlags;
	uint32	mStickyStatusFlags;
	uint32	mStatusCounter[8];
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
{
	mStatusCounter[0] = 1;
	mStatusCounter[1] = 2;
	mStatusCounter[2] = 3;
	mStatusCounter[3] = 4;
	mStatusCounter[4] = 5;
	mStatusCounter[5] = 6;
	mStatusCounter[6] = 7;
	mStatusCounter[7] = 8;
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

void ATUIRenderer::SetCassettePosition(float pos) {
	mCassettePos = pos;
}

void ATUIRenderer::Render(const VDPixmap& pxdst, const uint32 *palette) {
	uint32 statusFlags = mStatusFlags | mStickyStatusFlags;
	mStickyStatusFlags = mStatusFlags;

	int x = pxdst.w;
	int y = pxdst.h - 7;

	for(int i = 7; i >= 0; --i) {
		if (statusFlags & (1 << i)) {
			char buf[11];

			sprintf(buf, "%u", mStatusCounter[i]);

			x -= 4 * strlen(buf) + 1;

			DrawString8(pxdst, palette, x, y, 0, 0x1F + (i << 5), buf);
		} else {
			x -= 5;
		}
	}

	if (statusFlags & 0x100) {
		DrawString8(pxdst, palette, 0, y, 0, 0x9F, "C");
		mShowCassetteIndicatorCounter = 60;
	} else if (mbShowCassetteIndicator)
		mShowCassetteIndicatorCounter = 60;

	char buf[64];
	if (mShowCassetteIndicatorCounter) {
		--mShowCassetteIndicatorCounter;

		int cpos = VDRoundToInt(mCassettePos);

		int secs = cpos % 60;
		int mins = cpos / 60;
		int hours = mins / 60;
		mins %= 60;

		sprintf(buf, "%02u:%02u:%02u", hours, mins, secs);
		DrawString8(pxdst, palette, 5, y, 0x94, 0, buf);
	}

	if (mRecordingPos > 0) {
		int cpos = VDRoundToInt(mRecordingPos);

		int secs = cpos % 60;
		int mins = cpos / 60;
		int hours = mins / 60;
		mins %= 60;

		sprintf(buf, "R%02u:%02u:%02u", hours, mins, secs);
		DrawString8(pxdst, palette, 66, y, 0x0f, 0x34, buf);
	}

	if (mbHardDiskRead || mbHardDiskWrite) {
		sprintf(buf, "%c%u", mbHardDiskWrite ? 'W' : 'R', mHardDiskLBA);
		DrawString8(pxdst, palette, 110, y, 0x00, 0xDC, buf);

		if (!--mHardDiskCounter) {
			mbHardDiskRead = false;
			mbHardDiskWrite = false;
		}
	}

	if (mHReadCounter || mHWriteCounter) {
		DrawString8(pxdst, palette, 48, y, 0x00, 0xB4, "H:  ");

		if (mHReadCounter) {
			--mHReadCounter;
			DrawString8(pxdst, palette, 56, y, mHReadCounter >= 25 ? 0xFF : 0x00, 0xB4, "R");
		}

		if (mHWriteCounter) {
			--mHWriteCounter;
			DrawString8(pxdst, palette, 60, y, mHWriteCounter >= 25 ? 0xFF : 0x00, 0xB4, "W");
		}
	}
}
