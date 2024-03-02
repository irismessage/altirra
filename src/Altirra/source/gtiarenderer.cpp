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
#include "savestate.h"

using namespace ATGTIA;

namespace {
	static const uint8 kAnalysisColorTable[]={
		// players
		0x1a,
		0x5a,
		0x7a,
		0x9a,

		// playfields
		0x03,
		0x07,
		0x0b,
		0x0f,

		// background
		0x01,

		// black
		0x00,

		// p0+p1, p2+p3
		0x3a,
		0x8a,

		// pf0+p0/p1/p0p1
		0x13,
		0x53,
		0x33,

		// pf1+p0/p1/p0p1
		0x17,
		0x57,
		0x37,

		// pf2+p2/p3/p2p3
		0x1b,
		0x5b,
		0x3b,

		// pf3+p2/p3/p2p3
		0x1f,
		0x5f,
		0x3f,
	};
}

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

	InitPriorityTables();

	mpColorTable = mColorTable;
	mpPriTable = mPriorityTables[0];
}

ATGTIARenderer::~ATGTIARenderer() {
}

void ATGTIARenderer::SetAnalysisMode(bool enable) {
	mpColorTable = enable ? kAnalysisColorTable : mColorTable;
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

	int w = x2 - x1;
	while(w--) {
		uint8 lb = *lumasrc++;

		uint8 c0 = colorTable[priTable[*src++]];
		uint8 c1 = c0;

		if (lb & 2) c0 = (c0 & 0xf0) + luma1;
		if (lb & 1) c1 = (c1 & 0xf0) + luma1;

		dst[0] = c0;
		dst[1] = c1;
		dst += 2;
		++x1;
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

void ATGTIARenderer::InitPriorityTables() {
	// Priority table initialization
	//
	// The priority logic in the GTIA works as follows:
	//
	//	SP0 = P0 * /(PF01*PRI23) * /(PRI2*PF23)
	//	SP1 = P1 * /(PF01*PRI23) * /(PRI2*PF23) * (/P0 + MULTI)
	//	SP2 = P2 * /P01 * /(PF23*PRI12) * /(PF01*/PRI0)
	//	SP3 = P3 * /P01 * /(PF23*PRI12) * /(PF01*/PRI0) * (/P2 + MULTI)
	//	SF0 = PF0 * /(P23*PRI0) * /(P01*PRI01) * /SF3
	//	SF1 = PF1 * /(P23*PRI0) * /(P01*PRI01) * /SF3
	//	SF2 = PF2 * /(P23*PRI03) * /(P01*/PRI2) * /SF3
	//	SF3 = PF3 * /(P23*PRI03) * /(P01*/PRI2)
	//	SB  = /P01 * /P23 * /PF01 * /PF23
	//
	// Normally, both players and missiles contribute to P0-P3. If fifth player enable
	// is set, missiles 0-3 contribute to PF3 instead of P0-P3.
	//
	// There are a couple of notable anomalies in the above:
	//
	//	* When all priority bits are zero, the result is NOT all black as
	//	  the hardware manual implies. In fact, priority breaks and players
	//	  0-1 and playfields 0-1 and 3 can mix.
	//
	//	* The fifth player always has priority over all playfields.
	//
	// The result is that there are 24 colors possible:
	//
	//	* black
	//	* BAK
	//	* P0 - P3
	//	* PF0 - PF3
	//	* P0 | P1
	//	* P2 | P3
	//	* PF0 | P0
	//	* PF0 | P1
	//	* PF0 | P0 | P1
	//	* PF1 | P0
	//	* PF1 | P1
	//	* PF1 | P0 | P1
	//	* PF2 | P2
	//	* PF2 | P3
	//	* PF2 | P2 | P3
	//	* PF3 | P2
	//	* PF3 | P3
	//	* PF3 | P2 | P3
	//
	// A maximum of 23 of these can be accessed at a time, via a PRIOR setting of
	// xxx10000. xxx00000 gets to 17, and the rest of the illegal modes access
	// either 10 or 12 through the addition of black.

	memset(mPriorityTables, 0, sizeof mPriorityTables);
	
	for(int prior=0; prior<32; ++prior) {
		const bool multi = (prior & 16) != 0;
		const bool pri0 = (prior & 1) != 0;
		const bool pri1 = (prior & 2) != 0;
		const bool pri2 = (prior & 4) != 0;
		const bool pri3 = (prior & 8) != 0;
		const bool pri01 = pri0 | pri1;
		const bool pri12 = pri1 | pri2;
		const bool pri23 = pri2 | pri3;
		const bool pri03 = pri0 | pri3;

		for(int i=0; i<256; ++i) {
			// The way the ANx decode logic works in GTIA, there is no possibility of any
			// conflict between playfield bits except for PF3, which can conflict due to
			// being reused for the fifth player. Therefore, we remap the table so that
			// any conflicts are resolved in favor of the higher playfield.
			static const uint8 kPlayfieldPriorityTable[8]={
				0,
				1,
				2,
				2,
				4,
				4,
				4,
				4,
			};

			const uint8 v = kPlayfieldPriorityTable[i & 7];

			const bool pf0 = (v & 1) != 0;
			const bool pf1 = (v & 2) != 0;
			const bool pf2 = (v & 4) != 0;
			const bool pf3 = (i & 8) != 0;
			const bool p0 = (i & 16) != 0;
			const bool p1 = (i & 32) != 0;
			const bool p2 = (i & 64) != 0;
			const bool p3 = (i & 128) != 0;

			const bool p01 = p0 | p1;
			const bool p23 = p2 | p3;
			const bool pf01 = pf0 | pf1;
			const bool pf23 = pf2 | pf3;

			const bool sp0 = p0 & !(pf01 & pri23) & !(pri2 & pf23);
			const bool sp1 = p1 & !(pf01 & pri23) & !(pri2 & pf23) & (!p0 | multi);
			const bool sp2 = p2 & !p01 & !(pf23 & pri12) & !(pf01 & !pri0);
			const bool sp3 = p3 & !p01 & !(pf23 & pri12) & !(pf01 & !pri0) & (!p2 | multi);

			const bool sf3 = pf3 & !(p23 & pri03) & !(p01 & !pri2);
			const bool sf2 = pf2 & !(p23 & pri03) & !(p01 & !pri2) & !sf3;
			const bool sf1 = pf1 & !(p23 & pri0) & !(p01 & pri01) & !sf3;
			const bool sf0 = pf0 & !(p23 & pri0) & !(p01 & pri01) & !sf3;

			const bool sb = !p01 & !p23 & !pf01 & !pf23;

			int out = 0;
			if (sf0) out += 0x001;
			if (sf1) out += 0x002;
			if (sf2) out += 0x004;
			if (sf3) out += 0x008;
			if (sp0) out += 0x010;
			if (sp1) out += 0x020;
			if (sp2) out += 0x040;
			if (sp3) out += 0x080;
			if (sb ) out += 0x100;

			uint8 c;

			switch(out) {
				default:
					VDASSERT(!"Invalid priority table decode detected.");
				case 0x000:		c = kColorBlack;	break;
				case 0x001:		c = kColorPF0;		break;
				case 0x002:		c = kColorPF1;		break;
				case 0x004:		c = kColorPF2;		break;
				case 0x008:		c = kColorPF3;		break;
				case 0x010:		c = kColorP0;		break;
				case 0x011:		c = kColorPF0P0;	break;
				case 0x012:		c = kColorPF1P0;	break;
				case 0x020:		c = kColorP1;		break;
				case 0x021:		c = kColorPF0P1;	break;
				case 0x022:		c = kColorPF1P1;	break;
				case 0x030:		c = kColorP0P1;		break;
				case 0x031:		c = kColorPF0P0P1;	break;
				case 0x032:		c = kColorPF1P0P1;	break;
				case 0x040:		c = kColorP2;		break;
				case 0x044:		c = kColorPF2P2;	break;
				case 0x048:		c = kColorPF3P2;	break;
				case 0x080:		c = kColorP3;		break;
				case 0x084:		c = kColorPF2P3;	break;
				case 0x088:		c = kColorPF3P3;	break;
				case 0x0c0:		c = kColorP2P3;		break;
				case 0x0c4:		c = kColorPF2P2P3;	break;
				case 0x0c8:		c = kColorPF3P2P3;	break;
				case 0x100:		c = kColorBAK;		break;
			}

			mPriorityTables[prior][i] = c;
		}
	}
}