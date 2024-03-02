//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2018 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef f_AT_ATCORE_RANDOMIZATION_H
#define f_AT_ATCORE_RANDOMIZATION_H

// This is a structure of seeds for various systems that need to generate data.
// All of these seeds are generated from the master seed by a strong PRNG,
// from which individual streams are derived with a fast PRNG. All fields here
// are autopopulated as the struct memory is rand-filled. It's not critical
// that backwards compatibility be maintained here but we do try to preserve
// existing fields so seeds are compatible between versions.
//
// Because several fast PRNGs lock up with a value of zero, the master seeder
// ensures that all values here are non-zero. Any that are zero are re-rolled
// from fresh entropy.
//
// Users are allowed to advance the seeds in this structure after each use
// between cold resets. This is only allowed for exclusively main thread usage
// (which is the common case for this code base).

struct ATRandomizationSeeds {
	uint32 mMainMemory;
	uint32 mHighMemory;
	uint32 mAxlonMemory;
	uint32 mVBXEMemory;
	uint32 mProgramLaunchDelay;
	uint32 mPIAFloatingInputs;
	uint32 mDiskStartPos;
	uint32 mCassetteStartPos;
};

inline uint32 ATRandomizeAdvanceFast(uint32& seed) {
	uint32 v = seed;
	// apply one step of Xorshift32 algorithm
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;

	return v;
}

extern ATRandomizationSeeds g_ATRandomizationSeeds;

void ATSetRandomizationSeeds(uint32 masterSeed);

#endif
