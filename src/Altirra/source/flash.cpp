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

#include <stdafx.h>
#include "flash.h"
#include "scheduler.h"
#include "debuggerlog.h"

ATDebuggerLogChannel g_ATLCFlash(false, false, "FLASH", "Flash memory erase operations");
ATDebuggerLogChannel g_ATLCFlashWrite(false, false, "FLASHWR", "Flash memory write operations");

namespace {
	// 150us timeout for writes to Atmel devices
	const uint32 kAtmelWriteTimeoutCycles = 268;

	// 80us timeout for multiple sector erase (AMD)
	const uint32 kAMDSectorEraseTimeoutCycles = 143;

	// 50us timeout for multiple sector erase (Amic)
	const uint32 kAmicSectorEraseTimeoutCycles = 89;
}

ATFlashEmulator::ATFlashEmulator()
	: mpMemory(NULL)
	, mpScheduler(NULL)
	, mpWriteEvent(NULL)
{
}

ATFlashEmulator::~ATFlashEmulator() {
	Shutdown();
}

void ATFlashEmulator::Init(void *mem, ATFlashType type, ATScheduler *sch) {
	mpMemory = (uint8 *)mem;
	mpScheduler = sch;
	mFlashType = type;
	mpWriteEvent = NULL;

	mbDirty = false;
	mbWriteActivity = false;
	mbAtmelSDP = (type == kATFlashType_AT29C010A || type == kATFlashType_AT29C040);

	switch(mFlashType) {
		case kATFlashType_A29040:
			mSectorEraseTimeoutCycles = kAmicSectorEraseTimeoutCycles;
			break;

		case kATFlashType_Am29F010:
		case kATFlashType_Am29F040:
		case kATFlashType_Am29F040B:
			mSectorEraseTimeoutCycles = kAMDSectorEraseTimeoutCycles;
			break;

		default:
			mSectorEraseTimeoutCycles = 0;
			break;
	}

	ColdReset();
}

void ATFlashEmulator::Shutdown() {
	if (mpWriteEvent)
		mpScheduler->UnsetEvent(mpWriteEvent);

	mpScheduler = NULL;
	mpMemory = NULL;
}

void ATFlashEmulator::ColdReset() {
	if (mpWriteEvent)
		mpScheduler->UnsetEvent(mpWriteEvent);

	mReadMode = kReadMode_Normal;
	mCommandPhase = 0;
}

bool ATFlashEmulator::ReadByte(uint32 address, uint8& data) const {
	data = 0xFF;

	switch(mReadMode) {
		case kReadMode_Normal:
			data = mpMemory[address];
			return true;

		case kReadMode_Autoselect:
			switch(address & 0xFF) {
				case 0x00:
					switch(mFlashType) {
						case kATFlashType_Am29F010:
						case kATFlashType_Am29F040:
						case kATFlashType_Am29F040B:
							data = 0x01;	// XX00 Manufacturer ID: AMD
							break;

						case kATFlashType_AT29C010A:
						case kATFlashType_AT29C040:
							data = 0x1F;	// XX00 Manufacturer ID: Atmel
							break;

						case kATFlashType_SST39F040:
							data = 0xBF;	// XX00 Manufacturer ID: SST
							break;

						case kATFlashType_A29040:
							data = 0x37;	// XX00 Manufacturer ID: AMIC
							break;
					}
					break;

				case 0x01:
					switch(mFlashType) {
						case kATFlashType_Am29F010:
							data = 0x21;
							break;
					
						case kATFlashType_Am29F040:
						case kATFlashType_Am29F040B:
							// Yes, the 29F040 and 29F040B both have the same code even though
							// the 040 validates A0-A14 and the 040B only does A0-A10.
							data = 0xA4;
							break;

						case kATFlashType_AT29C010A:
							data = 0xD5;
							break;

						case kATFlashType_AT29C040:
							data = 0x5B;
							break;

						case kATFlashType_SST39F040:
							data = 0xB7;
							break;

						case kATFlashType_A29040:
							data = 0x86;
							break;
					}
					break;

				default:
					data = 0x00;	// XX02 Sector Protect Verify: 00 not protected
					break;
			}
			break;

		case kReadMode_WriteStatusPending:
			data = ~mpMemory[address] & 0x80;
			break;
	}

	return false;
}

bool ATFlashEmulator::WriteByte(uint32 address, uint8 value) {
	uint32 address15 = address & 0x7fff;
	uint32 address11 = address & 0x7ff;
	bool resetMapping = false;

	g_ATLCFlashWrite("Write[$%05X] = $%05X\n", address, value);

	switch(mCommandPhase) {
		case 0:
			// $F0 written at phase 0 deactivates autoselect mode
			if (value == 0xF0) {
				if (!mReadMode)
					break;

				g_ATLCFlash("Exiting autoselect mode.\n");
				mReadMode = kReadMode_Normal;
				return true;
			}

			switch(mFlashType) {
				case kATFlashType_Am29F010:
				case kATFlashType_Am29F040:
				case kATFlashType_SST39F040:
					if (address15 == 0x5555 && value == 0xAA)
						mCommandPhase = 1;
					break;

				case kATFlashType_AT29C010A:
				case kATFlashType_AT29C040:
					if (address == 0x5555 && value == 0xAA)
						mCommandPhase = 7;
					break;

				case kATFlashType_A29040:
				case kATFlashType_Am29F040B:
					if ((address & 0x7FF) == 0x555 && value == 0xAA)
						mCommandPhase = 1;
					break;
			}

			if (mCommandPhase)
				g_ATLCFlash("Unlock: step 1 OK.\n");
			else
				g_ATLCFlash("Unlock: step 1 FAILED [($%05X) = $%02X].\n", address, value);
			break;

		case 1:
			mCommandPhase = 0;

			if (value == 0x55) {
				switch(mFlashType) {
					case kATFlashType_A29040:
					case kATFlashType_Am29F040B:
						if ((address & 0x7FF) == 0x2AA)
							mCommandPhase = 2;
						break;

					default:
						if (address15 == 0x2AAA)
							mCommandPhase = 2;
						break;
				}
			}

			if (mCommandPhase)
				g_ATLCFlash("Unlock: step 2 OK.\n");
			else
				g_ATLCFlash("Unlock: step 2 FAILED [($%05X) = $%02X].\n", address, value);
			break;

		case 2:
			if (mFlashType == kATFlashType_A29040 || mFlashType == kATFlashType_Am29F040B ? (address & 0x7FF) != 0x555 : address15 != 0x5555) {
				g_ATLCFlash("Unlock: step 3 FAILED [($%05X) = $%02X].\n", address, value);
				mCommandPhase = 0;
				break;
			}

			switch(value) {
				case 0x80:	// $80: sector or chip erase
					g_ATLCFlash("Entering sector erase mode.\n");
					mCommandPhase = 3;
					break;

				case 0x90:	// $90: autoselect mode
					g_ATLCFlash("Entering autoselect mode.\n");
					mReadMode = kReadMode_Autoselect;
					mCommandPhase = 0;
					return true;

				case 0xA0:	// $A0: program mode
					g_ATLCFlash("Entering program mode.\n");
					mCommandPhase = 6;
					break;

				case 0xF0:	// $F0: reset
					g_ATLCFlash("Exiting autoselect mode.\n");
					mCommandPhase = 0;
					mReadMode = kReadMode_Normal;
					return true;

				default:
					g_ATLCFlash("Unknown command $%02X.\n", value);
					mCommandPhase = 0;
					break;
			}

			break;

		case 3:		// 5555[AA] 2AAA[55] 5555[80]
			mCommandPhase = 0;
			if (value == 0xAA) {
				switch(mFlashType) {
					case kATFlashType_A29040:
					case kATFlashType_Am29F040B:
						if (address11 == 0x555)
							mCommandPhase = 4;
						break;

					default:
						if (address15 == 0x5555)
							mCommandPhase = 4;
						break;
				}
			}

			if (mCommandPhase)
				g_ATLCFlash("Erase: step 4 OK.\n");
			else
				g_ATLCFlash("Erase: step 4 FAILED [($%05X) = $%02X].\n", address, value);
			break;

		case 4:		// 5555[AA] 2AAA[55] 5555[80] 5555[AA]
			mCommandPhase = 0;
			if (value == 0x55) {
				switch(mFlashType) {
					case kATFlashType_A29040:
					case kATFlashType_Am29F040B:
						if (address11 == 0x2AA)
							mCommandPhase = 5;
						break;

					default:
						if (address15 == 0x2AAA)
							mCommandPhase = 5;
						break;
				}
			}

			if (mCommandPhase)
				g_ATLCFlash("Erase: step 5 OK.\n");
			else
				g_ATLCFlash("Erase: step 5 FAILED [($%05X) = $%02X].\n", address, value);
			break;

		case 5:		// 5555[AA] 2AAA[55] 5555[80] 5555[AA] 2AAA[55]
			if (value == 0x10 && (mFlashType == kATFlashType_A29040 || mFlashType == kATFlashType_Am29F040B ? address11 == 0x555 : address15 == 0x5555)) {
				// full chip erase
				switch(mFlashType) {
					case kATFlashType_Am29F010:
						memset(mpMemory, 0xFF, 0x20000);
						break;

					case kATFlashType_Am29F040:
					case kATFlashType_Am29F040B:
					case kATFlashType_SST39F040:
					case kATFlashType_A29040:
						memset(mpMemory, 0xFF, 0x80000);
						break;
				}

				g_ATLCFlash("Erasing entire flash chip.\n");
				mbWriteActivity = true;
				mbDirty = true;

			} else if (value == 0x30) {
				// sector erase
				switch(mFlashType) {
					case kATFlashType_Am29F010:
						address &= 0x1C000;
						memset(mpMemory + address, 0xFF, 0x4000);
						g_ATLCFlash("Erasing sector $%05X-%05X\n", address, address + 0x3FFF);
						break;
					case kATFlashType_Am29F040:
					case kATFlashType_Am29F040B:
					case kATFlashType_A29040:
						address &= 0x70000;
						memset(mpMemory + address, 0xFF, 0x10000);
						g_ATLCFlash("Erasing sector $%05X-%05X\n", address, address + 0xFFFF);
						break;
					case kATFlashType_SST39F040:
						address &= 0x7F000;
						memset(mpMemory + address, 0xFF, 0x1000);
						g_ATLCFlash("Erasing sector $%05X-%05X\n", address, address + 0xFFF);
						break;
				}

				mbWriteActivity = true;
				mbDirty = true;

				if (mSectorEraseTimeoutCycles) {
					// Once a sector erase has happened, other sector erase commands
					// may be issued... but ONLY sector erase commands. This window is
					// only guaranteed to last between 50us and 80us.
					mpScheduler->SetEvent(mSectorEraseTimeoutCycles, this, 2, mpWriteEvent);
					mCommandPhase = 14;
					return true;
				}
			} else {
				g_ATLCFlash("Erase: step 6 FAILED [($%05X) = $%02X].\n", address, value);
			}

			// unknown command
			mCommandPhase = 0;
			mReadMode = kReadMode_Normal;
			return true;

		case 6:		// 5555[AA] 2AAA[55] 5555[A0]
			mpMemory[address] &= value;
			mbDirty = true;
			mbWriteActivity = true;

			mCommandPhase = 0;
			mReadMode = kReadMode_Normal;
			return true;

		case 7:		// Atmel 5555[AA]
			if (address == 0x2AAA && value == 0x55)
				mCommandPhase = 8;
			else
				mCommandPhase = 0;
			break;

		case 8:		// Atmel 5555[AA] 2AAA[55]
			if (address != 0x5555) {
				mCommandPhase = 0;
				break;
			}

			switch(value) {
				case 0x80:	// $80: chip erase
					mCommandPhase = 9;
					break;

				case 0x90:	// $90: autoselect mode
					mReadMode = kReadMode_Autoselect;
					mCommandPhase = 0;
					break;

				case 0xA0:	// $A0: program mode
					mCommandPhase = 12;
					mbAtmelSDP = true;
					break;

				case 0xF0:	// $F0: reset
					mCommandPhase = 0;
					mReadMode = kReadMode_Normal;
					return true;

				default:
					mCommandPhase = 0;
					break;
			}
			break;

		case 9:		// Atmel 5555[AA] 2AAA[55] 5555[80]
			if (address == 0x5555 && value == 0xAA)
				mCommandPhase = 10;
			else
				mCommandPhase = 0;
			break;

		case 10:	// Atmel 5555[AA] 2AAA[55] 5555[80] 2AAA[55]
			if (address == 0x2AAA && value == 0x55)
				mCommandPhase = 11;
			else
				mCommandPhase = 0;
			break;

		case 11:	// Atmel 5555[AA] 2AAA[55] 5555[80] 2AAA[55]
			if (address == 0x5555) {
				switch(value) {
					case 0x10:		// chip erase
						switch(mFlashType) {
							case kATFlashType_AT29C010A:
								memset(mpMemory, 0xFF, 0x20000);
								break;

							case kATFlashType_AT29C040:
								memset(mpMemory, 0xFF, 0x80000);
								break;
						}
						mbDirty = true;
						mbWriteActivity = true;
						break;

					case 0x20:		// software data protection disable
						mbAtmelSDP = false;
						mCommandPhase = 12;
						return false;
				}
			}

			mReadMode = kReadMode_Normal;
			mCommandPhase = 0;
			break;

		case 12:	// Atmel SDP program mode - initial sector write
			mWriteSector = address & 0xFFF00;
			mReadMode = kReadMode_WriteStatusPending;
			memset(mpMemory, 0xFF, 256);
			mCommandPhase = 13;
			// fall through

		case 13:	// Atmel program mode - sector write
			mpMemory[address] = value;
			mbDirty = true;
			mbWriteActivity = true;
			mpScheduler->SetEvent(kAtmelWriteTimeoutCycles, this, 1, mpWriteEvent);
			return true;

		case 14:	// Multiple sector erase mode (AMD/Amic)
			if (value == 0x30) {
				// sector erase
				switch(mFlashType) {
					case kATFlashType_Am29F010:
						address &= 0x1C000;
						memset(mpMemory + address, 0xFF, 0x4000);
						g_ATLCFlash("Erasing sector $%05X-%05X\n", address, address + 0x3FFF);
						break;
					case kATFlashType_Am29F040:
					case kATFlashType_Am29F040B:
					case kATFlashType_A29040:
						address &= 0x70000;
						memset(mpMemory + address, 0xFF, 0x10000);
						g_ATLCFlash("Erasing sector $%05X-%05X\n", address, address + 0xFFFF);
						break;
					case kATFlashType_SST39F040:
						address &= 0x7F000;
						memset(mpMemory + address, 0xFF, 0x1000);
						g_ATLCFlash("Erasing sector $%05X-%05X\n", address, address + 0xFFF);
						break;
				}

				mbWriteActivity = true;
				mbDirty = true;

				mpScheduler->SetEvent(mSectorEraseTimeoutCycles, this, 2, mpWriteEvent);
			}
			return true;
	}

	return false;
}

void ATFlashEmulator::OnScheduledEvent(uint32 id) {
	mpWriteEvent = NULL;

	g_ATLCFlash("Ending multiple sector tiemout.\n");
	mReadMode = kReadMode_Normal;

	switch(mFlashType) {
		case kATFlashType_AT29C010A:
		case kATFlashType_AT29C040:
			if (mbAtmelSDP)
				mCommandPhase = 0;
			else
				mCommandPhase = 12;
			break;

		default:
			mCommandPhase = 0;
			break;
	}
}
