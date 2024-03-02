//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2018 Avery Lee
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

#ifndef f_AT_POKEYTABLES_H
#define f_AT_POKEYTABLES_H

struct ATPokeyTables {
	// Rate of decay per sample (28 cycles) for the first stage amplifier output. This
	// affects POKEY output but not GTIA (CONSOL) output.
	float	mReferenceDecayPerSample;

	// Min/max clamps for first stage amplifier output. These are in terms of final output
	// polarity, so negated from volume values.
	float	mReferenceClampLo;
	float	mReferenceClampHi;

	// Volume table for sums of all four audio channels (0-15). This ranges from [0,-1/56]
	// because it is divided by 56 to account for 56 half-cycles being accumulated in the
	// POKEY renderer, and inverted to account for POKEY's inverted output (it pulls down
	// its output harder as volume is increased).
	float	mMixTable[61];

	// Bit 0 = 17-bit polynomial
	// Bit 1 = 9-bit polynomial
	// Bit 2 = 5-bit polynomial
	// Bit 3 = 4-bit polynomial
	uint8	mPolyBuffer[131071 * 2];
	uint8	mInitModeBuffer[131071 * 2];

	ATPokeyTables();
};

#endif	// f_AT_POKEYTABLES_H
