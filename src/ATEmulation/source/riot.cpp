//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2016 Avery Lee
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
#include <at/atcore/consoleoutput.h>
#include <at/atcore/scheduler.h>
#include <at/atemulation/riot.h>

ATRIOT6532Emulator::ATRIOT6532Emulator() {
	mpFnIrqChanged = [](bool) {};
}

ATRIOT6532Emulator::~ATRIOT6532Emulator() {
	Shutdown();
}

void ATRIOT6532Emulator::Init(ATScheduler *sch) {
	mpScheduler = sch;
}

void ATRIOT6532Emulator::Shutdown() {
	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpTimerIrqEvent);
		mpScheduler = nullptr;
	}
}

void ATRIOT6532Emulator::DumpStatus(ATConsoleOutput& out) {
	out("Port A:           [ORA $%02X] & [DDRA $%02X] <+> input $%02X => read $%02X, output $%02X", mORA, mDDRA, mInputA, DebugReadByte(0), ReadOutputA());
	out("Port B:           [ORB $%02X] & [DDRB $%02X] <+> input $%02X => read $%02X, output $%02X", mORB, mDDRB, mInputB, DebugReadByte(2), ReadOutputB());
	out("PA7 edge detect:  %s, IRQ: %s, %s", mbEdgePositive ? "Positive" : "Negative", mbEdgeIrqEnabled ? "enabled" : "disabled", mbEdgeIrqActive ? "asserted" : "negated");

	const uint64 timerDelta = mTimerDeadline - mpScheduler->GetTick64();
	const bool timerPassed = timerDelta >= (UINT64_C(1) << 63);
	const uint64 absTimerDelta = timerPassed ? 0 - timerDelta : timerDelta;
	out("Timer IRQ:        %llu cycles %s, IRQ: %s, %s"
		, absTimerDelta
		, timerPassed ? "passed" : "remain"
		, mbTimerIrqEnabled ? "enabled" : "disabled"
		, mbTimerIrqActive ? "asserted" : "negated");
}

void ATRIOT6532Emulator::Reset() {
	mTimerDeadline = mpScheduler->GetTick64() - 1;
	mTimerPrescalerOffset = 0;
	mTimerPrescalerShift = 0;

	mDDRA = 0;
	mORA = 0;
	mDDRB = 0;
	mORB = 0;
	mbEdgePositive = false;
	mbEdgeIrqActive = false;
	mbEdgeIrqEnabled = false;
	mbTimerIrqEnabled = false;
	mbTimerIrqActive = false;

	UpdateIrqStatus();
}

void ATRIOT6532Emulator::SetInputA(uint8 value, uint8 mask) {
	value = ((mInputA ^ value) & mask) ^ mInputA;

	if (mInputA == value)
		return;

	// Check if PA7 is being toggled and is not pulled down, in which case
	// we might have an edge transition.
	if ((mInputA ^ value) & 0x80) {
		// Check for correct polarity.
		if (mbEdgePositive ? (value & 0x80) : !(value & 0x80)) {
			// Check if not being held down by output.
			if (!(mDDRA & 0x80) || (mORA & 0x80))
				mbEdgeIrqActive = true;
		}
	}

	mInputA = value;
}

void ATRIOT6532Emulator::SetInputB(uint8 value, uint8 mask) {
	mInputB ^= ((mInputB ^ value) & mask);
}

uint8 ATRIOT6532Emulator::DebugReadByte(uint8 address) const {
	switch(address & 7) {
		case 0:		// read DRA (outputs merge with inputs)
		default:
			return (mORA | ~mDDRA) & mInputA;

		case 1:		// read DDRA
			return mDDRA;

		case 2:		// read DRB (outputs override inputs)
			return ((mInputB ^ mORB) & mDDRB) ^ mInputB;

		case 3:		// read DDRB
			return mDDRB;

		case 4:		// read timer
		case 6: {
			const uint64 delta = mTimerDeadline - mpScheduler->GetTick64();

			return delta < (UINT64_C(1) << 63) ? (uint8)(delta >> mTimerPrescalerShift) : (uint8)delta;
		}

		case 5:		// read interrupt flag
		case 7: {
			uint8 v = 0;

			if (mbTimerIrqActive)
				v += 0x80;

			if (mbEdgeIrqActive)
				v += 0x40;

			return v;
		}
	}
}

uint8 ATRIOT6532Emulator::ReadByte(uint8 address) {
	switch(address & 7) {
		case 0:		// read DRA (outputs merge with inputs)
		default:
			return (mORA | ~mDDRA) & mInputA;

		case 1:		// read DDRA
			return mDDRA;

		case 2:		// read DRB (outputs override inputs)
			return ((mInputB ^ mORB) & mDDRB) ^ mInputB;

		case 3:		// read DDRB
			return mDDRB;

		case 4:		// read timer (also clears timer interrupt)
		case 6: {
			const uint64 t = mpScheduler->GetTick64();
			const uint64 delta = mTimerDeadline - t;

			uint8 v;
			if (mbTimerIrqActive) {
				// The timer flag is not cleared if it is read exactly when it would be set from the timer
				// underflowing. Since the interrupt flag is active, we have to be in 1T mode, so we are
				// looking for N*256 cycles from the last deadline regardless of prescaler setting.
				if ((delta & (UINT64_C(1) << 63)) && (delta & 255)) {
					mbTimerIrqActive = false;

					// Recompute the timer deadline. The timer interrupt flag was set, so the timer's
					// been counting in 1T mode since the deadline. Compute the current timer value as
					// mod 256 from the deadline, then time to next underflow at prescaler rate. Note
					// that we must keep the original prescaler offset from when the timer was last
					// written, which doesn't change no matter how many times it switches between 1T
					// and prescaled rate.
					//
					// Also, the counter's value on the deadline is $FF, not $00, because it just
					// underflowed last cycle.
					const uint32 counterValue = ((uint32)delta - 1) & 0xFF;

					// - If the prescaler is 1T and the counter is 0, deadline is next cycle.
					// - If the prescaler is 8T, the counter is 4, and (t & psMask) == 0, we have 8
					//   cycles until the next prescaler tick and 8+4*8 = 40 cycles to deadline.
					// - If the prescaler is 8T, the counter is 4, and (t & psMask) == 2, we have 6
					//   cycles until the next prescaler tick and 6+4*8 = 38 cycles to deadline.
					mTimerDeadline = t + (~(t - mTimerPrescalerOffset) & mTimerPrescalerMask) + (counterValue << mTimerPrescalerShift) + 1;

					UpdateIrqStatus();
					UpdateTimerIrqEvent();
				}

				// Timer interrupt flag (was) set -- 1T mode
				v = (uint8)(delta - 1);
			} else {
				// Timer interrupt flag not set -- prescaled mode
				v = (uint8)(((uint32)delta - 1) >> mTimerPrescalerShift);
			}

			const bool timerIrqEnabled = (address & 8) != 0;
			if (mbTimerIrqEnabled != timerIrqEnabled) {
				mbTimerIrqEnabled = timerIrqEnabled;

				UpdateIrqStatus();
			}

			return v;
		}

		case 5:		// read interrupt flag (also clears PA7 interrupt)
		case 7: {
			uint8 v = 0;

			if (mbTimerIrqActive)
				v += 0x80;

			if (mbEdgeIrqActive) {
				v += 0x40;
				mbEdgeIrqActive = false;
			}

			return v;
		}
	}
}

void ATRIOT6532Emulator::WriteByte(uint8 address, uint8 value) {
	if (address & 4) {
		if (address & 16) {
			// Write timer
			static const int kPrescalerShifts[4] = {
				0,		// 1T
				3,		// 8T
				6,		// 64T
				10		// 1024T (*not* 512T!).
			};

			mTimerPrescalerShift = kPrescalerShifts[address & 3];
			mTimerPrescalerMask = (1U << mTimerPrescalerShift) - 1;
			const uint64 t = mpScheduler->GetTick64();

			// Compute deadline to underflow+interrupt. A value of zero triggers next cycle.
			mTimerDeadline = t + (value << mTimerPrescalerShift) + 1;

			// Compute absolute time base offset for prescaler-driven decrements.
			mTimerPrescalerOffset = (uint32)mTimerDeadline & mTimerPrescalerMask;

			mbTimerIrqEnabled = (address & 8) != 0;
			mbTimerIrqActive = false;

			UpdateIrqStatus();
			UpdateTimerIrqEvent();
		} else {
			// Write edge detect control
			mbEdgePositive = (address & 1) != 0;
			mbEdgeIrqEnabled = (address & 2) != 0;

			UpdateIrqStatus();
		}
	} else {
		switch(address & 3) {
			case 0:
			default:
				mORA = value;
				break;

			case 1:
				mDDRA = value;
				break;

			case 2:
				mORB = value;
				break;

			case 3:
				mDDRB = value;
				break;
		}
	}
}

void ATRIOT6532Emulator::OnScheduledEvent(uint32 id) {
	mpTimerIrqEvent = nullptr;

	if (!mbTimerIrqActive) {
		mbTimerIrqActive = true;

		UpdateIrqStatus();

		// No need to reschedule event for now, until something resets the timer
		// IRQ again.
	}
}

void ATRIOT6532Emulator::UpdateTimerIrqEvent() {
	if (mbTimerIrqActive) {
		// Timer interrupt flag is already set, so no need to run ticks to set it some more.
		// The counter now counts at 1T down from the last deadline and underflows every 256T, which
		// we can extrapolate out as necessary if someone reads the timer and clears the interrupt flag
		// to reset it back to prescaled mode.
		mpScheduler->UnsetEvent(mpTimerIrqEvent);
	} else {
		// Timer interrupt flag is not set, so we need to set an event until it does. This also means
		// that we are in prescaled mode, even if the timer has already underflowed once (which means
		// that someone read the timer and switched it back from 1T mode). If the deadline hasn't passed
		// yet, we can just use that; otherwise, we're back to 1T mode (again) and should estimate the
		// next 256T interval from the deadline.
		uint64 ticksLeft = mTimerDeadline - mpScheduler->GetTick64();
		uint32 ticksLeft32 = (uint32)ticksLeft;

		if ((ticksLeft - 1) & (UINT64_C(1) << 63)) {
			// The deadline is in the past or now. Adjust it to the next deadline 256*N cycles away.
			ticksLeft32 = ((ticksLeft32 - 1) & 0xFF) + 1;
		}

		mpScheduler->SetEvent(ticksLeft32, this, 1, mpTimerIrqEvent);
	}
}

void ATRIOT6532Emulator::UpdateIrqStatus() {
	const bool irq = (mbTimerIrqActive && mbTimerIrqEnabled) || (mbEdgeIrqActive && mbEdgeIrqEnabled);

	if (mbIrqActive != irq) {
		mbIrqActive = irq;

		mpFnIrqChanged(irq);
	}
}
