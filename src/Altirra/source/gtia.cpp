//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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
#include <vd2/Riza/display.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/triblt.h>
#include "gtia.h"
#include "console.h"
#include "artifacting.h"
#include "savestate.h"

class ATFrameTracker : public vdrefcounted<IVDRefCount> {
public:
	ATFrameTracker() : mActiveFrames(0) { }
	VDAtomicInt mActiveFrames;
};

class ATFrameBuffer : public VDVideoDisplayFrame {
public:
	ATFrameBuffer(ATFrameTracker *tracker) : mpTracker(tracker) {
		++mpTracker->mActiveFrames;
	}

	~ATFrameBuffer() {
		--mpTracker->mActiveFrames;
	}

	const vdrefptr<ATFrameTracker> mpTracker;
	VDPixmapBuffer mBuffer;
};

namespace {
	const uint8 PF0		= 0x01;
	const uint8 PF1		= 0x02;
	const uint8 PF01	= 0x03;
	const uint8 PF2		= 0x04;
	const uint8 PF3		= 0x08;
	const uint8 PF23	= 0x0c;
	const uint8 PF		= 0x0f;
	const uint8 P0		= 0x10;
	const uint8 P1		= 0x20;
	const uint8 P01		= 0x30;
	const uint8 P2		= 0x40;
	const uint8 P3		= 0x80;
	const uint8 P23		= 0xc0;

	static const uint8 kMissileTables[2][16]={
		{ 0, P0, P1, P0|P1, P2, P2|P0, P2|P1, P2|P0|P1, P3, P0|P3, P1|P3, P0|P1|P3, P2|P3, P3|P2|P0, P3|P2|P1, P3|P2|P0|P1 },
		{ 0, PF3, PF3, PF3, PF3, PF3, PF3, PF3, PF3, PF3, PF3, PF3, PF3, PF3, PF3, PF3 }
	};

	enum {
		kColorP0		= 0,
		kColorP1,
		kColorP2,
		kColorP3,
		kColorPF0,
		kColorPF1,
		kColorPF2,
		kColorPF3,
		kColorBAK,
		kColorBlack,
		kColorP0P1,
		kColorP2P3,
		kColorPF0P0,
		kColorPF0P1,
		kColorPF0P0P1,
		kColorPF1P0,
		kColorPF1P1,
		kColorPF1P0P1,
		kColorPF2P2,
		kColorPF2P3,
		kColorPF2P2P3,
		kColorPF3P2,
		kColorPF3P3,
		kColorPF3P2P3
	};

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

	const uint8 kPlayerMaskLookup[16]={0xff};
}

ATGTIAEmulator::ATGTIAEmulator()
	: mpFrameTracker(new ATFrameTracker)
	, mArtifactMode(0)
	, mArtifactModeThisFrame(0)
	, mbShowCassetteIndicator(false)
	, mPreArtifactFrameBuffer(456*262)
	, mpArtifactingEngine(new ATArtifactingEngine)
{
	mPreArtifactFrame.data = mPreArtifactFrameBuffer.data();
	mPreArtifactFrame.pitch = 456;
	mPreArtifactFrame.palette = mPalette;
	mPreArtifactFrame.data2 = NULL;
	mPreArtifactFrame.data3 = NULL;
	mPreArtifactFrame.pitch2 = 0;
	mPreArtifactFrame.pitch3 = 0;
	mPreArtifactFrame.w = 456;
	mPreArtifactFrame.h = 262;
	mPreArtifactFrame.format = nsVDPixmap::kPixFormat_Pal8;

	mpFrameTracker->AddRef();

	mCONSOL = 7;
	mPRIOR = 0;
	mTRIG[0] = 0x01;
	mTRIG[1] = 0x01;
	mTRIG[2] = 0x01;
	mTRIG[3] = 0x01;
	SetAnalysisMode(kAnalyzeNone);
	mStatusFlags = 0;
	mStickyStatusFlags = 0;
	mCassettePos = 0;
	memset(mColorTable, 0, sizeof mColorTable);
	memset(mPlayerCollFlags, 0, sizeof mPlayerCollFlags);
	memset(mMissileCollFlags, 0, sizeof mMissileCollFlags);

	mPlayerSize[0] = 0;
	mPlayerSize[1] = 0;
	mPlayerSize[2] = 0;
	mPlayerSize[3] = 0;
	mMissileSize = 0;
	mPlayerWidth[0] = 8;
	mPlayerWidth[1] = 8;
	mPlayerWidth[2] = 8;
	mPlayerWidth[3] = 8;
	mMissileWidth[0] = 2;
	mMissileWidth[1] = 2;
	mMissileWidth[2] = 2;
	mMissileWidth[3] = 2;

	mpPriTable = mPriorityTables[0];
	mpMissileTable = kMissileTables[0];

	mbTurbo = false;
	mbVideoDelayed = false;
	mbForcedBorder = false;

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
	//	* The fifth player always has priority over all playfields.\
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

	static const int kBasePriorities[4][4]={
		{ P01, P23, PF, PF },
		{ P01, PF, PF, P23 },
		{ PF, PF, P01, P23 },
		{ PF01, P01, P23, PF23 },
	};

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

	SetPALMode(false);
}

ATGTIAEmulator::~ATGTIAEmulator() {
	if (mpFrameTracker) {
		mpFrameTracker->Release();
		mpFrameTracker = NULL;
	}

	delete mpArtifactingEngine;
}

void ATGTIAEmulator::Init(IATGTIAEmulatorConnections *conn) {
	mpConn = conn;
	mX = 0;
	mY = 0;

	memset(mPlayerPos, 0, sizeof mPlayerPos);
	memset(mMissilePos, 0, sizeof mMissilePos);
	memset(mPlayerSize, 0, sizeof mPlayerSize);
	memset(mPlayerData, 0, sizeof mPlayerData);
	mMissileData = 0;
	memset(mPMColor, 0, sizeof mPMColor);
	memset(mPFColor, 0, sizeof mPFColor);
	RecomputePalette();
}

void ATGTIAEmulator::AdjustColors(double baseDelta, double rangeDelta) {
	mPaletteColorBase += baseDelta;
	mPaletteColorRange += rangeDelta;
	RecomputePalette();
}

void ATGTIAEmulator::SetAnalysisMode(AnalysisMode mode) {
	mAnalysisMode = mode;
	mpColorTable = mode ? kAnalysisColorTable : mColorTable;
}

void ATGTIAEmulator::SetVideoOutput(IVDVideoDisplay *pDisplay) {
	mpDisplay = pDisplay;

	if (!pDisplay) {
		mpFrame = NULL;
		mpDst = NULL;
	}
}

void ATGTIAEmulator::SetCassettePosition(float pos) {
	mCassettePos = pos;
}

void ATGTIAEmulator::SetPALMode(bool enabled) {
	mbPALMode = enabled;

	// These constants, and the algorithm in RecomputePalette(), come from an AtariAge post
	// by Olivier Galibert. Sadly, the PAL palette doesn't quite look right, so it's disabled
	// for now.

	// PAL palette doesn't look quite right.
//	if (enabled) {
//		mPaletteColorBase = -58.0/360.0;
//		mPaletteColorRange = 14.0;
//	} else {
		mPaletteColorBase = -15/360.0;
		mPaletteColorRange = 14.4;
//	}

	RecomputePalette();
}

void ATGTIAEmulator::SetConsoleSwitch(uint8 c, bool set) {
	mCONSOL &= ~c;

	if (!set)			// bit is active low
		mCONSOL |= c;
}

void ATGTIAEmulator::DumpStatus() {
	for(int i=0; i<4; ++i) {
		ATConsolePrintf("Player  %d: color = %02x, pos = %02x, size=%d, data = %02x\n"
			, i
			, mPMColor[i]
			, mPlayerPos[i]
			, mPlayerSize[i]
			, mPlayerData[i]
			);
	}

	for(int i=0; i<4; ++i) {
		ATConsolePrintf("Missile %d: color = %02x, pos = %02x, size=%d, data = %02x\n"
			, i
			, mPRIOR & 0x10 ? mPFColor[3] : mPMColor[i]
			, mMissilePos[i]
			, (mMissileSize >> (2*i)) & 3
			, (mMissileData >> (2*i)) & 3
			);
	}

	ATConsolePrintf("Playfield colors: %02x | %02x %02x %02x %02x\n"
		, mPFBAK
		, mPFColor[0]
		, mPFColor[1]
		, mPFColor[2]
		, mPFColor[3]);

	ATConsolePrintf("PRIOR:  %02x (pri=%2d%s%s %s)\n"
		, mPRIOR
		, mPRIOR & 15
		, mPRIOR & 0x10 ? ", pl5" : ""
		, mPRIOR & 0x20 ? ", multicolor" : ""
		, (mPRIOR & 0xc0) == 0x00 ? ", normal"
		: (mPRIOR & 0xc0) == 0x40 ? ", 1 color / 16 lumas"
		: (mPRIOR & 0xc0) == 0x80 ? ", 9 colors"
		: ", 16 colors / 1 luma");

	ATConsolePrintf("VDELAY: %02x\n", mVDELAY);

	ATConsolePrintf("GRACTL: %02x%s%s%s\n"
		, mGRACTL
		, mGRACTL & 0x04 ? ", latched" : ""
		, mGRACTL & 0x02 ? ", player DMA" : ""
		, mGRACTL & 0x01 ? ", missile DMA" : ""
		);

	ATConsolePrintf("CONSOL: %02x%s%s%s%s\n"
		, mCONSOL
		, mCONSOL & 0x08 ? ", speaker" : ""
		, mCONSOL & 0x04 ? ", option" : ""
		, mCONSOL & 0x02 ? ", select" : ""
		, mCONSOL & 0x01 ? ", start" : ""
		);

	uint8 v;
	for(int i=0; i<4; ++i) {
		v = ReadByte(0x00 + i);
		ATConsolePrintf("M%cPF:%s%s%s%s\n"
			, '0' + i
			, v & 0x01 ? " PF0" : ""
			, v & 0x02 ? " PF1" : ""
			, v & 0x04 ? " PF2" : ""
			, v & 0x08 ? " PF3" : "");
	}

	for(int i=0; i<4; ++i) {
		v = ReadByte(0x04 + i);
		ATConsolePrintf("P%cPF:%s%s%s%s\n"
			, '0' + i
			, v & 0x01 ? " PF0" : ""
			, v & 0x02 ? " PF1" : ""
			, v & 0x04 ? " PF2" : ""
			, v & 0x08 ? " PF3" : "");
	}

	for(int i=0; i<4; ++i) {
		v = ReadByte(0x08 + i);
		ATConsolePrintf("M%cPL:%s%s%s%s\n"
			, '0' + i
			, v & 0x01 ? " P0" : ""
			, v & 0x02 ? " P1" : ""
			, v & 0x04 ? " P2" : ""
			, v & 0x08 ? " P3" : "");
	}

	for(int i=0; i<4; ++i) {
		v = ReadByte(0x0c + i);
		ATConsolePrintf("P%cPL:%s%s%s%s\n"
			, '0' + i
			, v & 0x01 ? " PF0" : ""
			, v & 0x02 ? " PF1" : ""
			, v & 0x04 ? " PF2" : ""
			, v & 0x08 ? " PF3" : "");
	}
}

void ATGTIAEmulator::LoadState(ATSaveStateReader& reader) {
	mPlayerPos[0] = reader.ReadUint8();
	mPlayerPos[1] = reader.ReadUint8();
	mPlayerPos[2] = reader.ReadUint8();
	mPlayerPos[3] = reader.ReadUint8();
	mMissilePos[0] = reader.ReadUint8();
	mMissilePos[1] = reader.ReadUint8();
	mMissilePos[2] = reader.ReadUint8();
	mMissilePos[3] = reader.ReadUint8();
	mPlayerSize[0] = reader.ReadUint8();
	mPlayerSize[1] = reader.ReadUint8();
	mPlayerSize[2] = reader.ReadUint8();
	mPlayerSize[3] = reader.ReadUint8();
	mMissileSize = reader.ReadUint8();
	mPlayerData[0] = reader.ReadUint8();
	mPlayerData[1] = reader.ReadUint8();
	mPlayerData[2] = reader.ReadUint8();
	mPlayerData[3] = reader.ReadUint8();
	mDelayedPlayerData[0] = reader.ReadUint8();
	mDelayedPlayerData[1] = reader.ReadUint8();
	mDelayedPlayerData[2] = reader.ReadUint8();
	mDelayedPlayerData[3] = reader.ReadUint8();
	mMissileData = reader.ReadUint8();
	mDelayedMissileData = reader.ReadUint8();
	mPMColor[0] = reader.ReadUint8();
	mPMColor[1] = reader.ReadUint8();
	mPMColor[2] = reader.ReadUint8();
	mPMColor[3] = reader.ReadUint8();
	mPFColor[0] = reader.ReadUint8();
	mPFColor[1] = reader.ReadUint8();
	mPFColor[2] = reader.ReadUint8();
	mPFColor[3] = reader.ReadUint8();
	mPFBAK = reader.ReadUint8();
	mPRIOR = reader.ReadUint8();
	mVDELAY = reader.ReadUint8();
	mGRACTL = reader.ReadUint8();
	mCONSOL = reader.ReadUint8();

	mPlayerCollFlags[0] = reader.ReadUint8() & 15;
	mPlayerCollFlags[1] = reader.ReadUint8() & 15;
	mPlayerCollFlags[2] = reader.ReadUint8() & 15;
	mPlayerCollFlags[3] = reader.ReadUint8() & 15;
	mMissileCollFlags[0] = reader.ReadUint8() & 15;
	mMissileCollFlags[1] = reader.ReadUint8() & 15;
	mMissileCollFlags[2] = reader.ReadUint8() & 15;
	mMissileCollFlags[3] = reader.ReadUint8() & 15;

	mbHiresMode = reader.ReadBool();

	mpConn->GTIASetSpeaker(0 != (mCONSOL & 8));

	mColorTable[kColorP0] = mPMColor[0];
	mColorTable[kColorP1] = mPMColor[1];
	mColorTable[kColorP2] = mPMColor[2];
	mColorTable[kColorP3] = mPMColor[3];
	mColorTable[kColorPF0] = mPFColor[0];
	mColorTable[kColorPF1] = mPFColor[1];
	mColorTable[kColorPF2] = mPFColor[2];
	mColorTable[kColorPF3] = mPFColor[3];

	mColorTable[kColorP0P1] = mPMColor[0] | mPMColor[1];
	mColorTable[kColorP2P3] = mPMColor[1] | mPMColor[3];

	mColorTable[kColorPF0P0]	= mPFColor[0] | mPMColor[0];
	mColorTable[kColorPF0P1]	= mPFColor[0] | mPMColor[1];
	mColorTable[kColorPF0P0P1]	= mPFColor[0] | mColorTable[kColorP0P1];
	mColorTable[kColorPF1P0]	= mPFColor[1] | mPMColor[0];
	mColorTable[kColorPF1P1]	= mPFColor[1] | mPMColor[1];
	mColorTable[kColorPF1P0P1]	= mPFColor[1] | mColorTable[kColorP0P1];
	mColorTable[kColorPF2P2]	= mPFColor[2] | mPMColor[2];
	mColorTable[kColorPF2P3]	= mPFColor[2] | mPMColor[3];
	mColorTable[kColorPF2P2P3]	= mPFColor[2] | mColorTable[kColorP2P3];
	mColorTable[kColorPF3P2]	= mPFColor[3] | mPMColor[2];
	mColorTable[kColorPF3P3]	= mPFColor[3] | mPMColor[3];
	mColorTable[kColorPF3P2P3]	= mPFColor[3] | mColorTable[kColorP2P3];
	mColorTable[kColorBAK] = mPFBAK;

	mpPriTable = mPriorityTables[(mPRIOR & 15) + (mPRIOR&32 ? 16 : 0)];

	if (mPRIOR & 16)
		mpMissileTable = kMissileTables[1];
	else
		mpMissileTable = kMissileTables[0];

	mbVideoDelayed = ((mPRIOR & 0xC0) == 0x80) != 0;
}

void ATGTIAEmulator::SaveState(ATSaveStateWriter& writer) {
	writer.WriteUint8(mPlayerPos[0]);
	writer.WriteUint8(mPlayerPos[1]);
	writer.WriteUint8(mPlayerPos[2]);
	writer.WriteUint8(mPlayerPos[3]);
	writer.WriteUint8(mMissilePos[0]);
	writer.WriteUint8(mMissilePos[1]);
	writer.WriteUint8(mMissilePos[2]);
	writer.WriteUint8(mMissilePos[3]);
	writer.WriteUint8(mPlayerSize[0]);
	writer.WriteUint8(mPlayerSize[1]);
	writer.WriteUint8(mPlayerSize[2]);
	writer.WriteUint8(mPlayerSize[3]);
	writer.WriteUint8(mMissileSize);
	writer.WriteUint8(mPlayerData[0]);
	writer.WriteUint8(mPlayerData[1]);
	writer.WriteUint8(mPlayerData[2]);
	writer.WriteUint8(mPlayerData[3]);
	writer.WriteUint8(mDelayedPlayerData[0]);
	writer.WriteUint8(mDelayedPlayerData[1]);
	writer.WriteUint8(mDelayedPlayerData[2]);
	writer.WriteUint8(mDelayedPlayerData[3]);
	writer.WriteUint8(mMissileData);
	writer.WriteUint8(mDelayedMissileData);
	writer.WriteUint8(mPMColor[0]);
	writer.WriteUint8(mPMColor[1]);
	writer.WriteUint8(mPMColor[2]);
	writer.WriteUint8(mPMColor[3]);
	writer.WriteUint8(mPFColor[0]);
	writer.WriteUint8(mPFColor[1]);
	writer.WriteUint8(mPFColor[2]);
	writer.WriteUint8(mPFColor[3]);
	writer.WriteUint8(mPFBAK);
	writer.WriteUint8(mPRIOR);
	writer.WriteUint8(mVDELAY);
	writer.WriteUint8(mGRACTL);
	writer.WriteUint8(mCONSOL);

	writer.WriteUint8(mPlayerCollFlags[0]);
	writer.WriteUint8(mPlayerCollFlags[1]);
	writer.WriteUint8(mPlayerCollFlags[2]);
	writer.WriteUint8(mPlayerCollFlags[3]);
	writer.WriteUint8(mMissileCollFlags[0]);
	writer.WriteUint8(mMissileCollFlags[1]);
	writer.WriteUint8(mMissileCollFlags[2]);
	writer.WriteUint8(mMissileCollFlags[3]);

	writer.WriteBool(mbHiresMode);
}

bool ATGTIAEmulator::BeginFrame(bool force) {
	if (mpFrame)
		return true;

	if (!mpDisplay)
		return true;

	if (!mpDisplay->RevokeBuffer(false, ~mpFrame)) {
		if (mpFrameTracker->mActiveFrames < 3) {
			ATFrameBuffer *fb = new ATFrameBuffer(mpFrameTracker);
			mpFrame = fb;

			fb->mPixmap.format = 0;
			fb->mbAllowConversion = true;
			fb->mbInterlaced = false;
			fb->mFlags = IVDVideoDisplay::kAllFields | IVDVideoDisplay::kVSync;
		} else if (!mbTurbo && !force)
			return false;
	}

	if (mpFrame) {
		ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);

		mArtifactModeThisFrame = mArtifactMode;

		int format = mArtifactMode ? nsVDPixmap::kPixFormat_XRGB8888 : nsVDPixmap::kPixFormat_Pal8;

		if (mpFrame->mPixmap.format != format) {
			if (mArtifactMode)
				fb->mBuffer.init(456, 262, format);
			else
				fb->mBuffer.init(456, 262, format);
		}

		fb->mPixmap = fb->mBuffer;
		fb->mPixmap.palette = mPalette;

		mPreArtifactFrameVisible = mPreArtifactFrame;

		if (!mAnalysisMode && !mbForcedBorder) {
			ptrdiff_t offset = 34*2;

			if (mArtifactMode)
				offset *= 4;

			offset += fb->mPixmap.pitch * 8;

			fb->mPixmap.data = (char *)fb->mPixmap.data + offset;
			fb->mPixmap.w = (222 - 34)*2;
			fb->mPixmap.h = 240;

			mPreArtifactFrameVisible.data = (char *)mPreArtifactFrameVisible.data + 34*2 + mPreArtifactFrameVisible.pitch * 8;
			mPreArtifactFrameVisible.w = fb->mPixmap.w;
			mPreArtifactFrameVisible.h = fb->mPixmap.h;

		}
	}

	return true;
}

void ATGTIAEmulator::BeginScanline(int y, bool hires) {
	mbANTICHiresMode = hires;
	mbHiresMode = hires && !(mPRIOR & 0xc0);

	mpDst = NULL;
	
	mY = y;
	mX = 0;
	mLastSyncX = 0;

	if (mpFrame) {
		ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);

		if (mArtifactModeThisFrame)
			mpDst = &mPreArtifactFrameBuffer[y * 456];
		else
			mpDst = (uint8 *)fb->mBuffer.data + y * fb->mBuffer.pitch;

		if (y < 262) {
			memset(mpDst, 0, 456);
		} else {
			mpDst = NULL;
		}
	}
	memset(mMergeBuffer, 0, sizeof mMergeBuffer);
	memset(mAnticData, 0, sizeof mAnticData);

	mPlayerTriggerPos[0] = mPlayerPos[0] < 34 ? mPlayerPos[0] : 0;
	mPlayerTriggerPos[1] = mPlayerPos[1] < 34 ? mPlayerPos[1] : 0;
	mPlayerTriggerPos[2] = mPlayerPos[2] < 34 ? mPlayerPos[2] : 0;
	mPlayerTriggerPos[3] = mPlayerPos[3] < 34 ? mPlayerPos[3] : 0;
	mMissileTriggerPos[0] = mMissilePos[0] < 34 ? mMissilePos[0] : 0;
	mMissileTriggerPos[1] = mMissilePos[1] < 34 ? mMissilePos[1] : 0;
	mMissileTriggerPos[2] = mMissilePos[2] < 34 ? mMissilePos[2] : 0;
	mMissileTriggerPos[3] = mMissilePos[3] < 34 ? mMissilePos[3] : 0;
}

void ATGTIAEmulator::EndScanline(uint8 dlControl) {
	// obey VBLANK
	if (mY < 8 || mY >= 248) {
		if (mpDst)
			memset(mpDst, mColorTable[kColorBAK], 456);
	} else {
		Sync();

		mPlayerData[0] = mDelayedPlayerData[0];
		mPlayerData[1] = mDelayedPlayerData[1];
		mPlayerData[2] = mDelayedPlayerData[2];
		mPlayerData[3] = mDelayedPlayerData[3];
		mMissileData = mDelayedMissileData;
	}

	if (!mpDst)
		return;

	switch(mAnalysisMode) {
		case kAnalyzeNone:
			break;
		case kAnalyzeColors:
			for(int i=0; i<9; ++i)
				mpDst[i*2+0] = mpDst[i*2+1] = mColorTable[i];
			break;
		case kAnalyzeDList:
			mpDst[0] = mpDst[1] = (dlControl & 0x80) ? 0x1f : 0x00;
			mpDst[2] = mpDst[3] = (dlControl & 0x40) ? 0x3f : 0x00;
			mpDst[4] = mpDst[5] = (dlControl & 0x20) ? 0x5f : 0x00;
			mpDst[6] = mpDst[7] = (dlControl & 0x10) ? 0x7f : 0x00;
			mpDst[8] = mpDst[9] = mpDst[10] = mpDst[11] = ((dlControl & 0x0f) << 4) + 15;
			break;
	}
}

void ATGTIAEmulator::UpdatePlayer(int index, uint8 byte) {
	if (mGRACTL & 2) {
		mDelayedPlayerData[index] = byte;
		if (!(mVDELAY & (0x10 << index)))
			mPlayerData[index] = byte;
	}
}

void ATGTIAEmulator::UpdateMissile(uint8 byte) {
	if (mGRACTL & 1) {
		mDelayedMissileData = byte;

		uint8 mask = 0;
		if (mVDELAY & 1)
			mask |= 3;
		if (mVDELAY & 2)
			mask |= 0x0c;
		if (mVDELAY & 4)
			mask |= 0x30;
		if (mVDELAY & 8)
			mask |= 0xc0;

		mMissileData ^= (mMissileData ^ byte) & ~mask;
	}
}

void ATGTIAEmulator::UpdatePlayfield160(uint32 x, uint8 byte) {
	uint8 *dst = &mMergeBuffer[x*2];

	dst[0] = (byte >>  4) & 15;
	dst[1] = (byte      ) & 15;
}

void ATGTIAEmulator::UpdatePlayfield320(uint32 x, uint8 byte) {
	uint8 *dstx = &mMergeBuffer[x];
	dstx[0] = PF2;
	dstx[1] = PF2;

	uint8 *dst = &mAnticData[x];
	dst[0] = (byte >> 2) & 3;
	dst[1] = (byte >> 0) & 3;
}

void ATGTIAEmulator::EndPlayfield() {
}

namespace {
	uint32 Expand(uint8 x, uint8 mode) {
		static const uint8 tab2[16]={
			0x00,
			0x03,
			0x0c,
			0x0f,
			0x30,
			0x33,
			0x3c,
			0x3f,
			0xc0,
			0xc3,
			0xcc,
			0xcf,
			0xf0,
			0xf3,
			0xfc,
			0xff,
		};
		static const uint16 tab4[16]={
			0x0000,
			0x000f,
			0x00f0,
			0x00ff,
			0x0f00,
			0x0f0f,
			0x0ff0,
			0x0fff,
			0xf000,
			0xf00f,
			0xf0f0,
			0xf0ff,
			0xff00,
			0xff0f,
			0xfff0,
			0xffff,
		};

		switch(mode) {
			default:
				return (uint32)x << 24;
			case 1:
				return ((uint32)tab2[x >> 4] << 24) + ((uint32)tab2[x & 15] << 16);
			case 3:
				return ((uint32)tab4[x >> 4] << 16) + (uint32)tab4[x & 15];
		}
	}

	void Convert160To320(int x1, int x2, uint8 *dst, const uint8 *src) {
		static const uint8 kPriTable[16]={
			0,		// BAK
			0,		// PF0
			1,		// PF1
			1,		// PF01
			2,		// PF2
			2,		// PF02
			2,		// PF12
			2,		// PF012
			3,		// PF3
			3,		// PF03
			3,		// PF13
			3,		// PF013
			3,		// PF23
			3,		// PF023
			3,		// PF123
			3,		// PF0123
		};

		for(int x=x1; x<x2; ++x)
			dst[x] = kPriTable[src[x]];
	}

	void Convert320To160(int x1, int x2, uint8 *dst, const uint8 *src) {
		for(int x=x1; x<x2; ++x) {
			uint8 c = src[x];

			if (dst[x] & PF2)
				dst[x] = 1 << c;
		}
	}
}

void ATGTIAEmulator::Sync() {
	mpConn->GTIARequestAnticSync();

	uint32 targetX = mpConn->GTIAGetXClock();

	int x1 = mLastSyncX;
	int x2 = targetX;
	mLastSyncX = targetX;

	if (x1 < 34)
		x1 = 34;
	if (x2 > 222)
		x2 = 222;
	if (x1 >= x2)
		return;

	// obey VBLANK
	if (mY < 8 || mY >= 248)
		return;

	// convert modes if necessary
	bool needHires = mbHiresMode || (mPRIOR & 0xC0);
	if (needHires != mbANTICHiresMode) {
		if (mbANTICHiresMode)
			Convert320To160(x1, x2, mMergeBuffer, mAnticData);
		else
			Convert160To320(x1, x2, mAnticData, mMergeBuffer);
	}

	switch(mPRIOR & 0xC0) {
		case 0x00:
			break;
		case 0x80:
			{
				static const uint8 kPFTable[8]={
					0, 0, 0, 0,
					PF0, PF1, PF2, PF3,
				};

				for(int x=x1; x<x2; ++x) {
					const uint8 *ad = &mAnticData[(x - 1) & ~1];

					uint8 c = ad[0]*4 + ad[1];

					mMergeBuffer[x] = kPFTable[c & 7];
				}
			}
			break;
		case 0x40:
		case 0xC0:
			memset(mMergeBuffer + x1, 0, (x2 - x1));
			break;
	}

	uint8 *dst = mMergeBuffer;

	for(uint32 player=0; player<4; ++player) {
		uint8 data = mPlayerData[player];

		if (data) {
			int xst = x1;
			int xend;

			while(xst < x2) {
				xend = x2;

				int px = mPlayerTriggerPos[player];
				int pw = mPlayerWidth[player];
				int ptx = mPlayerPos[player]; 
				if (ptx >= xst && ptx < x2) {
					if (px + pw > xst) {
						xend = ptx;
						mPlayerTriggerPos[player] = 0;
					} else {
						px = ptx;
						mPlayerTriggerPos[player] = ptx;
					}
				}

				int px1 = px;
				int px2 = px + mPlayerWidth[player];

				if (px1 < xst)
					px1 = xst;
				if (px2 > xend)
					px2 = xend;

				if (px1 != px2) {
					uint8 *pldst = mMergeBuffer + px1;
					uint8 bit = P0 << player;
					sint32 mask = Expand(data, mPlayerSize[player]) << (px1 - px);
					sint32 mask2 = mask;
					uint8 flags = 0;
					for(int x=px2-px1; x >= 0; --x) {
						if (mask < 0) {
							flags |= *pldst;
							*pldst |= bit;
						}

						++pldst;
						mask += mask;
					}

					if (mbHiresMode) {
						flags &= ~PF;

						for(int x=px1; x < px2; ++x) {
							if (mask2 < 0) {
								if (mAnticData[x])
									flags |= PF2;
							}

							++pldst;
							mask2 += mask2;
						}
					}

					if (flags)
						mPlayerCollFlags[player] |= flags;
				}

				xst = xend;
			}
		}
	}

	if (mMissileData) {
		static const int kMissileShifts[4]={6,4,2,0};

		struct MissileRange {
			int mX;
			int mX1;
			int mX2;
			int mIndex;
			uint8 mData;
		};

		MissileRange mranges[8];
		MissileRange *mrnext = mranges;

		for(uint32 missile=0; missile<4; ++missile) {
			uint8 data = (mMissileData << kMissileShifts[missile]) & 0xc0;

			if (data) {
				int xst = x1;
				int xend;

				while(xst < x2) {
					xend = x2;

					int px = mMissileTriggerPos[missile];
					int pw = mMissileWidth[missile];
					int ptx = mMissilePos[missile]; 
					if (ptx >= xst && ptx < x2) {
						if (px + pw > xst) {
							xend = ptx;
							mMissileTriggerPos[missile] = 0;
						} else {
							px = ptx;
							mMissileTriggerPos[missile] = ptx;
						}
					}

					int px1 = px;
					int px2 = px + mMissileWidth[missile];

					if (px1 < xst)
						px1 = xst;
					if (px2 > xend)
						px2 = xend;

					if (px1 != px2) {
						mrnext->mX = px;
						mrnext->mX1 = px1;
						mrnext->mX2 = px2;
						mrnext->mIndex = missile;
						mrnext->mData = data;
						++mrnext;

						uint8 *pldst = mMergeBuffer + px1;
						int mwidx = (mMissileSize >> (2*missile)) & 3;
						sint32 mask = Expand(data, mwidx) << (px1 - px);
						sint32 mask2 = mask;
						uint8 flags = 0;
						for(int x=px2-px1; x >= 0; --x) {
							if (mask < 0)
								flags |= *pldst;

							++pldst;
							mask += mask;
						}

						if (mbHiresMode) {
							flags &= ~PF;

							for(int x=px1; x < px2; ++x) {
								if (mask2 < 0) {
									if (mAnticData[x])
										flags |= PF2;
								}

								++pldst;
								mask2 += mask2;
							}
						}

						if (flags)
							mMissileCollFlags[missile] |= flags;
					}

					xst = xend;
				}
			}
		}

		for(MissileRange *mr = mranges; mr != mrnext; ++mr) {
			int missile = mr->mIndex;

			uint8 data = mr->mData;
			int px = mr->mX;
			int px1 = mr->mX1;
			int px2 = mr->mX2;

			uint8 *pldst = mMergeBuffer + px1;
			uint8 bit = (mPRIOR & 0x10) ? PF3 : P0 << missile;
			sint32 mask = Expand(data, (mMissileSize >> (2*missile)) & 3) << (px1 - px);
			for(int x=px2-px1; x >= 0; --x) {
				if (mask < 0)
					*pldst |= bit;

				++pldst;
				mask += mask;
			}
		}
	}

	// render scanline
	if (!mpDst)
		return;

	Render(x1, x2);
}

void ATGTIAEmulator::RenderActivityMap(const uint8 *src) {
	if (!mpFrame)
		return;

	ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);
	uint8 *dst = (uint8 *)fb->mBuffer.data;
	for(int y=0; y<262; ++y) {
		uint8 *dst2 = dst;

		for(int x=0; x<114; ++x) {
			uint8 add = src[x] & 1 ? 0x08 : 0x00;
			dst2[0] = (dst2[0] & 0xf0) + ((dst2[0] & 0xf) >> 1) + add;
			dst2[1] = (dst2[1] & 0xf0) + ((dst2[1] & 0xf) >> 1) + add;
			dst2[2] = (dst2[2] & 0xf0) + ((dst2[2] & 0xf) >> 1) + add;
			dst2[3] = (dst2[3] & 0xf0) + ((dst2[3] & 0xf) >> 1) + add;
			dst2 += 4;
		}
		src += 114;
		dst += fb->mBuffer.pitch;
	}
}

void ATGTIAEmulator::UpdateScreen() {
	if (!mpFrame)
		return;

	ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);

	if (mY < 261) {
		const VDPixmap& pxdst = mArtifactModeThisFrame ? mPreArtifactFrame : fb->mBuffer;
		Sync();
		uint32 x = mpConn->GTIAGetXClock();

		uint8 *row = (uint8 *)pxdst.data + (mY+1)*pxdst.pitch;
		VDMemset8(row, 0x00, 2*x);
		VDMemset8(row + x*2, 0xFF, 456 - 2*x);

		ApplyArtifacting();

		mpDisplay->SetSourcePersistent(true, mpFrame->mPixmap);
	} else {
		const VDPixmap& pxdst = mArtifactModeThisFrame ? mPreArtifactFrameVisible : fb->mPixmap;
		uint32 statusFlags = mStatusFlags | mStickyStatusFlags;
		mStickyStatusFlags = mStatusFlags;

		static const uint8 chars[4][7][5]={
			{
#define X 0x1F
				X,X,X,X,X,
				X,X,0,X,X,
				X,0,0,X,X,
				X,X,0,X,X,
				X,X,0,X,X,
				X,0,0,0,X,
				X,X,X,X,X,
#undef X
			},
			{
#define X 0x3F
				X,X,X,X,X,
				X,0,0,X,X,
				X,X,X,0,X,
				X,X,0,X,X,
				X,0,X,X,X,
				X,0,0,0,X,
				X,X,X,X,X,
#undef X
			},
			{
#define X 0x5F
				X,X,X,X,X,
				X,0,0,X,X,
				X,X,X,0,X,
				X,X,0,X,X,
				X,X,X,0,X,
				X,0,0,X,X,
				X,X,X,X,X,
#undef X
			},
			{
#define X 0x7F
				X,X,X,X,X,
				X,0,X,0,X,
				X,0,X,0,X,
				X,0,0,0,X,
				X,X,X,0,X,
				X,X,X,0,X,
				X,X,X,X,X,
#undef X
			},
		};

		for(int i=0; i<4; ++i) {
			if (statusFlags & (1 << i)) {
				VDPixmap pxsrc;
				pxsrc.data = (void *)chars[i];
				pxsrc.pitch = sizeof chars[i][0];
				pxsrc.format = nsVDPixmap::kPixFormat_Pal8;
				pxsrc.palette = mPalette;
				pxsrc.w = 5;
				pxsrc.h = 7;

				VDPixmapBlt(pxdst, pxdst.w - 20 + 5*i, pxdst.h - 7, pxsrc, 0, 0, 5, 7);
			}
		}

		if (mbShowCassetteIndicator) {
			static const int kIndices[]={
				0,1,2,0,2,3,
				4,5,6,4,6,7,
				8,9,10,8,10,11,
				12,13,14,12,14,15,
				16,17,18,16,18,19,
			};

			VDTriColorVertex vx[20];

			float f = (mCassettePos - floor(mCassettePos)) * 20.0f;

			for(int i=0; i<20; i+=4) {
				float a1 = (float)(i + f) * nsVDMath::kfTwoPi / 20.0f;
				float a2 = (float)(i + f + 2.0f) * nsVDMath::kfTwoPi / 20.0f;
				float x1 = cosf(a1);
				float y1 = -sinf(a1);
				float x2 = cosf(a2);
				float y2 = -sinf(a2);

				vx[i+0].x = 424.0f + 10.0f*x1;
				vx[i+0].y = 222.0f + 10.0f*y1;
				vx[i+0].z = 0.0f;
				vx[i+0].a = 255;
				vx[i+0].r = 255;
				vx[i+0].g = 0x0E;
				vx[i+0].b = 255;

				vx[i+1].x = 424.0f + 15.0f*x1;
				vx[i+1].y = 222.0f + 15.0f*y1;
				vx[i+1].z = 0.0f;
				vx[i+1].a = 255;
				vx[i+1].r = 255;
				vx[i+1].g = 0x0E;
				vx[i+1].b = 255;

				vx[i+2].x = 424.0f + 15.0f*x2;
				vx[i+2].y = 222.0f + 15.0f*y2;
				vx[i+2].z = 0.0f;
				vx[i+2].a = 255;
				vx[i+2].r = 255;
				vx[i+2].g = 0x0E;
				vx[i+2].b = 255;

				vx[i+3].x = 424.0f + 10.0f*x2;
				vx[i+3].y = 222.0f + 10.0f*y2;
				vx[i+3].z = 0.0f;
				vx[i+3].a = 255;
				vx[i+3].r = 255;
				vx[i+3].g = 0x0E;
				vx[i+3].b = 255;
			}

			const float xf[16]={
				2.0f / 456.0f, 0, 0, -1.0f,
				0, -2.0f / 262.0f, 0, 1.0f,
				0, 0, 0, 0,
				0, 0, 0, 1.0f
			};

			VDPixmap tmp(pxdst);
			tmp.format = nsVDPixmap::kPixFormat_Y8;
			VDPixmapTriFill(tmp, vx, 20, kIndices, 30, xf);
		}

		ApplyArtifacting();
		mpDisplay->PostBuffer(mpFrame);
		mpFrame = NULL;
	}
}

void ATGTIAEmulator::RecomputePalette() {
	uint32 *dst = mPalette;
	for(int luma = 0; luma<16; ++luma) {
		*dst++ = 0x111111 * luma;
	}

	double angle = mPaletteColorBase * nsVDMath::krTwoPi;
	double angleStep = nsVDMath::krTwoPi / mPaletteColorRange;

	for(int hue=0; hue<15; ++hue) {
		double i = (75.0/255.0) * cos(angle);
		double q = (75.0/255.0) * sin(angle);

		for(int luma=0; luma<16; ++luma) {
			double y = (double)(luma + 1) / 16.0;
			double r = y + 0.956*i + 0.621*q;
			double g = y - 0.272*i - 0.647*q;
			double b = y - 1.107*i + 1.704*q;

			*dst++	= (VDClampedRoundFixedToUint8Fast((float)r) << 16)
					+ (VDClampedRoundFixedToUint8Fast((float)g) <<  8)
					+ (VDClampedRoundFixedToUint8Fast((float)b)      );
		}

		angle += angleStep;
	}
}

uint8 ATGTIAEmulator::ReadByte(uint8 reg) {
	// fast registers
	switch(reg) {
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
			return mTRIG[reg - 0x10];
		case 0x14:
			return mbPALMode ? 0x00 : 0x0F;
		case 0x15:
		case 0x16:
		case 0x17:	// must return LSB0 set or Recycle hangs
		case 0x18:
		case 0x19:
		case 0x1A:
		case 0x1B:
		case 0x1C:
		case 0x1D:
		case 0x1E:
			return 0x0F;
		case 0x1F:		// $D01F CONSOL
			return mCONSOL & 0x07;
	}

	Sync();	

	switch(reg) {
		// missile-to-playfield collisions
		case 0x00:	return mMissileCollFlags[0] & 15;
		case 0x01:	return mMissileCollFlags[1] & 15;
		case 0x02:	return mMissileCollFlags[2] & 15;
		case 0x03:	return mMissileCollFlags[3] & 15;

		// player-to-playfield collisions
		case 0x04:	return mPlayerCollFlags[0] & 15;
		case 0x05:	return mPlayerCollFlags[1] & 15;
		case 0x06:	return mPlayerCollFlags[2] & 15;
		case 0x07:	return mPlayerCollFlags[3] & 15;

		// missile-to-player collisions
		case 0x08:	return mMissileCollFlags[0] >> 4;
		case 0x09:	return mMissileCollFlags[1] >> 4;
		case 0x0A:	return mMissileCollFlags[2] >> 4;
		case 0x0B:	return mMissileCollFlags[3] >> 4;

		// player-to-player collisions
		case 0x0C:	return    ((mPlayerCollFlags[1] >> 3) & 0x02)	// 1 -> 0
							+ ((mPlayerCollFlags[2] >> 2) & 0x04)	// 2 -> 0
							+ ((mPlayerCollFlags[3] >> 1) & 0x08);	// 3 -> 0

		case 0x0D:	return    ((mPlayerCollFlags[1] >> 4) & 0x01)	// 1 -> 0
							+ ((mPlayerCollFlags[2] >> 3) & 0x04)	// 2 -> 1
							+ ((mPlayerCollFlags[3] >> 2) & 0x08);	// 3 -> 1

		case 0x0E:	return    ((mPlayerCollFlags[2] >> 4) & 0x03)	// 2 -> 0, 1
							+ ((mPlayerCollFlags[3] >> 3) & 0x08);	// 3 -> 2

		case 0x0F:	return    ((mPlayerCollFlags[3] >> 4) & 0x07);	// 3 -> 0, 1, 2

		default:
//			__debugbreak();
			break;
	}
	return 0;
}

void ATGTIAEmulator::WriteByte(uint8 reg, uint8 value) {
	switch(reg) {
		case 0x1C:
			mVDELAY = value;
			break;

		case 0x1D:
			mGRACTL = value;
			break;

		case 0x1F:		// $D01F CONSOL
			{
				uint8 newConsol = (mCONSOL & 0xf7) + (value & 0x08);
				if ((newConsol ^ mCONSOL) & 8)
					mpConn->GTIASetSpeaker(0 != (newConsol & 8));
				mCONSOL = newConsol;
			}
			break;
	}

	Sync();

	static const int kPlayerWidths[4]={8,16,8,32};
	static const int kMissileWidths[4]={2,4,2,8};

	switch(reg) {
		case 0x00:	mPlayerPos[0] = value;			break;
		case 0x01:	mPlayerPos[1] = value;			break;
		case 0x02:	mPlayerPos[2] = value;			break;
		case 0x03:	mPlayerPos[3] = value;			break;
		case 0x04:	mMissilePos[0] = value;			break;
		case 0x05:	mMissilePos[1] = value;			break;
		case 0x06:	mMissilePos[2] = value;			break;
		case 0x07:	mMissilePos[3] = value;			break;
		case 0x08:
			value &= 3;
			mPlayerSize[0] = value;
			mPlayerWidth[0] = kPlayerWidths[value];
			break;
		case 0x09:
			value &= 3;
			mPlayerSize[1] = value;
			mPlayerWidth[1] = kPlayerWidths[value];
			break;
		case 0x0A:
			value &= 3;
			mPlayerSize[2] = value;
			mPlayerWidth[2] = kPlayerWidths[value];
			break;
		case 0x0B:
			value &= 3;
			mPlayerSize[3] = value;
			mPlayerWidth[3] = kPlayerWidths[value];
			break;
		case 0x0C:
			mMissileSize = value;
			mMissileWidth[0] = kMissileWidths[(value >> 0) & 3];
			mMissileWidth[1] = kMissileWidths[(value >> 2) & 3];
			mMissileWidth[2] = kMissileWidths[(value >> 4) & 3];
			mMissileWidth[3] = kMissileWidths[(value >> 6) & 3];
			break;
		case 0x0D:
			mDelayedPlayerData[0] = value;
			if (!(mVDELAY & 0x10))
				mPlayerData[0] = value;
			break;
		case 0x0E:
			mDelayedPlayerData[1] = value;
			if (!(mVDELAY & 0x20))
				mPlayerData[1] = value;
			break;
		case 0x0F:
			mDelayedPlayerData[2] = value;
			if (!(mVDELAY & 0x40))
				mPlayerData[2] = value;
			break;
		case 0x10:
			mDelayedPlayerData[3] = value;
			if (!(mVDELAY & 0x80))
				mPlayerData[3] = value;
			break;
		case 0x11:
			mDelayedMissileData = value;
			{
				uint8 mask = 0;
				if (mVDELAY & 1)
					mask |= 3;
				if (mVDELAY & 2)
					mask |= 0x0c;
				if (mVDELAY & 4)
					mask |= 0x30;
				if (mVDELAY & 8)
					mask |= 0xc0;

				mMissileData ^= (mMissileData ^ value) & ~mask;
			}
			break;

		case 0x12:
			value &= 0xfe;
			mPMColor[0] = value;
			mColorTable[kColorP0] = value;
			mColorTable[kColorP0P1] = value | mPMColor[1];
			mColorTable[kColorPF0P0] = mPFColor[0] | value;
			mColorTable[kColorPF0P0P1] = mColorTable[kColorPF0P1] | value;
			mColorTable[kColorPF1P0] = mPFColor[1] | value;
			mColorTable[kColorPF1P0P1] = mColorTable[kColorPF1P1] | value;
			break;
		case 0x13:
			value &= 0xfe;
			mPMColor[1] = value;
			mColorTable[kColorP1] = value;
			mColorTable[kColorP0P1] = mPMColor[0] | value;
			mColorTable[kColorPF0P1] = mPFColor[0] | value;
			mColorTable[kColorPF0P0P1] = mColorTable[kColorPF0P0] | value;
			mColorTable[kColorPF1P1] = mPFColor[1] | value;
			mColorTable[kColorPF1P0P1] = mColorTable[kColorPF1P0] | value;
			break;
		case 0x14:
			value &= 0xfe;
			mPMColor[2] = value;
			mColorTable[kColorP2] = value;
			mColorTable[kColorP2P3] = value | mPMColor[3];
			mColorTable[kColorPF2P2] = mPFColor[2] | value;
			mColorTable[kColorPF2P2P3] = mColorTable[kColorPF2P3] | value;
			mColorTable[kColorPF3P2] = mPFColor[3] | value;
			mColorTable[kColorPF3P2P3] = mColorTable[kColorPF3P3] | value;
			break;
		case 0x15:
			value &= 0xfe;
			mPMColor[3] = value;
			mColorTable[kColorP3] = value;
			mColorTable[kColorP2P3] = mPMColor[2] | value;
			mColorTable[kColorPF2P3] = mPFColor[2] | value;
			mColorTable[kColorPF2P2P3] = mColorTable[kColorPF2P2] | value;
			mColorTable[kColorPF3P3] = mPFColor[3] | value;
			mColorTable[kColorPF3P2P3] = mColorTable[kColorPF3P2] | value;
			break;
		case 0x16:
			value &= 0xfe;
			mPFColor[0] = value;
			mColorTable[kColorPF0] = value;
			mColorTable[kColorPF0P0] = value | mPMColor[0];
			mColorTable[kColorPF0P1] = value | mPMColor[1];
			mColorTable[kColorPF0P0P1] = value | mColorTable[kColorP0P1];
			break;
		case 0x17:
			value &= 0xfe;
			mPFColor[1] = value;
			mColorTable[kColorPF1] = value;
			mColorTable[kColorPF1P0] = value | mPMColor[0];
			mColorTable[kColorPF1P1] = value | mPMColor[1];
			mColorTable[kColorPF1P0P1] = value | mColorTable[kColorP0P1];
			break;
		case 0x18:
			value &= 0xfe;
			mPFColor[2] = value;
			mColorTable[kColorPF2] = value;
			mColorTable[kColorPF2P2] = value | mPMColor[2];
			mColorTable[kColorPF2P3] = value | mPMColor[3];
			mColorTable[kColorPF2P2P3] = value | mColorTable[kColorP2P3];
			break;
		case 0x19:
			value &= 0xfe;
			mPFColor[3] = value;
			mColorTable[kColorPF3] = value;
			mColorTable[kColorPF3P2] = value | mPMColor[2];
			mColorTable[kColorPF3P3] = value | mPMColor[3];
			mColorTable[kColorPF3P2P3] = value | mColorTable[kColorP2P3];
			break;
		case 0x1A:
			value &= 0xfe;
			mPFBAK = value;
			mColorTable[kColorBAK] = value;
			break;
		case 0x1B:
			mPRIOR = value;
			mpPriTable = mPriorityTables[(value & 15) + (value&32 ? 16 : 0)];
//			mpPriTable = mPriorityTables[1];
			if (mPRIOR & 16)
				mpMissileTable = kMissileTables[1];
			else
				mpMissileTable = kMissileTables[0];

			if (value & 0xC0)
				mbHiresMode = false;

			mbVideoDelayed = ((value & 0xC0) == 0x80) != 0;
			break;
		case 0x1E:		// $D01E HITCLR
			memset(mPlayerCollFlags, 0, sizeof mPlayerCollFlags);
			memset(mMissileCollFlags, 0, sizeof mMissileCollFlags);
			break;
		default:
//			__debugbreak();
			break;
	}
}

void ATGTIAEmulator::ApplyArtifacting() {
	if (!mArtifactModeThisFrame)
		return;

	ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);
	char *dstrow = (char *)fb->mBuffer.data;
	ptrdiff_t dstpitch = fb->mBuffer.pitch;

	const uint8 *srcrow = (const uint8 *)mPreArtifactFrame.data;
	ptrdiff_t srcpitch = mPreArtifactFrame.pitch;

	for(uint32 row=0; row<262; ++row) {
		uint32 *dst = (uint32 *)dstrow;
		const uint8 *src = srcrow;

		mpArtifactingEngine->Artifact(dst, src);

		srcrow += srcpitch;
		dstrow += dstpitch;
	}
}

void ATGTIAEmulator::Render(int x1, int x2) {
	if (x1 == 34)
		memset(mpDst, mpColorTable[8], 68);

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

	if (x2 == 222)
		memset(mpDst + 444, mpColorTable[8], 456 - 444);
}

void ATGTIAEmulator::RenderLores(int x1, int x2) {
	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 *__restrict priTable = mpPriTable;

	uint8 *dst = mpDst + x1*2;
	const uint8 *src = mMergeBuffer + x1;

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

void ATGTIAEmulator::RenderMode8(int x1, int x2) {
	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 *__restrict priTable = mpPriTable;

	const uint8 *lumasrc = &mAnticData[x1];
	uint8 *dst = mpDst + x1*2;
	const uint8 *src = mMergeBuffer + x1;

	const uint8 luma1 = mPFColor[1] & 0xf;

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

void ATGTIAEmulator::RenderMode9(int x1, int x2) {
	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 *__restrict priTable = mpPriTable;

	uint8 *dst = mpDst + x1*2;
	const uint8 *src = mMergeBuffer + x1;

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

		const uint8 *lumasrc = &mAnticData[x1++ & ~1];
		uint8 l1 = (lumasrc[0] << 2) + lumasrc[1];

		uint8 c4 = pri0 | (l1 & kPlayerMaskLookup[code0 >> 4]);

		dst[0] = dst[1] = c4;
		dst += 2;
	}
}

void ATGTIAEmulator::RenderMode10(int x1, int x2) {
	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 *__restrict priTable = mpPriTable;

	uint8 *dst = mpDst + x1*2;
	const uint8 *src = mMergeBuffer + x1;

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
		const uint8 *lumasrc = &mAnticData[(x1++ - 1) & ~1];
		uint8 l1 = lumasrc[0]*4 + lumasrc[1];

		uint8 c4 = kMode10Lookup[l1];

		dst[0] = dst[1] = colorTable[priTable[c4 | (*src++ & 0xf8)]];
		dst += 2;
	}
}

void ATGTIAEmulator::RenderMode11(int x1, int x2) {
	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 *__restrict priTable = mpPriTable;

	uint8 *dst = mpDst + x1*2;
	const uint8 *src = mMergeBuffer + x1;

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

		const uint8 *lumasrc = &mAnticData[x1++ & ~1];
		uint8 l0 = (lumasrc[0] << 6) + (lumasrc[1] << 4);

		uint8 c0 = (pri0 | (l0 & kMode11Lookup[code0 >> 4][l0 == 0][0])) & kMode11Lookup[code0 >> 4][l0 == 0][1];

		dst[0] = dst[1] = c0;
		dst += 2;
	}
}
