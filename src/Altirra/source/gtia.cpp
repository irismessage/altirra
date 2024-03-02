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
#include <vd2/system/cpuaccel.h>
#include <vd2/system/math.h>
#include <vd2/Riza/display.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/triblt.h>
#include "gtia.h"
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
}

ATGTIAEmulator::ATGTIAEmulator()
	: mpConn(NULL)
	, mpVideoTap(NULL)
	, mpFrameTracker(new ATFrameTracker)
	, mbPALMode(false)
	, mbSECAMMode(false)
	, mArtifactMode(kArtifactNone)
	, mOverscanMode(kOverscanExtended)
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
	memset(mPlayerPos, 0, sizeof mPlayerPos);
	memset(mMissilePos, 0, sizeof mMissilePos);
	memset(mPlayerSize, 0, sizeof mPlayerSize);
	memset(mPlayerData, 0, sizeof mPlayerData);
	mMissileData = 0;
	memset(mPMColor, 0, sizeof mPMColor);
	memset(mPFColor, 0, sizeof mPFColor);

	memset(&mState, 0, sizeof mState);

	memset(mPMColor, 0, sizeof mPMColor);
	memset(mPFColor, 0, sizeof mPFColor);
	mPFBAK = 0;
	mPRIOR = 0;
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

void ATGTIAEmulator::ResetColors() {
	{
		ATColorParams& colpa = mColorSettings.mNTSCParams;
		colpa.mHueStart = -51.0f;
		colpa.mHueRange = 27.9f * 15.0f;
		colpa.mBrightness = 0.0f;
		colpa.mContrast = 1.0f;
		colpa.mSaturation = 75.0f / 255.0f;
		colpa.mArtifactHue = 96.0f;
		colpa.mArtifactSat = 2.76f;
		colpa.mArtifactBias = 0.35f;
		colpa.mbUsePALQuirks = false;
	}

	{
		ATColorParams& colpa = mColorSettings.mPALParams;
		colpa.mHueStart = -23.0f;
		colpa.mHueRange = 23.5f * 15.0f;
		colpa.mBrightness = 0.0f;
		colpa.mContrast = 1.0f;
		colpa.mSaturation = 0.29f;
		colpa.mArtifactHue = 96.0f;
		colpa.mArtifactSat = 2.76f;
		colpa.mArtifactBias = 0.35f;
		colpa.mbUsePALQuirks = true;
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

	switch(mOverscanMode) {
		case kOverscanFull:
			xlo = 0;
			xhi = 228;
			ylo = 0;
			yhi = 262;

			if (palext) {
				ylo = -25;
				yhi = 287;
			}
			break;

		case kOverscanExtended:
			xlo = 34;
			xhi = 222;
			break;

		case kOverscanOSScreen:
			xlo = 48;
			xhi = 208;
			break;
	}

	return vdrect32(xlo, ylo, xhi, yhi);
}

void ATGTIAEmulator::GetRawFrameFormat(int& w, int& h, bool& rgb32) const {
	rgb32 = (mpVBXE != NULL) || mArtifactMode || mbBlendMode;

	OverscanMode omode = mOverscanMode;

	if (mAnalysisMode || mbForcedBorder)
		omode = kOverscanFull;

	switch(omode) {
		case kOverscanFull:
			w = 456;

			if (mbPALMode && mbOverscanPALExtended)
				h = 312;
			else
				h = 262;
			break;

		case kOverscanExtended:
			w = 376;

			if (mbPALMode && mbOverscanPALExtended)
				h = 288;
			else
				h = 240;
			break;

		case kOverscanNormal:
			w = 336;

			if (mbPALMode && mbOverscanPALExtended)
				h = 288;
			else
				h = 240;
			break;

		case kOverscanOSScreen:
			w = 320;
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

	if (mAnalysisMode || mbForcedBorder)
		omode = kOverscanFull;

	switch(omode) {
		case kOverscanFull:
			w = 456;

			if (mbPALMode && mbOverscanPALExtended)
				h = 312;
			else
				h = 262;
			break;

		case kOverscanExtended:
			w = 376;

			if (mbPALMode && mbOverscanPALExtended)
				h = 288;
			else
				h = 240;
			break;

		case kOverscanNormal:
			w = 336;

			if (mbPALMode && mbOverscanPALExtended)
				h = 288;
			else
				h = 240;
			break;

		case kOverscanOSScreen:
			w = 320;
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
		mCollisionMask |= 0xf0;
	} else {
		mCollisionMask &= 0x0f;

		for(int i=0; i<4; ++i) {
			mPlayerCollFlags[i] &= 0x0f;
			mMissileCollFlags[i] &= 0x0f;
		}
	}
}

bool ATGTIAEmulator::ArePFCollisionsEnabled() const {
	return (mCollisionMask & 0x0f) != 0;
}

void ATGTIAEmulator::SetPFCollisionsEnabled(bool enable) {
	if (enable) {
		mCollisionMask |= 0x0f;
	} else {
		mCollisionMask &= 0xf0;

		for(int i=0; i<4; ++i) {
			mPlayerCollFlags[i] &= 0xf0;
			mMissileCollFlags[i] &= 0xf0;
		}
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
void ATGTIAEmulator::ExchangeState(T& io) {
	for(int i=0; i<4; ++i)
		io != mPlayerPos[i];

	for(int i=0; i<4; ++i)
		io != mMissilePos[i];

	for(int i=0; i<4; ++i)
		io != mPlayerSize[i];

	io != mMissileSize;

	for(int i=0; i<4; ++i)
		io != mPlayerData[i];

	io != mMissileData;

	for(int i=0; i<4; ++i)
		io != mPMColor[i];

	for(int i=0; i<4; ++i)
		io != mPFColor[i];

	io != mPFBAK;
	io != mPRIOR;
	io != mVDELAY;
	io != mGRACTL;
	io != mSwitchOutput;

	for(int i=0; i<4; ++i)
		io != mPlayerCollFlags[i];

	for(int i=0; i<4; ++i)
		io != mMissileCollFlags[i];

	io != mbHiresMode;
}

void ATGTIAEmulator::LoadState(ATSaveStateReader& reader) {
	ExchangeState(reader);

	// read register changes
	mRegisterChanges.resize(mRCCount);
	for(int i=0; i<mRCCount; ++i) {
		RegisterChange& rc = mRegisterChanges[i];

		rc.mPos = reader.ReadUint8();
		rc.mReg = reader.ReadUint8();
		rc.mValue = reader.ReadUint8();
	}

	mpRenderer->LoadState(reader);

	// recompute derived state
	mpConn->GTIASetSpeaker(0 != (mSwitchOutput & 8));

	for(int i=0; i<4; ++i) {
		mPlayerWidth[i] = kPlayerWidths[mPlayerSize[i]];
		mMissileWidth[i] = kMissileWidths[(mMissileSize >> (i+i)) & 3];
	}

	for(int i=0; i<4; ++i) {
		mpRenderer->SetRegisterImmediate(0x12 + i, mPMColor[i]);
		mpRenderer->SetRegisterImmediate(0x16 + i, mPFColor[i]);
	}

	mpRenderer->SetRegisterImmediate(0x1A, mPFBAK);

	// Terminate existing scan line
	mpDst = NULL;
	mpRenderer->EndScanline();
}

void ATGTIAEmulator::SaveState(ATSaveStateWriter& writer) {
	ExchangeState(writer);

	// write register changes
	for(int i=0; i<mRCCount; ++i) {
		const RegisterChange& rc = mRegisterChanges[i];

		writer.WriteUint8(rc.mPos);
		writer.WriteUint8(rc.mReg);
		writer.WriteUint8(rc.mValue);
	}

	mpRenderer->SaveState(writer);
}

void ATGTIAEmulator::GetRegisterState(ATGTIARegisterState& state) const {
	state = mState;
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

		if (mbForcedBorder || mAnalysisMode)
			omode = kOverscanFull;

		if (omode != kOverscanFull) {
			ptrdiff_t rawoffset = (omode == kOverscanExtended ? 34*2 : omode == kOverscanNormal ? 44*2 : 48*2);
			ptrdiff_t offset = rawoffset;

			if (use14MHz)
				offset *= 8;
			else if (mbPostProcessThisFrame)
				offset *= 4;

			ptrdiff_t scanPitch = fb->mPixmap.pitch;

			if (mbInterlaceEnabledThisFrame || mbScanlinesEnabledThisFrame)
				scanPitch *= 2;

			if (omode == kOverscanOSScreen)
				offset += scanPitch * 32;
			else if (!mbOverscanPALExtendedThisFrame)
				offset += scanPitch * 8;

			fb->mPixmap.data = (char *)fb->mPixmap.data + offset;
			fb->mPixmap.w = ((omode == kOverscanExtended) ? 376 : (omode == kOverscanNormal) ? 336 : 320) * (use14MHz ? 2 : 1);
			fb->mPixmap.h = (omode == kOverscanOSScreen) ? 192 : mbOverscanPALExtendedThisFrame ? 288 : 240;

			mPreArtifactFrameVisibleY1 = omode == kOverscanOSScreen ? 32 : mbOverscanPALExtendedThisFrame ? 0 : 8;
			mPreArtifactFrameVisibleY2 = mPreArtifactFrameVisibleY1 + fb->mPixmap.h;

			mPreArtifactFrameVisible.data = (char *)mPreArtifactFrameVisible.data + rawoffset * (use14MHz ? 2 : 1) + mPreArtifactFrameVisibleY1*mPreArtifactFrameVisible.pitch;
			mPreArtifactFrameVisible.w = fb->mPixmap.w;
			mPreArtifactFrameVisible.h = fb->mPixmap.h;

			if (mbInterlaceEnabledThisFrame || mbScanlinesEnabledThisFrame)
				fb->mPixmap.h *= 2;
		}
	}

	mFrameTimestamp = mpConn->GTIAGetTimestamp();
	return true;
}

void ATGTIAEmulator::BeginScanline(int y, bool hires) {
	// flush remaining register changes (required for PRIOR to interact properly with hires)
	Sync();

	mbMixedRendering = false;
	mbANTICHiresMode = hires;
	mbHiresMode = hires && !(mPRIOR & 0xc0);
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
	// obey VBLANK
	if (mVBlankMode != kVBlankModeOn)
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

	// flush remaining register changes
	if (mRCIndex < mRCCount)
		UpdateRegisters(&mRegisterChanges[mRCIndex], mRCCount - mRCIndex);

	mRegisterChanges.clear();
	mRCCount = 0;
	mRCIndex = 0;
	mLastSyncX = 0;

	ResetSprites();

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
	// obey VBLANK
	if (mVBlankMode == kVBlankModeOn)
		return;

	mpConn->GTIARequestAnticSync();

	int xend = (int)mpConn->GTIAGetXClock() + 2;

	if (xend > 222)
		xend = 222;

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
			SyncTo(x1, x2);
			x1 = x2;
		}
	} while(x1 < xend);

	mLastSyncX = x1;
}

void ATGTIAEmulator::SyncTo(int x1, int x2) {
	// determine displayed range
	int xc1 = x1;
	if (xc1 < 34)
		xc1 = 34;

	int xc2 = x2;
	if (xc2 < xc1)
		xc2 = xc1;

	// convert modes if necessary
	bool needHires = mbHiresMode || (mPRIOR & 0xC0);
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
		switch(mPRIOR & 0xC0) {
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

	static const int kOverlapShifts[4] = {0,1,0,2};
	static const int kOverlapOffsets[4] = {0,1,0,3};

	for(uint32 player=0; player<4; ++player) {
		uint8 data = mPlayerData[player];

		if (data | mPlayerShiftData[player]) {
			int xst = x1;
			int xend;

			while(xst < x2) {
				xend = x2;

				int ptx = mPlayerTriggerPos[player];
				int pw = mPlayerWidth[player];
				int px = mPlayerPos[player];

				// check if the player is set to retrigger within this range
				if (px >= xst && px < x2) {
					// check if a previous image will still be shifting out for at least one color
					// cycle
					if (ptx + pw > xst && px > xst) {
						// We're still shifting out a player image, so continue shifting the
						// existing image until the trigger point and then truncate it.
						xend = px;
					} else {
						// It's time to swap in the new image.
						
						// Check if there is overlap with a previous image. If so, we need to merge
						// the contents of the shift register.
						VDASSERT(ptx < px);   

						if (ptx >= 0) {
							const int size = mPlayerSize[player];
							int offset = px - ptx + kOverlapOffsets[size];

							if (offset >= 0) {
								offset >>= kOverlapShifts[size];

								if (offset < 8)
									data |= mPlayerShiftData[player] << offset;
							}
						}

						mPlayerShiftData[player] = data;

						// Retrigger at the position set in the HPOSPx register.
						ptx = px;
						mPlayerTriggerPos[player] = px;
					}
				}

				int px1 = ptx;
				int px2 = ptx + pw;

				if (px1 < xst)
					px1 = xst;
				if (px2 > xend)
					px2 = xend;

				if (px1 < xc1)
					px1 = xc1;
				if (px2 > xc2)
					px2 = xc2;

				if (px1 < px2) {
					mbPMRendered = true;

					uint8 *pldst = mMergeBuffer + px1;
					uint8 bit = P0 << player;
					sint32 mask = Expand(mPlayerShiftData[player], mPlayerSize[player]) << (px1 - ptx);
					sint32 mask2 = mask;
					uint8 flags = 0;
					for(int x=px2-px1; x > 0; --x) {
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
						mPlayerCollFlags[player] |= flags & mCollisionMask;
				}

				xst = xend;
			}
		}
	}

	if (mMissileData | mMissileShiftData[0] | mMissileShiftData[1] | mMissileShiftData[2] | mMissileShiftData[3]) {
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

					int ptx = mMissileTriggerPos[missile];
					int pw = mMissileWidth[missile];
					int px = mMissilePos[missile]; 

					// check if the missile is set to retrigger within this range
					if (px >= xst && px < x2) {
						// check if a previous image will still be shifting out for at least one color
						// cycle
						if (ptx + pw > xst && px > xst) {
							// We're still shifting out a player image, so continue shifting the
							// existing image until the trigger point and then truncate it.
							xend = px;
						} else {
							// It's time to swap in the new image.
							
							// Check if there is overlap with a previous image. If so, we need to merge
							// the contents of the shift register.
							VDASSERT(ptx < px);   

							if (ptx >= 0) {
								const int size = (mMissileSize >> (2*missile)) & 3;
								int offset = px - ptx + kOverlapOffsets[size];

								if (offset >= 0) {
									offset >>= kOverlapShifts[size];

									if (offset < 2)
										data |= mMissileShiftData[missile] << offset;
								}
							}

							mMissileShiftData[missile] = data;

							ptx = px;
							mMissileTriggerPos[missile] = ptx;
						}
					}

					int px1 = ptx;
					int px2 = ptx + mMissileWidth[missile];

					if (px1 < xst)
						px1 = xst;
					if (px2 > xend)
						px2 = xend;

					if (px1 < xc1)
						px1 = xc1;
					if (px2 > xc2)
						px2 = xc2;

					if (px1 < px2) {
						mrnext->mX = ptx;
						mrnext->mX1 = px1;
						mrnext->mX2 = px2;
						mrnext->mIndex = missile;
						mrnext->mData = data;
						++mrnext;

						uint8 *pldst = mMergeBuffer + px1;
						int mwidx = (mMissileSize >> (2*missile)) & 3;
						sint32 mask = Expand(mMissileShiftData[missile], mwidx) << (px1 - ptx);
						sint32 mask2 = mask;
						uint8 flags = 0;
						for(int x=px2-px1; x > 0; --x) {
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
							mMissileCollFlags[missile] |= flags & mCollisionMask;
					}

					xst = xend;
				}
			}
		}

		if (mranges != mrnext)
			mbPMRendered = true;

		for(MissileRange *mr = mranges; mr != mrnext; ++mr) {
			int missile = mr->mIndex;

			uint8 data = mr->mData;
			int ptx = mr->mX;
			int px1 = mr->mX1;
			int px2 = mr->mX2;

			uint8 *pldst = mMergeBuffer + px1;
			uint8 bit = (mPRIOR & 0x10) ? PF3 : P0 << missile;
			sint32 mask = Expand(data, (mMissileSize >> (2*missile)) & 3) << (px1 - ptx);
			for(int x=px2-px1; x > 0; --x) {
				if (mask < 0)
					*pldst |= bit;

				++pldst;
				mask += mask;
			}
		}
	}
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

		if (mbInterlaceEnabledThisFrame) {
			y += y;

			if (mbFieldPolarity)
				++y;
		} else if (mbScanlinesEnabledThisFrame) {
			y += y;
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

		ApplyArtifacting();

		mpDisplay->SetSourcePersistent(true, mpFrame->mPixmap);
	} else {
		const VDPixmap& pxdst = mbPostProcessThisFrame ? mPreArtifactFrameVisible : fb->mPixmap;

		ApplyArtifacting();

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

		if (mpUIRenderer)
			mpUIRenderer->Render(mpFrame->mPixmap, mPalette);

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
				float i2 = cos(angle2) * co[1];
				float q2 = sin(angle2) * co[1];
				float i3 = cos(angle3) * co[3];
				float q3 = sin(angle3) * co[3];

				i = (i2 + i3) * (0.5f * params.mSaturation);
				q = (q2 + q3) * (0.5f * params.mSaturation);
			} else {
				i = params.mSaturation * cos(angle);
				q = params.mSaturation * sin(angle);
				angle += angleStep;
			}
		}

		for(int luma=0; luma<16; ++luma) {
			double y = (double)luma * params.mContrast / 15.0f + params.mBrightness;
			double r = y + 0.956*i + 0.621*q;
			double g = y - 0.272*i - 0.647*q;
			double b = y - 1.107*i + 1.704*q;

			*dst++	= (VDClampedRoundFixedToUint8Fast((float)r) << 16)
					+ (VDClampedRoundFixedToUint8Fast((float)g) <<  8)
					+ (VDClampedRoundFixedToUint8Fast((float)b)      );
		}
	}

	if (mpVBXE)
		mpVBXE->SetDefaultPalette(mPalette);

	mpArtifactingEngine->SetColorParams(params);
}

uint8 ATGTIAEmulator::DebugReadByte(uint8 reg) const {
	reg &= 0x1F;

	return reg >= 0x10 ? const_cast<ATGTIAEmulator *>(this)->ReadByte(reg) : 0xFF;
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
			AddRegisterChange(xpos + 4, reg, value);
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

void ATGTIAEmulator::ApplyArtifacting() {
	if (mpVBXE) {
		if (mArtifactMode == kArtifactPAL || mbBlendMode) {
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

				mpArtifactingEngine->Artifact32(row, dst, 912);

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

	const uint32 vstart = mbOverscanPALExtendedThisFrame ? 24 : 8;
	const uint32 w = mb14MHzThisFrame ? 912 : 456;

	for(uint32 row=y1; row<y2; ++row) {
		uint32 *dst = (uint32 *)dstrow;
		const uint8 *src = srcrow;

		uint32 relativeRow = row - vstart;

		mpArtifactingEngine->Artifact8(row, dst, src, relativeRow < 240 && mbScanlinesWithHiRes[relativeRow]);

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

void ATGTIAEmulator::UpdateRegisters(const RegisterChange *rc, int count) {
	while(count--) {
		uint8 value = rc->mValue;

		switch(rc->mReg) {
			case 0x00:	mPlayerPos[0] = value;			break;
			case 0x01:	mPlayerPos[1] = value;			break;
			case 0x02:	mPlayerPos[2] = value;			break;
			case 0x03:	mPlayerPos[3] = value;			break;
			case 0x04:	mMissilePos[0] = value;			break;
			case 0x05:	mMissilePos[1] = value;			break;
			case 0x06:	mMissilePos[2] = value;			break;
			case 0x07:	mMissilePos[3] = value;			break;

			case 0x08:
			case 0x09:
			case 0x0A:
			case 0x0B:
				{
					int idx = rc->mReg & 3;

					static const uint8 kSizeMap[4]={0,1,0,3};

					const uint8 newSize = kSizeMap[value & 3];

					const uint8 oldSize = mPlayerSize[idx];
					if (newSize != oldSize) {
						// Check if we are in the middle of shifting out the player. If so, we need to
						// change the trigger position to reflect the correct shift position.
						const uint32 tpos = mPlayerTriggerPos[idx];
						if (tpos) {
							static const int kBitShifts[4] = {2,1,2,0};

							const uint32 offset = rc->mPos - tpos;
							int shiftedBits = offset << kBitShifts[mPlayerSize[idx]];

							static const int kPerturb1[4]={
								1, 2, 3, 0
							};

							static const int kPerturb2[4]={
								-2, -2, -1, -1
							};

							if (newSize == 1 && oldSize == 3)
								shiftedBits += kPerturb1[shiftedBits & 3];
							else if (newSize == 3 && oldSize == 1)
								shiftedBits += kPerturb2[shiftedBits & 2];
							else if (newSize == 0 && oldSize == 3)
								shiftedBits += 3;
							else if (newSize == 0 && oldSize == 1)
								shiftedBits++;

							mPlayerTriggerPos[idx] = rc->mPos - (shiftedBits >> kBitShifts[newSize]);
						}

						mPlayerSize[idx] = newSize;
						mPlayerWidth[idx] = kPlayerWidths[newSize];
					}
				}
				break;

			case 0x0C:
				mMissileSize = value;
				mMissileWidth[0] = kMissileWidths[(value >> 0) & 3];
				mMissileWidth[1] = kMissileWidths[(value >> 2) & 3];
				mMissileWidth[2] = kMissileWidths[(value >> 4) & 3];
				mMissileWidth[3] = kMissileWidths[(value >> 6) & 3];
				break;
			case 0x0D:
				mPlayerData[0] = value;
				break;
			case 0x0E:
				mPlayerData[1] = value;
				break;
			case 0x0F:
				mPlayerData[2] = value;
				break;
			case 0x10:
				mPlayerData[3] = value;
				break;
			case 0x11:
				mMissileData = value;
				break;

			case 0x1B:
				if (!(value & 0xc0) && (mPRIOR & 0xc0))
					mbGTIADisableTransition = true;

				mPRIOR = value;

				if (value & 0xC0)
					mbHiresMode = false;

				break;
			case 0x1E:		// $D01E HITCLR
				memset(mPlayerCollFlags, 0, sizeof mPlayerCollFlags);
				memset(mMissileCollFlags, 0, sizeof mMissileCollFlags);
				break;

			case 0x20:		// missile DMA
				{
					uint8 mask = 0xFF;

					static const uint8 kDelayTable[16]={
						0xFF, 0xFC, 0xF3, 0xF0,
						0xCF, 0xCC, 0xC3, 0xC0,
						0x3F, 0x3C, 0x33, 0x30,
						0x0F, 0x0C, 0x03, 0x00,
					};

					if (!(mY & 1))
						mask = kDelayTable[mVDELAY & 15];

					mMissileData ^= (mMissileData ^ value) & mask;
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
	mPlayerTriggerPos[0] = -32;
	mPlayerTriggerPos[1] = -32;
	mPlayerTriggerPos[2] = -32;
	mPlayerTriggerPos[3] = -32;
	mMissileTriggerPos[0] = -32;
	mMissileTriggerPos[1] = -32;
	mMissileTriggerPos[2] = -32;
	mMissileTriggerPos[3] = -32;
	mPlayerShiftData[0] = 0;
	mPlayerShiftData[1] = 0;
	mPlayerShiftData[2] = 0;
	mPlayerShiftData[3] = 0;
	mMissileShiftData[0] = 0;
	mMissileShiftData[1] = 0;
	mMissileShiftData[2] = 0;
	mMissileShiftData[3] = 0;
}
