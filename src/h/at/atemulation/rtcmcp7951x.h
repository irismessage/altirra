//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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

#ifndef f_AT_RTCMCP7951X_H
#define f_AT_RTCMCP7951X_H

class ATConsoleOutput;

// MCP7951X/MCP7952X real-time clock emulator.
class ATRTCMCP7951XEmulator {
public:
	ATRTCMCP7951XEmulator();

	void Init();

	void ColdReset();

	struct NVState {
		uint8 mRAM[0x60];
		uint8 mEEPROM[0x100];
		uint8 mIDEEPROM[0x10];
	};

	void Load(const NVState& state);
	void Save(NVState& state) const;

	void Reselect();
	uint8 Transfer(uint8 dataIn);

	void DumpStatus(ATConsoleOutput& output);

protected:
	void ReadRegister();
	void WriteRegister();
	void IncrementAddressRegister();
	void UpdateClock();

	uint8	mState = 0;
	uint8	mAddress = 0;
	uint8	mStatus = 0;
	uint8	mUnlockState = 0;
	bool	mbWriteEnabled = false;

	uint8	mRAM[0x60] {};
	uint8	mEEPROM[0x100] {};
	uint8	mIDEEPROM[0x10] {};
};

#endif
