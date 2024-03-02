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
#include <vd2/system/binary.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/math.h>
#include <vd2/VDDisplay/display.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/triblt.h>
#include "gtia.h"
#include "gtiatables.h"
#include "gtiarenderer.h"
#include "console.h"
#include "artifacting.h"
#include "savestate.h"
#include "uirender.h"
#include "vbxe.h"

using namespace ATGTIA;

#ifdef VD_CPU_X86
extern "C" void atasm_update_playfield_160_sse2(
	void *dst,
	const uint8 *src,
	uint32 n
);
#endif

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
	const int kPlayerWidths[4]={8,16,8,32};
	const int kMissileWidths[4]={2,4,2,8};

	const uint32 kSpriteShiftMasks[4]={
		0xFFFFFFFF,
		0x00,
		0x00,
		0x00,
	};

	const uint8 kSpriteStateTransitions[4][4]={
		{ 0, 0, 0, 0 },
		{ 1, 0, 1, 0 },
		{ 0, 2, 2, 0 },
		{ 1, 2, 3, 0 },
	};
}

///////////////////////////////////////////////////////////////////////////

void ATGTIAEmulator::SpriteState::Reset() {
	mShiftRegister = 0;
	mShiftState = 0;
	mSizeMode = 0;
	mDataLatch = 0;
}

// Detect collisions
uint8 ATGTIAEmulator::SpriteState::Detect(uint32 ticks, const uint8 *src) {
	uint8 shifter = mShiftRegister;
	int state = mShiftState;
	const uint8 *VDRESTRICT stateTransitions = kSpriteStateTransitions[mSizeMode];
	uint8 detect = 0;

	do {
		detect |= (*src++) & (uint8)((sint8)shifter >> 7);

		state = stateTransitions[state];
		shifter += shifter & kSpriteShiftMasks[state];
	} while(--ticks);

	return detect;
}

uint8 ATGTIAEmulator::SpriteState::Detect(uint32 ticks, const uint8 *src, const uint8 *hires) {
	uint8 shifter = mShiftRegister;
	int state = mShiftState;
	const uint8 *VDRESTRICT stateTransitions = kSpriteStateTransitions[mSizeMode];
	uint8 detect = 0;

	do {
		if ((sint8)shifter < 0) {
			detect |= (*src & (P01 | P23));

			if (*hires)
				detect |= PF2;
		}

		++src;
		++hires;

		state = stateTransitions[state];
		shifter += shifter & kSpriteShiftMasks[state];
	} while(--ticks);

	return detect;
}

uint8 ATGTIAEmulator::SpriteState::Generate(uint32 ticks, uint8 mask, uint8 *dst) {
	uint8 shifter = mShiftRegister;
	int state = mShiftState;
	const uint8 *VDRESTRICT stateTransitions = kSpriteStateTransitions[mSizeMode];
	uint8 detect = 0;

	do {
		if ((sint8)shifter < 0) {
			detect |= *dst;
			*dst |= mask;
		}

		++dst;

		state = stateTransitions[state];
		shifter += shifter & kSpriteShiftMasks[state];
	} while(--ticks);

	return detect;
}

uint8 ATGTIAEmulator::SpriteState::Generate(uint32 ticks, uint8 mask, uint8 *dst, const uint8 *hires) {
	uint8 shifter = mShiftRegister;
	int state = mShiftState;
	const uint8 *VDRESTRICT stateTransitions = kSpriteStateTransitions[mSizeMode];
	uint8 detect = 0;

	do {
		if ((sint8)shifter < 0) {
			detect |= (*dst & (P01 | P23));
			*dst |= mask;

			if (*hires)
				detect |= PF2;
		}

		++dst;
		++hires;

		state = stateTransitions[state];
		shifter += shifter & kSpriteShiftMasks[state];
	} while(--ticks);

	return detect;
}

// Advance the shift state for a player or missile by a number of ticks,
// without actually generating image data.
void ATGTIAEmulator::SpriteState::Advance(uint32 ticks) {
	int shifts = 0;

	switch(mSizeMode) {
		case 0:
			shifts = ticks;
			mShiftState = 0;
			break;

		case 1:
			shifts = ((mShiftState & 1) + ticks) >> 1;
			mShiftState = (ticks + mShiftState) & 1;
			break;

		case 2:
			// 00,11 -> 00
			// 01,10 -> 10
			switch(mShiftState) {
				case 0:
				case 3:
					shifts = ticks;
					mShiftState = 0;
					break;

				case 1:
					mShiftState = 2;
				case 2:
					break;
			}
			break;

		case 3:
			shifts = (mShiftState + ticks) >> 2;
			mShiftState = (mShiftState + ticks) & 3;
			break;
	}

	if (shifts >= 32)
		mShiftRegister = 0;
	else
		mShiftRegister <<= shifts;
}

///////////////////////////////////////////////////////////////////////////

void ATGTIAEmulator::Sprite::Sync(int pos) {
	if (mLastSync != pos) {
		mState.Advance(pos - mLastSync);
		mLastSync = pos;
	}
}

///////////////////////////////////////////////////////////////////////////

ATGTIAEmulator::ATGTIAEmulator()
	: mpConn(NULL)
	, mpVideoTap(NULL)
	, mpFrameTracker(new ATFrameTracker)
	, mbPALMode(false)
	, mbSECAMMode(false)
	, mArtifactMode(kArtifactNone)
	, mOverscanMode(kOverscanExtended)
	, mVerticalOverscanMode(kVerticalOverscan_Default)
	, mVBlankMode(kVBlankModeOn)
	, mbVsyncEnabled(true)
	, mbBlendMode(false)
	, mbOverscanPALExtended(false)
	, mbOverscanPALExtendedThisFrame(false)
	, mbPALThisFrame(false)
	, mbInterlaceEnabled(false)
	, mbInterlaceEnabledThisFrame(false)
	, mbScanlinesEnabled(false)
	, mbScanlinesEnabledThisFrame(false)
	, mbFieldPolarity(false)
	, mbLastFieldPolarity(false)
	, mbPostProcessThisFrame(false)
	, mPreArtifactFrameBuffer(464*312+16)
	, mpArtifactingEngine(new ATArtifactingEngine)
	, mpRenderer(new ATGTIARenderer)
	, mpUIRenderer(NULL)
	, mpVBXE(NULL)
	, mRCIndex(0)
	, mRCCount(0)
	, mpFreeSpriteImages(NULL)
{
	ResetColors();

	mPreArtifactFrame.data = mPreArtifactFrameBuffer.data();
	mPreArtifactFrame.pitch = 464;
	mPreArtifactFrame.palette = mPalette;
	mPreArtifactFrame.data2 = NULL;
	mPreArtifactFrame.data3 = NULL;
	mPreArtifactFrame.pitch2 = 0;
	mPreArtifactFrame.pitch3 = 0;
	mPreArtifactFrame.w = 456;
	mPreArtifactFrame.h = 262;
	mPreArtifactFrame.format = nsVDPixmap::kPixFormat_Pal8;

	mpFrameTracker->AddRef();

	mSwitchOutput = 8;
	mSwitchInput = 15;
	mForcedSwitchInput = 15;
	mPRIOR = 0;
	mActivePRIOR = 0;

	for(int i=0; i<4; ++i) {
		mTRIG[i] = 0x01;
		mTRIGLatched[i] = 0x01;
		mTRIGSECAM[i] = 0x01;
		mTRIGSECAMLastUpdate[i] = 0;
	}

	SetAnalysisMode(kAnalyzeNone);
	memset(mPlayerCollFlags, 0, sizeof mPlayerCollFlags);
	memset(mMissileCollFlags, 0, sizeof mMissileCollFlags);
	mCollisionMask = 0xFF;

	mbTurbo = false;
	mbForcedBorder = false;

	SetPALMode(false);
}

ATGTIAEmulator::~ATGTIAEmulator() {
	mpLastFrame = NULL;

	if (mpFrameTracker) {
		mpFrameTracker->Release();
		mpFrameTracker = NULL;
	}

	delete mpArtifactingEngine;
	delete mpRenderer;
}

void ATGTIAEmulator::Init(IATGTIAEmulatorConnections *conn) {
	mpConn = conn;
	mY = 0;

	ColdReset();
}

void ATGTIAEmulator::ColdReset() {
	memset(mSpritePos, 0, sizeof mSpritePos);
	memset(mPMColor, 0, sizeof mPMColor);
	memset(mPFColor, 0, sizeof mPFColor);

	memset(&mState, 0, sizeof mState);

	memset(mPMColor, 0, sizeof mPMColor);
	memset(mPFColor, 0, sizeof mPFColor);
	mPFBAK = 0;
	mPRIOR = 0;
	mActivePRIOR = 0;
	mVDELAY = 0;
	mGRACTL = 0;
	mSwitchOutput = 0;

	memset(mPlayerCollFlags, 0, sizeof mPlayerCollFlags);
	memset(mMissileCollFlags, 0, sizeof mMissileCollFlags);

	ResetSprites();
}

void ATGTIAEmulator::SetVBXE(ATVBXEEmulator *vbxe) {
	mpVBXE = vbxe;

	if (mpVBXE)
		mpVBXE->SetDefaultPalette(mPalette);

	// kill current frame update
	mpDst = NULL;
	mpFrame = NULL;
}

void ATGTIAEmulator::SetUIRenderer(IATUIRenderer *r) {
	mpUIRenderer = r;
}

ATColorSettings ATGTIAEmulator::GetColorSettings() const {
	return mColorSettings;
}

void ATGTIAEmulator::SetColorSettings(const ATColorSettings& settings) {
	mColorSettings = settings;
	RecomputePalette();
}

ATArtifactingParams ATGTIAEmulator::GetArtifactingParams() const {
	return mpArtifactingEngine->GetArtifactingParams();
}

void ATGTIAEmulator::SetArtifactingParams(const ATArtifactingParams& params) {
	mpArtifactingEngine->SetArtifactingParams(params);
}

void ATGTIAEmulator::ResetColors() {
	{
		ATColorParams& colpa = mColorSettings.mNTSCParams;
		colpa.mHueStart = -36.0f;
		colpa.mHueRange = 25.5f * 15.0f;
		colpa.mBrightness = -0.08f;
		colpa.mContrast = 1.08f;
		colpa.mSaturation = 75.0f / 255.0f;
		colpa.mGammaCorrect = 1.0f;
		colpa.mArtifactHue = 279.0f;
		colpa.mArtifactSat = 2.76f;
		colpa.mArtifactBias = 0.35f;
		colpa.mbUsePALQuirks = false;
		colpa.mLumaRampMode = kATLumaRampMode_XL;
	}

	{
		ATColorParams& colpa = mColorSettings.mPALParams;
		colpa.mHueStart = -23.0f;
		colpa.mHueRange = 23.5f * 15.0f;
		colpa.mBrightness = 0.0f;
		colpa.mContrast = 1.0f;
		colpa.mSaturation = 0.29f;
		colpa.mGammaCorrect = 1.0f;
		colpa.mArtifactHue = 96.0f;
		colpa.mArtifactSat = 2.76f;
		colpa.mArtifactBias = 0.35f;
		colpa.mbUsePALQuirks = true;
		colpa.mLumaRampMode = kATLumaRampMode_XL;
	}

	mColorSettings.mbUsePALParams = true;

	RecomputePalette();
}

void ATGTIAEmulator::GetPalette(uint32 pal[256]) const {
	memcpy(pal, mPalette, sizeof(uint32)*256);
}

void ATGTIAEmulator::SetAnalysisMode(AnalysisMode mode) {
	mAnalysisMode = mode;
	mpRenderer->SetAnalysisMode(mode != kAnalyzeNone);
}

void ATGTIAEmulator::SetOverscanMode(OverscanMode mode) {
	mOverscanMode = mode;
}

void ATGTIAEmulator::SetVerticalOverscanMode(VerticalOverscanMode mode) {
	mVerticalOverscanMode = mode;
}

void ATGTIAEmulator::SetOverscanPALExtended(bool extended) {
	mbOverscanPALExtended = extended;
}

vdrect32 ATGTIAEmulator::GetFrameScanArea() const {
	int xlo = 44;
	int xhi = 212;
	int ylo = 8;
	int yhi = 248;

	bool palext = mbPALMode && mbOverscanPALExtended;
	if (palext) {
		ylo -= 25;
		yhi += 25;
	}

	OverscanMode omode = mOverscanMode;
	VerticalOverscanMode vomode = DeriveVerticalOverscanMode();

	if (mAnalysisMode || mbForcedBorder) {
		omode = kOverscanFull;
		vomode = kVerticalOverscan_Full;
	}

	switch(omode) {
		case kOverscanFull:
			xlo = 0;
			xhi = 228;
			break;

		case kOverscanExtended:
			xlo = 34;
			xhi = 222;
			break;

		case kOverscanNormal:
			break;

		case kOverscanOSScreen:
			xlo = 48;
			xhi = 208;
			break;
	}

	switch(vomode) {
		case kVerticalOverscan_Full:
			ylo = 0;
			yhi = 262;

			if (palext) {
				ylo = -25;
				yhi = 287;
			}
			break;

		case kVerticalOverscan_Extended:
			break;

		case kVerticalOverscan_OSScreen:
			if (!palext) {
				ylo = 32;
				yhi = 224;
			}
			break;

		case kVerticalOverscan_Normal:
			if (!mbPALMode) {
				ylo = 16;
				yhi = 240;
			}
			break;
	}

	return vdrect32(xlo, ylo, xhi, yhi);
}

void ATGTIAEmulator::GetRawFrameFormat(int& w, int& h, bool& rgb32) const {
	rgb32 = (mpVBXE != NULL) || mArtifactMode || mbBlendMode;

	OverscanMode omode = mOverscanMode;
	VerticalOverscanMode vomode = DeriveVerticalOverscanMode();

	if (mAnalysisMode || mbForcedBorder) {
		omode = kOverscanFull;
		vomode = kVerticalOverscan_Full;
	}

	switch(omode) {
		case kOverscanFull:
			w = 456;
			break;

		case kOverscanExtended:
			w = 376;
			break;

		case kOverscanNormal:
			w = 336;
			break;

		case kOverscanOSScreen:
			w = 320;
			break;
	}

	const bool extpal = (mbPALMode && mbOverscanPALExtended);
	switch(vomode) {
		case kVerticalOverscan_Full:
			if (extpal)
				h = 312;
			else
				h = 262;
			break;

		case kVerticalOverscan_Extended:
			if (extpal)
				h = 288;
			else
				h = 240;
			break;

		case kVerticalOverscan_Normal:
			if (extpal)
				h = 288;
			else if (mbPALMode)
				h = 240;
			else
				h = 224;
			break;

		case kVerticalOverscan_OSScreen:
			if (extpal)
				h = 288;
			else
				h = 192;
			break;
	}

	if (mbInterlaceEnabled || mbScanlinesEnabled)
		h *= 2;

	if (mpVBXE != NULL || mArtifactMode == kArtifactNTSCHi || mArtifactMode == kArtifactPALHi)
		w *= 2;
}

void ATGTIAEmulator::GetFrameSize(int& w, int& h) const {
	OverscanMode omode = mOverscanMode;
	VerticalOverscanMode vomode = DeriveVerticalOverscanMode();

	if (mAnalysisMode || mbForcedBorder) {
		omode = kOverscanFull;
		vomode = kVerticalOverscan_Full;
	}

	switch(omode) {
		case kOverscanFull:
			w = 456;
			break;

		case kOverscanExtended:
			w = 376;
			break;

		case kOverscanNormal:
			w = 336;
			break;

		case kOverscanOSScreen:
			w = 320;
			break;
	}

	switch(vomode) {
		case kVerticalOverscan_Full:
			if (mbPALMode && mbOverscanPALExtended)
				h = 312;
			else
				h = 262;
			break;

		case kVerticalOverscan_Extended:
			if (mbPALMode && mbOverscanPALExtended)
				h = 288;
			else
				h = 240;
			break;

		case kVerticalOverscan_Normal:
			if (mbPALMode && mbOverscanPALExtended)
				h = 288;
			else if (mbPALMode)
				h = 240;
			else
				h = 224;
			break;

		case kVerticalOverscan_OSScreen:
			if (mbPALMode && mbOverscanPALExtended)
				h = 288;
			else
				h = 192;
			break;
	}

	if (mb14MHzThisFrame || mbInterlaceEnabled || mbScanlinesEnabled) {
		w *= 2;
		h *= 2;
	}
}

void ATGTIAEmulator::GetPixelAspectMultiple(int& x, int& y) const {
	int ix = 1;
	int iy = 1;

	if (mbInterlaceEnabled || mbScanlinesEnabled)
		iy = 2;

	if (mpVBXE != NULL || mArtifactMode == kArtifactNTSCHi || mArtifactMode == kArtifactPALHi)
		ix = 2;

	x = ix;
	y = iy;
}

bool ATGTIAEmulator::ArePMCollisionsEnabled() const {
	return (mCollisionMask & 0xf0) != 0;
}

void ATGTIAEmulator::SetPMCollisionsEnabled(bool enable) {
	if (enable) {
		if (!(mCollisionMask & 0xf0)) {
			// we clear the collision flags directly when re-enabling collisions
			// as they were being masked in the register read
			for(int i=0; i<4; ++i) {
				mPlayerCollFlags[i] &= 0x0f;
				mMissileCollFlags[i] &= 0x0f;
			}
		}

		mCollisionMask |= 0xf0;
	} else {
		mCollisionMask &= 0x0f;
	}
}

bool ATGTIAEmulator::ArePFCollisionsEnabled() const {
	return (mCollisionMask & 0x0f) != 0;
}

void ATGTIAEmulator::SetPFCollisionsEnabled(bool enable) {
	if (enable) {
		if (!(mCollisionMask & 0x0f)) {
			// we clear the collision flags directly when re-enabling collisions
			// as they were being masked in the register read
			for(int i=0; i<4; ++i) {
				mPlayerCollFlags[i] &= 0xf0;
				mMissileCollFlags[i] &= 0xf0;
			}
		}

		mCollisionMask |= 0x0f;
	} else {
		mCollisionMask &= 0xf0;
	}
}

void ATGTIAEmulator::SetVideoOutput(IVDVideoDisplay *pDisplay) {
	mpDisplay = pDisplay;

	if (!pDisplay) {
		mpFrame = NULL;
		mpDst = NULL;
	}
}

void ATGTIAEmulator::SetPALMode(bool enabled) {
	mbPALMode = enabled;

	RecomputePalette();
}

void ATGTIAEmulator::SetSECAMMode(bool enabled) {
	mbSECAMMode = enabled;

	mpRenderer->SetSECAMMode(enabled);
}

void ATGTIAEmulator::SetConsoleSwitch(uint8 c, bool set) {
	mSwitchInput &= ~c;

	if (!set)			// bit is active low
		mSwitchInput |= c;
}

void ATGTIAEmulator::SetForcedConsoleSwitches(uint8 c) {
	mForcedSwitchInput = c;
}

void ATGTIAEmulator::SetVideoTap(IATGTIAVideoTap *vtap) {
	mpVideoTap = vtap;
}

const VDPixmap *ATGTIAEmulator::GetLastFrameBuffer() const {
	return mpLastFrame ? &mpLastFrame->mPixmap : NULL;
}

void ATGTIAEmulator::DumpStatus() {
	for(int i=0; i<4; ++i) {
		ATConsolePrintf("Player  %d: color = %02x, pos = %02x, size=%d, data = %02x\n"
			, i
			, mPMColor[i]
			, mSpritePos[i]
			, mSprites[i].mState.mSizeMode
			, mSprites[i].mState.mDataLatch
			);
	}

	for(int i=0; i<4; ++i) {
		ATConsolePrintf("Missile %d: color = %02x, pos = %02x, size=%d, data = %02x\n"
			, i
			, mPRIOR & 0x10 ? mPFColor[3] : mPMColor[i]
			, mSpritePos[i+4]
			, mSprites[i+4].mState.mSizeMode
			, mSprites[i+4].mState.mDataLatch >> 6
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

	uint8 consol = ~(mSwitchInput & mForcedSwitchInput & ~mSwitchOutput);
	ATConsolePrintf("CONSOL: %02x set <-> %02x input%s%s%s%s\n"
		, mSwitchOutput
		, mSwitchInput
		, mSwitchOutput & 0x08 ? ", speaker" : ""
		, consol & 0x04 ? ", option" : ""
		, consol & 0x02 ? ", select" : ""
		, consol & 0x01 ? ", start" : ""
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

template<class T>
void ATGTIAEmulator::ExchangeStatePrivate(T& io) {
	for(int i=0; i<4; ++i)
		io != mPlayerCollFlags[i];

	for(int i=0; i<4; ++i)
		io != mMissileCollFlags[i];

	io != mbHiresMode;
	io != mActivePRIOR;
}

void ATGTIAEmulator::BeginLoadState(ATSaveStateReader& reader) {
	reader.RegisterHandlerMethod(kATSaveStateSection_Arch, VDMAKEFOURCC('G', 'T', 'I', 'A'), this, &ATGTIAEmulator::LoadStateArch);
	reader.RegisterHandlerMethod(kATSaveStateSection_Private, VDMAKEFOURCC('G', 'T', 'I', 'A'), this, &ATGTIAEmulator::LoadStatePrivate);
	reader.RegisterHandlerMethod(kATSaveStateSection_ResetPrivate, 0, this, &ATGTIAEmulator::LoadStateResetPrivate);
	reader.RegisterHandlerMethod(kATSaveStateSection_End, 0, this, &ATGTIAEmulator::EndLoadState);

	ResetSprites();
}

void ATGTIAEmulator::LoadStateArch(ATSaveStateReader& reader) {
	// P/M pos
	for(int i=0; i<8; ++i)
		reader != mSpritePos[i];

	// P/M size
	for(int i=0; i<4; ++i)
		mSprites[i].mState.mSizeMode = reader.ReadUint8() & 3;

	const uint8 missileSize = reader.ReadUint8();
	for(int i=0; i<4; ++i)
		mSprites[i+4].mState.mSizeMode = (missileSize >> (2*i)) & 3;

	// graphics latches
	for(int i=0; i<4; ++i)
		mSprites[i].mState.mDataLatch = reader.ReadUint8();

	const uint8 missileData = reader.ReadUint8();
	for(int i=0; i<4; ++i)
		mSprites[i+4].mState.mDataLatch = ((missileData >> (2*i)) & 3) << 6;

	// colors
	for(int i=0; i<4; ++i)
		reader != mPMColor[i];

	for(int i=0; i<4; ++i)
		reader != mPFColor[i];

	reader != mPFBAK;

	// misc registers
	reader != mPRIOR;
	reader != mVDELAY;
	reader != mGRACTL;
	reader != mSwitchOutput;
}

void ATGTIAEmulator::LoadStatePrivate(ATSaveStateReader& reader) {
	ExchangeStatePrivate(reader);

	// read register changes
	mRCCount = reader.ReadUint32();
	mRCIndex = 0;
	mRegisterChanges.resize(mRCCount);
	for(int i=0; i<mRCCount; ++i) {
		RegisterChange& rc = mRegisterChanges[i];

		rc.mPos = reader.ReadUint8();
		rc.mReg = reader.ReadUint8();
		rc.mValue = reader.ReadUint8();
	}

	mpRenderer->LoadState(reader);
}

void ATGTIAEmulator::LoadStateResetPrivate(ATSaveStateReader& reader) {
	for(int i=0; i<8; ++i) {
		mSprites[i].mState.mShiftState = 0;
		mSprites[i].mState.mShiftRegister = 0;
	}

	mbHiresMode = false;
	
	mRegisterChanges.clear();
	mRCCount = 0;
	mRCIndex = 0;

	mpRenderer->ResetState();
}

void ATGTIAEmulator::EndLoadState(ATSaveStateReader& writer) {
	// recompute derived state
	mpConn->GTIASetSpeaker(0 != (mSwitchOutput & 8));

	for(int i=0; i<4; ++i) {
		mpRenderer->SetRegisterImmediate(0x12 + i, mPMColor[i]);
		mpRenderer->SetRegisterImmediate(0x16 + i, mPFColor[i]);
	}

	mpRenderer->SetRegisterImmediate(0x1A, mPFBAK);

	// Terminate existing scan line
	mpDst = NULL;
	mpRenderer->EndScanline();
}

void ATGTIAEmulator::BeginSaveState(ATSaveStateWriter& writer) {
	writer.RegisterHandlerMethod(kATSaveStateSection_Arch, this, &ATGTIAEmulator::SaveStateArch);
	writer.RegisterHandlerMethod(kATSaveStateSection_Private, this, &ATGTIAEmulator::SaveStatePrivate);	
}

void ATGTIAEmulator::SaveStateArch(ATSaveStateWriter& writer) {
	writer.BeginChunk(VDMAKEFOURCC('G', 'T', 'I', 'A'));

	// P/M pos
	for(int i=0; i<8; ++i)
		writer != mSpritePos[i];

	// P/M size
	for(int i=0; i<4; ++i)
		writer.WriteUint8(mSprites[i].mState.mSizeMode);

	writer.WriteUint8(
		(mSprites[4].mState.mSizeMode << 0) +
		(mSprites[5].mState.mSizeMode << 2) +
		(mSprites[6].mState.mSizeMode << 4) +
		(mSprites[7].mState.mSizeMode << 6));

	// graphics latches
	for(int i=0; i<4; ++i)
		writer.WriteUint8(mSprites[i].mState.mDataLatch);

	writer.WriteUint8(
		(mSprites[4].mState.mDataLatch >> 6) +
		(mSprites[5].mState.mDataLatch >> 4) +
		(mSprites[6].mState.mDataLatch >> 2) +
		(mSprites[7].mState.mDataLatch >> 0));

	// colors
	for(int i=0; i<4; ++i)
		writer != mPMColor[i];

	for(int i=0; i<4; ++i)
		writer != mPFColor[i];

	writer != mPFBAK;

	// misc registers
	writer != mPRIOR;
	writer != mVDELAY;
	writer != mGRACTL;
	writer != mSwitchOutput;

	writer.EndChunk();
}

void ATGTIAEmulator::SaveStatePrivate(ATSaveStateWriter& writer) {
	writer.BeginChunk(VDMAKEFOURCC('G', 'T', 'I', 'A'));
	ExchangeStatePrivate(writer);

	// write register changes
	writer.WriteUint32(mRCCount - mRCIndex);
	for(int i=mRCIndex; i<mRCCount; ++i) {
		const RegisterChange& rc = mRegisterChanges[i];

		writer.WriteSint16(rc.mPos);
		writer.WriteUint8(rc.mReg);
		writer.WriteUint8(rc.mValue);
	}

	mpRenderer->SaveState(writer);
	writer.EndChunk();
}

void ATGTIAEmulator::GetRegisterState(ATGTIARegisterState& state) const {
	state = mState;

	// $D000-D007 HPOSP0-3, HPOSM0-3
	for(int i=0; i<8; ++i)
		state.mReg[i] = mSpritePos[i];

	// $D008-D00B SIZEP0-3
	for(int i=0; i<4; ++i)
		state.mReg[i+8] = mSprites[i].mState.mSizeMode;

	// $D00C SIZEM
	state.mReg[0x0C] = 
		(mSprites[4].mState.mSizeMode << 0) +
		(mSprites[5].mState.mSizeMode << 2) +
		(mSprites[6].mState.mSizeMode << 4) +
		(mSprites[7].mState.mSizeMode << 6);

	// GRAFP0-GRAFP3
	state.mReg[0x0D] = mSprites[0].mState.mDataLatch;
	state.mReg[0x0E] = mSprites[1].mState.mDataLatch;
	state.mReg[0x0F] = mSprites[2].mState.mDataLatch;
	state.mReg[0x10] = mSprites[3].mState.mDataLatch;

	// GRAFM
	state.mReg[0x11] = 
		(mSprites[4].mState.mDataLatch >> 6) +
		(mSprites[5].mState.mDataLatch >> 4) +
		(mSprites[6].mState.mDataLatch >> 2) +
		(mSprites[7].mState.mDataLatch >> 0);

	state.mReg[0x12] = mPMColor[0];
	state.mReg[0x13] = mPMColor[1];
	state.mReg[0x14] = mPMColor[2];
	state.mReg[0x15] = mPMColor[3];
	state.mReg[0x16] = mPFColor[0];
	state.mReg[0x17] = mPFColor[1];
	state.mReg[0x18] = mPFColor[2];
	state.mReg[0x19] = mPFColor[3];
	state.mReg[0x1A] = mPFBAK;
	state.mReg[0x1B] = mPRIOR;
	state.mReg[0x1C] = mVDELAY;
	state.mReg[0x1D] = mGRACTL;
	state.mReg[0x1F] = mSwitchOutput;
}

void ATGTIAEmulator::SetFieldPolarity(bool polarity) {
	mbFieldPolarity = polarity;
}

void ATGTIAEmulator::SetVBLANK(VBlankMode vblMode) {
	mVBlankMode = vblMode;
}

bool ATGTIAEmulator::BeginFrame(bool force, bool drop) {
	if (mpFrame)
		return true;

	if (!mpDisplay)
		return true;

	if (!drop && !mpDisplay->RevokeBuffer(false, ~mpFrame)) {
		if (mpFrameTracker->mActiveFrames < 3) {
			ATFrameBuffer *fb = new ATFrameBuffer(mpFrameTracker);
			mpFrame = fb;

			fb->mPixmap.format = 0;
			fb->mbAllowConversion = true;
			fb->mbInterlaced = false;
			fb->mFlags = IVDVideoDisplay::kAllFields;
		} else if ((mpVideoTap || !mbTurbo) && !force) {
			if (!mpDisplay->RevokeBuffer(true, ~mpFrame))
				return false;
		}
	}

	bool use14MHz = (mpVBXE != NULL) || mArtifactMode == kArtifactNTSCHi || mArtifactMode == kArtifactPALHi;

	if (mpFrame) {
		ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);

		if (mbVsyncEnabled)
			fb->mFlags |= IVDVideoDisplay::kVSync;
		else
			fb->mFlags &= ~IVDVideoDisplay::kVSync;

		mbFrameCopiedFromPrev = false;
		mbPALThisFrame = mbPALMode;
		mbOverscanPALExtendedThisFrame = mbPALThisFrame && mbOverscanPALExtended;
		mb14MHzThisFrame = use14MHz;
		mbInterlaceEnabledThisFrame = mbInterlaceEnabled;
		mbScanlinesEnabledThisFrame = mbScanlinesEnabled && !mbInterlaceEnabled;
		mbPostProcessThisFrame = (mArtifactMode || mbBlendMode || mbScanlinesEnabledThisFrame) && !mpVBXE;

		const bool useArtifacting = mArtifactMode != kArtifactNone;
		const bool usePalArtifacting = mArtifactMode == kArtifactPAL || mArtifactMode == kArtifactPALHi;
		const bool useHighArtifacting = mArtifactMode == kArtifactNTSCHi || mArtifactMode == kArtifactPALHi;

		if (mbPostProcessThisFrame) {
			mPreArtifactFrame.h = mbOverscanPALExtendedThisFrame ? 312 : 262;

			mpArtifactingEngine->BeginFrame(usePalArtifacting, useArtifacting, useHighArtifacting, mbBlendMode);
		} else if (mpVBXE && (mArtifactMode == kArtifactPAL || mbBlendMode)) {
			mpArtifactingEngine->BeginFrame(usePalArtifacting, useArtifacting, useHighArtifacting, mbBlendMode);
		}

		int format = mArtifactMode || mbBlendMode || mbScanlinesEnabledThisFrame || use14MHz ? nsVDPixmap::kPixFormat_XRGB8888 : nsVDPixmap::kPixFormat_Pal8;

		int width = 456;
		if (use14MHz)
			width *= 2;

		int height = mbOverscanPALExtendedThisFrame ? 312 : 262;
		
		if (mbInterlaceEnabledThisFrame || mbScanlinesEnabledThisFrame)
			height *= 2;

		// check if we need to reinitialize the frame bitmap
		if (mpFrame->mPixmap.format != format || mpFrame->mPixmap.w != width || mpFrame->mPixmap.h != height) {
			VDPixmapLayout layout;
			VDPixmapCreateLinearLayout(layout, format, width, height, 16);

			// Add a little extra width on the end so we can go over slightly with MASKMOVDQU on SSE2
			// routines.
			fb->mBuffer.init(layout, 32);

			memset(fb->mBuffer.base(), 0, fb->mBuffer.size());
		}

		fb->mPixmap = fb->mBuffer;
		fb->mPixmap.palette = mPalette;

		mPreArtifactFrameVisible = mPreArtifactFrame;
		mPreArtifactFrameVisibleY1 = 0;
		mPreArtifactFrameVisibleY2 = mPreArtifactFrame.h;

		OverscanMode omode = mOverscanMode;
		VerticalOverscanMode vomode = DeriveVerticalOverscanMode();

		if (mbForcedBorder || mAnalysisMode) {
			omode = kOverscanFull;
			vomode = kVerticalOverscan_Full;
		}

		if (omode != kOverscanFull || vomode != kVerticalOverscan_Full) {
			int scanx1;
			int scanx2;
			int scany1;
			int scany2;

			switch(omode) {
				case kOverscanFull:
					scanx1 = 0*2;
					scanx2 = 228*2;
					break;

				case kOverscanExtended:
					scanx1 = 34*2;
					scanx2 = 222*2;
					break;

				case kOverscanNormal:
					scanx1 = 44*2;
					scanx2 = 212*2;
					break;

				case kOverscanOSScreen:
					scanx1 = 48*2;
					scanx2 = 208*2;
					break;
			}

			switch(vomode) {
				case kVerticalOverscan_Full:
					scany1 = 0;
					scany2 = 262;
					break;

				case kVerticalOverscan_Extended:
					scany1 = 8;
					scany2 = 248;
					break;

				case kVerticalOverscan_Normal:
					if (mbPALMode) {
						scany1 = 8;
						scany2 = 248;
					} else {
						scany1 = 16;
						scany2 = 240;
					}
					break;

				case kVerticalOverscan_OSScreen:
					scany1 = 32;
					scany2 = 224;
					break;
			}

			if (mbOverscanPALExtendedThisFrame) {
				if (vomode == kVerticalOverscan_Full) {
					scany1 = 0;
					scany2 = 312;
				} else {
					scany1 = 0;
					scany2 = 288;
				}
			}

			ptrdiff_t rawoffset = scanx1;
			ptrdiff_t offset = rawoffset;

			if (use14MHz) {
				// We're generating pixels at 2x rate, in 32-bit -> 8x normal.
				offset *= 8;
			} else if (mbPostProcessThisFrame) {
				// We're generating pixels at 1x rate, in 32-bit -> 4x normal.
				offset *= 4;
			}

			ptrdiff_t scanPitch = fb->mPixmap.pitch;

			if (mbInterlaceEnabledThisFrame || mbScanlinesEnabledThisFrame)
				scanPitch *= 2;

			fb->mPixmap.data = (char *)fb->mPixmap.data + offset + scanPitch * scany1;
			fb->mPixmap.w = scanx2 - scanx1;
			fb->mPixmap.h = scany2 - scany1;

			mPreArtifactFrameVisibleY1 = scany1;
			mPreArtifactFrameVisibleY2 = scany2;

			if (use14MHz) {
				fb->mPixmap.w *= 2;
				rawoffset *= 2;
			}

			mPreArtifactFrameVisible.data = (char *)mPreArtifactFrameVisible.data + rawoffset + mPreArtifactFrameVisibleY1*mPreArtifactFrameVisible.pitch;
			mPreArtifactFrameVisible.w = fb->mPixmap.w;
			mPreArtifactFrameVisible.h = fb->mPixmap.h;

			if (mbInterlaceEnabledThisFrame || mbScanlinesEnabledThisFrame)
				fb->mPixmap.h *= 2;
		}
	}

	mFrameTimestamp = mpConn->GTIAGetTimestamp();

	// Reset Y to avoid weirdness in immediate updates from being between BeginFrame() and
	// the first BeginScanline().
	mY = 0;

	return true;
}

void ATGTIAEmulator::BeginScanline(int y, bool hires) {
	// Flush remaining register changes (required for PRIOR to interact properly with hires)
	//
	// Note that we must use a cycle offset of -1 here because we haven't done DMA fetches
	// for this cycle yet!
	Sync(-1);

	mbMixedRendering = false;
	mbANTICHiresMode = hires;
	mbHiresMode = hires && !(mActivePRIOR & 0xc0);
	mbGTIADisableTransition = false;

	if ((unsigned)(y - 8) < 240)
		mbScanlinesWithHiRes[y - 8] = mbHiresMode;

	if (y == 8 && mpVBXE)
		mpVBXE->BeginFrame();

	mpDst = NULL;
	
	mY = y;
	mbPMRendered = false;

	if (mpFrame) {
		ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);

		int yw = y;
		int h = fb->mBuffer.h;

		if (mbPostProcessThisFrame)
			h = mPreArtifactFrame.h;
		else if (mbInterlaceEnabledThisFrame || mbScanlinesEnabledThisFrame)
			h >>= 1;

		if (mbOverscanPALExtendedThisFrame) {
			// What we do here is wrap the last 16 lines back up to the top of
			// the display. This isn't correct, as it causes those lines to
			// lead by a frame, but it at least solves the vertical position
			// issue.
			if (yw >= 312 - 16)
				yw -= 312 - 16;
			else
				yw += 16;
		}

		if (yw < h) {
			if (mbPostProcessThisFrame)
				mpDst = &mPreArtifactFrameBuffer[yw * 464];
			else if (mbInterlaceEnabledThisFrame)
				mpDst = (uint8 *)fb->mBuffer.data + yw * fb->mBuffer.pitch*2 + (mbFieldPolarity ? fb->mBuffer.pitch : 0);
			else if (mbScanlinesEnabledThisFrame) 
				mpDst = (uint8 *)fb->mBuffer.data + yw * fb->mBuffer.pitch*2;
			else
				mpDst = (uint8 *)fb->mBuffer.data + yw * fb->mBuffer.pitch;
		} else {
			mpDst = NULL;
		}
	}

	memset(mMergeBuffer, 0, sizeof mMergeBuffer);
	memset(mAnticData, 0, sizeof mAnticData);

	if (mpVBXE)
		mpVBXE->BeginScanline((uint32*)mpDst, mMergeBuffer, mAnticData, mbHiresMode);
	else if (mpDst) {
		mpRenderer->SetVBlank((uint32)(y - 8) >= 240);
		mpRenderer->BeginScanline(mpDst, mMergeBuffer, mAnticData, mbHiresMode);
	}
}

void ATGTIAEmulator::EndScanline(uint8 dlControl, bool pfrendered) {
	// flush any remaining changes
	Sync();

	if (mpDst) {
		if (mpVBXE)
			mpVBXE->RenderScanline(222, pfrendered || mbPMRendered);
		else
			mpRenderer->RenderScanline(222, pfrendered, mbPMRendered, mbMixedRendering);
	}

	if (mpVBXE)
		mpVBXE->EndScanline();
	else
		mpRenderer->EndScanline();

	// move down buffers as necessary and offset all pending render changes by -scanline
	if (mRCIndex >= 64) {
		mRegisterChanges.erase(mRegisterChanges.begin(), mRegisterChanges.begin() + mRCIndex);
		mRCCount -= mRCIndex;
		mRCIndex = 0;
	}

	for(int i=mRCIndex; i<mRCCount; ++i) {
		mRegisterChanges[i].mPos -= 228;
	}

	for(int i=0; i<8; ++i) {
		Sprite& sprite = mSprites[i];

		sprite.mLastSync -= 228;

		// make sure the sprites don't get too far behind
		if (sprite.mLastSync < -10000)
			sprite.Sync(-2);

		for(SpriteImage *image = sprite.mpImageHead; image; image = image->mpNext) {
			image->mX1 -= 228;
			image->mX2 -= 228;
		}

		// delete any sprites that are too old (this can happen in vblank)
		while(sprite.mpImageHead && sprite.mpImageHead->mX2 < 34) {
			SpriteImage *next = sprite.mpImageHead->mpNext;
			FreeSpriteImage(sprite.mpImageHead);
			sprite.mpImageHead = next;

			if (!next)
				sprite.mpImageTail = NULL;
		}

		// check if the sprite is stuck -- if so, continue extending the current image
		if (sprite.mpImageTail && sprite.mState.mSizeMode == 2 && (sprite.mState.mShiftState == 1 || sprite.mState.mShiftState == 2)) {
			sprite.mpImageTail->mX1 = -2;
			sprite.mpImageTail->mX2 = 1024;
		}
	}

	// We have to restart at -2 instead of 0 because GTIA runs two color clocks head of ANTIC
	// for timing purposes.
	mLastSyncX = -2;

	if (!mpDst)
		return;

	switch(mAnalysisMode) {
		case kAnalyzeNone:
			break;
		case kAnalyzeColors:
			for(int i=0; i<9; ++i)
				mpDst[i*2+0] = mpDst[i*2+1] = ((const uint8 *)mPMColor)[i];
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

void ATGTIAEmulator::UpdatePlayer(bool odd, int index, uint8 byte) {
	if (mGRACTL & 2) {
		if (odd || !(mVDELAY & (0x10 << index))) {
			const uint8 xpos = mpConn->GTIAGetXClock();
			AddRegisterChange(xpos + 3, 0x0D + index, byte);
		}
	}
}

void ATGTIAEmulator::UpdateMissile(bool odd, uint8 byte) {
	if (mGRACTL & 1) {
		const uint8 xpos = mpConn->GTIAGetXClock();
		AddRegisterChange(xpos + 3, 0x20, byte);
	}
}

void ATGTIAEmulator::UpdatePlayfield160(uint32 x, uint8 byte) {
	VDASSERT(x < 114);

	uint8 *dst = &mMergeBuffer[x*2];

	dst[0] = (byte >>  4) & 15;
	dst[1] = (byte      ) & 15;
}

void ATGTIAEmulator::UpdatePlayfield160(uint32 x, const uint8 *__restrict src, uint32 n) {
	if (!n)
		return;

	VDASSERT(x < 114);
	uint8 *__restrict dst = &mMergeBuffer[x*2];

#ifdef VD_CPU_X86
	if (SSE2_enabled) {
		atasm_update_playfield_160_sse2(dst, src, n);
		return;
	}
#endif

	do {
		const uint8 byte = *src++;
		dst[0] = (byte >>  4) & 15;
		dst[1] = (byte      ) & 15;
		dst += 2;
	} while(--n);
}

void ATGTIAEmulator::UpdatePlayfield320(uint32 x, uint8 byte) {
	uint8 *dstx = &mMergeBuffer[x];
	dstx[0] = PF2;
	dstx[1] = PF2;
	
	VDASSERT(x < 228);

	uint8 *dst = &mAnticData[x];
	dst[0] = (byte >> 2) & 3;
	dst[1] = (byte >> 0) & 3;
}

void ATGTIAEmulator::UpdatePlayfield320(uint32 x, const uint8 *src, uint32 n) {
	VDASSERT(x < 228);

	memset(&mMergeBuffer[x], PF2, n*2);
	
	uint8 *dst = &mAnticData[x];
	do {
		const uint8 byte = *src++;
		dst[0] = (byte >> 2) & 3;
		dst[1] = (byte >> 0) & 3;
		dst += 2;
	} while(--n);
}

namespace {
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

void ATGTIAEmulator::Sync(int offset) {
	mpConn->GTIARequestAnticSync(offset);

	int xend = (int)mpConn->GTIAGetXClock() + 2;

	if (xend > 228)
		xend = 228;

	SyncTo(xend);
}

void ATGTIAEmulator::SyncTo(int xend) {
	int x1 = mLastSyncX;

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

		if (x2 > x1) {
			if (mbSpritesActive)
				GenerateSpriteImages(x1, x2);

			Render(x1, x2);
			x1 = x2;
		}
	} while(x1 < xend);

	mLastSyncX = x1;
}

void ATGTIAEmulator::Render(int x1, int x2) {
	if (mVBlankMode == kVBlankModeOn)
		return;

	// determine displayed range
	int xc1 = x1;
	if (xc1 < 34)
		xc1 = 34;

	int xc2 = x2;
	if (xc2 > 222)
		xc2 = 222;

	if (xc2 <= xc1)
		return;

	// convert modes if necessary
	bool needHires = mbHiresMode || (mActivePRIOR & 0xC0);
	if (needHires != mbANTICHiresMode) {
		mbMixedRendering = true;

		if (mbANTICHiresMode)
			Convert320To160(xc1, xc2, mMergeBuffer, mAnticData);
		else
			Convert160To320(xc1, xc2, mAnticData, mMergeBuffer);
	}

	static const uint8 kPFTable[16]={
		0, 0, 0, 0, PF0, PF1, PF2, PF3,
		0, 0, 0, 0, PF0, PF1, PF2, PF3,
	};

	static const uint8 kPFMask[16]={
		0xF0, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF,
	};

	if (xc1 < xc2) {
		switch(mActivePRIOR & 0xC0) {
			case 0x00:
				break;
			case 0x80:
				if (mbANTICHiresMode) {
					const uint8 *__restrict ad = &mAnticData[(xc1 - 1) & ~1];
					uint8 *__restrict dst = &mMergeBuffer[xc1];

					int w = xc2 - xc1;
					if (!(xc1 & 1)) {
						uint8 c = ad[0]*4 + ad[1];
						ad += 2;

						*dst++ = kPFTable[c];
						--w;
					}

					int w2 = w >> 1;
					while(w2--) {
						uint8 c = ad[0]*4 + ad[1];
						ad += 2;

						dst[0] = dst[1] = kPFTable[c];
						dst += 2;
					}

					if (w & 1) {
						uint8 c = ad[0]*4 + ad[1];
						*dst++ = kPFTable[c];
					}
				} else {
					const uint8 *__restrict ad = &mAnticData[(xc1 - 1) & ~1];
					uint8 *__restrict dst = &mMergeBuffer[xc1];

					int w = xc2 - xc1;
					if (!(xc1 & 1)) {
						uint8 c = ad[0]*4 + ad[1];
						ad += 2;

						*dst = kPFTable[c] & kPFMask[dst[-1] & 15];
						++dst;
						--w;
					}

					int w2 = w >> 1;
					while(w2--) {
						uint8 c = ad[0]*4 + ad[1];
						ad += 2;

						dst[0] = dst[1] = kPFTable[c] & kPFMask[dst[0] & 15];
						dst += 2;
					}

					if (w & 1) {
						uint8 c = ad[0]*4 + ad[1];
						*dst = kPFTable[c] & kPFMask[dst[0] & 15];
					}
				}
				break;
			case 0x40:
			case 0xC0:
				memset(mMergeBuffer + xc1, 0, (xc2 - xc1));
				break;
		}
	}

	if (mbGTIADisableTransition) {
		mbGTIADisableTransition = false;

		// The effects of the GTIA ANx latches are still in effect, which causes the low
		// two bits to be repeated.

		if (x1 >= xc1 && mMergeBuffer[x1])
			mMergeBuffer[x1] = kPFTable[4 + mAnticData[x1 - 1]];
	}

	uint8 *dst = mMergeBuffer;

	// flush player images
	for(int i=0; i<4; ++i) {
		Sprite& sprite = mSprites[i];

		// check if we have any sprite images
		if (!sprite.mpImageHead)
			continue;

		// expire old sprite images
		for(;;) {
			if (sprite.mpImageHead->mX2 > xc1)
				break;

			SpriteImage *next = sprite.mpImageHead->mpNext;
			FreeSpriteImage(sprite.mpImageHead);
			sprite.mpImageHead = next;

			if (!next) {
				sprite.mpImageTail = NULL;
				break;
			}
		}

		// render out existing images
		for(SpriteImage *image = sprite.mpImageHead; image; image = image->mpNext) {
			if (image->mX1 >= xc2)
				break;

			if (image->mX1 < xc1) {
				image->mState.Advance((uint32)(xc1 - image->mX1));
				image->mX1 = xc1;
			}

			int minx2 = image->mX2;
			if (minx2 > xc2)
				minx2 = xc2;

			if (mbHiresMode)
				mPlayerCollFlags[i] |= image->mState.Generate(minx2 - image->mX1, P0 << i, mMergeBuffer + image->mX1, mAnticData + image->mX1);
			else
				mPlayerCollFlags[i] |= image->mState.Generate(minx2 - image->mX1, P0 << i, mMergeBuffer + image->mX1); 

			mbPMRendered = true;
		}
	}

	// Flush missile images.
	//
	// We _have_ to do this as two pass as the missiles will start overlapping with
	// the players once the scanout has started. Therefore, we do detection over all
	// missiles first before rendering any of them.

	for(int i=0; i<4; ++i) {
		Sprite& sprite = mSprites[i + 4];

		// check if we have any sprite images
		if (!sprite.mpImageHead)
			continue;

		// expire old sprite images
		for(;;) {
			if (sprite.mpImageHead->mX2 > xc1)
				break;

			SpriteImage *next = sprite.mpImageHead->mpNext;
			FreeSpriteImage(sprite.mpImageHead);
			sprite.mpImageHead = next;

			if (!next) {
				sprite.mpImageTail = NULL;
				break;
			}
		}

		// render out existing images
		for(SpriteImage *image = sprite.mpImageHead; image; image = image->mpNext) {
			if (image->mX1 >= xc2)
				break;

			if (image->mX1 < xc1) {
				image->mState.Advance((uint32)(xc1 - image->mX1));
				image->mX1 = xc1;
			}

			int minx2 = image->mX2;
			if (minx2 > xc2)
				minx2 = xc2;

			if (mbHiresMode)
				mMissileCollFlags[i] |= image->mState.Detect(minx2 - image->mX1, mMergeBuffer + image->mX1, mAnticData + image->mX1);
			else
				mMissileCollFlags[i] |= image->mState.Detect(minx2 - image->mX1, mMergeBuffer + image->mX1);
		}
	}

	for(int i=0; i<4; ++i) {
		Sprite& sprite = mSprites[i + 4];

		// render out existing images
		for(SpriteImage *image = sprite.mpImageHead; image; image = image->mpNext) {
			if (image->mX1 >= xc2)
				break;

			int minx2 = image->mX2;
			if (minx2 > xc2)
				minx2 = xc2;

			image->mState.Generate(minx2 - image->mX1, (mActivePRIOR & 0x10) ? PF3 : (P0 << i), mMergeBuffer + image->mX1); 
			mbPMRendered = true;
		}
	}
}

void ATGTIAEmulator::RenderActivityMap(const uint8 *src) {
	if (!mpFrame)
		return;

	ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);
	uint8 *dst = (uint8 *)fb->mBuffer.data;

	// if PAL extended is enabled, there are 16 lines wrapped from the bottom to the top
	// of the framebuffer that we must skip and loop back to
	if (mbOverscanPALExtendedThisFrame)
		dst += 16 * fb->mBuffer.pitch;

	int h = this->mbOverscanPALExtendedThisFrame ? 312 : 262;

	for(int y=0; y<h; ++y) {
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

		if (y == 312 - 16 - 1)
			dst = (uint8 *)fb->mBuffer.data;
	}
}

void ATGTIAEmulator::UpdateScreen(bool immediate, bool forceAnyScreen) {
	if (!mpFrame) {
		if (forceAnyScreen && mpLastFrame)
			mpDisplay->SetSourcePersistent(true, mpLastFrame->mPixmap);

		mbLastFieldPolarity = mbFieldPolarity;
		return;
	}

	ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);

	if (immediate) {
		const VDPixmap& pxdst = mbPostProcessThisFrame ? mPreArtifactFrame : fb->mBuffer;
		uint32 x = mpConn->GTIAGetXClock();

		Sync();

		if (mpDst) {
			if (mpVBXE)
				mpVBXE->RenderScanline(x, true);
			else
				mpRenderer->RenderScanline(x, true, mbPMRendered, mbMixedRendering);
		}

		uint32 y = mY + 1;

		if (mbOverscanPALExtendedThisFrame) {
			// What we do here is wrap the last 16 lines back up to the top of
			// the display. This isn't correct, as it causes those lines to
			// lead by a frame, but it at least solves the vertical position
			// issue.
			if (y >= 312 - 16)
				y -= 312 - 16;
			else
				y += 16;
		}

		if (!mbPostProcessThisFrame && mbInterlaceEnabledThisFrame) {
			y += y;

			if (mbFieldPolarity)
				++y;
		}

		if (y < (uint32)pxdst.h) {
			uint8 *row = (uint8 *)pxdst.data + y*pxdst.pitch;

			if (mpVBXE) {
				VDMemset32(row, 0x00, 4*x);
				VDMemset32(row + x*4*4, 0xFFFF00, 912 - 4*x);
			} else {
				VDMemset8(row, 0x00, 2*x);
				VDMemset8(row + x*2, 0xFF, 464 - 2*x);
			}
		}

		if (!mbFrameCopiedFromPrev && !mbPostProcessThisFrame && mpLastFrame && y+1 < (uint32)pxdst.h) {
			mbFrameCopiedFromPrev = true;

			VDPixmapBlt(fb->mBuffer, 0, y+1, static_cast<ATFrameBuffer *>(&*mpLastFrame)->mBuffer, 0, y+1, pxdst.w, pxdst.h - (y + 1));
		}

		ApplyArtifacting(true);

		mpDisplay->SetSourcePersistent(true, mpFrame->mPixmap);
	} else {
		const VDPixmap& pxdst = mbPostProcessThisFrame ? mPreArtifactFrameVisible : fb->mPixmap;

		ApplyArtifacting(false);

		// copy over previous field
		if (mbInterlaceEnabledThisFrame) {
			VDPixmap dstField(VDPixmapExtractField(mpFrame->mPixmap, !mbFieldPolarity));

			if (mpLastFrame &&
				mpLastFrame->mPixmap.w == mpFrame->mPixmap.w &&
				mpLastFrame->mPixmap.h == mpFrame->mPixmap.h &&
				mpLastFrame->mPixmap.format == mpFrame->mPixmap.format &&
				mbFieldPolarity != mbLastFieldPolarity) {
				VDPixmap srcField(VDPixmapExtractField(mpLastFrame->mPixmap, !mbFieldPolarity));

				VDPixmapBlt(dstField, srcField);
			} else {
				VDPixmap srcField(VDPixmapExtractField(mpFrame->mPixmap, mbFieldPolarity));
				
				VDPixmapBlt(dstField, srcField);
			}

			mbLastFieldPolarity = mbFieldPolarity;
		}

		if (mpVideoTap)
			mpVideoTap->WriteFrame(mpFrame->mPixmap, mFrameTimestamp);

		if (mbTurbo)
			mpFrame->mFlags |= IVDVideoDisplay::kDoNotWait;
		else
			mpFrame->mFlags &= ~IVDVideoDisplay::kDoNotWait;

		mpDisplay->PostBuffer(mpFrame);

		mpLastFrame = mpFrame;

		mpFrame = NULL;
	}
}

void ATGTIAEmulator::RecomputePalette() {
	uint32 *dst = mPalette;

	const ATColorParams& params = mbPALMode && mColorSettings.mbUsePALParams ? mColorSettings.mPALParams : mColorSettings.mNTSCParams;
	const bool palQuirks = params.mbUsePALQuirks;
	float angle = (params.mHueStart + (palQuirks ? -33.0f : 0.0f)) * (nsVDMath::kfTwoPi / 360.0f);
	float angleStep = params.mHueRange * (nsVDMath::kfTwoPi / (360.0f * 15.0f));
	float gamma = 1.0f / params.mGammaCorrect;

	float lumaRamp[16];

	ATComputeLumaRamp(params.mLumaRampMode, lumaRamp);

	for(int hue=0; hue<16; ++hue) {
		float i = 0;
		float q = 0;

		if (hue) {
			if (palQuirks) {
				static const float kPALPhaseLookup[][4]={
					{ -1.0f,  1, -5.0f,  1 },
					{  0.0f,  1, -6.0f,  1 },
					{ -7.0f, -1, -7.0f,  1 },
					{ -6.0f, -1,  0.0f, -1 },
					{ -5.0f, -1, -1.0f, -1 },
					{ -4.0f, -1, -2.0f, -1 },
					{ -2.0f, -1, -4.0f, -1 },
					{ -1.0f, -1, -5.0f, -1 },
					{  0.0f, -1, -6.0f, -1 },
					{ -7.0f,  1, -7.0f, -1 },
					{ -5.0f,  1, -1.0f,  1 },
					{ -4.0f,  1, -2.0f,  1 },
					{ -3.0f,  1, -3.0f,  1 },
					{ -2.0f,  1, -4.0f,  1 },
					{ -1.0f,  1, -5.0f,  1 },
				};

				const float *co = kPALPhaseLookup[hue - 1];

				float angle2 = angle + angleStep * (co[0] + 3.0f);
				float angle3 = angle + angleStep * (-co[2] - 3.0f);
				float i2 = cosf(angle2) * co[1];
				float q2 = sinf(angle2) * co[1];
				float i3 = cosf(angle3) * co[3];
				float q3 = sinf(angle3) * co[3];

				i = (i2 + i3) * (0.5f * params.mSaturation);
				q = (q2 + q3) * (0.5f * params.mSaturation);
			} else {
				i = params.mSaturation * cos(angle);
				q = params.mSaturation * sin(angle);
				angle += angleStep;
			}
		}

		for(int luma=0; luma<16; ++luma) {
			double y = params.mContrast * lumaRamp[luma] + params.mBrightness;

			double r = y + 0.956*i + 0.621*q;
			double g = y - 0.272*i - 0.647*q;
			double b = y - 1.107*i + 1.704*q;

			if (r > 0.0f)
				r = pow(r, gamma);

			if (g > 0.0f)
				g = pow(g, gamma);

			if (b > 0.0f)
				b = pow(b, gamma);

			*dst++	= (VDClampedRoundFixedToUint8Fast((float)r) << 16)
					+ (VDClampedRoundFixedToUint8Fast((float)g) <<  8)
					+ (VDClampedRoundFixedToUint8Fast((float)b)      );
		}
	}

	if (mpVBXE)
		mpVBXE->SetDefaultPalette(mPalette);

	mpArtifactingEngine->SetColorParams(params);
}

uint8 ATGTIAEmulator::ReadByte(uint8 reg) {
	reg &= 0x1F;

	// fast registers
	switch(reg) {
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
			if (mbSECAMMode)
				UpdateSECAMTriggerLatch(reg - 0x10);

			return (mGRACTL & 4) ? mTRIGLatched[reg - 0x10] : mTRIG[reg - 0x10];
		case 0x14:
			return mbPALMode ? 0x01 : 0x0F;
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
			return (~mSwitchOutput & mSwitchInput & mForcedSwitchInput) & 15;
	}

	Sync();	

	switch(reg) {
		// missile-to-playfield collisions
		case 0x00:	return mMissileCollFlags[0] & 15 & mCollisionMask;
		case 0x01:	return mMissileCollFlags[1] & 15 & mCollisionMask;
		case 0x02:	return mMissileCollFlags[2] & 15 & mCollisionMask;
		case 0x03:	return mMissileCollFlags[3] & 15 & mCollisionMask;

		// player-to-playfield collisions
		case 0x04:	return mPlayerCollFlags[0] & 15 & mCollisionMask;
		case 0x05:	return mPlayerCollFlags[1] & 15 & mCollisionMask;
		case 0x06:	return mPlayerCollFlags[2] & 15 & mCollisionMask;
		case 0x07:	return mPlayerCollFlags[3] & 15 & mCollisionMask;

		// missile-to-player collisions
		case 0x08:	return (mMissileCollFlags[0] & mCollisionMask) >> 4;
		case 0x09:	return (mMissileCollFlags[1] & mCollisionMask) >> 4;
		case 0x0A:	return (mMissileCollFlags[2] & mCollisionMask) >> 4;
		case 0x0B:	return (mMissileCollFlags[3] & mCollisionMask) >> 4;

		// player-to-player collisions
		case 0x0C:	return (  ((mPlayerCollFlags[1] >> 3) & 0x02)	// 1 -> 0
							+ ((mPlayerCollFlags[2] >> 2) & 0x04)	// 2 -> 0
							+ ((mPlayerCollFlags[3] >> 1) & 0x08)) & (mCollisionMask >> 4);	// 3 -> 0

		case 0x0D:	return (  ((mPlayerCollFlags[1] >> 4) & 0x01)	// 1 -> 0
							+ ((mPlayerCollFlags[2] >> 3) & 0x04)	// 2 -> 1
							+ ((mPlayerCollFlags[3] >> 2) & 0x08)) & (mCollisionMask >> 4);	// 3 -> 1

		case 0x0E:	return (  ((mPlayerCollFlags[2] >> 4) & 0x03)	// 2 -> 0, 1
							+ ((mPlayerCollFlags[3] >> 3) & 0x08)) & (mCollisionMask >> 4);	// 3 -> 2

		case 0x0F:	return (  ((mPlayerCollFlags[3] >> 4) & 0x07)) & (mCollisionMask >> 4);	// 3 -> 0, 1, 2

		default:
//			__debugbreak();
			break;
	}
	return 0;
}

void ATGTIAEmulator::WriteByte(uint8 reg, uint8 value) {
	reg &= 0x1F;

	mState.mReg[reg] = value;

	switch(reg) {
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x15:
			mPMColor[reg - 0x12] = value & 0xfe;
			if (mpVBXE)
				mpVBXE->AddRegisterChange(mpConn->GTIAGetXClock() + 1, reg, value);
			else
				mpRenderer->AddRegisterChange(mpConn->GTIAGetXClock() + 1, reg, value);
			break;

		case 0x16:
		case 0x17:
		case 0x18:
		case 0x19:
			mPFColor[reg - 0x16] = value & 0xfe;
			if (mpVBXE)
				mpVBXE->AddRegisterChange(mpConn->GTIAGetXClock() + 1, reg, value);
			else
				mpRenderer->AddRegisterChange(mpConn->GTIAGetXClock() + 1, reg, value);
			break;

		case 0x1A:
			mPFBAK = value & 0xfe;
			if (mpVBXE)
				mpVBXE->AddRegisterChange(mpConn->GTIAGetXClock() + 1, reg, value);
			else
				mpRenderer->AddRegisterChange(mpConn->GTIAGetXClock() + 1, reg, value);
			break;

		case 0x1B:
			mPRIOR = value;
			break;

		case 0x1C:
			mVDELAY = value;
			return;

		case 0x1D:
			// We actually need to sync the latches when latching is *enabled*, since they
			// are always updated but only read when latching is enabled.
			if (~mGRACTL & value & 4) {
				if (mbSECAMMode) {
					for(int i=0; i<4; ++i)
						UpdateSECAMTriggerLatch(i);
				}

				mTRIGLatched[0] = mTRIG[0];
				mTRIGLatched[1] = mTRIG[1];
				mTRIGLatched[2] = mTRIG[2];
				mTRIGLatched[3] = mTRIG[3];
			}

			mGRACTL = value;
			return;

		case 0x1F:		// $D01F CONSOL
			{
				uint8 newConsol = value & 0x0F;
				uint8 delta = newConsol ^ mSwitchOutput;
				if (delta) {
					if (delta & 8)
						mpConn->GTIASetSpeaker(0 != (newConsol & 8));

					if (delta & 7)
						mpConn->GTIASelectController(newConsol & 3, (newConsol & 4) != 0);
				}
				mSwitchOutput = newConsol;
			}
			return;
	}

	const uint8 xpos = mpConn->GTIAGetXClock();

	switch(reg) {
		case 0x00:	// $D000 HPOSP0
		case 0x01:	// $D001 HPOSP1
		case 0x02:	// $D002 HPOSP2
		case 0x03:	// $D003 HPOSP3
		case 0x04:	// $D004 HPOSM0
		case 0x05:	// $D005 HPOSM1
		case 0x06:	// $D006 HPOSM2
		case 0x07:	// $D007 HPOSM3
			AddRegisterChange(xpos + 5, reg, value);
			break;
		case 0x08:	// $D008 SIZEP0
		case 0x09:	// $D009 SIZEP1
		case 0x0A:	// $D00A SIZEP2
		case 0x0B:	// $D00B SIZEP3
		case 0x0C:	// $D00C SIZEM
			AddRegisterChange(xpos + 3, reg, value);
			break;
		case 0x0D:	// $D00D GRAFP0
		case 0x0E:	// $D00E GRAFP1
		case 0x0F:	// $D00F GRAFP2
		case 0x10:	// $D010 GRAFP3
		case 0x11:	// $D011 GRAFM
			AddRegisterChange(xpos + 3, reg, value);
			break;

		case 0x1B:	// $D01B PRIOR

			// PRIOR is quite an annoying register, since several of its components
			// take effect at different stages in the color pipeline:
			//
			//	|			|			|			|			|			|
			//	|			B->	PFx	----B-> priA ---B-> priB ---B->color----B-> output
			//	|			|	decode	|	 ^		|	 ^		|  lookup	|     ^
			//	|			|			|  pri0-3	|   5th		|			|	  |
			//	|			|			|			|  player	| mode 9/11	B-----/
			//	|			|			|			|			|	enable	|
			//	|			|			|			|			|			|
			//	2			1			2			1			2			1

			AddRegisterChange(xpos + 2, reg, value);
			if (mpVBXE)
				mpVBXE->AddRegisterChange(xpos + 1, reg, value);
			else
				mpRenderer->AddRegisterChange(xpos + 1, reg, value);
			break;

		case 0x1E:	// $D01E HITCLR
			AddRegisterChange(xpos + 3, reg, value);
			break;
	}
}

void ATGTIAEmulator::ApplyArtifacting(bool immediate) {
	if (mpVBXE) {
		const bool doBlending = mArtifactMode == kArtifactPAL || mbBlendMode;

		if (doBlending || mbScanlinesEnabledThisFrame) {
			ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);
			char *dstrow = (char *)fb->mBuffer.data;
			ptrdiff_t dstpitch = fb->mBuffer.pitch;
			uint32 h = fb->mBuffer.h;

			if (mbInterlaceEnabledThisFrame) {
				if (mbFieldPolarity)
					dstrow += dstpitch;

				dstpitch *= 2;
				h >>= 1;
			} else if (mbScanlinesEnabledThisFrame)
				h >>= 1;

			for(uint32 row=0; row<h; ++row) {
				uint32 *dst = (uint32 *)dstrow;

				if (doBlending)
					mpArtifactingEngine->Artifact32(row, dst, 912, immediate);

				if (mbScanlinesEnabledThisFrame) {
					if (row)
						mpArtifactingEngine->InterpolateScanlines((uint32 *)(dstrow - dstpitch), (const uint32 *)(dstrow - 2*dstpitch), dst, 912);

					dstrow += dstpitch;
				}

				dstrow += dstpitch;
			}

			if (mbScanlinesEnabledThisFrame) {
				mpArtifactingEngine->InterpolateScanlines(
					(uint32 *)(dstrow - dstpitch),
					(const uint32 *)(dstrow - 2*dstpitch),
					(const uint32 *)(dstrow - 2*dstpitch),
					912);
			}
		}

		return;
	}

	if (!mbPostProcessThisFrame)
		return;

	ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);
	char *dstrow = (char *)fb->mBuffer.data;
	ptrdiff_t dstpitch = fb->mBuffer.pitch;

	if (mbInterlaceEnabledThisFrame) {
		if (mbFieldPolarity)
			dstrow += dstpitch;

		dstpitch *= 2;
	}

	const uint8 *srcrow = (const uint8 *)mPreArtifactFrame.data;
	ptrdiff_t srcpitch = mPreArtifactFrame.pitch;
	uint32 srch = mPreArtifactFrame.h;

	uint32 y1 = mPreArtifactFrameVisibleY1;
	uint32 y2 = mPreArtifactFrameVisibleY2;

	if (y1)
		--y1;

	if (mbScanlinesEnabledThisFrame)
		dstrow += dstpitch * 2 * y1;
	else
		dstrow += dstpitch * y1;

	srcrow += srcpitch * y1;

	// In PAL extended mode, we wrap the bottom 16 lines back up to the top, thus
	// the weird adjustment here.
	const uint32 vstart = mbOverscanPALExtendedThisFrame ? 24 : 8;
	const uint32 w = mb14MHzThisFrame ? 912 : 456;

	for(uint32 row=y1; row<y2; ++row) {
		uint32 *dst = (uint32 *)dstrow;
		const uint8 *src = srcrow;

		uint32 relativeRow = row - vstart;

		mpArtifactingEngine->Artifact8(row, dst, src, relativeRow < 240 && mbScanlinesWithHiRes[relativeRow], immediate);

		if (mbScanlinesEnabledThisFrame) {
			if (row > y1)
				mpArtifactingEngine->InterpolateScanlines((uint32 *)(dstrow - dstpitch), (const uint32 *)(dstrow - 2*dstpitch), dst, w);

			dstrow += dstpitch;
		}

		srcrow += srcpitch;
		dstrow += dstpitch;
	}

	if (mbScanlinesEnabledThisFrame) {
		mpArtifactingEngine->InterpolateScanlines(
			(uint32 *)(dstrow - dstpitch),
			(const uint32 *)(dstrow - 2*dstpitch),
			(const uint32 *)(dstrow - 2*dstpitch),
			w);
	}
}

void ATGTIAEmulator::AddRegisterChange(uint8 pos, uint8 addr, uint8 value) {
	RegisterChanges::iterator it(mRegisterChanges.end()), itBegin(mRegisterChanges.begin() + mRCIndex);

	while(it != itBegin && it[-1].mPos > pos)
		--it;

	RegisterChange change;
	change.mPos = pos;
	change.mReg = addr;
	change.mValue = value;
	mRegisterChanges.insert(it, change);

	++mRCCount;
}

void ATGTIAEmulator::UpdateRegisters(const RegisterChange *rc, int count) {
	while(count--) {
		uint8 value = rc->mValue;

		switch(rc->mReg) {
			case 0x00:
			case 0x01:
			case 0x02:
			case 0x03:
			case 0x04:
			case 0x05:
			case 0x06:
			case 0x07:
				mSpritePos[rc->mReg] = value;
				break;

			case 0x08:
			case 0x09:
			case 0x0A:
			case 0x0B:
				{
					Sprite& sprite = mSprites[rc->mReg & 3];
					const uint8 newSize = value & 3;

					if (sprite.mState.mSizeMode != newSize) {
						// catch sprite state up to this point
						sprite.Sync(rc->mPos);

						// change sprite mode
						sprite.mState.mSizeMode = newSize;

						// generate update image
						GenerateSpriteImage(sprite, rc->mPos);
					}
				}
				break;

			case 0x0C:
				for(int i=0; i<4; ++i) {
					Sprite& sprite = mSprites[i+4];

					const uint8 newSize = (value >> (2*i)) & 3;

					if (sprite.mState.mSizeMode != newSize) {
						// catch sprite state up to this point
						sprite.Sync(rc->mPos);

						// switch size mode
						sprite.mState.mSizeMode = newSize;

						// generate update image
						GenerateSpriteImage(sprite, rc->mPos);
					}
				}
				break;
			case 0x0D:
			case 0x0E:
			case 0x0F:
			case 0x10:
				mSprites[rc->mReg - 0x0D].mState.mDataLatch = value;
				if (value)
					mbSpritesActive = true;
				break;
			case 0x11:
				mSprites[4].mState.mDataLatch = (value << 6) & 0xc0;
				mSprites[5].mState.mDataLatch = (value << 4) & 0xc0;
				mSprites[6].mState.mDataLatch = (value << 2) & 0xc0;
				mSprites[7].mState.mDataLatch = (value     ) & 0xc0;
				if (value)
					mbSpritesActive = true;
				break;

			case 0x1B:
				if (!(value & 0xc0) && (mActivePRIOR & 0xc0))
					mbGTIADisableTransition = true;

				mActivePRIOR = value;

				if (value & 0xC0)
					mbHiresMode = false;

				break;
			case 0x1E:		// $D01E HITCLR
				memset(mPlayerCollFlags, 0, sizeof mPlayerCollFlags);
				memset(mMissileCollFlags, 0, sizeof mMissileCollFlags);
				break;

			case 0x20:		// missile DMA
				{
					uint8 mask = 0x0F;

					// We get called after ANTIC has bumped the scanline but before GTIA knows about it,
					// so mY is actually one off.
					if (mY & 1)
						mask = ~mVDELAY;

					if (mask & 0x01)
						mSprites[4].mState.mDataLatch = (value << 6) & 0xc0;

					if (mask & 0x02)
						mSprites[5].mState.mDataLatch = (value << 4) & 0xc0;

					if (mask & 0x04)
						mSprites[6].mState.mDataLatch = (value << 2) & 0xc0;

					if (mask & 0x08)
						mSprites[7].mState.mDataLatch = (value     ) & 0xc0;

					if (value)
						mbSpritesActive = true;
				}
				break;
		}

		++rc;
	}
}

void ATGTIAEmulator::UpdateSECAMTriggerLatch(int index) {
	// The 107 is a guess. The triggers start shifting into FGTIA on the
	// third cycle of horizontal blank according to the FGTIA doc.
	uint32 t = mpConn->GTIAGetLineEdgeTimingId(107 + index);

	if (mTRIGSECAMLastUpdate[index] != t) {
		mTRIGSECAMLastUpdate[index] = t;

		const uint8 v = mTRIGSECAM[index];
		mTRIG[index] = v;
		mTRIGLatched[index] &= v;
	}
}

void ATGTIAEmulator::ResetSprites() {
	for(int i=0; i<8; ++i) {
		Sprite& sprite = mSprites[i];

		sprite.mLastSync = 0;
		sprite.mState.Reset();

		SpriteImage *p = sprite.mpImageHead;
		while(p) {
			SpriteImage *next = p->mpNext;

			FreeSpriteImage(p);

			p = next;
		}

		sprite.mpImageHead = NULL;
		sprite.mpImageTail = NULL;
	}

	mbSpritesActive = false;
}

void ATGTIAEmulator::GenerateSpriteImages(int x1, int x2) {
	unsigned xr = (unsigned)(x2 - x1);

	// Trigger new sprite images
	bool foundActiveSprite = false;

	for(int i=0; i<8; ++i) {
		Sprite& sprite = mSprites[i];

		// Check if there is any latched or shifting data -- if not, we do not care because:
		//
		// - the only impact of the shifter state is its output for priority and collision
		// - shifting in any non-zero data would reset the state to 0
		//
		// Note that we still need to do this if data is still available to shift out as
		// we can re-capture that in a new image. Also, we are doing this on the last synced
		// state of the shift hardware instead of the state at the time of the trigger, but
		// we will check again below.

		if (!(sprite.mState.mDataLatch | sprite.mState.mShiftRegister))
			continue;

		foundActiveSprite = true;

		int pos = mSpritePos[i];

		if ((unsigned)(pos - x1) < xr) {
			// catch sprite state up to this point
			sprite.Sync(pos);

			// latch in new image
			if (sprite.mState.mShiftState) {
				sprite.mState.mShiftState = 0;
				sprite.mState.mShiftRegister += sprite.mState.mShiftRegister;
			}

			sprite.mState.mShiftRegister |= sprite.mState.mDataLatch;

			// generate new sprite image
			GenerateSpriteImage(sprite, pos);
		}
	}

	if (!foundActiveSprite)
		mbSpritesActive = false;
}

void ATGTIAEmulator::GenerateSpriteImage(Sprite& sprite, int pos) {
	// if we have a previous image, truncate it
	if (sprite.mpImageTail && sprite.mpImageTail->mX2 > pos)
		sprite.mpImageTail->mX2 = pos;

	// skip all zero images
	if (sprite.mState.mShiftRegister) {
		// compute sprite width
		static const int kWidthLookup[4] = {8,16,8,32};
		int width = kWidthLookup[sprite.mState.mSizeMode];

		// check for special case lockup
		if (sprite.mState.mSizeMode == 2 && (sprite.mState.mShiftState == 1 || sprite.mState.mShiftState == 2))
			width = 1024;

		// record image
		SpriteImage *image = AllocSpriteImage();
		image->mX1 = pos;
		image->mX2 = pos + width;
		image->mState = sprite.mState;
		image->mpNext = NULL;

		if (sprite.mpImageTail)
			sprite.mpImageTail->mpNext = image;
		else
			sprite.mpImageHead = image;

		sprite.mpImageTail = image;
	}
}

void ATGTIAEmulator::FreeSpriteImage(SpriteImage *p) {
	p->mpNext = mpFreeSpriteImages;
	mpFreeSpriteImages = p;
}

ATGTIAEmulator::SpriteImage *ATGTIAEmulator::AllocSpriteImage() {
	if (!mpFreeSpriteImages)
		return mNodeAllocator.Allocate<SpriteImage>();

	SpriteImage *p = mpFreeSpriteImages;
	mpFreeSpriteImages = p->mpNext;

	return p;
}

ATGTIAEmulator::VerticalOverscanMode ATGTIAEmulator::DeriveVerticalOverscanMode() const {
	if (mVerticalOverscanMode != kVerticalOverscan_Default)
		return mVerticalOverscanMode;

	switch(mOverscanMode) {
		case kOverscanFull:
			return kVerticalOverscan_Full;

		case kOverscanExtended:
			return kVerticalOverscan_Extended;

		default:
		case kOverscanNormal:
			return kVerticalOverscan_Normal;

		case kOverscanOSScreen:
			return kVerticalOverscan_OSScreen;
	}
}
