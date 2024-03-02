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

#include "gtia.h"

class ATArtifactingEngine {
	ATArtifactingEngine(const ATArtifactingEngine&);
	ATArtifactingEngine& operator=(const ATArtifactingEngine&);
public:
	ATArtifactingEngine();
	~ATArtifactingEngine();

	void SetColorParams(const ATColorParams& params);

	enum {
		N = 456,
		M = 262
	};

	void BeginFrame(bool pal, bool chromaArtifact, bool blend);
	void Artifact8(uint32 y, uint32 dst[N], const uint8 src[N], bool scanlineHasHiRes);
	void Artifact32(uint32 y, uint32 *dst, uint32 width);

protected:
	void ArtifactPAL8(uint32 dst[N], const uint8 src[N]);
	void ArtifactPAL32(uint32 *dst, uint32 width);

	void ArtifactNTSC(uint32 dst[N], const uint8 src[N], bool scanlineHasHiRes);
	void BlitNoArtifacts(uint32 dst[N], const uint8 src[N]);
	void BlendExchange(uint32 dst[N], uint32 blendDst[N]);

	bool mbPAL;
	bool mbChromaArtifacts;
	bool mbBlendActive;
	bool mbBlendCopy;

	int mYScale;
	int mYBias;
	int mArtifactRed;
	int mArtifactGreen;
	int mArtifactBlue;
	int mArtifactYScale;

	int mChromaVectors[16][3];
	uint32 mPalette[256];

	union {
		uint8 mPALDelayLine[N];
		uint8 mPALDelayLine32[N*2*3];	// RGB24 @ 14MHz resolution
	};

	uint32 mPrevFrame[M][N];
};

#endif	// f_ARTIFACTING_H
