//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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
#include <vd2/system/vdalloc.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/ksyms.h>
#include "test.h"
#include <at/ataudio/pokey.h>
#include <at/ataudio/pokeytables.h>

AT_DEFINE_TEST(Emu_PokeyPots) {
	using namespace ATKernelSymbols;

	class DummyConnections final : public IATPokeyEmulatorConnections {
	public:
		void PokeyAssertIRQ(bool cpuBased) override {}
		void PokeyNegateIRQ(bool cpuBased) override {}
		void PokeyBreak() override {}
		bool PokeyIsInInterrupt() const override { return false; }
		bool PokeyIsKeyPushOK(uint8 scanCode, bool cooldownExpired) const override { return false; }
	} conn;

	ATScheduler sch;
	vdautoptr tables(new ATPokeyTables);

	vdautoptr pokey { new ATPokeyEmulator(false) };

	pokey->Init(&conn, &sch, nullptr, tables);
	pokey->ColdReset();

	const auto WritePokeyByte = [&pokey = *pokey, &sch](uint32 addr, uint8 val) {
		pokey.WriteByte((uint8)addr, val);
		ATSCHEDULER_ADVANCE(&sch);
	};

	// start normal pot scan mode with clocks frozen
	WritePokeyByte(SKCTL, 0x00);
	WritePokeyByte(POTGO, 0x00);

	// wait a bit more than full pot scan time
	for(int i=0; i<240*228; ++i)
		ATSCHEDULER_ADVANCE(&sch);

	// check that ALLPOT shows all pots still being read
	AT_TEST_ASSERT(pokey->DebugReadByte((uint8)ALLPOT) == 0xFF);

	// enable clocks
	WritePokeyByte(SKCTL, 0x03);

	// wait another bit more than full pot scan time
	for(int i=0; i<240*228; ++i)
		ATSCHEDULER_ADVANCE(&sch);

	// check that ALLPOT shows pots successfully read
	AT_TEST_ASSERT(pokey->DebugReadByte((uint8)ALLPOT) == 0);

	// ground pot 0, set pot 1 to max, pot 2 to mid, pot3 to min
	pokey->SetPotPosHires(0, 228 << 16, true);
	pokey->SetPotPosHires(1, 228 << 16, false);
	pokey->SetPotPosHires(2, 114 << 16, false);
	pokey->SetPotPosHires(3, 1 << 16, false);

	// hit POTGO and wait for poll to complete
	WritePokeyByte(POTGO, 0);
	
	for(int i=0; i<240*228; ++i) {
		ATSCHEDULER_ADVANCE(&sch);

		if (pokey->DebugReadByte((uint8)ALLPOT) == 0)
			break;
	}

	AT_TEST_ASSERT(pokey->DebugReadByte((uint8)ALLPOT) == 0);

	// check read values
	AT_TEST_ASSERT(pokey->DebugReadByte((uint8)POT0) == 228);
	AT_TEST_ASSERT(pokey->DebugReadByte((uint8)POT1) == 228);
	AT_TEST_ASSERT(pokey->DebugReadByte((uint8)POT2) == 114);
	AT_TEST_ASSERT(pokey->DebugReadByte((uint8)POT3) == 1);

	// hit POTGO again and check the counter states on the fly
	WritePokeyByte(POTGO, 0);

	for(int i=0; i<228; ++i) {
		for(int j=0; j<114; ++j)
			ATSCHEDULER_ADVANCE(&sch);

		AT_TEST_ASSERT(abs((int)pokey->DebugReadByte((uint8)POT0) - i) <= 1);
		AT_TEST_ASSERT(abs((int)pokey->DebugReadByte((uint8)POT1) - i) <= 1);
		AT_TEST_ASSERT(abs((int)pokey->DebugReadByte((uint8)POT2) - std::min(i, 114)) <= 1);
		AT_TEST_ASSERT(abs((int)pokey->DebugReadByte((uint8)POT3) - 1) <= 1);
	}

	return 0;
}

