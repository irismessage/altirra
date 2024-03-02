//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009 Avery Lee
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
#include "gtiarenderer.h"
#include "gtiatables.h"
#include "savestate.h"

using namespace ATGTIA;

ATGTIARenderer::ATGTIARenderer()
	: mpMergeBuffer(NULL)
	, mpAnticBuffer(NULL)
	, mpDst(NULL)
	, mX(0)
	, mRCIndex(0)
	, mRCCount(0)
	, mbHiresMode(false)
	, mPRIOR(0)
	, mpPriTable(NULL)
	, mpColorTable(NULL)
{
	memset(mColorTable, 0, sizeof mColorTable);

	ATInitGTIAPriorityTables(mPriorityTables);

	mpColorTable = mColorTable;
	mpPriTable = mPriorityTables[0];
}

ATGTIARenderer::~ATGTIARenderer() {
}

void ATGTIARenderer::SetAnalysisMode(bool enable) {
	mpColorTable = enable ? kATAnalysisColorTable : mColorTable;
}

void ATGTIARenderer::BeginScanline(uint8 *dst, const uint8 *mergeBuffer, const uint8 *anticBuffer, bool hires) {
	mpDst = dst;
	mbHiresMode = hires;
	mX = 0;
	mpMergeBuffer = mergeBuffer;
	mpAnticBuffer = anticBuffer;

	memset(mpDst, mpColorTable[kColorBAK], 68);
}

void ATGTIARenderer::RenderScanline(int xend) {
	int x1 = mX;

	if (x1 >= xend)
		return;

	// render spans and process register changes
	do {
		int x2 = xend;

		if (mRCIndex < mRCCount) {
			const RegisterChange *rc0 = &mRegisterChanges[mRCIndex];
			const RegisterChange *rc = rc0;
			do {
				int xchg = rc->mPos;
				if (xchg > x1) {
					if (x2 > xchg)
						x2 = xchg;
					break;
				}

				++rc;
			} while(++mRCIndex < mRCCount);

			UpdateRegisters(rc0, rc - rc0);
		}

		// 40 column mode is set by ANTIC during horizontal blank if ANTIC modes 2, 3, or
		// F are used. 40 column mode has the following effects:
		//
		//	* The priority logic always sees PF2.
		//	* The collision logic sees either BAK or PF2. Adjacent bits are ORed each color
		//	  clock to determine this (PF2C in schematic).
		//	* The playfield bits are used instead to substitute the luminance of PF1 on top
		//	  of the priority logic output. This happens even if players have priority.
		//
		// The flip-flip in the GTIA that controls 40 column mode can only be set by the
		// horizontal sync command, but can be reset at any time whenever either of the
		// top two bits of PRIOR are set. If this happens, the GTIA will begin interpreting
		// AN0-AN2 in lores mode, but ANTIC will continue sending in hires mode. The result
		// is that the bit pair patterns 00-11 produce PF0-PF3 instead of BAK + PF0-PF2 as
		// usual.

		switch(mPRIOR & 0xc0) {
			case 0x00:
				if (mbHiresMode)
					RenderMode8(x1, x2);
				else
					RenderLores(x1, x2);
				break;

			case 0x40:
				RenderMode9(x1, x2);
				break;

			case 0x80:
				RenderMode10(x1, x2);
				break;

			case 0xC0:
				RenderMode11(x1, x2);
				break;
		}

		x1 = x2;
	} while(x1 < xend);

	mX = x1;
}

void ATGTIARenderer::EndScanline() {
	if (mpDst) {
		memset(mpDst + 444, mpColorTable[8], 456 - 444);
		mpDst = NULL;
	}

	// commit any outstanding register changes
	if (mRCIndex < mRCCount)
		UpdateRegisters(&mRegisterChanges[mRCIndex], mRCCount - mRCIndex);

	mRCCount = 0;
	mRCIndex = 0;
	mRegisterChanges.clear();
}

void ATGTIARenderer::AddRegisterChange(uint8 pos, uint8 addr, uint8 value) {
	RegisterChanges::iterator it(mRegisterChanges.end()), itBegin(mRegisterChanges.begin());

	while(it != itBegin && it[-1].mPos > pos)
		--it;

	RegisterChange change;
	change.mPos = pos;
	change.mReg = addr;
	change.mValue = value;
	change.mPad = 0;
	mRegisterChanges.insert(it, change);

	++mRCCount;
}

void ATGTIARenderer::SetRegisterImmediate(uint8 addr, uint8 value) {
	RegisterChange change;
	change.mPos = 0;
	change.mReg = addr;
	change.mValue = value;
	change.mPad = 0;

	UpdateRegisters(&change, 1);
}

template<class T>
void ATGTIARenderer::ExchangeState(T& io) {
	io != mX;
	io != mRCIndex;
	io != mRCCount;
	io != mbHiresMode;
	io != mPRIOR;

	// Note that we don't include the color table here as that is reloaded by the GTIA emulator.
}

void ATGTIARenderer::LoadState(ATSaveStateReader& reader) {
	ExchangeState(reader);

	// read register changes
	mRegisterChanges.resize(mRCCount);
	for(int i=0; i<mRCCount; ++i) {
		RegisterChange& rc = mRegisterChanges[i];

		rc.mPos = reader.ReadUint8();
		rc.mReg = reader.ReadUint8();
		rc.mValue = reader.ReadUint8();
	}
}

void ATGTIARenderer::SaveState(ATSaveStateWriter& writer) {
	ExchangeState(writer);

	// write register changes
	for(int i=0; i<mRCCount; ++i) {
		const RegisterChange& rc = mRegisterChanges[i];

		writer.WriteUint8(rc.mPos);
		writer.WriteUint8(rc.mReg);
		writer.WriteUint8(rc.mValue);
	}
}

void ATGTIARenderer::UpdateRegisters(const RegisterChange *rc, int count) {
	while(count--) {
		// process register change
		uint8 value = rc->mValue;

		switch(rc->mReg) {
		case 0x12:
			value &= 0xfe;
			mColorTable[kColorP0] = value;
			mColorTable[kColorP0P1] = value | mColorTable[kColorP1];
			mColorTable[kColorPF0P0] = mColorTable[kColorPF0] | value;
			mColorTable[kColorPF0P0P1] = mColorTable[kColorPF0P1] | value;
			mColorTable[kColorPF1P0] = mColorTable[kColorPF1] | value;
			mColorTable[kColorPF1P0P1] = mColorTable[kColorPF1P1] | value;
			break;
		case 0x13:
			value &= 0xfe;
			mColorTable[kColorP1] = value;
			mColorTable[kColorP0P1] = mColorTable[kColorP0] | value;
			mColorTable[kColorPF0P1] = mColorTable[kColorPF0] | value;
			mColorTable[kColorPF0P0P1] = mColorTable[kColorPF0P0] | value;
			mColorTable[kColorPF1P1] = mColorTable[kColorPF1] | value;
			mColorTable[kColorPF1P0P1] = mColorTable[kColorPF1P0] | value;
			break;
		case 0x14:
			value &= 0xfe;
			mColorTable[kColorP2] = value;
			mColorTable[kColorP2P3] = value | mColorTable[kColorP3];
			mColorTable[kColorPF2P2] = mColorTable[kColorPF2] | value;
			mColorTable[kColorPF2P2P3] = mColorTable[kColorPF2P3] | value;
			mColorTable[kColorPF3P2] = mColorTable[kColorPF3] | value;
			mColorTable[kColorPF3P2P3] = mColorTable[kColorPF3P3] | value;
			break;
		case 0x15:
			value &= 0xfe;
			mColorTable[kColorP3] = value;
			mColorTable[kColorP2P3] = mColorTable[kColorP2] | value;
			mColorTable[kColorPF2P3] = mColorTable[kColorPF2] | value;
			mColorTable[kColorPF2P2P3] = mColorTable[kColorPF2P2] | value;
			mColorTable[kColorPF3P3] = mColorTable[kColorPF3] | value;
			mColorTable[kColorPF3P2P3] = mColorTable[kColorPF3P2] | value;
			break;
		case 0x16:
			value &= 0xfe;
			mColorTable[kColorPF0] = value;
			mColorTable[kColorPF0P0] = value | mColorTable[kColorP0];
			mColorTable[kColorPF0P1] = value | mColorTable[kColorP1];
			mColorTable[kColorPF0P0P1] = value | mColorTable[kColorP0P1];
			break;
		case 0x17:
			value &= 0xfe;
			mColorTable[kColorPF1] = value;
			mColorTable[kColorPF1P0] = value | mColorTable[kColorP0];
			mColorTable[kColorPF1P1] = value | mColorTable[kColorP1];
			mColorTable[kColorPF1P0P1] = value | mColorTable[kColorP0P1];
			break;
		case 0x18:
			value &= 0xfe;
			mColorTable[kColorPF2] = value;
			mColorTable[kColorPF2P2] = value | mColorTable[kColorP2];
			mColorTable[kColorPF2P3] = value | mColorTable[kColorP3];
			mColorTable[kColorPF2P2P3] = value | mColorTable[kColorP2P3];
			break;
		case 0x19:
			value &= 0xfe;
			mColorTable[kColorPF3] = value;
			mColorTable[kColorPF3P2] = value | mColorTable[kColorP2];
			mColorTable[kColorPF3P3] = value | mColorTable[kColorP3];
			mColorTable[kColorPF3P2P3] = value | mColorTable[kColorP2P3];
			break;
		case 0x1A:
			value &= 0xfe;
			mColorTable[kColorBAK] = value;
			break;
		case 0x1B:
			mPRIOR = value;
			mpPriTable = mPriorityTables[(value & 15) + (value&32 ? 16 : 0)];

			if (value & 0xC0)
				mbHiresMode = false;
			break;
		}

		++rc;
	}
}

void ATGTIARenderer::RenderLores(int x1, int x2) {
	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 *__restrict priTable = mpPriTable;

	uint8 *dst = mpDst + x1*2;
	const uint8 *src = mpMergeBuffer + x1;

	int w = x2 - x1;
	int w4 = w >> 2;

	for(int i=0; i<w4; ++i) {
		dst[0] = dst[1] = colorTable[priTable[src[0]]];
		dst[2] = dst[3] = colorTable[priTable[src[1]]];
		dst[4] = dst[5] = colorTable[priTable[src[2]]];
		dst[6] = dst[7] = colorTable[priTable[src[3]]];
		src += 4;
		dst += 8;
	}

	for(int i=w & 3; i; --i) {
		dst[0] = dst[1] = colorTable[priTable[src[0]]];
		++src;
		dst += 2;
	}
}

void ATGTIARenderer::RenderMode8(int x1, int x2) {
	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 *__restrict priTable = mpPriTable;

	const uint8 *lumasrc = &mpAnticBuffer[x1];
	uint8 *dst = mpDst + x1*2;
	const uint8 *src = mpMergeBuffer + x1;

	const uint8 luma1 = mpColorTable[5] & 0xf;

	const uint32 andtab[4]={
		0xffff,
		0xf0ff,
		0xfff0,
		0xf0f0,
	};

	const uint32 addtab[4]={
		0x0000,
		(uint32)luma1 << 8,
		luma1,
		(uint32)luma1 * 0x0101
	};

	int w = x2 - x1;
	while(w--) {
		uint32 lb = *lumasrc++ & 3;

		uint32 c0 = (uint32)colorTable[priTable[*src++]];

		c0 += (c0 << 8);

		*(uint16 *)dst = (c0 & andtab[lb]) + addtab[lb];
		dst += 2;
	}
}

void ATGTIARenderer::RenderMode9(int x1, int x2) {
	static const uint8 kPlayerMaskLookup[16]={0xff};

	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 *__restrict priTable = mpPriTable;

	uint8 *dst = mpDst + x1*2;
	const uint8 *src = mpMergeBuffer + x1;

	// 1 color / 16 luma mode
	//
	// In this mode, PF0-PF3 are forced off, so no playfield collisions ever register
	// and the playfield always registers as the background color. Luminance is
	// ORed in after the priority logic, but its substitution is gated by all P/M bits
	// and so it does not affect players or missiles. It does, however, affect PF3 if
	// the fifth player is enabled.

	int w = x2 - x1;

	while(w--) {
		uint8 code0 = *src++ & (P0|P1|P2|P3|PF3);
		uint8 pri0 = colorTable[priTable[code0]];

		const uint8 *lumasrc = &mpAnticBuffer[x1++ & ~1];
		uint8 l1 = (lumasrc[0] << 2) + lumasrc[1];

		uint8 c4 = pri0 | (l1 & kPlayerMaskLookup[code0 >> 4]);

		dst[0] = dst[1] = c4;
		dst += 2;
	}
}

void ATGTIARenderer::RenderMode10(int x1, int x2) {
	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 *__restrict priTable = mpPriTable;

	uint8 *dst = mpDst + x1*2;
	const uint8 *src = mpMergeBuffer + x1;

	// 9 colors
	//
	// This mode works by using AN0-AN1 to trigger either the playfield or the player/missle
	// bits going into the priority logic. This means that when player colors are used, the
	// playfield takes the same priority as that player. Playfield collisions are triggered
	// only for PF0-PF3; P0-P3 colors coming from the playfield do not trigger collisions.

	static const uint8 kMode10Lookup[16]={
		P0,
		P1,
		P2,
		P3,
		PF0,
		PF1,
		PF2,
		PF3,
		0,
		0,
		0,
		0,
		PF0,
		PF1,
		PF2,
		PF3
	};

	int w = x2 - x1;

	while(w--) {
		const uint8 *lumasrc = &mpAnticBuffer[(x1++ - 1) & ~1];
		uint8 l1 = lumasrc[0]*4 + lumasrc[1];

		uint8 c4 = kMode10Lookup[l1];

		dst[0] = dst[1] = colorTable[priTable[c4 | (*src++ & 0xf8)]];
		dst += 2;
	}
}

void ATGTIARenderer::RenderMode11(int x1, int x2) {
	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 *__restrict priTable = mpPriTable;

	uint8 *dst = mpDst + x1*2;
	const uint8 *src = mpMergeBuffer + x1;

	// 16 colors / 1 luma
	//
	// In this mode, PF0-PF3 are forced off, so no playfield collisions ever register
	// and the playfield always registers as the background color. Chroma is
	// ORed in after the priority logic, but its substitution is gated by all P/M bits
	// and so it does not affect players or missiles. It does, however, affect PF3 if
	// the fifth player is enabled.

	static const uint8 kMode11Lookup[16][2][2]={
		{{0xff,0xff},{0xff,0xf0}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}}
	};

	int w = x2 - x1;

	while(w--) {
		const uint8 code0 = *src++ & (P0|P1|P2|P3|PF3);
		uint8 pri0 = colorTable[priTable[code0]];

		const uint8 *lumasrc = &mpAnticBuffer[x1++ & ~1];
		uint8 l0 = (lumasrc[0] << 6) + (lumasrc[1] << 4);

		uint8 c0 = (pri0 | (l0 & kMode11Lookup[code0 >> 4][l0 == 0][0])) & kMode11Lookup[code0 >> 4][l0 == 0][1];

		dst[0] = dst[1] = c0;
		dst += 2;
	}
}
