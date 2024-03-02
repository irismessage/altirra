#include <stdafx.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/scheduler.h>
#include <at/atcpu/co6502.h>
#include <at/atcpu/history.h>
#include <at/atcpu/memorymap.h>
#include <test.h>

namespace {
	enum {
		kModeInvalid,
		kModeImplied,		// implied
		kModeRel,			// beq rel-offset
		kModeRel16,			// brl rel16-offset
		kModeImm,			// lda #$00
		kModeImmMode16,		// lda #$0100
		kModeImmIndex16,	// ldx #$0100
		kModeImm16,			// pea #$2000
		kModeIVec,			// cop 0
		kModeZp,			// lda $00
		kModeZpX,			// lda $00,x
		kModeZpY,			// lda $00,y
		kModeAbs,			// lda $0100
		kModeAbsX,			// lda $0100,x
		kModeAbsY,			// lda $0100,y
		kModeIndA,			// jmp ($0100)
		kModeIndAL,			// jmp [$0100]
		kModeIndX,			// lda ($00,x)
		kModeIndY,			// lda ($00),y
		kModeInd,			// lda ($00)
		kModeIndAX,			// jmp ($0100,x)
		kModeBit,			// smb0 $04
		kModeBBit,			// bbr0 $04,rel-offset
		kModeLong,			// lda $010000
		kModeLongX,			// lda $010000,x
		kModeStack,			// lda 1,s
		kModeStackIndY,		// lda (1,s),y
		kModeDpIndLong,		// lda [$00]
		kModeDpIndLongY,	// lda [$00],y
		kModeMove,			// mvp #$01,#$02
	};

									//	inv	imp	rel	r16	imm	imM	imX	im6	ivc	zp	zpX	zpY	abs	abX	abY	(a)	[a]	(zX	(zY	(z)	(aX	bz	z,r	al	alX	o,S	S)Y	[d]	[dY	#sd
	static const uint8 kBPM_M8_X8[]={	1,	1,	2,	3,	2,	2,	2,	3,	1,	2,	2,	2,	3,	3,	3,	3,	3,	2,	2,	2,	3,	2,	3,	4,	4,	2,	2,	2,	2,	3	};

#define PROCESS_OPCODES			\
		PROCESS_OPCODE(bad, None),	\
		PROCESS_OPCODE(ADC, M),	\
		PROCESS_OPCODE(ANC, M),	\
		PROCESS_OPCODE(AND, M),	\
		PROCESS_OPCODE(ANE, M),	\
		PROCESS_OPCODE(ARR, M),	\
		PROCESS_OPCODE(ASL, M),	\
		PROCESS_OPCODE(ASR, M),	\
		PROCESS_OPCODE(BCC, None),	\
		PROCESS_OPCODE(BCS, None),	\
		PROCESS_OPCODE(BEQ, None),	\
		PROCESS_OPCODE(BIT, M),	\
		PROCESS_OPCODE(BMI, None),	\
		PROCESS_OPCODE(BNE, None),	\
		PROCESS_OPCODE(BPL, None),	\
		PROCESS_OPCODE(BRK, None),	\
		PROCESS_OPCODE(BVC, None),	\
		PROCESS_OPCODE(BVS, None),	\
		PROCESS_OPCODE(CLC, None),	\
		PROCESS_OPCODE(CLD, None),	\
		PROCESS_OPCODE(CLI, None),	\
		PROCESS_OPCODE(CLV, None),	\
		PROCESS_OPCODE(CMP, M),	\
		PROCESS_OPCODE(CPX, X),	\
		PROCESS_OPCODE(CPY, X),	\
		PROCESS_OPCODE(DCP, M),	\
		PROCESS_OPCODE(DEC, M),	\
		PROCESS_OPCODE(DEX, X),	\
		PROCESS_OPCODE(DEY, X),	\
		PROCESS_OPCODE(EOR, M),	\
		PROCESS_OPCODE(INC, M),	\
		PROCESS_OPCODE(INX, X),	\
		PROCESS_OPCODE(INY, X),	\
		PROCESS_OPCODE(ISB, M),	\
		PROCESS_OPCODE(JMP, None),	\
		PROCESS_OPCODE(JSR, None),	\
		PROCESS_OPCODE(KIL, None),	\
		PROCESS_OPCODE(LAS, M),	\
		PROCESS_OPCODE(LAX, M),	\
		PROCESS_OPCODE(LDA, M),	\
		PROCESS_OPCODE(LDX, X),	\
		PROCESS_OPCODE(LDY, X),	\
		PROCESS_OPCODE(LSR, M),	\
		PROCESS_OPCODE(LXA, M),	\
		PROCESS_OPCODE(NOP, M),	\
		PROCESS_OPCODE(ORA, M),	\
		PROCESS_OPCODE(PHA, M),	\
		PROCESS_OPCODE(PHP, 8),	\
		PROCESS_OPCODE(PLA, M),	\
		PROCESS_OPCODE(PLP, 8),	\
		PROCESS_OPCODE(RLA, M),	\
		PROCESS_OPCODE(ROL, M),	\
		PROCESS_OPCODE(ROR, M),	\
		PROCESS_OPCODE(RRA, M),	\
		PROCESS_OPCODE(RTI, None),	\
		PROCESS_OPCODE(RTS, None),	\
		PROCESS_OPCODE(SAX, M),	\
		PROCESS_OPCODE(SBC, M),	\
		PROCESS_OPCODE(SBX, M),	\
		PROCESS_OPCODE(SEC, None),	\
		PROCESS_OPCODE(SED, None),	\
		PROCESS_OPCODE(SEI, None),	\
		PROCESS_OPCODE(SHA, M),	\
		PROCESS_OPCODE(SHS, M),	\
		PROCESS_OPCODE(SHX, X),	\
		PROCESS_OPCODE(SHY, X),	\
		PROCESS_OPCODE(SLO, M),	\
		PROCESS_OPCODE(SRE, M),	\
		PROCESS_OPCODE(STA, M),	\
		PROCESS_OPCODE(STX, X),	\
		PROCESS_OPCODE(STY, X),	\
		PROCESS_OPCODE(TAX, None),	\
		PROCESS_OPCODE(TAY, None),	\
		PROCESS_OPCODE(TSX, None),	\
		PROCESS_OPCODE(TXA, None),	\
		PROCESS_OPCODE(TXS, None),	\
		PROCESS_OPCODE(TYA, None),	\
								\
		/* 65C02 */				\
		PROCESS_OPCODE(BRA, None),	\
		PROCESS_OPCODE(TSB, M),	\
		PROCESS_OPCODE(RMB, M),	\
		PROCESS_OPCODE(BBR, None),	\
		PROCESS_OPCODE(TRB, M),	\
		PROCESS_OPCODE(STZ, M),	\
		PROCESS_OPCODE(SMB, M),	\
		PROCESS_OPCODE(BBS, None),	\
		PROCESS_OPCODE(WAI, None),	\
		PROCESS_OPCODE(STP, None),	\
		PROCESS_OPCODE(PLX, None),	\
		PROCESS_OPCODE(PLY, None),	\
		PROCESS_OPCODE(PHX, None),	\
		PROCESS_OPCODE(PHY, None),	\
								\
		/* 65C816 */			\
		PROCESS_OPCODE(BRL, None),	\
		PROCESS_OPCODE(COP, None),	\
		PROCESS_OPCODE(JML, None),	\
		PROCESS_OPCODE(JSL, None),	\
		PROCESS_OPCODE(MVN, None),	\
		PROCESS_OPCODE(MVP, None),	\
		PROCESS_OPCODE(PEA, None),	\
		PROCESS_OPCODE(PEI, 16),	\
		PROCESS_OPCODE(PER, None),	\
		PROCESS_OPCODE(PHB, 8),	\
		PROCESS_OPCODE(PHD, 16),	\
		PROCESS_OPCODE(PHK, 8),	\
		PROCESS_OPCODE(PLB, 8),	\
		PROCESS_OPCODE(PLD, 16),	\
		PROCESS_OPCODE(REP, None),	\
		PROCESS_OPCODE(RTL, None),	\
		PROCESS_OPCODE(SEP, None),	\
		PROCESS_OPCODE(TCD, None),	\
		PROCESS_OPCODE(TCS, None),	\
		PROCESS_OPCODE(TDC, None),	\
		PROCESS_OPCODE(TSC, None),	\
		PROCESS_OPCODE(TXY, None),	\
		PROCESS_OPCODE(TYX, None),	\
		PROCESS_OPCODE(XBA, None),	\
		PROCESS_OPCODE(XCE, None),	\
		PROCESS_OPCODE(WDM, None),	\

	enum {
#define PROCESS_OPCODE(name, mode) kOpcode##name
		PROCESS_OPCODES
#undef PROCESS_OPCODE
	};

	#define xx(op) { kModeInvalid, 0 }
	#define Ip(op) { kModeImplied, kOpcode##op }
	#define Re(op) { kModeRel, kOpcode##op }
	#define Rl(op) { kModeRel16, kOpcode##op }
	#define Im(op) { kModeImm, kOpcode##op }
	#define ImM(op) { kModeImmMode16, kOpcode##op }
	#define ImX(op) { kModeImmIndex16, kOpcode##op }
	#define I2(op) { kModeImm16, kOpcode##op }
	#define Iv(op) { kModeIVec, kOpcode##op }
	#define Zp(op) { kModeZp, kOpcode##op }
	#define Zx(op) { kModeZpX, kOpcode##op }
	#define Zy(op) { kModeZpY, kOpcode##op }
	#define Ab(op) { kModeAbs, kOpcode##op }
	#define Ax(op) { kModeAbsX, kOpcode##op }
	#define Ay(op) { kModeAbsY, kOpcode##op }
	#define Ia(op) { kModeIndA, kOpcode##op }
	#define Il(op) { kModeIndAL, kOpcode##op }
	#define Ix(op) { kModeIndX, kOpcode##op }
	#define Iy(op) { kModeIndY, kOpcode##op }
	#define Iz(op) { kModeInd, kOpcode##op }
	#define It(op) { kModeIndAX, kOpcode##op }
	#define Bz(op) { kModeBit, kOpcode##op }
	#define Bb(op) { kModeBBit, kOpcode##op }

	#define Lg(op) { kModeLong, kOpcode##op }
	#define Lx(op) { kModeLongX, kOpcode##op }
	#define Sr(op) { kModeStack, kOpcode##op }
	#define Sy(op) { kModeStackIndY, kOpcode##op }
	#define Xd(op) { kModeDpIndLong, kOpcode##op }
	#define Xy(op) { kModeDpIndLongY, kOpcode##op }
	#define Mv(op) { kModeMove, kOpcode##op }

	const uint8 kModeTbl_6502[256][2]={
		//			   0,       1,       2,       3,       4,       5,       6,       7,       8,       9,       A,       B,       C,       D,       E,       F
		/* 00 */	Ip(BRK), Ix(ORA), Ip(KIL), Ix(SLO), Zp(NOP), Zp(ORA), Zp(ASL), Zp(SLO), Ip(PHP), Im(ORA), Ip(ASL), Im(ANC), Ab(NOP), Ab(ORA), Ab(ASL), Ab(SLO), 
		/* 10 */	Re(BPL), Iy(ORA), Ip(KIL), Iy(SLO), Zx(NOP), Zx(ORA), Zx(ASL), Zx(SLO), Ip(CLC), Ay(ORA), Ip(NOP), Ay(SLO), Ax(NOP), Ax(ORA), Ax(ASL), Ax(SLO), 
		/* 20 */	Ab(JSR), Ix(AND), Ip(KIL), Ix(RLA), Zp(BIT), Zp(AND), Zp(ROL), Zp(RLA), Ip(PLP), Im(AND), Ip(ROL), Im(ANC), Ab(BIT), Ab(AND), Ab(ROL), Ab(RLA), 
		/* 30 */	Re(BMI), Iy(AND), Ip(KIL), Iy(RLA), Zx(NOP), Zx(AND), Zx(ROL), Zx(RLA), Ip(SEC), Ay(AND), Ip(NOP), Ay(RLA), Ax(NOP), Ax(AND), Ax(ROL), Ax(RLA), 
		/* 40 */	Ip(RTI), Ix(EOR), Ip(KIL), Ix(SRE), Zp(NOP), Zp(EOR), Zp(LSR), Zp(SRE), Ip(PHA), Im(EOR), Ip(LSR), Im(ASR), Ab(JMP), Ab(EOR), Ab(LSR), Ab(SRE), 
		/* 50 */	Re(BVC), Iy(EOR), Ip(KIL), Iy(SRE), Zx(NOP), Zx(EOR), Zx(LSR), Zx(SRE), Ip(CLI), Ay(EOR), Ip(NOP), Ay(SRE), Ax(NOP), Ax(EOR), Ax(LSR), Ax(SRE), 
		/* 60 */	Ip(RTS), Ix(ADC), Ip(KIL), Ix(RRA), Zp(NOP), Zp(ADC), Zp(ROR), Zp(RRA), Ip(PLA), Im(ADC), Ip(ROR), Im(ARR), Ia(JMP), Ab(ADC), Ab(ROR), Ab(RRA), 
		/* 70 */	Re(BVS), Iy(ADC), Ip(KIL), Iy(RRA), Zx(NOP), Zx(ADC), Zx(ROR), Zx(RRA), Ip(SEI), Ay(ADC), Ip(NOP), Ay(RRA), Ax(NOP), Ax(ADC), Ax(ROR), Ax(RRA), 
		/* 80 */	Im(NOP), Ix(STA), Im(NOP), Ix(SAX), Zp(STY), Zp(STA), Zp(STX), Zp(SAX), Ip(DEY), Im(NOP), Ip(TXA), Im(ANE), Ab(STY), Ab(STA), Ab(STX), Ab(SAX), 
		/* 90 */	Re(BCC), Iy(STA), Ip(KIL), Iy(SHA), Zx(STY), Zx(STA), Zy(STX), Zy(SAX), Ip(TYA), Ay(STA), Ip(TXS), Ay(SHS), Ax(SHY), Ax(STA), Ay(SHX), Ay(SHA), 
		/* A0 */	Im(LDY), Ix(LDA), Im(LDX), Ix(LAX), Zp(LDY), Zp(LDA), Zp(LDX), Zp(LAX), Ip(TAY), Im(LDA), Ip(TAX), Im(LXA), Ab(LDY), Ab(LDA), Ab(LDX), Ab(LAX), 
		/* B0 */	Re(BCS), Iy(LDA), Ip(KIL), Iy(LAX), Zx(LDY), Zx(LDA), Zy(LDX), Zy(LAX), Ip(CLV), Ay(LDA), Ip(TSX), Ab(LAS), Ax(LDY), Ax(LDA), Ay(LDX), Ay(LAX), 
		/* C0 */	Im(CPY), Ix(CMP), Im(NOP), Ix(DCP), Zp(CPY), Zp(CMP), Zp(DEC), Zp(DCP), Ip(INY), Im(CMP), Ip(DEX), Im(SBX), Ab(CPY), Ab(CMP), Ab(DEC), Ab(DCP), 
		/* D0 */	Re(BNE), Iy(CMP), Ip(KIL), Iy(DCP), Zx(NOP), Zx(CMP), Zx(DEC), Zx(DCP), Ip(CLD), Ay(CMP), Ip(NOP), Ay(DCP), Ax(NOP), Ax(CMP), Ax(DEC), Ax(DCP), 
		/* E0 */	Im(CPX), Ix(SBC), Im(NOP), Ix(ISB), Zp(CPX), Zp(SBC), Zp(INC), Zp(ISB), Ip(INX), Im(SBC), Ip(NOP), Im(SBC), Ab(CPX), Ab(SBC), Ab(INC), Ab(ISB), 
		/* F0 */	Re(BEQ), Iy(SBC), Ip(KIL), Iy(ISB), Zx(NOP), Zx(SBC), Zx(INC), Zx(ISB), Ip(SED), Ay(SBC), Ip(NOP), Ay(ISB), Ax(NOP), Ax(SBC), Ax(INC), Ax(ISB),
	};

	#undef xx
	#undef Ip
	#undef Re
	#undef Rl
	#undef Im
	#undef ImM
	#undef ImX
	#undef I2
	#undef Iv
	#undef Zp
	#undef Zx
	#undef Zy
	#undef Ab
	#undef Ax
	#undef Ay
	#undef Ia
	#undef Il
	#undef Ix
	#undef Iy
	#undef Iz
	#undef It
	#undef Bz
	#undef Bb

	#undef Lg
	#undef Lx
	#undef Sr
	#undef Sy
	#undef Xd
	#undef Xy
	#undef Mv
}

void RunCoProc6502Test(bool traceable, bool pageCrossing) {
	alignas(2) uint8 dummyRead[256] {};
	alignas(2) uint8 dummyWrite[256] {};

	vdblock<uint8> mem(4096);
	vdblock<uint8> rom(16384);
	memset(mem.data(), 0, 4096);
	memset(rom.data(), 0, 16384);

	vdautoptr<ATCoProc6502> cpu { new ATCoProc6502(false, traceable) };
	ATCoProcMemoryMapView mmapView(cpu->GetReadMap(), cpu->GetWriteMap(), cpu->GetTraceMap());

	mmapView.Clear(dummyRead, dummyWrite);
	mmapView.SetMemory(0, 0x10, mem.data());

	if (traceable)
		mmapView.SetReadMemTraceable(0xC0, 0x40, rom.data());
	else
		mmapView.SetReadMem(0xC0, 0x40, rom.data());

	uint8 *dst0 = rom.data();
	uint8 *dst = dst0;

	for(int op = 0; op < 256; ++op) {
		const auto mode = kModeTbl_6502[op][0];
		const auto insn = kModeTbl_6502[op][1];

		// skip any KIL opcodes
		if (insn == kOpcodeKIL)
			continue;

		uint16 pc = (uint16)(0xC000 + (dst - dst0));

		// check for special cases
		switch(op) {
			case 0x00:		// BRK
				*dst++ = 0x00;
				*dst++ = 0x00;
				rom[0xFFFE - 0xC000] = (uint8)(pc + 2);
				rom[0xFFFF - 0xC000] = (uint8)((pc + 2) >> 8);
				continue;

			case 0x20:		// JSR
			case 0x4C:		// JMP -> next insn
				*dst++ = (uint8)op;
				*dst++ = (uint8)(pc + 3);
				*dst++ = (uint8)((pc + 3) >> 8);
				continue;

			case 0x40:		// RTI -> LDA #imm / PHA / LDA #imm / PHA / PHP / RTI
				*dst++ = 0xA9;
				*dst++ = (uint8)((pc + 8) >> 8);
				*dst++ = 0x48;
				*dst++ = 0xA9;
				*dst++ = (uint8)(pc + 8);
				*dst++ = 0x48;
				*dst++ = 0x08;
				*dst++ = 0x40;
				continue;

			case 0x60:		// RTS -> JSR $FFA0 -> RTS
				*dst++ = 0x20;
				*dst++ = 0xA0;
				*dst++ = 0xFF;
				rom[0xFFA0 - 0xC000] = 0x60;
				continue;

			case 0x6C:		// JMP (abs) -> JMP ($FEFF)
				*dst++ = 0x6C;
				*dst++ = 0xFF;
				*dst++ = 0xFE;
				rom[0xFEFF - 0xC000] = (uint8)(pc + 3);
				rom[0xFE00 - 0xC000] = (uint8)((pc + 3) >> 8);
				continue;

			case 0x10:		// BPL
				*dst++ = 0xA9;
				*dst++ = 0x80;
				*dst++ = 0x10;
				*dst++ = 0x00;
				continue;

			case 0x30:		// BMI
				*dst++ = 0xA9;
				*dst++ = 0x00;
				*dst++ = 0x30;
				*dst++ = 0x00;
				continue;

			case 0x50:		// BVC
				*dst++ = 0xA9;
				*dst++ = 0x80;
				*dst++ = 0x69;
				*dst++ = 0x80;
				*dst++ = 0x50;
				*dst++ = 0x00;
				continue;

			case 0x70:		// BVS
				*dst++ = 0xB8;
				*dst++ = 0x70;
				*dst++ = 0x00;
				continue;

			case 0x90:		// BCC
				*dst++ = 0x38;
				*dst++ = 0x90;
				*dst++ = 0x00;
				continue;

			case 0xB0:		// BCS
				*dst++ = 0x18;
				*dst++ = 0xB0;
				*dst++ = 0x00;
				continue;

			case 0xD0:		// BNE
				*dst++ = 0xA9;
				*dst++ = 0x00;
				*dst++ = 0xD0;
				*dst++ = 0x00;
				continue;

			case 0xF0:		// BEQ
				*dst++ = 0xA9;
				*dst++ = 0x01;
				*dst++ = 0xF0;
				*dst++ = 0x00;
				continue;
		}
		
		uint8 b2 = 0x00;

		if (mode == kModeIndY) {
			*dst++ = 0xA0;
			*dst++ = pageCrossing ? 0xFF : 0x00;
			*dst++ = 0x84;
			*dst++ = 0x00;
		} else if (mode == kModeAbsX && pageCrossing) {
			*dst++ = 0xA2;
			*dst++ = 0xFF;
			b2 = 0xFF;
		} else if (mode == kModeAbsY && pageCrossing) {
			*dst++ = 0xA0;
			*dst++ = 0xFF;
			b2 = 0xFF;
		}

		*dst++ = (uint8)op;

		switch(kBPM_M8_X8[mode]) {
			case 2:
				*dst++ = b2;
				break;

			case 3:
				*dst++ = b2;
				*dst++ = 0;
				break;
		}
	}

	// now drop in a KIL
	*dst++ = 0x02;

	rom[0xFFFC - 0xC000] = 0x00;
	rom[0xFFFD - 0xC000] = 0xC0;

	vdblock<ATCPUHistoryEntry> history(131072);
	cpu->SetHistoryBuffer(history.data());

	cpu->ColdReset();
	cpu->SetBreakOnUnsupportedOpcode(true);

	uint32 hidx = cpu->GetHistoryCounter();

	// execute
	ATScheduler sch;
	sch.SetRate(VDFraction(1000000, 1));
	sch.SetStopTime(sch.GetTick() + 1000000);
	if (cpu->Run(sch)) {
		printf("CPU execution failed: PC=$%04X\n", cpu->GetPC());
		throw AssertionException("CPU did not stop at end of test.");
	}

	// iterate over the history and check cycle counts
	uint32 hlimit = cpu->GetHistoryCounter();

	for(uint32 op = 0; op < 256; ++op) {
		const auto mode = kModeTbl_6502[op][0];
		const auto insn = kModeTbl_6502[op][1];

		// skip any KIL opcodes
		if (insn == kOpcodeKIL)
			continue;

		uint32 insnExCount = 1;
		uint32 insnExCycles = 2;

		switch(insn) {
			case kOpcodeBRK:
				insnExCycles = 7;
				break;

			case kOpcodePHA:
			case kOpcodePHP:
				insnExCycles = 3;
				break;

			case kOpcodePLA:
			case kOpcodePLP:
				insnExCycles = 4;
				break;

			case kOpcodeRTI:
				insnExCount = 6;
				insnExCycles = 19;
				break;

			case kOpcodeRTS:
				insnExCount = 2;
				insnExCycles = 12;
				break;

			case kOpcodeJMP:
				if (op == 0x4C)
					insnExCycles = 3;
				else
					insnExCycles = 5;
				break;

			case kOpcodeBPL:
			case kOpcodeBMI:
			case kOpcodeBVS:
			case kOpcodeBCC:
			case kOpcodeBCS:
			case kOpcodeBNE:
			case kOpcodeBEQ:
				insnExCount = 2;
				insnExCycles = 4;
				break;

			case kOpcodeBVC:
				insnExCount = 3;
				insnExCycles = 6;
				break;

			case kOpcodeJSR:
				insnExCycles = 6;
				break;
		
			default:
				switch(mode) {
					case kModeImm:
					default:
						insnExCycles = 2;
						break;

					case kModeImplied:
						insnExCycles = 2;
						break;

					case kModeZp:
						insnExCycles = 3;
						break;

					case kModeZpX:
					case kModeZpY:
					case kModeAbs:
						insnExCycles = 4;
						break;

					case kModeAbsX:
					case kModeAbsY:
						switch(insn) {
							case kOpcodeSTA:
							case kOpcodeSTX:
							case kOpcodeSTY:
							case kOpcodeSHA:
							case kOpcodeSHS:
							case kOpcodeSHX:
							case kOpcodeSHY:
							case kOpcodeSLO:
							case kOpcodeSRE:
							case kOpcodeRLA:
							case kOpcodeRRA:
							case kOpcodeDCP:
							case kOpcodeISB:
							case kOpcodeROL:
							case kOpcodeROR:
							case kOpcodeASL:
							case kOpcodeLSR:
							case kOpcodeINC:
							case kOpcodeDEC:
								insnExCycles = 5;
								break;

							default:
								insnExCycles = pageCrossing ? 5 : 4;
								break;
						}

						if (pageCrossing) {
							insnExCount += 1;
							insnExCycles += 2;
						}

						break;

					case kModeInd:
						insnExCycles = 5;
						break;

					case kModeIndA:
						insnExCycles = 6;
						break;

					case kModeIndY:
						switch(insn) {
							case kOpcodeSTA:
							case kOpcodeSTX:
							case kOpcodeSTY:
							case kOpcodeSHA:
							case kOpcodeSHS:
							case kOpcodeSHX:
							case kOpcodeSHY:
							case kOpcodeSLO:
							case kOpcodeSRE:
							case kOpcodeRLA:
							case kOpcodeRRA:
							case kOpcodeDCP:
							case kOpcodeISB:
							case kOpcodeROL:
							case kOpcodeROR:
							case kOpcodeASL:
							case kOpcodeLSR:
							case kOpcodeINC:
							case kOpcodeDEC:
								insnExCycles = 6;
								break;

							default:
								insnExCycles = pageCrossing ? 6 : 5;
								break;
						}

						insnExCycles += 5;
						insnExCount += 2;
						break;

					case kModeIndX:
						insnExCycles = 6;
						break;
				}
				break;
		}

		// add RMW cycles
		switch(insn) {
			case kOpcodeSLO:
			case kOpcodeSRE:
			case kOpcodeRLA:
			case kOpcodeRRA:
			case kOpcodeDCP:
			case kOpcodeISB:
			case kOpcodeROL:
			case kOpcodeROR:
			case kOpcodeASL:
			case kOpcodeLSR:
			case kOpcodeINC:
			case kOpcodeDEC:
				switch(mode) {
					case kModeImplied:
						break;

					default:
						insnExCycles += 2;
						break;
				}
				break;
		}

		if (hlimit - hidx < insnExCount + 1) {
			printf("History truncated for opcode: %02X\n", op);
			throw AssertionException("History check failed");
		}

		if (insnExCount == 1) {
			const uint32 exop = history[hidx].mOpcode[0];

			if (exop != op) {
				printf("Executed opcode incorrect for opcode $%02X: got $%02X\n", op, exop);
				throw AssertionException("Executed opcode check failed");
			}
		}

		const uint32 cycles = history[hidx + insnExCount].mUnhaltedCycle - history[hidx].mUnhaltedCycle;
		if (cycles != insnExCycles) {
			printf("Cycle count incorrect for opcode $%02X: expected %u, got %u\n", op, insnExCycles, cycles);
			throw AssertionException("Cycle count check failed");
		}

		hidx += insnExCount;
	}

	// do taken branch tests
	rom[0xC000 - 0xC000] = 0x4C;			// C000: JMP $C100
	rom[0xC001 - 0xC000] = 0x00;
	rom[0xC002 - 0xC000] = 0xC1;

	rom[0xC100 - 0xC000] = 0xA9;			// C100: LDA #$30
	rom[0xC101 - 0xC000] = 0x30;
	rom[0xC102 - 0xC000] = 0x48;			// C102: PHA
	rom[0xC103 - 0xC000] = 0x28;			// C103: PLP
	rom[0xC104 - 0xC000] = 0x10;			// C104: BPL
	rom[0xC105 - 0xC000] = pageCrossing ? (uint8)(0xC0E0 - 0xC106) : 0x00;
	rom[0xC106 - 0xC000] = 0x4C;			// C106: JMP *+3
	rom[0xC107 - 0xC000] = 0x09;
	rom[0xC108 - 0xC000] = 0xC1;
	rom[0xC109 - 0xC000] = 0x50;			// C109: BVC
	rom[0xC10A - 0xC000] = pageCrossing ? (uint8)(0xC0E3 - 0xC10B) : 0x00;
	rom[0xC10B - 0xC000] = 0x4C;			// C10B: JMP *+3
	rom[0xC10C - 0xC000] = 0x0E;
	rom[0xC10D - 0xC000] = 0xC1;
	rom[0xC10E - 0xC000] = 0x90;			// C10E: BCC
	rom[0xC10F - 0xC000] = pageCrossing ? (uint8)(0xC0E6 - 0xC110) : 0x00;
	rom[0xC110 - 0xC000] = 0x4C;			// C110: JMP *+3
	rom[0xC111 - 0xC000] = 0x13;
	rom[0xC112 - 0xC000] = 0xC1;
	rom[0xC113 - 0xC000] = 0xD0;			// C113: BNE
	rom[0xC114 - 0xC000] = pageCrossing ? (uint8)(0xC0E9 - 0xC115) : 0x00;
	rom[0xC115 - 0xC000] = 0x4C;			// C115: JMP *+3
	rom[0xC116 - 0xC000] = 0x18;
	rom[0xC117 - 0xC000] = 0xC1;
	rom[0xC118 - 0xC000] = 0xA9;			// C118: LDA #$F3
	rom[0xC119 - 0xC000] = 0xF3;
	rom[0xC11A - 0xC000] = 0x48;			// C11A: PHA
	rom[0xC11B - 0xC000] = 0x28;			// C11B: PLP
	rom[0xC11C - 0xC000] = 0x30;			// C11C: BMI
	rom[0xC11D - 0xC000] = pageCrossing ? (uint8)(0xC0EC - 0xC11E) : 0x00;
	rom[0xC11E - 0xC000] = 0x4C;			// C11E: JMP *+3
	rom[0xC11F - 0xC000] = 0x21;
	rom[0xC120 - 0xC000] = 0xC1;
	rom[0xC121 - 0xC000] = 0x70;			// C121: BVS
	rom[0xC122 - 0xC000] = pageCrossing ? (uint8)(0xC0EF - 0xC123) : 0x00;
	rom[0xC123 - 0xC000] = 0x4C;			// C123: JMP *+3
	rom[0xC124 - 0xC000] = 0x26;
	rom[0xC125 - 0xC000] = 0xC1;
	rom[0xC126 - 0xC000] = 0xB0;			// C126: BCS
	rom[0xC127 - 0xC000] = pageCrossing ? (uint8)(0xC0F2 - 0xC128) : 0x00;
	rom[0xC128 - 0xC000] = 0x4C;			// C128: JMP *+3
	rom[0xC129 - 0xC000] = 0x2B;
	rom[0xC12A - 0xC000] = 0xC1;
	rom[0xC12B - 0xC000] = 0xF0;			// C12B: BEQ
	rom[0xC12C - 0xC000] = pageCrossing ? (uint8)(0xC0F5 - 0xC12D) : 0x00;
	rom[0xC12D - 0xC000] = 0x4C;			// C12D: JMP *+3
	rom[0xC12E - 0xC000] = 0x30;
	rom[0xC12F - 0xC000] = 0xC1;
	rom[0xC130 - 0xC000] = 0x02;			// KIL

	// add trampolines if we're doing branch crossing tests
	rom[0xC0E0 - 0xC000] = 0x4C;			// C0E0: JMP $C109
	rom[0xC0E1 - 0xC000] = 0x09;
	rom[0xC0E2 - 0xC000] = 0xC1;
	rom[0xC0E3 - 0xC000] = 0x4C;			// C0E3: JMP $C10E
	rom[0xC0E4 - 0xC000] = 0x0E;
	rom[0xC0E5 - 0xC000] = 0xC1;
	rom[0xC0E6 - 0xC000] = 0x4C;			// C0E6: JMP $C113
	rom[0xC0E7 - 0xC000] = 0x13;
	rom[0xC0E8 - 0xC000] = 0xC1;
	rom[0xC0E9 - 0xC000] = 0x4C;			// C0E9: JMP $C118
	rom[0xC0EA - 0xC000] = 0x18;
	rom[0xC0EB - 0xC000] = 0xC1;
	rom[0xC0EC - 0xC000] = 0x4C;			// C0EC: JMP $C121
	rom[0xC0ED - 0xC000] = 0x21;
	rom[0xC0EE - 0xC000] = 0xC1;
	rom[0xC0EF - 0xC000] = 0x4C;			// C0EF: JMP $C126
	rom[0xC0F0 - 0xC000] = 0x26;
	rom[0xC0F1 - 0xC000] = 0xC1;
	rom[0xC0F2 - 0xC000] = 0x4C;			// C0F2: JMP $C12B
	rom[0xC0F3 - 0xC000] = 0x2B;
	rom[0xC0F4 - 0xC000] = 0xC1;
	rom[0xC0F5 - 0xC000] = 0x4C;			// C0F5: JMP $C130
	rom[0xC0F6 - 0xC000] = 0x30;
	rom[0xC0F7 - 0xC000] = 0xC1;

	// flush the trace cache as we just modified traceable code
	cpu->InvalidateTraceCache();

	// reset the CPU and run the new test
	sch.SetStopTime(sch.GetTick() + 10000);
	cpu->ColdReset();
	if (cpu->Run(sch)) {
		printf("CPU execution failed: PC=$%04X\n", cpu->GetPC());
		throw AssertionException("CPU did not stop at end of taken branch test.");
	}

	hidx = hlimit;
	hlimit = cpu->GetHistoryCounter();

	// check branch instructions
	static constexpr uint8 kBranchInsnOpcodes[] = { 0x10, 0x50, 0x90, 0xD0, 0x30, 0x70, 0xB0, 0xF0 };
	static constexpr int kBranchInsnIndices[] = { 4, 6, 8, 10, 15, 17, 19, 21 };

	if (hlimit - hidx < 22)
		throw AssertionException("CPU did not execute enough instructions for taken branch test.");

	for(int i=0; i<8; ++i) {
		const auto& he = history[hidx + kBranchInsnIndices[i]];
		const uint32 cycles = history[hidx + kBranchInsnIndices[i] + 1].mUnhaltedCycle - he.mUnhaltedCycle;

		if (he.mOpcode[0] != kBranchInsnOpcodes[i]) {
			printf("Taken[%u]: saw opcode $%02X, expected $%02X\n", i, he.mOpcode[0], kBranchInsnOpcodes[i]);
			throw AssertionException("Taken branch opcode mismatch");
		}

		if (cycles != (pageCrossing ? 4 : 3)) {
			printf("Taken[%u]: expected %u cycles, got %u\n", i, pageCrossing ? 4 : 3, cycles);
			throw AssertionException("Taken branch cycle count mismatch");
		}
	}
}

DEFINE_TEST(CoProc_6502) {
	for(int i=0; i<4; ++i) {
		try {
			RunCoProc6502Test((i&1) != 0, (i&2) != 0);
		} catch(const AssertionException&) {
			printf("While testing with tracing %s and page crossings %s:\n", i&1 ? "on" : "off", i&2 ? "on": "off");
			throw;
		}
	}

	return 0;
}
