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

#ifndef f_ARTIFACTING_H
#define f_ARTIFACTING_H

#include <vd2/system/memory.h>
#include "gtia.h"

class ATArtifactingEngine : public VDAlignedObject<16> {
	ATArtifactingEngine(const ATArtifactingEngine&);
	ATArtifactingEngine& operator=(const ATArtifactingEngine&);
public:
	ATArtifactingEngine();
	~ATArtifactingEngine();

	void SetColorParams(const ATColorParams& params);

	enum {
		N = 456,
		M = 312
	};

	void BeginFrame(bool pal, bool chromaArtifact, bool chromaArtifactHi, bool blend);
	void Artifact8(uint32 y, uint32 dst[N], const uint8 src[N], bool scanlineHasHiRes, bool temporaryUpdate);
	void Artifact32(uint32 y, uint32 *dst, uint32 width, bool temporaryUpdate);
	void InterpolateScanlines(uint32 *dst, const uint32 *src1, const uint32 *src2, uint32 n);

protected:
	void ArtifactPAL8(uint32 dst[N], const uint8 src[N]);
	void ArtifactPAL32(uint32 *dst, uint32 width);

	void ArtifactNTSC(uint32 dst[N], const uint8 src[N], bool scanlineHasHiRes);
	void ArtifactNTSCHi(uint32 dst[N], const uint8 src[N], bool scanlineHasHiRes);
	void ArtifactPALHi(uint32 dst[N], const uint8 src[N], bool scanlineHasHiRes, bool oddline);
	void BlitNoArtifacts(uint32 dst[N], const uint8 src[N]);
	void Blend(uint32 *VDRESTRICT dst, const uint32 *VDRESTRICT src, uint32 n);
	void BlendExchange(uint32 *VDRESTRICT dst, uint32 *VDRESTRICT blendDst, uint32 n);

	void RecomputeNTSCTables(const ATColorParams& params);
	void RecomputePALTables(const ATColorParams& params);

	bool mbPAL;
	bool mbHighNTSCTablesInited;
	bool mbHighPALTablesInited;
	bool mbChromaArtifacts;
	bool mbChromaArtifactsHi;
	bool mbBlendActive;
	bool mbBlendCopy;
	bool mbScanlineDelayValid;

	int mYScale;
	int mYBias;
	int mArtifactRed;
	int mArtifactGreen;
	int mArtifactBlue;
	int mArtifactYScale;

	ATColorParams mColorParams;

	int mChromaVectors[16][3];
	uint32 mPalette[256];

	union {
		uint8 mPALDelayLine[N];
		uint8 mPALDelayLine32[N*2*3];	// RGB24 @ 14MHz resolution
		VDALIGN(16) uint32 mPALDelayLineUV[2][N];
	};

	uint32 mScanlineDelay[N*2];		// RGB32 @ 14MHz resolution

	union {
		uint32 mPrevFrame7MHz[M][N];
		uint32 mPrevFrame14MHz[M][N*2];
	};

	union {
		// NTSC high artifacting - scalar/MMX (64-bit)
		struct {
			VDALIGN(8) uint32 mPalToR[256][2][12];
			VDALIGN(8) uint32 mPalToG[256][2][12];
			VDALIGN(8) uint32 mPalToB[256][2][12];
			VDALIGN(8) uint32 mPalToRTwin[256][12];
			VDALIGN(8) uint32 mPalToGTwin[256][12];
			VDALIGN(8) uint32 mPalToBTwin[256][12];
		} m2x;

		// NTSC high artifacting - SSE2 (128-bit)
		struct {
			VDALIGN(16) uint32 mPalToR[256][4][16];
			VDALIGN(16) uint32 mPalToG[256][4][16];
			VDALIGN(16) uint32 mPalToB[256][4][16];
			VDALIGN(16) uint32 mPalToRTwin[256][2][16];
			VDALIGN(16) uint32 mPalToGTwin[256][2][16];
			VDALIGN(16) uint32 mPalToBTwin[256][2][16];
		} m4x;

		// PAL high artifacting - scalar (32-bit) (448K)
		struct {
			// [oddLine][color][phase][offset]
			uint32 mPalToY[2][256][8][4];
			uint32 mPalToU[2][256][8][12];
			uint32 mPalToV[2][256][8][12];
		} mPal2x;

		// PAL high artifacting - scalar (64-bit) (608K)
		struct {
			// [oddLine][color][phase][offset]
			uint32 mPalToY[2][256][8][6];
			uint32 mPalToU[2][256][8][16];
			uint32 mPalToV[2][256][8][16];

			uint32 mPalToYTwin[2][256][4][6];
			uint32 mPalToUTwin[2][256][4][16];
			uint32 mPalToVTwin[2][256][4][16];
		} mPal4x;

		// PAL high artifacting - scalar (128-bit) (960K)
		struct {
			// [oddLine][color][phase][offset]
			uint32 mPalToY[2][256][8][8];
			uint32 mPalToU[2][256][8][16];
			uint32 mPalToV[2][256][8][16];

			uint32 mPalToYTwin[2][256][4][8];
			uint32 mPalToUTwin[2][256][4][16];
			uint32 mPalToVTwin[2][256][4][16];
		} mPal8x;
	};
};

#endif	// f_ARTIFACTING_H
