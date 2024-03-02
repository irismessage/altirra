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

#include <stdafx.h>
#include <time.h>
#include <vd2/system/date.h>
#include <at/atcore/consoleoutput.h>
#include <at/atcore/logging.h>
#include <at/atemulation/rtcmcp7951x.h>

ATLogChannel g_ATLCMCP7951XCRead(false, false, "MCP7951XCR", "MCP7951X real time clock reads");
ATLogChannel g_ATLCMCP7951XURead(false, false, "MCP7951XUR", "MCP7951X real time clock user reads");
ATLogChannel g_ATLCMCP7951XWrite(false, false, "MCP7951XW",  "MCP7951X real time clock writes");

namespace {
	uint8 ToBCD(uint8 v) {
		return ((v / 10) << 4) + (v % 10);
	}
}

ATRTCMCP7951XEmulator::ATRTCMCP7951XEmulator() {
}

void ATRTCMCP7951XEmulator::Init() {
	memset(mRAM, 0, sizeof mRAM);
	memset(mEEPROM, 0, sizeof mEEPROM);
	memset(mIDEEPROM, 0, sizeof mIDEEPROM);

	ColdReset();
}

void ATRTCMCP7951XEmulator::ColdReset() {
}

void ATRTCMCP7951XEmulator::Load(const NVState& state) {
    memcpy(mRAM, state.mRAM, sizeof mRAM);
    memcpy(mEEPROM, state.mEEPROM, sizeof mEEPROM);
    memcpy(mIDEEPROM, state.mIDEEPROM, sizeof mIDEEPROM);
}

void ATRTCMCP7951XEmulator::Save(NVState& state) const {
    memcpy(state.mRAM, mRAM, sizeof state.mRAM);
    memcpy(state.mEEPROM, mEEPROM, sizeof state.mEEPROM);
    memcpy(state.mIDEEPROM, mIDEEPROM, sizeof state.mIDEEPROM);
}

void ATRTCMCP7951XEmulator::Reselect() {
    mState = 0;
}

uint8 ATRTCMCP7951XEmulator::Transfer(uint8 dataIn) {
    uint8 readData = 0xFF;

    switch(mState) {
        case 0:     // initial state - process command
            mState = 1;

            // if the command isn't UNLOCK or IDWRITE, reset the unlock state
            if (dataIn != 0x32 && dataIn != 0x14)
                mUnlockState = 0;

            switch(dataIn) {
                case 0x03: mState = 6;  break;  // EEREAD
                case 0x02: mState = 1;  break;  // EEWRITE
                case 0x04: mbWriteEnabled = false; break;  // EEWRDI
                case 0x06: mbWriteEnabled = true; break;   // EEWREN
                case 0x05: mState = 11; break;  // SRREAD
                case 0x01: mState = 12; break;  // SRWRITE
                case 0x13: mState = 2;  break;  // READ
                case 0x12: mState = 4;  break;  // WRITE
                case 0x14: mState = 17; break;  // UNLOCK
                case 0x32: mState = 15; break;  // IDWRITE
                case 0x33: mState = 13; break;  // IDREAD
                case 0x54: mState = 10; break;  // CLRRAM
            }
            break;

        case 1:     // jam state -- discard and ignore all further data until -CS
            break;

        case 2:     // READ address state
            mAddress = dataIn;

            if (mAddress < 0x60) {
                if (mAddress < 0x20)
                    UpdateClock();

                mState = 3;
            } else
                mState = 1;
            break;

        case 3:     // READ data state
            readData = mRAM[mAddress];

            // wrap within RTCC or SRAM areas
            mAddress = mAddress + 1;

            if (mAddress == 0x20)
                mAddress = 0x00;
            else if (mAddress == 0x60)
                mAddress = 0x20;
            break;

        case 4:     // WRITE address state
            mAddress = dataIn;

            if (mAddress < 0x60)
                mState = 5;
            else
                mState = 1;
            break;

        case 5:     // WRITE data state
            mRAM[mAddress] = dataIn;

            // wrap within RTCC or SRAM areas
            mAddress = mAddress + 1;

            if (mAddress == 0x20)
                mAddress = 0x00;
            else if (mAddress == 0x60)
                mAddress = 0x20;
            break;

        case 6:     // EEREAD address state
            mAddress = dataIn;
            mState = 7;
            break;

        case 7:     // EEREAD data state
            readData = mEEPROM[mAddress];

            // address autoincrements within whole EEPROM
            mAddress = (mAddress + 1) & 0xFF;
            break;

        case 8:     // EEWRITE address state
            mAddress = dataIn;

            if (mbWriteEnabled)
                mState = 9;
            else
                mState = 1;
            break;

        case 9:     // EEWRITE data state
            mEEPROM[mAddress] = dataIn;

            // address autoincrements but wraps within a 'page' of 8 bytes
            mAddress = (mAddress & 0xF8) + ((mAddress + 1) & 7);

            // reset WEL on successful EEWRITE
            mbWriteEnabled = false;
            break;

        case 10:    // CLRRAM dummy data state
            memset(mRAM + 0x20, 0, 0x40);
            break;

        case 11:    // SRREAD data state
            readData = mStatus;

            // merge WEL
            if (mUnlockState)
                readData = readData + 0x02;
            break;

        case 12:    // SRWRITE data state
            // only bits 2-3 can be changed
            mStatus = dataIn & 0x0C;

            // reset WEL on successful SRWRITE
            mbWriteEnabled = false;
            break;

        case 13:    // IDREAD address state
            mAddress = dataIn;
            break;

        case 14:    // IDREAD data state
            readData = mEEPROM[mAddress];
            mAddress = (mAddress + 1) & 0x0F;
            break;

        case 15:    // IDWRITE address state
            mAddress = dataIn;

            if (mUnlockState == 2 && mUnlockState)
                mState = 16;
            else
                mState = 1;
            break;

        case 16:    // IDWRITE data state
            mEEPROM[mAddress] = dataIn;

            // autoincrement within 8-byte page
            mAddress = (mAddress & 0x08) + ((mAddress + 1) & 0x07);

            // reset WEL on successful IDWRITE
            mbWriteEnabled = false;
            break;

        case 17:    // UNLOCK data state
            if (mUnlockState == 0) {
                if (dataIn == 0x55)
                    mUnlockState = 1;
            } else if (mUnlockState == 1) {
                if (dataIn == 0xAA)
                    mUnlockState = 2;
                else
                    mUnlockState = 0;
            } else {
                mUnlockState = 0;
            }
            break;
    }

    return readData;
}

void ATRTCMCP7951XEmulator::DumpStatus(ATConsoleOutput& output) {
	output <<= "MCP7951X status:";
	output <<= "";

	output("RAM:");

	for(int offset = 0; offset < 0x60; offset += 0x10) {
		output("%02X: %02X %02X %02X %02X %02X %02X %02X %02X-%02X %02X %02X %02X %02X %02X %02X %02X"
			, offset
			, mRAM[offset + 0]
			, mRAM[offset + 1]
			, mRAM[offset + 2]
			, mRAM[offset + 3]
			, mRAM[offset + 4]
			, mRAM[offset + 5]
			, mRAM[offset + 6]
			, mRAM[offset + 7]
			, mRAM[offset + 8]
			, mRAM[offset + 9]
			, mRAM[offset + 10]
			, mRAM[offset + 11]
			, mRAM[offset + 12]
			, mRAM[offset + 13]
			, mRAM[offset + 14]
			, mRAM[offset + 15]
			);
	}

	output <<= "";
	output <<= "User EEPROM:";

	for(int offset = 0; offset < 0x100; offset += 0x10) {
		output("%02X: %02X %02X %02X %02X %02X %02X %02X %02X-%02X %02X %02X %02X %02X %02X %02X %02X"
			, mEEPROM[offset + 0]
			, mEEPROM[offset + 1]
			, mEEPROM[offset + 2]
			, mEEPROM[offset + 3]
			, mEEPROM[offset + 4]
			, mEEPROM[offset + 5]
			, mEEPROM[offset + 6]
			, mEEPROM[offset + 7]
			, mEEPROM[offset + 8]
			, mEEPROM[offset + 9]
			, mEEPROM[offset + 10]
			, mEEPROM[offset + 11]
			, mEEPROM[offset + 12]
			, mEEPROM[offset + 13]
			, mEEPROM[offset + 14]
			, mEEPROM[offset + 15]
			);
	}

	output <<= "";
	output <<= "ID EEPROM:";
	output("%02X %02X %02X %02X %02X %02X %02X %02X-%02X %02X %02X %02X %02X %02X %02X %02X"
		, mIDEEPROM[0]
		, mIDEEPROM[1]
		, mIDEEPROM[2]
		, mIDEEPROM[3]
		, mIDEEPROM[4]
		, mIDEEPROM[5]
		, mIDEEPROM[6]
		, mIDEEPROM[7]
		, mIDEEPROM[8]
		, mIDEEPROM[9]
		, mIDEEPROM[10]
		, mIDEEPROM[11]
		, mIDEEPROM[12]
		, mIDEEPROM[13]
		, mIDEEPROM[14]
		, mIDEEPROM[15]
		);
}

void ATRTCMCP7951XEmulator::UpdateClock() {
	// copy clock to RAM
    const VDExpandedDate& ed = VDGetLocalDate(VDGetCurrentDate());

    // leap years are divisible by 4, but not divisible by 100 unless divisible
    // by 400
    const int twoDigitYear = ed.mYear % 100;
    const bool isLeapYear = (twoDigitYear || (ed.mYear % 400) == 0) && !(twoDigitYear & 3);

    // centiseconds (0-99)
    mRAM[0] = ToBCD(ed.mMilliseconds / 10);

    // seconds (0-59), start oscillator bit
    mRAM[1] = ToBCD(ed.mSecond) + 0x80;

    // minutes (0-59)
    mRAM[2] = ToBCD(ed.mMinute);

    // hours (0-23)
    mRAM[3] = ToBCD(ed.mHour);

    // weekday (1-7) + OSCRUN[7] + PWRFAIL[6] + VBATEN[5]
    mRAM[4] = 0x29 + ed.mDayOfWeek;

    // day (1-31)
    mRAM[5] = ToBCD(ed.mDay);

    // month (1-12), leap-year bit
    mRAM[6] = ToBCD(ed.mMonth) + (isLeapYear ? 0x20 : 0);

    // year (0-99)
    mRAM[7] = ToBCD(twoDigitYear);
}
