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
#include <at/atcore/ksyms.h>
#include "test.h"
#include <at/ataudio/pokey.h>
#include <at/ataudio/pokeytables.h>

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

	const auto WritePokeyByte = [&pokey = *pokey, &sch](uint32 addr, uint8 val) {
		pokey.WriteByte((uint8)addr, val);
		ATSCHEDULER_ADVANCE(&sch);
	};

	WritePokeyByte(STIMER, 0x00);
	WritePokeyByte(IRQEN, 0x00);
	WritePokeyByte(AUDCTL, 0x78);
	WritePokeyByte(AUDF1, 0x0E);
	WritePokeyByte(AUDF2, 0x00);
	WritePokeyByte(STIMER, 0x00);

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

	WritePokeyByte(STIMER, 0x00);
	WritePokeyByte(IRQEN, 0x00);
	WritePokeyByte(AUDCTL, 0x78);
	WritePokeyByte(AUDF3, 0x0E);
	WritePokeyByte(AUDF4, 0x00);
	WritePokeyByte(STIMER, 0x00);

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
	WritePokeyByte(STIMER, 0x00);
	WritePokeyByte(IRQEN, 0x00);
	WritePokeyByte(AUDCTL, 0x78);
	WritePokeyByte(AUDF1, 0x08);
	WritePokeyByte(AUDF2, 0x00);
	WritePokeyByte(STIMER, 0x00);

	for(int i=0; i<13; ++i)
		ATSCHEDULER_ADVANCE(&sch);

	WritePokeyByte((uint8)AUDCTL, 0x00);
	WritePokeyByte((uint8)AUDCTL, 0x78);

	{
		uint32 dt = pokey->GetCyclesToTimerFire(1);
		TEST_ASSERTF(dt < 100, "High timer switched to incorrectly long period when resetup during borrow: %d", dt);
	}

	// 8-bit timer preemption tests

#if 1
	AT_TEST_TRACE("")
	AT_TEST_TRACE("--- 8-bit preemption tests ---")

	for(uint8 audfVal : { 0, 8 }) {
		const uint8 audfVal2 = audfVal + 10;

		for(int delay=1; delay<40; ++delay) {
			WritePokeyByte(STIMER, 0x00);
			WritePokeyByte(IRQEN, 0x00);

			for(int i=0; i<4; ++i)
				ATSCHEDULER_ADVANCE(&sch);

			WritePokeyByte(AUDCTL, 0x60);
			WritePokeyByte(AUDF1, (uint8)audfVal);

			// reset timers
			WritePokeyByte(STIMER, 0x00);		// reference time 0

			// IRQ1 hot
			int irqTimes[3] = { -1, -1, -1 };
			bool irq1recorded = false;
			bool irq1needreset = true;

			for(int i=1; i<80; ++i) {
				const bool irq1 = !(pokey->DebugReadByte((uint8)IRQST) & 0x01);

				if (irq1 && !irq1recorded) {
					irq1recorded = true;

					for(int j=0; j<3; ++j) {
						if (irqTimes[j] < 0) {
							irqTimes[j] = i;
							break;
						}
					}
				}

				if (i == delay) {
					WritePokeyByte(AUDF1, (uint8)audfVal2);
				} else if (irq1) {
					WritePokeyByte(IRQEN, 0);
					irq1needreset = true;
					irq1recorded = false;
				} else if (irq1needreset) {
					irq1needreset = false;

					WritePokeyByte(IRQEN, 0x01);
				} else {
					ATSCHEDULER_ADVANCE(&sch);
				}
			}

			// The counter will first reload on cycle 3, then reload again at
			// [AUDFx]+1 cycles later, then fire IRQ 1 cycle after that. Whenever
			// AUDFx is rewritten in type, periods after that are affected.

			int expectedIrqTimes[3] = { -1, -1, -1 };
			int deadline = 3;
			int period = audfVal + 4;

			for(int i=0; i<3; ++i) {
				if (delay < deadline)
					period = audfVal2 + 4;

				deadline += period;
				expectedIrqTimes[i] = deadline + 1;
			}

			AT_TEST_TRACEF("AUDF1=%2d  delay=%2d  irqTimes=%2d,%2d,%2d  expectedIrqTimes=%2d,%2d,%2d"
				, audfVal
				, delay
				, irqTimes[0], irqTimes[1], irqTimes[2]
				, expectedIrqTimes[0], expectedIrqTimes[1], expectedIrqTimes[2]
			);

			AT_TEST_NONFATAL_ASSERTF(irqTimes[0] == expectedIrqTimes[0]
				&& irqTimes[1] == expectedIrqTimes[1]
				&& irqTimes[2] == expectedIrqTimes[2]
				, "8-bit preemption test failed with AUDF1=%d and delay %d: irqDelay=[%d,%d,%d], should have been [%d,%d,%d]"
				, period
				, delay
				, irqTimes[0], irqTimes[1], irqTimes[2]
				, expectedIrqTimes[0], expectedIrqTimes[1], expectedIrqTimes[2]
			);
		}
	}
#endif

	AT_TEST_TRACE("")
	AT_TEST_TRACE("--- 8-bit two-tone preemption tests ---")

	WritePokeyByte(AUDF2, 0xFF);

	// We need the serial output state in a deterministic state in order to use timer 1; it would
	// be more convenient to force timer 2, but timer 2 can't be run at 1.79MHz by itself. Thus,
	// we need to push a byte through the serial output shifter.

	WritePokeyByte(AUDCTL, 0x28);
	WritePokeyByte(AUDF3, 0);
	WritePokeyByte(AUDF4, 0);
	WritePokeyByte(SKCTL, 0x00);
	WritePokeyByte(SKCTL, 0x23);
	WritePokeyByte(IRQEN, 0);
	WritePokeyByte(IRQEN, 0x10);
	WritePokeyByte(SEROUT, 0);

	// wait for SEROR
	for(int i=0; i<1000; ++i) {
		if (!(pokey->DebugReadByte((uint8)IRQST) & 0x10))
			break;

		ATSCHEDULER_ADVANCE(&sch);
	}

	// wait for SEROC
	for(int i=0; i<1000; ++i) {
		if (!(pokey->DebugReadByte((uint8)IRQST) & 0x08))
			break;

		ATSCHEDULER_ADVANCE(&sch);
	}

	for(uint8 audfVal : { 0, 8 }) {
		const uint8 audfVal2 = audfVal + 10;

		for(int delay=1; delay<40; ++delay) {
			WritePokeyByte(STIMER, 0x00);
			WritePokeyByte(IRQEN, 0x00);
			WritePokeyByte(SKCTL, 0x08);

			for(int i=0; i<4; ++i)
				ATSCHEDULER_ADVANCE(&sch);

			WritePokeyByte(AUDCTL, 0x40);
			WritePokeyByte(AUDF1, (uint8)audfVal);

			// reset timers
			WritePokeyByte(STIMER, 0x00);		// reference time 0

			// IRQ1 hot
			int irqTimes[3] = { -1, -1, -1 };
			bool irqRecorded = false;
			bool irqNeedReset = true;

			for(int i=1; i<80; ++i) {
				const bool irq = !(pokey->DebugReadByte((uint8)IRQST) & 0x01);

				if (irq && !irqRecorded) {
					irqRecorded = true;

					for(int j=0; j<3; ++j) {
						if (irqTimes[j] < 0) {
							irqTimes[j] = i;
							break;
						}
					}
				}

				if (i == delay) {
					WritePokeyByte(AUDF1, (uint8)audfVal2);
				} else if (irq) {
					WritePokeyByte(IRQEN, 0);
					irqNeedReset = true;
					irqRecorded = false;
				} else if (irqNeedReset) {
					irqNeedReset = false;

					WritePokeyByte(IRQEN, 0x01);
				} else {
					ATSCHEDULER_ADVANCE(&sch);
				}
			}

			// The counter will first reload on cycle 3, then reload again at
			// [AUDFx]+1 cycles later, then fire IRQ 1 cycle after that, then reload
			// again 1 cycle after that. Whenever AUDFx is rewritten in type, periods
			// after that are affected.

			int expectedIrqTimes[3] = { -1, -1, -1 };
			int deadline = 3;
			int period = audfVal + 6;

			for(int i=0; i<3; ++i) {
				if (delay < deadline)
					period = audfVal2 + 6;

				deadline += period;
				expectedIrqTimes[i] = deadline - 1;
			}

			AT_TEST_TRACEF("AUDF1=%2d  delay=%2d  irqTimes=%2d,%2d,%2d  expectedIrqTimes=%2d,%2d,%2d"
				, audfVal
				, delay
				, irqTimes[0], irqTimes[1], irqTimes[2]
				, expectedIrqTimes[0], expectedIrqTimes[1], expectedIrqTimes[2]
			);

			AT_TEST_NONFATAL_ASSERTF(irqTimes[0] == expectedIrqTimes[0]
				&& irqTimes[1] == expectedIrqTimes[1]
				&& irqTimes[2] == expectedIrqTimes[2]
				, "8-bit preemption test failed with AUDF1=%d and delay %d: irqDelay=[%d,%d,%d], should have been [%d,%d,%d]"
				, period
				, delay
				, irqTimes[0], irqTimes[1], irqTimes[2]
				, expectedIrqTimes[0], expectedIrqTimes[1], expectedIrqTimes[2]
			);
		}
	}

	return 0;
}

