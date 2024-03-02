//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
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

#ifdef AT_CPU_RECORD_BUS_ACTIVITY
	#define AT_CPU_READ_BYTE(addr) (mpCallbacks->CPURecordBusActivity(mpMemory->ReadByte((addr))))
	#define AT_CPU_READ_BYTE_ADDR16(addr) (mpCallbacks->CPURecordBusActivity(mpMemory->ReadByteAddr16((addr))))
	#define AT_CPU_DUMMY_READ_BYTE(addr) (mpCallbacks->CPURecordBusActivity(mpMemory->ReadByte((addr))))
	#define AT_CPU_READ_BYTE_HL(addrhi, addrlo) (mpCallbacks->CPURecordBusActivity(mpMemory->ReadByte((((uint32)addrhi) << 8) + (addrlo))))
	#define AT_CPU_EXT_READ_BYTE(addr, bank) (mpCallbacks->CPURecordBusActivity(mpMemory->ExtReadByte((addr), (bank))))
	#define AT_CPU_DUMMY_EXT_READ_BYTE(addr, bank) (mpCallbacks->CPURecordBusActivity(mpMemory->ExtReadByte((addr), (bank))))
	#define AT_CPU_WRITE_BYTE(addr, value) (mpMemory->WriteByte((addr), mpCallbacks->CPURecordBusActivity((value))))
	#define AT_CPU_WRITE_BYTE_HL(addrhi, addrlo, value) (mpMemory->WriteByte(((uint32)(addrhi) << 8) + (addrlo), mpCallbacks->CPURecordBusActivity((value))))
	#define AT_CPU_EXT_WRITE_BYTE(addr, bank, value) (mpMemory->ExtWriteByte((addr), (bank), mpCallbacks->CPURecordBusActivity((value))))
#else
	#define AT_CPU_READ_BYTE(addr) (mpMemory->ReadByte((addr)))
	#define AT_CPU_READ_BYTE_ADDR16(addr) (mpMemory->ReadByteAddr16((addr)))
	#define AT_CPU_DUMMY_READ_BYTE(addr) (mpMemory->DummyReadByte((addr)))
	#define AT_CPU_READ_BYTE_HL(addrhi, addrlo) (mpMemory->ReadByte(((uint32)(addrhi) << 8) + (addrlo)))
	#define AT_CPU_EXT_READ_BYTE(addr, bank) (mpMemory->ExtReadByte((addr), (bank)))
	#define AT_CPU_DUMMY_EXT_READ_BYTE(addr, bank) (mpMemory->DummyExtReadByte((addr), (bank)))
	#define AT_CPU_WRITE_BYTE(addr, value) (mpMemory->WriteByte((addr), (value)))
	#define AT_CPU_WRITE_BYTE_HL(addrhi, addrlo, value) (mpMemory->WriteByte(((uint32)(addrhi) << 8) + (addrlo), (value)))
	#define AT_CPU_EXT_WRITE_BYTE(addr, bank, value) (mpMemory->ExtWriteByte((addr), (bank), (value)))
#endif

#ifdef AT_CPU_MACHINE_65C816
	#define INSN_FETCH() AT_CPU_EXT_READ_BYTE(mPC++, mK)
	#define INSN_DUMMY_FETCH_NOINC() AT_CPU_DUMMY_EXT_READ_BYTE(mPC, mK)
	#define INSN_FETCH_NOINC() AT_CPU_EXT_READ_BYTE(mPC, mK)
#else
	uint32 tpc;

	#define INSN_FETCH() ((tpc = mPC), (mPC = (uint16)(tpc + 1)), AT_CPU_READ_BYTE_ADDR16(tpc))
	#define INSN_DUMMY_FETCH_NOINC() AT_CPU_DUMMY_READ_BYTE(mPC)
	#define INSN_FETCH_NOINC() AT_CPU_READ_BYTE(mPC)
#endif

for(;;) {
	switch(*mpNextState++) {
		case kStateNop:
			break;

		case kStateReadOpcode:
			mInsnPC = mPC;

			{
				uint8 iflags = mInsnFlags[mPC];

				if (iflags & kInsnFlagBreakPt) {
					ATSimulatorEvent event = (ATSimulatorEvent)mpBkptManager->TestPCBreakpoint(mPC);
					if (event) {
						mbUnusedCycle = true;
						RedecodeInsnWithoutBreak();
						return event;
					}
				}
			}

			if (mDebugFlags) {
				uint8 stat = ProcessDebugging();

				if (stat)
					return stat;
			}

			// fall through

		case kStateReadOpcodeNoBreak:
			if (mIntFlags) {
				if (ProcessInterrupts())
					continue;
			}

			{
				uint8 iflags = mInsnFlags[mPC];

				if (iflags & kInsnFlagHook) {
					uint8 op = mpCallbacks->CPUHookHit(mPC);

					// if a jump is requested, loop around again
					if (op == 0x4C)
						break;

					if (op) {
						++mPC;
						mOpcode = op;
						mpNextState = mDecodeHeap + mDecodePtrs[mOpcode];
						return kATSimEvent_None;
					}
				}
			}

			mOpcode = INSN_FETCH();
			mpNextState = mDecodeHeap + mDecodePtrs[mOpcode];

			if (mbHistoryOrProfilingEnabled) {
#ifdef AT_CPU_MACHINE_65C816
				AddHistoryEntry(true);
#else
				AddHistoryEntry(false);
#endif
			}

			return kATSimEvent_None;

		case kStateReadImm:
			mData = INSN_FETCH();
			return kATSimEvent_None;

		case kStateReadAddrL:
			mAddr = INSN_FETCH();
			return kATSimEvent_None;

		case kStateReadAddrH:
			mAddr += INSN_FETCH() << 8;
			return kATSimEvent_None;

		case kStateRead:
			mData = AT_CPU_READ_BYTE_ADDR16(mAddr);
			return kATSimEvent_None;

		case kStateReadSetSZToA:
			{
				mA = AT_CPU_READ_BYTE_ADDR16(mAddr);

				uint8 p = mP & ~(kFlagN | kFlagZ);

				p += (mA & 0x80);	// N

				if (!mA)
					p += kFlagZ;

				mP = p;
			}
			return kATSimEvent_None;

		case kStateReadDummyOpcode:
			INSN_FETCH_NOINC();
			return kATSimEvent_None;

		case kStateReadAddrHX:
			mAddr += INSN_FETCH() << 8;
			mAddr2 = (mAddr & 0xff00) + ((mAddr + mX) & 0x00ff);
			mAddr = mAddr + mX;
			return kATSimEvent_None;

		case kStateReadAddrHX_SHY:
			{
				uint8 hiByte = INSN_FETCH();
				uint32 lowSum = (uint32)mAddr + mX;

				// compute borked result from page crossing
				mData = mY & (hiByte + 1);

				// replace high byte if page crossing detected
				if (lowSum >= 0x100) {
					hiByte = mData;
					lowSum &= 0xff;
				}

				mAddr = (uint16)(lowSum + ((uint32)hiByte << 8));
			}
			return kATSimEvent_None;

		case kStateReadAddrHY_SHA:
			{
				uint8 hiByte = INSN_FETCH();
				uint32 lowSum = (uint32)mAddr + mY;

				// compute borked result from page crossing
				mData = mA & mX & (hiByte + 1);

				// replace high byte if page crossing detected
				if (lowSum >= 0x100) {
					hiByte = mData;
					lowSum &= 0xff;
				}

				mAddr = (uint16)(lowSum + ((uint32)hiByte << 8));
			}
			return kATSimEvent_None;

		case kStateReadAddrHY_SHX:
			{
				uint8 hiByte = INSN_FETCH();
				uint32 lowSum = (uint32)mAddr + mY;

				// compute borked result from page crossing
				mData = mX & (hiByte + 1);

				// replace high byte if page crossing detected
				if (lowSum >= 0x100) {
					hiByte = mData;
					lowSum &= 0xff;
				}

				mAddr = (uint16)(lowSum + ((uint32)hiByte << 8));
			}
			return kATSimEvent_None;

		case kStateReadAddrHY:
			mAddr += INSN_FETCH() << 8;
			mAddr2 = (mAddr & 0xff00) + ((mAddr + mY) & 0x00ff);
			mAddr = mAddr + mY;
			return kATSimEvent_None;

		case kStateReadAddX:
			mData = AT_CPU_READ_BYTE_ADDR16(mAddr);
			mAddr = (uint8)(mAddr + mX);
			return kATSimEvent_None;

		case kStateReadAddY:
			mData = AT_CPU_READ_BYTE_ADDR16(mAddr);
			mAddr = (uint8)(mAddr + mY);
			return kATSimEvent_None;

		case kStateReadCarry:
			mData = AT_CPU_READ_BYTE_ADDR16(mAddr2);
			if (mAddr == mAddr2)
				++mpNextState;
			return kATSimEvent_None;

		case kStateReadCarryForced:
			mData = AT_CPU_READ_BYTE_ADDR16(mAddr2);
			return kATSimEvent_None;

		case kStateWrite:
			AT_CPU_WRITE_BYTE(mAddr, mData);
			return kATSimEvent_None;

		case kStateWriteA:
			AT_CPU_WRITE_BYTE(mAddr, mA);
			return kATSimEvent_None;

		case kStateReadAbsIndAddr:
			mAddr = mData + ((uint16)AT_CPU_READ_BYTE(mAddr + 1) << 8);
			return kATSimEvent_None;

		case kStateReadAbsIndAddrBroken:
			mAddr = mData + ((uint16)AT_CPU_READ_BYTE((mAddr & 0xff00) + ((mAddr + 1) & 0xff)) << 8);
			return kATSimEvent_None;

		case kStateReadIndAddr:
			mAddr = mData + ((uint16)AT_CPU_READ_BYTE(0xff & (mAddr + 1)) << 8);
			return kATSimEvent_None;

		case kStateReadIndYAddr:
			mAddr = mData + ((uint16)AT_CPU_READ_BYTE(0xff & (mAddr + 1)) << 8);
			mAddr2 = (mAddr & 0xff00) + ((mAddr + mY) & 0x00ff);
			mAddr = mAddr + mY;
			return kATSimEvent_None;

		case kStateReadIndYAddr_SHA:
			{
				uint32 lowSum = (uint32)mData + mY;
				uint8 hiByte = AT_CPU_READ_BYTE(0xff & (mAddr + 1));

				// compute "adjusted" high address byte
				mData = mA & mX & (hiByte + 1);

				// check for page crossing and replace high byte if so
				if (lowSum >= 0x100) {
					lowSum &= 0xff;
					hiByte = mData;
				}

				mAddr = (uint16)(lowSum + ((uint32)hiByte << 8));
			}
			return kATSimEvent_None;

		case kStateWait:
			INSN_DUMMY_FETCH_NOINC();
			return kATSimEvent_None;

		case kStateAtoD:
			mData = mA;
			break;

		case kStateXtoD:
			mData = mX;
			break;

		case kStateYtoD:
			mData = mY;
			break;

		case kStateStoD:
			mData = mS;
			break;

		case kStatePtoD:
			mData = mP;
			break;

		case kStatePtoD_B0:
			mData = mP & ~kFlagB;
			break;

		case kStatePtoD_B1:
			mData = mP | kFlagB;
			break;

		case kState0toD:
			mData = 0;
			break;

		case kStateDtoA:
			mA = mData;
			break;

		case kStateDtoX:
			mX = mData;
			break;

		case kStateDtoY:
			mY = mData;
			break;

		case kStateDtoS:
			mS = mData;
			break;

		case kStateDtoP:
			if ((mP ^ mData) & kFlagI) {
				if (mData & kFlagI)
					mIFlagSetCycle = mpCallbacks->CPUGetUnhaltedCycle();
				else
					mIntFlags |= kIntFlag_IRQReleasePending;
			}

			mP = mData | 0x30;
			break;

		case kStateDtoP_noICheck:
			mP = mData | 0x30;
			break;

		case kStateDSetSZ:
			{
				uint8 p = mP & ~(kFlagN | kFlagZ);

				p += (mData & 0x80);	// N

				if (!mData)
					p |= kFlagZ;

				mP = p;
			}
			break;

		case kStateDSetSZToA:
			{
				uint8 p = mP & ~(kFlagN | kFlagZ);

				p |= (mData & 0x80);	// copy N
				if (!mData)
					p |= kFlagZ;

				mP = p;
				mA = mData;
			}
			break;

		case kStateDSetSV:
			mP &= ~(kFlagN | kFlagV);
			mP |= (mData & 0xC0);
			break;

		case kStateAddrToPC:
			mPC = mAddr;
			break;

		case kStateCheckNMIBlocked:
			mbNMIForced = false;

			if (mIntFlags & kIntFlag_NMIPending) {
				mIntFlags &= ~kIntFlag_NMIPending;

				if (mNMIAssertTime != mpCallbacks->CPUGetUnhaltedCycle())
					mbNMIForced = true;
			}
			break;

		case kStateNMIVecToPC:
			mPC = 0xFFFA;
			break;

		case kStateIRQVecToPC:
			mPC = 0xFFFE;
			break;

		case kStateIRQVecToPCBlockNMIs:
			if (mNMIAssertTime + 1 == mpCallbacks->CPUGetUnhaltedCycle())
				mIntFlags &= ~kIntFlag_NMIPending;

			mPC = 0xFFFE;
			break;

		case kStateNMIOrIRQVecToPC:
			if (mIntFlags & kIntFlag_NMIPending) {
				mPC = 0xFFFA;
				mIntFlags &= ~kIntFlag_NMIPending;
			} else
				mPC = 0xFFFE;
			break;

		case kStateNMIOrIRQVecToPCBlockable:
			if (mbNMIForced)
				mPC = 0xFFFA;
			else
				mPC = 0xFFFE;
			break;

		case kStateDelayInterrupts:
			if (mIntFlags & kIntFlag_NMIPending)
				mNMIAssertTime = mpCallbacks->CPUGetUnhaltedCycle();

			if (mIntFlags & kIntFlag_IRQPending)
				mIRQAssertTime = mpCallbacks->CPUGetUnhaltedCycle();
			break;

		case kStatePush:
			AT_CPU_WRITE_BYTE(0x100 + (uint8)mS--, mData);
			return kATSimEvent_None;

		case kStatePushPCL:
			AT_CPU_WRITE_BYTE(0x100 + (uint8)mS--, mPC & 0xff);
			return kATSimEvent_None;

		case kStatePushPCH:
			AT_CPU_WRITE_BYTE(0x100 + (uint8)mS--, mPC >> 8);
			return kATSimEvent_None;

		case kStatePushPCLM1:
			AT_CPU_WRITE_BYTE(0x100 + (uint8)mS--, (mPC - 1) & 0xff);
			return kATSimEvent_None;

		case kStatePushPCHM1:
			AT_CPU_WRITE_BYTE(0x100 + (uint8)mS--, (mPC - 1) >> 8);
			return kATSimEvent_None;

		case kStatePop:
			mData = AT_CPU_READ_BYTE(0x100 + (uint8)++mS);
			return kATSimEvent_None;

		case kStatePopPCL:
			mPC = AT_CPU_READ_BYTE(0x100 + (uint8)++mS);
			return kATSimEvent_None;

		case kStatePopPCH:
			mPC += AT_CPU_READ_BYTE(0x100 + (uint8)++mS) << 8;
			return kATSimEvent_None;

		case kStatePopPCHP1:
			mPC += AT_CPU_READ_BYTE(0x100 + (uint8)++mS) << 8;
			++mPC;
			return kATSimEvent_None;

		case kStateAdc:
			if (mP & kFlagD) {
				// BCD
				uint8 carry = (mP & kFlagC);

				uint32 lowResult = (mA & 15) + (mData & 15) + carry;
				if (lowResult >= 10)
					lowResult += 6;

				if (lowResult >= 0x20)
					lowResult -= 0x10;

				uint32 highResult = (mA & 0xf0) + (mData & 0xf0) + lowResult;

				uint8 p = mP & ~(kFlagC | kFlagN | kFlagZ | kFlagV);

				p += (((highResult ^ mA) & ~(mData ^ mA)) >> 1) & kFlagV;

				p += (highResult & 0x80);	// N

				if (highResult >= 0xA0)
					highResult += 0x60;

				if (highResult >= 0x100)
					p += kFlagC;

				if (!(uint8)(mA + mData + carry))
					p += kFlagZ;

				mA = (uint8)highResult;
				mP = p;
			} else {
				uint32 carry =  (mP & kFlagC);
				uint32 carry7 = (mA & 0x7f) + (mData & 0x7f) + carry;
				uint32 result = mA + mData + carry;

				uint8 p = mP & ~(kFlagC | kFlagN | kFlagZ | kFlagV);

				p += (result & 0x80);	// N

				if (result >= 0x100)
					p += kFlagC;

				if (!(result & 0xff))
					p += kFlagZ;

				p += ((result >> 2) ^ (carry7 >> 1)) & kFlagV;

				mP = p;
				mA = (uint8)result;
			}
			break;

		case kStateSbc:
			if (mP & kFlagD) {
				// Pole Position needs N set properly here for its passing counter
				// to stop correctly!

				mData ^= 0xff;

				// Flags set according to binary op
				uint32 carry7 = (mA & 0x7f) + (mData & 0x7f) + (mP & kFlagC);
				uint32 result = carry7 + (mA & 0x80) + (mData & 0x80);

				// BCD
				uint32 lowResult = (mA & 15) + (mData & 15) + (mP & kFlagC);
				uint32 highCarry = 0x10;
				if (lowResult < 0x10) {
					lowResult -= 6;
					highCarry = 0;
				}

				uint32 highResult = (mA & 0xf0) + (mData & 0xf0) + (lowResult & 0x0f) + highCarry;

				if (highResult < 0x100)
					highResult -= 0x60;

				uint8 p = mP & ~(kFlagC | kFlagN | kFlagZ | kFlagV);

				p += (result & 0x80);	// N

				if (result >= 0x100)
					p += kFlagC;

				if (!(result & 0xff))
					p += kFlagZ;

				p += ((result >> 2) ^ (carry7 >> 1)) & kFlagV;

				mP = p;
				mA = (uint8)highResult;
			} else {
				mData ^= 0xff;
				uint32 carry = (mP & kFlagC);
				uint32 carry7 = (mA & 0x7f) + (mData & 0x7f) + carry;
				uint32 result = mData + mA + carry;

				uint8 p = mP & ~(kFlagC | kFlagN | kFlagZ | kFlagV);

				p += (result & 0x80);	// N

				if (result >= 0x100)
					p += kFlagC;

				if (!(result & 0xff))
					p += kFlagZ;

				p += ((result >> 2) ^ (carry7 >> 1)) & kFlagV;

				mP = p;
				mA = (uint8)result;
			}
			break;

		case kStateCmp:
			{
				// must leave data alone to not break DCP
				uint32 result = mA + (mData ^ 0xff) + 1;

				uint8 p = (mP & ~(kFlagC | kFlagN | kFlagZ));

				p += (result & 0x80);	// N
				p += (result >> 8);

				if (!(result & 0xff))
					p += kFlagZ;

				mP = p;
			}
			break;

		case kStateCmpX:
			{
				mData ^= 0xff;
				uint32 result = mX + mData + 1;

				mP &= ~(kFlagC | kFlagN | kFlagZ);

				mP |= (result & 0x80);	// N

				if (result >= 0x100)
					mP |= kFlagC;

				if (!(result & 0xff))
					mP |= kFlagZ;
			}
			break;

		case kStateCmpY:
			{
				mData ^= 0xff;
				uint32 result = mY + mData + 1;

				mP &= ~(kFlagC | kFlagN | kFlagZ);

				mP |= (result & 0x80);	// N

				if (result >= 0x100)
					mP |= kFlagC;

				if (!(result & 0xff))
					mP |= kFlagZ;
			}
			break;

		case kStateInc:
			{
				++mData;

				uint8 p = mP & ~(kFlagN | kFlagZ);
				p += (mData & 0x80);	// N

				if (!mData)
					p += kFlagZ;

				mP = p;
			}
			break;

		case kStateIncXWait:
			{
				uint8 p = mP & ~(kFlagN | kFlagZ);

				++mX;
				if (!mX)
					p += kFlagZ;

				p += (mX & 0x80);	// N

				mP = p;
			}
			INSN_DUMMY_FETCH_NOINC();
			return kATSimEvent_None;

		case kStateDec:
			{
				--mData;
				uint8 p = mP & ~(kFlagN | kFlagZ);
				p += (mData & 0x80);	// N

				if (!mData)
					p += kFlagZ;

				mP = p;
			}
			break;

		case kStateDecXWait:
			{
				uint8 p = mP & ~(kFlagN | kFlagZ);

				--mX;
				if (!mX)
					p += kFlagZ;

				p += (mX & 0x80);	// N

				mP = p;
			}
			INSN_DUMMY_FETCH_NOINC();
			return kATSimEvent_None;

		case kStateDecC:
			--mData;
			mP |= kFlagC;
			if (!mData)
				mP &= ~kFlagC;
			break;

		case kStateAnd:
			{
				mData &= mA;

				uint8 p = mP & ~(kFlagN | kFlagZ);

				p += (mData & 0x80);	// N

				if (!mData)
					p += kFlagZ;

				mP = p;
			}
			break;

		case kStateAnd_SAX:
			mData &= mA;
			break;

		case kStateAnc:
			mData &= mA;
			mP &= ~(kFlagN | kFlagZ | kFlagC);
			if (mData & 0x80)
				mP |= kFlagN | kFlagC;
			if (!mData)
				mP |= kFlagZ;
			break;

		case kStateXaa:
			mA &= (mData & mX);
			mP &= ~(kFlagN | kFlagZ);
			if (mA & 0x80)
				mP |= kFlagN;
			if (!mA)
				mP |= kFlagZ;
			break;

		case kStateLas:
			mA = mX = mS = (mData & mS);
			mP &= ~(kFlagN | kFlagZ);
			if (mS & 0x80)
				mP |= kFlagN;
			if (!mS)
				mP |= kFlagZ;
			break;

		case kStateSbx:
			mP &= ~(kFlagN | kFlagZ | kFlagC);
			mX &= mA;
			if (mX >= mData)
				mP |= kFlagC;
			mX -= mData;
			mP |= (mX & 0x80);	// N
			if (!mX)
				mP |= kFlagZ;
			break;

		case kStateArr:
			{
				mA &= mData;

				// stash off AND result for decimal correction
				uint8 andres = mA;

				mA = (mA >> 1) + (mP << 7);
				mP &= ~(kFlagN | kFlagZ | kFlagC | kFlagV);

				switch(mA & 0x60) {
					case 0x00:	break;
					case 0x20:	mP += kFlagV; break;
					case 0x40:	mP += kFlagC | kFlagV; break;
					case 0x60:	mP += kFlagC; break;
				}

				mP += (mA & 0x80);

				if (!mA)
					mP |= kFlagZ;

				// perform BCD adjustment and correct C if in decimal mode
				if (mP & kFlagD) {
					// low adjust
					if ((andres & 15) >= 5)
						mA = (mA & 0xf0) + ((mA + 6) & 15);

					// high adjust and carry out
					mP &= ~kFlagC;
					if (andres >= 0x50) {
						mA += 0x60;
						mP |= kFlagC;
					}
				}
			}
			break;

		case kStateXas:
			mS = (mX & mA);
			mData = mS & (uint32)((mAddr >> 8) + 1);
			break;

		case kStateOr:
			mA |= mData;
			mP &= ~(kFlagN | kFlagZ);
			mP |= (mA & 0x80);	// N
			if (!mA)
				mP |= kFlagZ;
			break;

		case kStateXor:
			mA ^= mData;
			mP &= ~(kFlagN | kFlagZ);
			mP |= (mA & 0x80);	// N
			if (!mA)
				mP |= kFlagZ;
			break;

		case kStateAsl:
			mP &= ~(kFlagN | kFlagZ | kFlagC);
			if (mData & 0x80)
				mP |= kFlagC;
			mData += mData;
			mP |= (mData & 0x80);	// N
			if (!mData)
				mP |= kFlagZ;
			break;

		case kStateLsr:
			mP &= ~(kFlagN | kFlagZ | kFlagC);
			if (mData & 0x01)
				mP |= kFlagC;
			mData >>= 1;
			if (!mData)
				mP |= kFlagZ;
			break;

		case kStateRol:
			{
				uint32 result = (uint32)mData + (uint32)mData + (mP & kFlagC);
				mP &= ~(kFlagN | kFlagZ | kFlagC);
				if (result & 0x100)
					mP |= kFlagC;
				mData = (uint8)result;
				if (mData & 0x80)
					mP |= kFlagN;
				if (!mData)
					mP |= kFlagZ;
			}
			break;

		case kStateRor:
			{
				uint32 result = (mData >> 1) + ((mP & kFlagC) << 7);
				mP &= ~(kFlagN | kFlagZ | kFlagC);
				if (mData & 0x1)
					mP |= kFlagC;
				mData = (uint8)result;
				if (mData & 0x80)
					mP |= kFlagN;
				if (!mData)
					mP |= kFlagZ;
			}
			break;

		case kStateBit:
			{
				uint8 result = mData & mA;
				mP &= ~kFlagZ;
				if (!result)
					mP |= kFlagZ;
			}
			break;

		case kStateSEI:
			if (!(mP & kFlagI)) {
				mP |= kFlagI;

				mIFlagSetCycle = mpCallbacks->CPUGetUnhaltedCycle();
			}
			break;

		case kStateCLI:
			if (mP & kFlagI) {
				mP &= ~kFlagI;
				mIntFlags |= kIntFlag_IRQReleasePending;
			}
			break;

		case kStateSEC:
			mP |= kFlagC;
			break;

		case kStateCLC:
			mP &= ~kFlagC;
			break;

		case kStateSED:
			mP |= kFlagD;
			break;

		case kStateCLD:
			mP &= ~kFlagD;
			break;

		case kStateCLV:
			mP &= ~kFlagV;
			break;

		case kStateJs:
			if (!(mP & kFlagN)) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJns:
			if (mP & kFlagN) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJc:
			if (!(mP & kFlagC)) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJnc:
			if (mP & kFlagC) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJz:
			if (!(mP & kFlagZ)) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJnz:
			if (mP & kFlagZ) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJo:
			if (!(mP & kFlagV)) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJno:
			if (mP & kFlagV) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		/////////
		case kStateJsAddToPath:
			if (!(mP & kFlagN)) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			mInsnFlags[mPC] |= kInsnFlagPathStart;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJnsAddToPath:
			if (mP & kFlagN) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			mInsnFlags[mPC] |= kInsnFlagPathStart;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJcAddToPath:
			if (!(mP & kFlagC)) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			mInsnFlags[mPC] |= kInsnFlagPathStart;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJncAddToPath:
			if (mP & kFlagC) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			mInsnFlags[mPC] |= kInsnFlagPathStart;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJzAddToPath:
			if (!(mP & kFlagZ)) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			mInsnFlags[mPC] |= kInsnFlagPathStart;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJnzAddToPath:
			if (mP & kFlagZ) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			mInsnFlags[mPC] |= kInsnFlagPathStart;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJoAddToPath:
			if (!(mP & kFlagV)) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			mInsnFlags[mPC] |= kInsnFlagPathStart;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJnoAddToPath:
			if (mP & kFlagV) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			mInsnFlags[mPC] |= kInsnFlagPathStart;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJccFalseRead:
			AT_CPU_READ_BYTE(mAddr);
			return kATSimEvent_None;

		case kStateInvokeHLE:
			if (mpHLE) {
				mHLEDelay = 0;
				int r = mpHLE->InvokeHLE(*this, *mpMemory, mPC - 1, mAddr);

				if (r == -2) {
					mpNextState += 2;
					break;
				}

				if (r >= 0)
					return r;
			}
			break;

		case kStateHLEDelay:
			if (mHLEDelay) {
				--mHLEDelay;
				return kATSimEvent_None;
			}
			break;

		case kStateResetBit:
			mData &= ~(1 << ((mOpcode >> 4) & 7));
			break;

		case kStateSetBit:
			mData |= 1 << ((mOpcode >> 4) & 7);
			break;

		case kStateReadRel:
			mRelOffset = INSN_FETCH();
			return kATSimEvent_None;

		case kStateJ0:
			if (mData & (1 << ((mOpcode >> 4) & 7))) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mRelOffset;
			mAddr += mPC & 0xff;
			if (mAddr == mPC)
				++mpNextState;
			return kATSimEvent_None;

		case kStateJ1:
			if (!(mData & (1 << ((mOpcode >> 4) & 7)))) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mRelOffset;
			mAddr += mPC & 0xff;
			if (mAddr == mPC)
				++mpNextState;
			return kATSimEvent_None;

		case kStateJ0AddToPath:
			if (mData & (1 << ((mOpcode >> 4) & 7))) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mRelOffset;
			mAddr += mPC & 0xff;
			mInsnFlags[mPC] |= kInsnFlagPathStart;
			if (mAddr == mPC)
				++mpNextState;
			return kATSimEvent_None;

		case kStateJ1AddToPath:
			if (!(mData & (1 << ((mOpcode >> 4) & 7)))) {
				++mpNextState;
				break;
			}

			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mRelOffset;
			mAddr += mPC & 0xff;
			mInsnFlags[mPC] |= kInsnFlagPathStart;
			if (mAddr == mPC)
				++mpNextState;
			return kATSimEvent_None;

		case kStateWaitForInterrupt:
			switch(mIntFlags & (kIntFlag_IRQPending | kIntFlag_IRQActive)) {
				case kIntFlag_IRQPending:
				case kIntFlag_IRQActive:
					UpdatePendingIRQState();
					break;
			}

			if (!(mIntFlags & (kIntFlag_IRQPending | kIntFlag_NMIPending)))
				--mpNextState;
			return kATSimEvent_None;

		case kStateStop:
			--mpNextState;
			return kATSimEvent_None;

		case kStateJ:
			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateJAddToPath:
			INSN_FETCH_NOINC();
			mAddr = mPC & 0xff00;
			mPC += (sint16)(sint8)mData;
			mAddr += mPC & 0xff;
			mInsnFlags[mPC] |= kInsnFlagPathStart;
			if (mAddr == mPC) {
				mNMIIgnoreUnhaltedCycle = mpCallbacks->CPUGetUnhaltedCycle() + 1;
				++mpNextState;
			}
			return kATSimEvent_None;

		case kStateTrb:
			mP &= ~kFlagZ;
			if (!(mData & mA))
				mP |= kFlagZ;

			mData &= ~mA;
			break;

		case kStateTsb:
			mP &= ~kFlagZ;
			if (!(mData & mA))
				mP |= kFlagZ;

			mData |= mA;
			break;

		case kStateC02_Adc:
			if (mP & kFlagD) {
				uint32 lowResult = (mA & 15) + (mData & 15) + (mP & kFlagC);
				if (lowResult >= 10)
					lowResult += 6;

				if (lowResult >= 0x20)
					lowResult -= 0x10;

				uint32 highResult = (mA & 0xf0) + (mData & 0xf0) + lowResult;

				mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

				mP |= (((highResult ^ mA) & ~(mData ^ mA)) >> 1) & kFlagV;

				if (highResult >= 0xA0)
					highResult += 0x60;

				if (highResult >= 0x100)
					mP |= kFlagC;

				uint8 result = (uint8)highResult;

				if (!result)
					mP |= kFlagZ;

				if (result & 0x80)
					mP |= kFlagN;

				mA = result;
			} else {
				uint32 carry7 = (mA & 0x7f) + (mData & 0x7f) + (mP & kFlagC);
				uint32 result = carry7 + (mA & 0x80) + (mData & 0x80);

				mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

				if (result & 0x80)
					mP |= kFlagN;

				if (result >= 0x100)
					mP |= kFlagC;

				if (!(result & 0xff))
					mP |= kFlagZ;

				mP |= ((result >> 2) ^ (carry7 >> 1)) & kFlagV;

				mA = (uint8)result;

				// No extra cycle unless decimal mode is on.
				++mpNextState;
			}
			break;

		case kStateC02_Sbc:
			if (mP & kFlagD) {
				// Pole Position needs N set properly here for its passing counter
				// to stop correctly!

				mData ^= 0xff;

				// Flags set according to binary op
				uint32 carry7 = (mA & 0x7f) + (mData & 0x7f) + (mP & kFlagC);
				uint32 result = carry7 + (mA & 0x80) + (mData & 0x80);

				// BCD
				uint32 lowResult = (mA & 15) + (mData & 15) + (mP & kFlagC);
				if (lowResult < 0x10)
					lowResult -= 6;

				uint32 highResult = (mA & 0xf0) + (mData & 0xf0) + (lowResult & 0x1f);

				if (highResult < 0x100)
					highResult -= 0x60;

				mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

				uint8 bcdresult = (uint8)highResult;

				if (bcdresult & 0x80)
					mP |= kFlagN;

				if (result >= 0x100)
					mP |= kFlagC;

				if (!(bcdresult & 0xff))
					mP |= kFlagZ;

				mP |= ((result >> 2) ^ (carry7 >> 1)) & kFlagV;

				mA = bcdresult;
			} else {
				mData ^= 0xff;
				uint32 carry7 = (mA & 0x7f) + (mData & 0x7f) + (mP & kFlagC);
				uint32 result = carry7 + (mA & 0x80) + (mData & 0x80);

				mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

				if (result & 0x80)
					mP |= kFlagN;

				if (result >= 0x100)
					mP |= kFlagC;

				if (!(result & 0xff))
					mP |= kFlagZ;

				mP |= ((result >> 2) ^ (carry7 >> 1)) & kFlagV;

				mA = (uint8)result;

				// No extra cycle unless decimal mode is on.
				++mpNextState;
			}
			break;

		case kStateAddEAToHistory:
			{
				HistoryEntry& he = mHistory[(mHistoryIndex - 1) & 131071];

				he.mEA = mAddr;
			}
			break;

		case kStateAddAsPathStart:
			if (!(mInsnFlags[mPC] & kInsnFlagPathStart)) {
				mInsnFlags[mPC] |= kInsnFlagPathStart;
			}
			break;

		case kStateAddToPath:
			{
				uint16 adjpc = mPC - 1;
				if (!(mInsnFlags[adjpc] & kInsnFlagPathExecuted)) {
					mInsnFlags[adjpc] |= kInsnFlagPathExecuted;

					if (mbPathBreakEnabled) {
						mbUnusedCycle = true;
						return kATSimEvent_CPUNewPath;
					}
				}
			}
			break;

		case kStateBreakOnUnsupportedOpcode:
			mbUnusedCycle = true;
			return kATSimEvent_CPUIllegalInsn;

		////////// 65C816 states

#ifdef AT_CPU_MACHINE_65C816
		case kStateReadImmL16:
			mData16 = INSN_FETCH();
			return kATSimEvent_None;

		case kStateReadImmH16:
			mData16 += (uint32)INSN_FETCH() << 8;
			return kATSimEvent_None;

		case kStateReadAddrDp:
			mAddr = (uint32)INSN_FETCH() + mDP;
			mAddrBank = 0;
			return kATSimEvent_None;

		case kStateReadAddrDpX:
			mAddr = (uint32)INSN_FETCH() + mDP + mX + ((uint32)mXH << 8);
			mAddrBank = 0;
			return kATSimEvent_None;

		case kStateReadAddrDpXInPage:
			mAddr = mDP + (uint8)(INSN_FETCH() + mX);
			mAddrBank = 0;
			return kATSimEvent_None;

		case kStateReadAddrDpY:
			mAddr = (uint32)INSN_FETCH() + mDP + mY + ((uint32)mYH << 8);
			mAddrBank = 0;
			return kATSimEvent_None;

		case kStateReadAddrDpYInPage:
			mAddr = mDP + (uint8)(INSN_FETCH() + mY);
			mAddrBank = 0;
			return kATSimEvent_None;

		case kStateReadIndAddrDp:
			mAddr = mData + ((uint16)AT_CPU_READ_BYTE(mDP + mAddr + 1) << 8);
			mAddrBank = mB;
			return kATSimEvent_None;

		case kStateReadIndAddrDpY:
			mAddr = mData + ((uint16)AT_CPU_READ_BYTE(mDP + mAddr + 1) << 8) + mY + ((uint32)mYH << 8);
			mAddrBank = mB;
			return kATSimEvent_None;

		case kStateReadIndAddrDpLongH:
			mData16 = mData + ((uint16)AT_CPU_READ_BYTE(mDP + mAddr + 1) << 8);
			return kATSimEvent_None;

		case kStateReadIndAddrDpLongB:
			mAddrBank = AT_CPU_READ_BYTE(mDP + mAddr + 2);
			mAddr = mData16;
			return kATSimEvent_None;

		case kStateReadAddrAddY:
			mAddr += mY + ((uint32)mYH << 8);
			break;

		case kState816ReadAddrL:
			mAddrBank = mB;
			mAddr = INSN_FETCH();
			return kATSimEvent_None;

		case kStateRead816AddrAbsHY:
			mAddr = mData + ((uint32)AT_CPU_EXT_READ_BYTE(mAddr + 1, mAddrBank) << 8) + mY + ((uint32)mYH << 8);
			mAddrBank = mB;
			return kATSimEvent_None;

		case kStateRead816AddrAbsLongL:
			mData = AT_CPU_EXT_READ_BYTE(mAddr, mAddrBank);
			return kATSimEvent_None;

		case kStateRead816AddrAbsLongH:
			mData16 = mData + ((uint32)AT_CPU_EXT_READ_BYTE(mAddr + 1, mAddrBank) << 8);
			return kATSimEvent_None;

		case kStateRead816AddrAbsLongB:
			mAddrBank = AT_CPU_EXT_READ_BYTE(mAddr + 2, mAddrBank);
			mAddr = mData16;
			return kATSimEvent_None;

		case kState816ReadAddrHX:
			mAddr += INSN_FETCH() << 8;
			mAddr2 = (mAddr & 0xff00) + ((mAddr + mX) & 0x00ff);
			mAddr = mAddr + mX + ((uint32)mXH << 8);
			return kATSimEvent_None;

		case kState816ReadAddrHY:
			mAddr += INSN_FETCH() << 8;
			mAddr2 = (mAddr & 0xff00) + ((mAddr + mY) & 0x00ff);
			mAddr = mAddr + mY + ((uint32)mYH << 8);
			return kATSimEvent_None;

		case kStateReadAddrB:
			mAddrBank = INSN_FETCH();
			return kATSimEvent_None;

		case kStateReadAddrBX:
			mAddrBank = INSN_FETCH();
			{
				uint32 ea = (uint32)mAddr + mX + ((uint32)mXH << 8);

				if (ea >= 0x10000)
					++mAddrBank;

				mAddr = (uint16)ea;
			}
			return kATSimEvent_None;

		case kStateReadAddrSO:
			mAddrBank = mB;
			mAddr = mS + ((uint32)mSH << 8) + INSN_FETCH();
			if (mbEmulationFlag)
				mAddr = (uint8)mAddr + 0x100;

			return kATSimEvent_None;

		case kStateBtoD:
			mData = mB;
			break;

		case kStateKtoD:
			mData = mK;
			break;

		case kState0toD16:
			mData16 = 0;
			break;

		case kStateAtoD16:
			mData16 = ((uint32)mAH << 8) + mA;
			break;

		case kStateXtoD16:
			mData16 = ((uint32)mXH << 8) + mX;
			break;

		case kStateYtoD16:
			mData16 = ((uint32)mYH << 8) + mY;
			break;

		case kStateStoD16:
			mData16 = ((uint32)mSH << 8) + mS;
			break;

		case kStateDPtoD16:
			mData16 = mDP;
			break;

		case kStateDtoB:
			mB = mData;
			break;

		case kStateDtoA16:
			mA = (uint8)mData16;
			mAH = (uint8)(mData16 >> 8);
			break;

		case kStateDtoX16:
			mX = (uint8)mData16;
			mXH = (uint8)(mData16 >> 8);
			break;

		case kStateDtoY16:
			mY = (uint8)mData16;
			mYH = (uint8)(mData16 >> 8);
			break;

		case kStateDtoPNative:
			if ((mP & ~mData) & kFlagI)
				mIntFlags |= kIntFlag_IRQReleasePending;
			mP = mData;
			Update65816DecodeTable();
			break;

		case kStateDtoS16:
			VDASSERT(!mbEmulationFlag);
			mS = (uint8)mData16;
			mSH = (uint8)(mData16 >> 8);
			break;

		case kStateDtoDP16:
			mDP = mData16;
			break;

		case kStateDSetSZ16:
			mP &= ~(kFlagN | kFlagZ);
			if (mData16 & 0x8000)
				mP |= kFlagN;
			if (!mData16)
				mP |= kFlagZ;
			break;

		case kStateDSetSV16:
			mP &= ~(kFlagN | kFlagV);
			mP |= ((uint8)(mData16 >> 8) & 0xC0);
			break;

		case kState816WriteByte:
			AT_CPU_EXT_WRITE_BYTE(mAddr, mAddrBank, mData);
			return kATSimEvent_None;

		case kStateWriteL16:
			AT_CPU_EXT_WRITE_BYTE(mAddr, mAddrBank, (uint8)mData16);
			return kATSimEvent_None;

		case kStateWriteH16:
			AT_CPU_EXT_WRITE_BYTE(mAddr + 1, mAddrBank, (uint8)(mData16 >> 8));
			return kATSimEvent_None;

		case kState816ReadByte:
			mData = AT_CPU_EXT_READ_BYTE(mAddr, mAddrBank);
			return kATSimEvent_None;

		case kState816ReadByte_PBK:
			mData = AT_CPU_EXT_READ_BYTE(mAddr, mK);
			return kATSimEvent_None;

		case kStateReadL16:
			mData16 = AT_CPU_EXT_READ_BYTE(mAddr, mAddrBank);
			return kATSimEvent_None;

		case kStateReadH16:
			mData16 += ((uint32)AT_CPU_EXT_READ_BYTE(mAddr + 1, mAddrBank) << 8);
			return kATSimEvent_None;

		case kStateAnd16:
			mData16 &= (mA + ((uint32)mAH << 8));
			mP &= ~(kFlagN | kFlagZ);
			if (mData16 & 0x8000)
				mP |= kFlagN;
			if (!mData16)
				mP |= kFlagZ;
			break;

		case kStateOr16:
			mA |= (uint8)mData16;
			mAH |= (uint8)(mData16 >> 8);
			mP &= ~(kFlagN | kFlagZ);
			if (mAH & 0x80)
				mP |= kFlagN;
			if (!(mA | mAH))
				mP |= kFlagZ;
			break;

		case kStateXor16:
			mA ^= (uint8)mData16;
			mAH ^= (uint8)(mData16 >> 8);
			mP &= ~(kFlagN | kFlagZ);
			if (mAH & 0x80)
				mP |= kFlagN;
			if (!(mA | mAH))
				mP |= kFlagZ;
			break;

		case kStateAdc16:
			if (mP & kFlagD) {
				uint32 lowResult = (mA & 15) + (mData16 & 15) + (mP & kFlagC);
				if (lowResult >= 10)
					lowResult += 6;

				uint32 midResult = (mA & 0xf0) + (mData16 & 0xf0) + lowResult;
				if (midResult >= 0xA0)
					midResult += 0x60;

				uint32 acchi = (uint32)mAH << 8;
				uint32 midHiResult = (acchi & 0xf00) + (mData16 & 0xf00) + midResult;
				if (midHiResult >= 0xA00)
					midHiResult += 0x600;

				uint32 highResult = (acchi & 0xf000) + (mData16 & 0xf000) + midHiResult;
				if (highResult >= 0xA000)
					highResult += 0x6000;

				mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

				if (highResult >= 0x10000)
					mP |= kFlagC;

				if (!(highResult & 0xffff))
					mP |= kFlagZ;

				if (highResult & 0x8000)
					mP |= kFlagN;

				mA = (uint8)highResult;
				mAH = (uint8)(highResult >> 8);
			} else {
				uint32 data = mData16;
				uint32 acc = mA + ((uint32)mAH << 8);
				uint32 carry15 = (acc & 0x7fff) + (data & 0x7fff) + (mP & kFlagC);
				uint32 result = carry15 + (acc & 0x8000) + (data & 0x8000);

				mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

				if (result & 0x8000)
					mP |= kFlagN;

				if (result >= 0x10000)
					mP |= kFlagC;

				if (!(result & 0xffff))
					mP |= kFlagZ;

				mP |= ((result >> 10) ^ (carry15 >> 9)) & kFlagV;

				mA = (uint8)result;
				mAH = (uint8)(result >> 8);
			}
			break;

		case kStateSbc16:
			if (mP & kFlagD) {
				uint32 data = (uint32)mData ^ 0xffff;
				uint32 acc = mA + ((uint32)mAH << 8);

				// BCD
				uint32 lowResult = (acc & 15) + (data & 15) + (mP & kFlagC);
				if (lowResult < 0x10)
					lowResult -= 6;

				uint32 midResult = (acc & 0xf0) + (data & 0xf0) + lowResult;
				if (midResult < 0x100)
					midResult -= 0x60;

				uint32 midHiResult = (acc & 0xf00) + (data & 0xf00) + midResult;
				if (midHiResult < 0x1000)
					midHiResult -= 0x600;

				uint32 highResult = (acc & 0xf000) + (data & 0xf000) + midHiResult;
				if (highResult < 0x10000)
					highResult -= 0x6000;

				mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

				if (highResult & 0x8000)
					mP |= kFlagN;

				if (highResult >= 0x10000)
					mP |= kFlagC;

				if (!(highResult & 0xffff))
					mP |= kFlagZ;

				mA = (uint8)highResult;
				mAH = (uint8)(highResult >> 8);
			} else {
				uint32 acc = ((uint32)mAH << 8) + mA;
				
				uint32 d16 = (uint32)mData16 ^ 0xffff;
				uint32 carry15 = (acc & 0x7fff) + (d16 & 0x7fff) + (mP & kFlagC);
				uint32 result = carry15 + (acc & 0x8000) + (d16 & 0x8000);

				mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

				if (result & 0x8000)
					mP |= kFlagN;

				if (result >= 0x10000)
					mP |= kFlagC;

				if (!(result & 0xffff))
					mP |= kFlagZ;

				mP |= ((result >> 10) ^ (carry15 >> 9)) & kFlagV;

				mA = (uint8)result;
				mAH = (uint8)(result >> 8);
			}
			break;

		case kStateCmp16:
			{
				uint32 acc = ((uint32)mAH << 8) + mA;
				uint32 d16 = (uint32)mData16 ^ 0xffff;
				uint32 result = acc + d16 + 1;

				mP &= ~(kFlagC | kFlagN | kFlagZ);

				if (result & 0x8000)
					mP |= kFlagN;

				if (result >= 0x10000)
					mP |= kFlagC;

				if (!(result & 0xffff))
					mP |= kFlagZ;
			}
			break;

		case kStateInc16:
			++mData16;
			mP &= ~(kFlagN | kFlagZ);
			if (mData16 & 0x8000)
				mP |= kFlagN;
			if (!mData16)
				mP |= kFlagZ;
			break;

		case kStateDec16:
			--mData16;
			mP &= ~(kFlagN | kFlagZ);
			if (mData16 & 0x8000)
				mP |= kFlagN;
			if (!mData16)
				mP |= kFlagZ;
			break;

		case kStateRol16:
			{
				uint32 result = (uint32)mData16 + (uint32)mData16 + (mP & kFlagC);
				mP &= ~(kFlagN | kFlagZ | kFlagC);
				if (result & 0x10000)
					mP |= kFlagC;
				mData16 = (uint16)result;
				if (mData16 & 0x8000)
					mP |= kFlagN;
				if (!mData16)
					mP |= kFlagZ;
			}
			break;

		case kStateRor16:
			{
				uint32 result = ((uint32)mData16 >> 1) + ((uint32)(mP & kFlagC) << 15);
				mP &= ~(kFlagN | kFlagZ | kFlagC);
				if (mData16 & 1)
					mP |= kFlagC;
				mData16 = (uint16)result;
				if (result & 0x8000)
					mP |= kFlagN;
				if (!result)
					mP |= kFlagZ;
			}
			break;

		case kStateAsl16:
			mP &= ~(kFlagN | kFlagZ | kFlagC);
			if (mData16 & 0x8000)
				mP |= kFlagC;
			mData16 += mData16;
			if (mData16 & 0x8000)
				mP |= kFlagN;
			if (!mData16)
				mP |= kFlagZ;
			break;

		case kStateLsr16:
			mP &= ~(kFlagN | kFlagZ | kFlagC);
			if (mData16 & 0x01)
				mP |= kFlagC;
			mData16 >>= 1;
			if (!mData16)
				mP |= kFlagZ;
			break;

		case kStateBit16:
			{
				uint32 acc = mA + ((uint32)mAH << 8);
				uint16 result = mData16 & acc;

				mP &= ~kFlagZ;
				if (!result)
					mP |= kFlagZ;
			}
			break;

		case kStateTrb16:
			{
				uint32 acc = mA + ((uint32)mAH << 8);

				mP &= ~kFlagZ;
				if (!(mData16 & acc))
					mP |= kFlagZ;

				mData16 &= ~acc;
			}
			break;

		case kStateTsb16:
			{
				uint32 acc = mA + ((uint32)mAH << 8);

				mP &= ~kFlagZ;
				if (!(mData16 & acc))
					mP |= kFlagZ;

				mData16 |= acc;
			}
			break;

		case kStateCmpX16:
			{
				uint32 data = (uint32)mData16 ^ 0xffff;
				uint32 result = mX + ((uint32)mXH << 8) + data + 1;

				mP &= ~(kFlagC | kFlagN | kFlagZ);

				if (result & 0x8000)
					mP |= kFlagN;

				if (result >= 0x10000)
					mP |= kFlagC;

				if (!(result & 0xffff))
					mP |= kFlagZ;
			}
			break;

		case kStateCmpY16:
			{
				uint32 data = (uint32)mData16 ^ 0xffff;
				uint32 result = mY + ((uint32)mYH << 8) + data + 1;

				mP &= ~(kFlagC | kFlagN | kFlagZ);

				if (result & 0x8000)
					mP |= kFlagN;

				if (result >= 0x10000)
					mP |= kFlagC;

				if (!(result & 0xffff))
					mP |= kFlagZ;
			}
			break;

		case kStateXba:
			{
				uint8 t = mAH;
				mAH = mA;
				mA = t;

				mP &= ~(kFlagN | kFlagZ);

				mP |= (t & kFlagN);

				if (!t)
					mP |= kFlagZ;
			}
			break;

		case kStateXce:
			{
				bool newEmuFlag = ((mP & kFlagC) != 0);
				mP &= ~kFlagC;
				if (mbEmulationFlag)
					mP |= kFlagC | kFlagM | kFlagX;

				mbEmulationFlag = newEmuFlag;
				Update65816DecodeTable();
			}
			break;

		case kStatePushNative:
			AT_CPU_WRITE_BYTE_HL(mSH, mS, mData);
			if (!mS-- && !mbEmulationFlag)
				--mSH;
			return kATSimEvent_None;

		case kStatePushL16:
			AT_CPU_WRITE_BYTE_HL(mSH, mS, (uint8)mData16);
			if (!mS-- && !mbEmulationFlag)
				--mSH;
			return kATSimEvent_None;

		case kStatePushH16:
			AT_CPU_WRITE_BYTE_HL(mSH, mS, (uint8)(mData16 >> 8));
			if (!mS-- && !mbEmulationFlag)
				--mSH;
			return kATSimEvent_None;

		case kStatePushPBKNative:
			AT_CPU_WRITE_BYTE_HL(mSH, mS, mK);
			if (!mS-- && !mbEmulationFlag)
				--mSH;
			return kATSimEvent_None;

		case kStatePushPCLNative:
			AT_CPU_WRITE_BYTE_HL(mSH, mS, mPC & 0xff);
			if (!mS-- && !mbEmulationFlag)
				--mSH;
			return kATSimEvent_None;

		case kStatePushPCHNative:
			AT_CPU_WRITE_BYTE_HL(mSH, mS, mPC >> 8);
			if (!mS-- && !mbEmulationFlag)
				--mSH;
			return kATSimEvent_None;

		case kStatePushPCLM1Native:
			AT_CPU_WRITE_BYTE_HL(mSH, mS, (mPC - 1) & 0xff);
			if (!mS-- && !mbEmulationFlag)
				--mSH;
			return kATSimEvent_None;

		case kStatePushPCHM1Native:
			AT_CPU_WRITE_BYTE_HL(mSH, mS, (mPC - 1) >> 8);
			if (!mS-- && !mbEmulationFlag)
				--mSH;
			return kATSimEvent_None;

		case kStatePopNative:
			if (!++mS && !mbEmulationFlag)
				++mSH;
			mData = AT_CPU_READ_BYTE_HL(mSH, mS);
			return kATSimEvent_None;

		case kStatePopL16:
			if (!++mS && !mbEmulationFlag)
				++mSH;

			mData16 = AT_CPU_READ_BYTE_HL(mSH, mS);
			return kATSimEvent_None;

		case kStatePopH16:
			if (!++mS && !mbEmulationFlag)
				++mSH;

			mData16 += (uint32)AT_CPU_READ_BYTE_HL(mSH, mS) << 8;
			return kATSimEvent_None;

		case kStatePopPCLNative:
			if (!++mS && !mbEmulationFlag)
				++mSH;
			mPC = AT_CPU_READ_BYTE_HL(mSH, mS);
			return kATSimEvent_None;

		case kStatePopPCHNative:
			if (!++mS && !mbEmulationFlag)
				++mSH;
			mPC += (uint32)AT_CPU_READ_BYTE_HL(mSH, mS) << 8;
			return kATSimEvent_None;

		case kStatePopPCHP1Native:
			if (!++mS && !mbEmulationFlag)
				++mSH;
			mPC += ((uint32)AT_CPU_READ_BYTE_HL(mSH, mS) << 8) + 1;
			return kATSimEvent_None;

		case kStatePopPBKNative:
			if (!++mS && !mbEmulationFlag)
				++mSH;
			mK = AT_CPU_READ_BYTE_HL(mSH, mS);
			return kATSimEvent_None;

		case kStateRep:
			if (mData & kFlagI)
				mIntFlags |= kIntFlag_IRQReleasePending;

			if (mbEmulationFlag)
				mP &= ~(mData & 0xcf);		// m and x are off-limits
			else
				mP &= ~mData;

			Update65816DecodeTable();
			return kATSimEvent_None;

		case kStateSep:
			if (mbEmulationFlag)
				mP |= mData & 0xcf;		// m and x are off-limits
			else
				mP |= mData;

			Update65816DecodeTable();
			return kATSimEvent_None;

		case kStateJ16:
			mPC += mData16;
			return kATSimEvent_None;

		case kStateJ16AddToPath:
			mPC += mData16;
			mInsnFlags[mPC] |= kInsnFlagPathStart;
			return kATSimEvent_None;

		case kState816_NatCOPVecToPC:
			mPC = 0xFFE4;
			mK = 0;
			break;

		case kState816_EmuCOPVecToPC:
			mPC = 0xFFF4;
			break;

		case kState816_NatNMIVecToPC:
			mPC = 0xFFEA;
			mK = 0;
			break;

		case kState816_NatIRQVecToPC:
			mPC = 0xFFEE;
			mK = 0;
			break;

		case kState816_NatBRKVecToPC:
			mPC = 0xFFE6;
			mK = 0;
			break;

		case kState816_SetI_ClearD:
			mP |= kFlagI;
			mP &= ~kFlagD;
			break;

		case kState816_LongAddrToPC:
			mPC = mAddr;
			mK = mAddrBank;
			break;

		case kState816_MoveRead:
			mData = AT_CPU_EXT_READ_BYTE(mX + ((uint32)mXH << 8), mAddrBank);
			return kATSimEvent_None;

		case kState816_MoveWriteP:
			mAddr = mY + ((uint32)mYH << 8);
			AT_CPU_EXT_WRITE_BYTE(mAddr, mB, mData);

			if (!mbEmulationFlag && !(mP & kFlagX)) {
				if (!mX)
					--mXH;

				if (!mY)
					--mYH;
			}

			--mX;
			--mY;

			if (mA-- || mAH--)
					mPC -= 3;

			return kATSimEvent_None;

		case kState816_MoveWriteN:
			mAddr = mY + ((uint32)mYH << 8);
			AT_CPU_EXT_WRITE_BYTE(mAddr, mB, mData);

			++mX;
			++mY;
			if (!mbEmulationFlag && !(mP & kFlagX)) {
				if (!mX)
					++mXH;

				if (!mY)
					++mYH;
			}

			if (mA-- || mAH--)
					mPC -= 3;

			return kATSimEvent_None;

		case kState816_Per:
			mData16 += mPC;
			break;
#endif

		case kStateVerifyJump:
			mpVerifier->VerifyJump(mAddr);
			break;

		case kStateVerifyIRQEntry:
			mpVerifier->OnIRQEntry();
			break;

		case kStateVerifyNMIEntry:
			mpVerifier->OnNMIEntry();
			break;

		case kStateVerifyReturn:
			mpVerifier->OnReturn();
			break;

#ifdef _DEBUG
		default:
			VDASSERT(!"Invalid CPU state detected.");
			break;
#else
		default:
			__assume(false);
#endif
	}
}

#undef AT_CPU_READ_BYTE
#undef AT_CPU_READ_BYTE_ADDR16
#undef AT_CPU_DUMMY_READ_BYTE
#undef AT_CPU_READ_BYTE_HL
#undef AT_CPU_EXT_READ_BYTE
#undef AT_CPU_DUMMY_EXT_READ_BYTE
#undef AT_CPU_WRITE_BYTE
#undef AT_CPU_WRITE_BYTE_HL
#undef AT_CPU_EXT_WRITE_BYTE

#undef INSN_FETCH
#undef INSN_FETCH_NOINC
#undef INSN_DUMMY_FETCH_NOINC
