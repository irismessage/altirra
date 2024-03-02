//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2011 Avery Lee
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

#ifndef f_AT_RTCV3021_H
#define f_AT_RTCV3021_H

/// V3021 real-time clock emulator.
class ATRTCV3021Emulator {
public:
	struct NVState {
		uint8 mData[10];
	};

	ATRTCV3021Emulator();

	void Init();

	void Load(const NVState& state);
	void Save(NVState& state) const;

	bool DebugReadBit() const;
	bool ReadBit();
	void WriteBit(bool bit);

protected:
	void CopyRAMToClock();
	void CopyClockToRAM();

	uint8	mPhase;
	uint8	mAddress;
	uint8	mValue;
	uint8	mRAM[16];
};

#endif
