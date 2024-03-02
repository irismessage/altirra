//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
#include "test.h"
#include <at/ataudio/pokey.h>
#include <at/ataudio/pokeytables.h>
#include "ksyms.h"

DEFINE_TEST(Emu_PokeyTimers) {
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

	pokey->WriteByte((uint8)STIMER, 0x00);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)IRQEN, 0x00);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)AUDCTL, 0x78);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)AUDF1, 0x0E);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)AUDF2, 0x00);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)STIMER, 0x00);
	ATSCHEDULER_ADVANCE(&sch);

	for(int j=0; j<2; ++j) {
		for(int i=0; i<512; i += 2) {
			const uint32 dt0 = pokey->GetCyclesToTimerFire(0);
			const uint32 dt0h = pokey->GetCyclesToTimerFire(1);
			pokey->WriteByte((uint8)IRQEN, j ? 0x02 : 0x01);
			const uint32 dt1 = pokey->GetCyclesToTimerFire(0);
			const uint32 dt1h = pokey->GetCyclesToTimerFire(1);
			//printf("%08X | %d, %d | %d, %d  IRQ on\n", sch.GetTick(), dt0, dt1, dt0h, dt1h);

			TEST_ASSERTF(dt0 == dt1, "Low timer 1 cycles to fire changed when enabling IRQ at offset %d: %u -> %u", i+0, dt0, dt1);
			TEST_ASSERTF(dt0h == dt1h, "High timer 2 cycles to fire changed when enabling IRQ at offset %d: %u -> %u", i+0, dt0h, dt1h);

			ATSCHEDULER_ADVANCE(&sch);
			const uint32 dt2 = pokey->GetCyclesToTimerFire(0);
			const uint32 dt2h = pokey->GetCyclesToTimerFire(1);
			pokey->WriteByte((uint8)IRQEN, 0x00);
			const uint32 dt3 = pokey->GetCyclesToTimerFire(0);
			const uint32 dt3h = pokey->GetCyclesToTimerFire(1);
			//printf("%08X | %d, %d | %d, %d  IRQ off\n", sch.GetTick(), dt2, dt3, dt2h, dt3h);
			TEST_ASSERTF(dt2 == dt3, "Low timer 1 cycles to fire changed when enabling IRQ at offset %d: %u -> %u", i+1, dt2, dt3);
			TEST_ASSERTF(dt2h == dt3h, "High timer 2 cycles to fire changed when enabling IRQ at offset %d: %u -> %u", i+1, dt2h, dt3h);

			ATSCHEDULER_ADVANCE(&sch);
		}
	}

	pokey->WriteByte((uint8)STIMER, 0x00);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)IRQEN, 0x00);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)AUDCTL, 0x78);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)AUDF3, 0x0E);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)AUDF4, 0x00);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)STIMER, 0x00);
	ATSCHEDULER_ADVANCE(&sch);

	for(int i=0; i<512; i += 2) {
		const uint32 dt0 = pokey->GetCyclesToTimerFire(2);
		const uint32 dt0h = pokey->GetCyclesToTimerFire(3);
		pokey->WriteByte((uint8)IRQEN, 0x04);
		const uint32 dt1 = pokey->GetCyclesToTimerFire(2);
		const uint32 dt1h = pokey->GetCyclesToTimerFire(3);
		//printf("%d, %d | %d, %d\n", dt0, dt1, dt0h, dt1h);

		TEST_ASSERTF(dt0 == dt1, "Low timer cycles 3 to fire changed when enabling IRQ at offset %d: %u -> %u", i+0, dt0, dt1);
		TEST_ASSERTF(dt0h == dt1h, "High timer cycles 4 to fire changed when enabling IRQ at offset %d: %u -> %u", i+0, dt0h, dt1h);

		ATSCHEDULER_ADVANCE(&sch);
		const uint32 dt2 = pokey->GetCyclesToTimerFire(2);
		const uint32 dt2h = pokey->GetCyclesToTimerFire(3);
		pokey->WriteByte((uint8)IRQEN, 0x00);
		const uint32 dt3 = pokey->GetCyclesToTimerFire(2);
		const uint32 dt3h = pokey->GetCyclesToTimerFire(3);
		//printf("%d, %d | %d, %d\n", dt2, dt3, dt2h, dt3h);
		TEST_ASSERTF(dt2 == dt3, "Low timer cycles 3 to fire changed when enabling IRQ at offset %d: %u -> %u", i+1, dt2, dt3);
		TEST_ASSERTF(dt2h == dt3h, "High timer cycles 4 to fire changed when enabling IRQ at offset %d: %u -> %u", i+1, dt2h, dt3h);

		ATSCHEDULER_ADVANCE(&sch);
	}

	// check for bug where high linked timer is set up incorrect (~64K) when in low borrow but
	// not high borrow
	pokey->WriteByte((uint8)STIMER, 0x00);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)IRQEN, 0x00);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)AUDCTL, 0x78);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)AUDF1, 0x08);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)AUDF2, 0x00);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)STIMER, 0x00);
	ATSCHEDULER_ADVANCE(&sch);

	for(int i=0; i<13; ++i)
		ATSCHEDULER_ADVANCE(&sch);

	pokey->WriteByte((uint8)AUDCTL, 0x00);
	ATSCHEDULER_ADVANCE(&sch);
	pokey->WriteByte((uint8)AUDCTL, 0x78);
	ATSCHEDULER_ADVANCE(&sch);

	{
		uint32 dt = pokey->GetCyclesToTimerFire(1);
		TEST_ASSERTF(dt < 100, "High timer switched to incorrectly long period when resetup during borrow: %d", dt);
	}

	return 0;
}

