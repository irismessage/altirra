//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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

#include "stdafx.h"
#include "simulator.h"
#include "console.h"
#include "debugger.h"
#include "symbols.h"
#include "disasm.h"

extern ATSimulator g_sim;

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
		kModeBBit,			// bbr0 $04,rel-offset
		kModeLong,			// lda $010000
		kModeLongX,			// lda $010000,x
		kModeStack,			// lda 1,s
		kModeStackIndY,		// lda (1,s),y
		kModeDpIndLong,		// lda [$00]
		kModeDpIndLongY,	// lda [$00],y
		kModeMove,			// mvp #$01,#$02
	};

									//	inv	imp	rel	r16	imm	imM	imX	im6	zp	zpX	zpY	abs	abX	abY	(a)	[a]	(zX	(zY	(z)	(aX	z,r	al	alX	o,S	S)Y	[d]	[dY	#sd
	static const uint8 kBPM_M8_X8[]={	1,	1,	2,	3,	2,	2,	2,	3,	2,	2,	2,	3,	3,	3,	3,	3,	2,	2,	2,	3,	3,	4,	4,	2,	2,	2,	2,	3	};
	static const uint8 kBPM_M8_X16[]={	1,	1,	2,	3,	2,	2,	3,	3,	2,	2,	2,	3,	3,	3,	3,	3,	2,	2,	2,	3,	3,	4,	4,	2,	2,	2,	2,	3	};
	static const uint8 kBPM_M16_X8[]={	1,	1,	2,	3,	2,	3,	2,	3,	2,	2,	2,	3,	3,	3,	3,	3,	2,	2,	2,	3,	3,	4,	4,	2,	2,	2,	2,	3	};
	static const uint8 kBPM_M16_X16[]={	1,	1,	2,	3,	2,	3,	3,	3,	2,	2,	2,	3,	3,	3,	3,	3,	2,	2,	2,	3,	3,	4,	4,	2,	2,	2,	2,	3	};

	static const uint8 *const kBytesPerModeTables[]={
		kBPM_M8_X8,		// 6502
		kBPM_M8_X8,		// 65C02
		kBPM_M8_X8,		// 65C816 emulation
		kBPM_M16_X16,	// 65C816 native M=0 X=0
		kBPM_M16_X8,	// 65C816 native M=0 X=1
		kBPM_M8_X16,	// 65C816 native M=1 X=0
		kBPM_M8_X8,		// 65C816 native M=1 X=1
	};

	enum {
		kOpcodebad,
		kOpcodeADC,
		kOpcodeANC,
		kOpcodeAND,
		kOpcodeASL,
		kOpcodeASR,
		kOpcodeATX,
		kOpcodeBCC,
		kOpcodeBCS,
		kOpcodeBEQ,
		kOpcodeBIT,
		kOpcodeBMI,
		kOpcodeBNE,
		kOpcodeBPL,
		kOpcodeBRK,
		kOpcodeBVC,
		kOpcodeBVS,
		kOpcodeCLC,
		kOpcodeCLD,
		kOpcodeCLI,
		kOpcodeCLV,
		kOpcodeCMP,
		kOpcodeCPX,
		kOpcodeCPY,
		kOpcodeDCP,
		kOpcodeDEC,
		kOpcodeDEX,
		kOpcodeDEY,
		kOpcodeDOP,
		kOpcodeEOR,
		kOpcodeHLE,
		kOpcodeINC,
		kOpcodeINX,
		kOpcodeINY,
		kOpcodeISB,
		kOpcodeJMP,
		kOpcodeJSR,
		kOpcodeLAX,
		kOpcodeLDA,
		kOpcodeLDX,
		kOpcodeLDY,
		kOpcodeLSR,
		kOpcodeNOP,
		kOpcodeORA,
		kOpcodePHA,
		kOpcodePHP,
		kOpcodePLA,
		kOpcodePLP,
		kOpcodeRLA,
		kOpcodeROL,
		kOpcodeROR,
		kOpcodeRTI,
		kOpcodeRTS,
		kOpcodeSAX,
		kOpcodeSBC,
		kOpcodeSEC,
		kOpcodeSED,
		kOpcodeSEI,
		kOpcodeSHY,
		kOpcodeSLO,
		kOpcodeSRE,
		kOpcodeSTA,
		kOpcodeSTX,
		kOpcodeSTY,
		kOpcodeTAX,
		kOpcodeTAY,
		kOpcodeTSX,
		kOpcodeTXA,
		kOpcodeTXS,
		kOpcodeTYA,
		kOpcodeXAA,

		// 65C02
		kOpcodeBRA,
		kOpcodeTSB,
		kOpcodeRMB,
		kOpcodeBBR,
		kOpcodeTRB,
		kOpcodeSTZ,
		kOpcodeSMB,
		kOpcodeBBS,
		kOpcodeWAI,
		kOpcodeSTP,
		kOpcodePLX,
		kOpcodePLY,
		kOpcodePHX,
		kOpcodePHY,

		// 65C816
		kOpcodeBRL,
		kOpcodeCOP,
		kOpcodeJML,
		kOpcodeJSL,
		kOpcodeMVN,
		kOpcodeMVP,
		kOpcodePEA,
		kOpcodePEI,
		kOpcodePER,
		kOpcodePHB,
		kOpcodePHD,
		kOpcodePHK,
		kOpcodePLB,
		kOpcodePLD,
		kOpcodeREP,
		kOpcodeRTL,
		kOpcodeSEP,
		kOpcodeTCD,
		kOpcodeTCS,
		kOpcodeTDC,
		kOpcodeTSC,
		kOpcodeTXY,
		kOpcodeTYX,
		kOpcodeXBA,
		kOpcodeXCE,
	};

	static const char *kOpcodes[]={
		"bad",
		"ADC",
		"ANC",
		"AND",
		"ASL",
		"ASR",
		"ATX",
		"BCC",
		"BCS",
		"BEQ",
		"BIT",
		"BMI",
		"BNE",
		"BPL",
		"BRK",
		"BVC",
		"BVS",
		"CLC",
		"CLD",
		"CLI",
		"CLV",
		"CMP",
		"CPX",
		"CPY",
		"DCP",
		"DEC",
		"DEX",
		"DEY",
		"DOP",
		"EOR",
		"HLE",
		"INC",
		"INX",
		"INY",
		"ISB",
		"JMP",
		"JSR",
		"LAX",
		"LDA",
		"LDX",
		"LDY",
		"LSR",
		"NOP",
		"ORA",
		"PHA",
		"PHP",
		"PLA",
		"PLP",
		"RLA",
		"ROL",
		"ROR",
		"RTI",
		"RTS",
		"SAX",
		"SBC",
		"SEC",
		"SED",
		"SEI",
		"SHY",
		"SLO",
		"SRE",
		"STA",
		"STX",
		"STY",
		"TAX",
		"TAY",
		"TSX",
		"TXA",
		"TXS",
		"TYA",
		"XAA",

		"BRA",
		"TSB",
		"RMB",
		"BBR",
		"TRB",
		"STZ",
		"SMB",
		"BBS",
		"WAI",
		"STP",
		"PLX",
		"PLY",
		"PHX",
		"PHY",

		// 65C816
		"BRL",
		"COP",
		"JML",
		"JSL",
		"MVN",
		"MVP",
		"PEA",
		"PEI",
		"PER",
		"PHB",
		"PHD",
		"PHK",
		"PLB",
		"PLD",
		"REP",
		"RTL",
		"SEP",
		"TCD",
		"TCS",
		"TDC",
		"TSC",
		"TXY",
		"TYX",
		"XBA",
		"XCE",
	};

	#define xx(op) { kModeInvalid, 0 }
	#define Ip(op) { kModeImplied, kOpcode##op }
	#define Re(op) { kModeRel, kOpcode##op }
	#define Rl(op) { kModeRel16, kOpcode##op }
	#define Im(op) { kModeImm, kOpcode##op }
	#define ImM(op) { kModeImmMode16, kOpcode##op }
	#define ImX(op) { kModeImmIndex16, kOpcode##op }
	#define I2(op) { kModeImm16, kOpcode##op }
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
		/* 00 */	Im(BRK), Ix(ORA), xx(bad), Ix(SLO), Zp(NOP), Zp(ORA), Zp(ASL), Zp(SLO), Ip(PHP), Im(ORA), Ip(ASL), Im(ANC), Ab(NOP), Ab(ORA), Ab(ASL), Ab(SLO), 
		/* 10 */	Re(BPL), Iy(ORA), xx(bad), xx(bad), Zx(NOP), Zx(ORA), Zx(ASL), Zx(SLO), Ip(CLC), Ay(ORA), Ip(NOP), xx(bad), Ax(NOP), Ax(ORA), Ax(ASL), Ax(SLO), 
		/* 20 */	Ab(JSR), Ix(AND), xx(bad), xx(bad), Zp(BIT), Zp(AND), Zp(ROL), xx(bad), Ip(PLP), Im(AND), Ip(ROL), Im(ANC), Ab(BIT), Ab(AND), Ab(ROL), xx(bad), 
		/* 30 */	Re(BMI), Iy(AND), xx(bad), xx(bad), xx(bad), Zx(AND), Zx(ROL), Zx(RLA), Ip(SEC), Ay(AND), Ip(NOP), xx(bad), Ax(NOP), Ax(AND), Ax(ROL), xx(bad), 
		/* 40 */	Ip(RTI), Ix(EOR), I2(HLE), Ix(SRE), Zp(NOP), Zp(EOR), Zp(LSR), Zp(SRE), Ip(PHA), Im(EOR), Ip(LSR), Im(ASR), Ab(JMP), Ab(EOR), Ab(LSR), Ab(SRE), 
		/* 50 */	Re(BVC), Iy(EOR), xx(bad), Iy(SRE), Zx(NOP), Zx(EOR), Zx(LSR), Zx(SRE), Ip(CLI), Ay(EOR), Ip(NOP), Ay(SRE), Ax(NOP), Ax(EOR), Ax(LSR), Ax(SRE), 
		/* 60 */	Ip(RTS), Ix(ADC), xx(bad), xx(bad), xx(bad), Zp(ADC), Zp(ROR), xx(bad), Ip(PLA), Im(ADC), Ip(ROR), xx(bad), Ia(JMP), Ab(ADC), Ab(ROR), xx(bad), 
		/* 70 */	Re(BVS), Iy(ADC), xx(bad), xx(bad), xx(bad), Zx(ADC), Zx(ROR), xx(bad), Ip(SEI), Ay(ADC), Ip(NOP), xx(bad), Ax(NOP), Ax(ADC), Ax(ROR), xx(bad), 
		/* 80 */	Im(NOP), Ix(STA), xx(bad), xx(bad), Zp(STY), Zp(STA), Zp(STX), Zp(SAX), Ip(DEY), Im(STA), Ip(TXA), Im(XAA), Ab(STY), Ab(STA), Ab(STX), Ab(SAX), 
		/* 90 */	Re(BCC), Iy(STA), xx(bad), xx(bad), Zx(STY), Zx(STA), Zy(STX), xx(bad), Ip(TYA), Ay(STA), Ip(TXS), xx(bad), Ax(SHY), Ax(STA), xx(bad), xx(bad), 
		/* A0 */	Im(LDY), Ix(LDA), Im(LDX), Ix(LAX), Zp(LDY), Zp(LDA), Zp(LDX), Zp(LAX), Ip(TAY), Im(LDA), Ip(TAX), Im(ATX), Ab(LDY), Ab(LDA), Ab(LDX), Ab(LAX), 
		/* B0 */	Re(BCS), Iy(LDA), xx(bad), Iy(LAX), Zx(LDY), Zx(LDA), Zy(LDX), Zy(LAX), Ip(CLV), Ay(LDA), Ip(TSX), xx(bad), Ax(LDY), Ax(LDA), Ay(LDX), Ay(LAX), 
		/* C0 */	Im(CPY), Ix(CMP), Im(DOP), Ix(DCP), Zp(CPY), Zp(CMP), Zp(DEC), Zp(DCP), Ip(INY), Im(CMP), Ip(DEX), xx(bad), Ab(CPY), Ab(CMP), Ab(DEC), Ab(DCP), 
		/* D0 */	Re(BNE), Iy(CMP), xx(bad), Iy(DCP), xx(bad), Zx(CMP), Zx(DEC), Zx(DCP), Ip(CLD), Ay(CMP), Ip(NOP), Ay(DCP), Ax(NOP), Ax(CMP), Ax(DEC), Ax(DCP), 
		/* E0 */	Im(CPX), Ix(SBC), xx(bad), Ix(ISB), Zp(CPX), Zp(SBC), Zp(INC), Zp(ISB), Ip(INX), Im(SBC), Ip(NOP), xx(bad), Ab(CPX), Ab(SBC), Ab(INC), Ab(ISB), 
		/* F0 */	Re(BEQ), Iy(SBC), xx(bad), Iy(ISB), Zx(NOP), Zx(SBC), Zx(INC), Zx(ISB), Ip(SED), Ay(SBC), Ip(NOP), Ay(ISB), Ax(NOP), Ax(SBC), Ax(INC), Ax(ISB),
	};

	const uint8 kModeTbl_65C02[256][2]={
		//			   0,       1,       2,       3,       4,       5,       6,       7,       8,       9,       A,       B,       C,       D,       E,       F
		/* 00 */	Im(BRK), Ix(ORA), xx(bad), xx(bad), Zp(TSB), Zp(ORA), Zp(ASL), Zp(RMB), Ip(PHP), Im(ORA), Ip(ASL), xx(bad), Ab(TSB), Ab(ORA), Ab(ASL), Bb(BBR), 
		/* 10 */	Re(BPL), Iy(ORA), Iz(ORA), xx(bad), Zp(TRB), Zx(ORA), Zx(ASL), Zp(RMB), Ip(CLC), Ay(ORA), Ip(INC), xx(bad), Ab(TRB), Ax(ORA), Ax(ASL), Bb(BBR), 
		/* 20 */	Ab(JSR), Ix(AND), xx(bad), xx(bad), Zp(BIT), Zp(AND), Zp(ROL), Zp(RMB), Ip(PLP), Im(AND), Ip(ROL), xx(bad), Ab(BIT), Ab(AND), Ab(ROL), Bb(BBR), 
		/* 30 */	Re(BMI), Iy(AND), Iz(AND), xx(bad), Zx(BIT), Zx(AND), Zx(ROL), Zp(RMB), Ip(SEC), Ay(AND), Ip(DEC), xx(bad), Ax(NOP), Ax(AND), Ax(ROL), Bb(BBR), 
		/* 40 */	Ip(RTI), Ix(EOR), I2(HLE), xx(bad), Zp(NOP), Zp(EOR), Zp(LSR), Zp(RMB), Ip(PHA), Im(EOR), Ip(LSR), xx(bad), Ab(JMP), Ab(EOR), Ab(LSR), Bb(BBR), 
		/* 50 */	Re(BVC), Iy(EOR), Iz(EOR), xx(bad), Zx(NOP), Zx(EOR), Zx(LSR), Zp(RMB), Ip(CLI), Ay(EOR), Ip(PHY), xx(bad), Ax(NOP), Ax(EOR), Ax(LSR), Bb(BBR), 
		/* 60 */	Ip(RTS), Ix(ADC), xx(bad), xx(bad), Zp(STZ), Zp(ADC), Zp(ROR), Zp(RMB), Ip(PLA), Im(ADC), Ip(ROR), xx(bad), Ia(JMP), Ab(ADC), Ab(ROR), Bb(BBR), 
		/* 70 */	Re(BVS), Iy(ADC), Iz(ADC), xx(bad), Zx(STZ), Zx(ADC), Zx(ROR), Zp(RMB), Ip(SEI), Ay(ADC), Ip(PLY), xx(bad), It(JMP), Ax(ADC), Ax(ROR), Bb(BBR), 
		/* 80 */	Re(BRA), Ix(STA), xx(bad), xx(bad), Zp(STY), Zp(STA), Zp(STX), Zp(SMB), Ip(DEY), Im(BIT), Ip(TXA), xx(bad), Ab(STY), Ab(STA), Ab(STX), Bb(BBS), 
		/* 90 */	Re(BCC), Iy(STA), Iz(STA), xx(bad), Zx(STY), Zx(STA), Zy(STX), Zp(SMB), Ip(TYA), Ay(STA), Ip(TXS), xx(bad), Ab(STZ), Ax(STA), Ax(STZ), Bb(BBS), 
		/* A0 */	Im(LDY), Ix(LDA), Im(LDX), xx(bad), Zp(LDY), Zp(LDA), Zp(LDX), Zp(SMB), Ip(TAY), Im(LDA), Ip(TAX), xx(bad), Ab(LDY), Ab(LDA), Ab(LDX), Bb(BBS), 
		/* B0 */	Re(BCS), Iy(LDA), Iz(LDA), xx(bad), Zx(LDY), Zx(LDA), Zy(LDX), Zp(SMB), Ip(CLV), Ay(LDA), Ip(TSX), xx(bad), Ax(LDY), Ax(LDA), Ay(LDX), Bb(BBS), 
		/* C0 */	Im(CPY), Ix(CMP), xx(bad), xx(bad), Zp(CPY), Zp(CMP), Zp(DEC), Zp(SMB), Ip(INY), Im(CMP), Ip(DEX), Ip(WAI), Ab(CPY), Ab(CMP), Ab(DEC), Bb(BBS), 
		/* D0 */	Re(BNE), Iy(CMP), Iz(CMP), xx(bad), xx(bad), Zx(CMP), Zx(DEC), Zp(SMB), Ip(CLD), Ay(CMP), Ip(PHX), Ip(STP), Ax(NOP), Ax(CMP), Ax(DEC), Bb(BBS), 
		/* E0 */	Im(CPX), Ix(SBC), xx(bad), xx(bad), Zp(CPX), Zp(SBC), Zp(INC), Zp(SMB), Ip(INX), Im(SBC), Ip(NOP), xx(bad), Ab(CPX), Ab(SBC), Ab(INC), Bb(BBS), 
		/* F0 */	Re(BEQ), Iy(SBC), Iz(SBC), xx(bad), Zx(NOP), Zx(SBC), Zx(INC), Zp(SMB), Ip(SED), Ay(SBC), Ip(PLX), xx(bad), Ax(NOP), Ax(SBC), Ax(INC), Bb(BBS),
	};

	const uint8 kModeTbl_65C816[256][2]={
		//			   0,       1,       2,       3,       4,       5,       6,       7,       8,       9,       A,       B,       C,       D,       E,       F
		/* 00 */	Im(BRK), Ix(ORA), Im(COP), Sr(ORA), Zp(TSB), Zp(ORA), Zp(ASL), Xd(ORA), Ip(PHP),ImM(ORA), Ip(ASL), Ip(PHD), Ab(TSB), Ab(ORA), Ab(ASL), Lg(ORA), 
		/* 10 */	Re(BPL), Iy(ORA), Iz(ORA), Sy(ORA), Zp(TRB), Zx(ORA), Zx(ASL), Xy(ORA), Ip(CLC), Ay(ORA), Ip(INC), Ip(TCS), Ab(TRB), Ax(ORA), Ax(ASL), Lx(ORA), 
		/* 20 */	Ab(JSR), Ix(AND), Lg(JSL), Sr(AND), Zp(BIT), Zp(AND), Zp(ROL), Xd(AND), Ip(PLP),ImM(AND), Ip(ROL), Ip(PLD), Ab(BIT), Ab(AND), Ab(ROL), Lg(AND), 
		/* 30 */	Re(BMI), Iy(AND), Iz(AND), Sy(AND), Zx(BIT), Zx(AND), Zx(ROL), Xy(AND), Ip(SEC), Ay(AND), Ip(DEC), Ip(TSC), Ax(BIT), Ax(AND), Ax(ROL), Lx(AND), 
		/* 40 */	Ip(RTI), Ix(EOR), I2(HLE), Sr(EOR), Mv(MVP), Zp(EOR), Zp(LSR), Xd(EOR), Ip(PHA),ImM(EOR), Ip(LSR), Ip(PHK), Ab(JMP), Ab(EOR), Ab(LSR), Lg(EOR), 
		/* 50 */	Re(BVC), Iy(EOR), Iz(EOR), Sy(EOR), Mv(MVN), Zx(EOR), Zx(LSR), Xy(EOR), Ip(CLI), Ay(EOR), Ip(PHY), Ip(TCD), Lg(JMP), Ax(EOR), Ax(LSR), Lx(EOR), 
		/* 60 */	Ip(RTS), Ix(ADC), Rl(PER), Sr(ADC), Zp(STZ), Zp(ADC), Zp(ROR), Xd(ADC), Ip(PLA),ImM(ADC), Ip(ROR), Ip(RTL), Ia(JMP), Ab(ADC), Ab(ROR), Lg(ADC), 
		/* 70 */	Re(BVS), Iy(ADC), Iz(ADC), Sy(ADC), Zx(STZ), Zx(ADC), Zx(ROR), Xy(ADC), Ip(SEI), Ay(ADC), Ip(PLY), Ip(TDC), It(JMP), Ax(ADC), Ax(ROR), Lx(ADC), 
		/* 80 */	Re(BRA), Ix(STA), Rl(BRL), Sr(STA), Zp(STY), Zp(STA), Zp(STX), Xd(STA), Ip(DEY),ImM(BIT), Ip(TXA), Ip(PHB), Ab(STY), Ab(STA), Ab(STX), Lg(STA), 
		/* 90 */	Re(BCC), Iy(STA), Iz(STA), Sy(STA), Zx(STY), Zx(STA), Zy(STX), Xy(STA), Ip(TYA), Ay(STA), Ip(TXS), Ip(TXY), Ab(STZ), Ax(STA), Ax(STZ), Lx(STA), 
		/* A0 */   ImX(LDY), Ix(LDA),ImX(LDX), Sr(LDA), Zp(LDY), Zp(LDA), Zp(LDX), Xd(LDA), Ip(TAY),ImM(LDA), Ip(TAX), Ip(PLB), Ab(LDY), Ab(LDA), Ab(LDX), Lg(LDA), 
		/* B0 */	Re(BCS), Iy(LDA), Iz(LDA), Sy(LDA), Zx(LDY), Zx(LDA), Zy(LDX), Xy(LDA), Ip(CLV), Ay(LDA), Ip(TSX), Ip(TYX), Ax(LDY), Ax(LDA), Ay(LDX), Lx(LDA), 
		/* C0 */   ImX(CPY), Ix(CMP), Im(REP), Sr(CMP), Zp(CPY), Zp(CMP), Zp(DEC), Xd(CMP), Ip(INY),ImM(CMP), Ip(DEX), Ip(WAI), Ab(CPY), Ab(CMP), Ab(DEC), Lg(CMP), 
		/* D0 */	Re(BNE), Iy(CMP), Iz(CMP), Sy(CMP), Iz(PEI), Zx(CMP), Zx(DEC), Xy(CMP), Ip(CLD), Ay(CMP), Ip(PHX), Ip(STP), Il(JML), Ax(CMP), Ax(DEC), Lx(CMP), 
		/* E0 */   ImX(CPX), Ix(SBC), Im(SEP), Sr(SBC), Zp(CPX), Zp(SBC), Zp(INC), Xd(SBC), Ip(INX),ImM(SBC), Ip(NOP), Ip(XBA), Ab(CPX), Ab(SBC), Ab(INC), Lg(SBC), 
		/* F0 */	Re(BEQ), Iy(SBC), Iz(SBC), Sy(SBC), I2(PEA), Zx(SBC), Zx(INC), Xy(SBC), Ip(SED), Ay(SBC), Ip(PLX), Ip(XCE), It(JSR), Ax(SBC), Ax(INC), Lx(SBC),
	};

	const uint8 (*kModeTbl[8])[2]={
		kModeTbl_6502,
		kModeTbl_65C02,
		kModeTbl_65C816,
		kModeTbl_65C816,
		kModeTbl_65C816,
		kModeTbl_65C816,
		kModeTbl_65C816,
	};
}

const char *ATGetSymbolName(uint16 addr, bool write) {
	IATDebuggerSymbolLookup *symlookup = ATGetDebuggerSymbolLookup();

	ATSymbol sym;
	if (!symlookup->LookupSymbol(addr, write ? kATSymbol_Write : kATSymbol_Read | kATSymbol_Execute, sym))
		return NULL;

	return sym.mpName;
}

uint16 ATDisassembleInsn(char *buf, uint16 addr, bool decodeReferences) {
	VDStringA line;

	addr = ATDisassembleInsn(line, addr, decodeReferences);

	line += '\n';
	line += (char)0;
	line.copy(buf, VDStringA::npos);

	return addr;
}

uint16 ATDisassembleInsn(VDStringA& line, uint16 addr, bool decodeReferences) {
	uint8 opcode = g_sim.DebugReadByte(addr);
	uint8 byte1 = g_sim.DebugReadByte(addr+1);
	uint8 byte2 = g_sim.DebugReadByte(addr+2);
	uint8 byte3 = g_sim.DebugReadByte(addr+3);

	ATCPUEmulator& cpu = g_sim.GetCPU();
	ATCPUHistoryEntry hent;
	hent.mOpcode[0] = opcode;
	hent.mOpcode[1] = byte1;
	hent.mOpcode[2] = byte2;
	hent.mOpcode[3] = byte3;
	hent.mPC = addr;
	hent.mP = cpu.GetP();
	hent.mX = cpu.GetX();
	hent.mY = cpu.GetY();
	hent.mbEmulation = cpu.GetEmulationFlag();

	return ATDisassembleInsn(line, hent, decodeReferences);
}

uint16 ATDisassembleInsn(VDStringA& line, const ATCPUHistoryEntry& hent, bool decodeReferences) {
	const uint8 opcode = hent.mOpcode[0];
	const uint8 byte1 = hent.mOpcode[1];
	const uint8 byte2 = hent.mOpcode[2];
	const uint8 byte3 = hent.mOpcode[3];

	const ATCPUEmulator& cpu = g_sim.GetCPU();
	ATCPUSubMode subMode = cpu.GetCPUSubMode();
	const uint8 (*const tbl)[2] = kModeTbl[subMode];
	uint8 mode = tbl[opcode][0];
	uint8 opid = tbl[opcode][1];
	const char *opname = kOpcodes[opid];

	line.append_sprintf("%04X:", hent.mPC);

	int opsize = kBytesPerModeTables[subMode][mode];

	switch(opsize) {
		case 1:
			line.append_sprintf(" %02X      ", opcode);
			break;
		case 2:
			line.append_sprintf(" %02X %02X   ", opcode, byte1);
			break;
		case 3:
			line.append_sprintf(" %02X %02X %02X", opcode, byte1, byte2);
			break;
		case 4:
			line.append_sprintf(" %02X%02X%02X%02X", opcode, byte1, byte2, byte3);
			break;
	}

	const uint16 addr = hent.mPC;
	const char *label = ATGetSymbolName(addr, false);
	line.append_sprintf("  %-6s %s", label ? label : "", opname);

	if (mode == kModeImm) {
		line.append_sprintf(" #$%02X", byte1);
	} else if (mode == kModeImmMode16 || mode == kModeImmIndex16) {
		if (opsize == 3)
			line.append_sprintf(" #$%02X%02X", byte2, byte1);
		else 
			line.append_sprintf(" #$%02X", byte1);
	} else if (mode == kModeImm16) {
		line.append_sprintf(" #$%02X%02X", byte2, byte1);
	} else if (mode == kModeMove) {
		line.append_sprintf(" #$%02X,#$%02X", byte1, byte2);
	} else if (mode != kModeInvalid && mode != kModeImplied) {
		line += ' ';

		switch(mode) {
			case kModeIndA:
			case kModeIndX:
			case kModeIndY:
			case kModeInd:
			case kModeIndAX:
			case kModeStackIndY:
				line += '(';
				break;
			case kModeDpIndLong:
			case kModeDpIndLongY:
			case kModeIndAL:
				line += '[';
				break;
		}

		uint32 base;
		uint32 ea;
		uint32 ea2;
		bool addr16 = false;
		bool addr24 = false;
		bool ea16 = false;
		bool ea24 = false;
		bool dolabel = true;

		switch(mode) {
			case kModeRel:
				base = ea = addr + 2 + (sint8)byte1;
				addr16 = true;
				ea16 = true;
				break;

			case kModeRel16:
				base = ea = addr + 3 + (sint16)((uint32)byte1 + ((uint32)byte2 << 8));
				addr16 = true;
				ea16 = true;
				break;

			case kModeZp:
				base = ea = byte1;
				break;

			case kModeZpX:
				base = byte1;
				ea = (uint8)(byte1+cpu.GetX());
				break;

			case kModeZpY:
				base = byte1;
				ea = (uint8)(byte1+cpu.GetY());
				break;

			case kModeAbs:
				base = ea = byte1 + (byte2 << 8);
				addr16 = true;
				ea16 = true;
				break;

			case kModeAbsX:
				base = byte1 + (byte2 << 8);
				ea = base + cpu.GetX();
				addr16 = true;
				ea16 = true;
				break;

			case kModeAbsY:
				base = byte1 + (byte2 << 8);
				ea = base + cpu.GetY();
				addr16 = true;
				ea16 = true;
				break;

			case kModeIndA:
				base = byte1 + (byte2 << 8);
				ea = g_sim.DebugReadByte(base) + 256*g_sim.DebugReadByte(base+1);
				addr16 = true;
				ea16 = true;
				break;

			case kModeIndAL:
				base = byte1 + (byte2 << 8);
				ea = g_sim.DebugRead24(base);
				addr16 = true;
				ea16 = true;
				ea24 = true;
				break;

			case kModeIndX:
				base = byte1;
				ea = g_sim.DebugReadByte((uint8)(base + cpu.GetX())) + 256*g_sim.DebugReadByte((uint8)(base + cpu.GetX() + 1));
				ea16 = true;
				break;

			case kModeIndY:
				base = byte1;
				ea = g_sim.DebugReadByte(base) + 256*g_sim.DebugReadByte((base+1) & 0xff) + cpu.GetY();
				ea16 = true;
				break;

			case kModeInd:
				base = byte1;
				ea = g_sim.DebugReadByte(base) + 256*g_sim.DebugReadByte((base+1) & 0xff);
				ea16 = true;
				break;

			case kModeIndAX:
				base = byte1 + (byte2 << 8) + cpu.GetX();
				ea = g_sim.DebugReadByte(base) + 256*g_sim.DebugReadByte(base+1);
				addr16 = true;
				ea16 = true;
				break;

			case kModeBBit:
				base = ea = byte1;
				ea2 = addr + 3 + (sint8)byte1;
				break;

			case kModeLong:
				base = ea = (uint32)byte1 + ((uint32)byte2 << 8) + ((uint32)byte3 << 16);
				addr16 = true;
				addr24 = true;
				ea16 = true;
				ea24 = true;
				break;

			case kModeLongX:
				base = (uint32)byte1 + ((uint32)byte2 << 8) + ((uint32)byte3 << 16);
				ea = base + cpu.GetX();
				addr16 = true;
				addr24 = true;
				ea16 = true;
				ea24 = true;
				break;

			case kModeStack:
				dolabel = false;
				base = byte1;
				ea = base + cpu.GetS16();
				break;

			case kModeStackIndY:
				dolabel = false;
				base = byte1 + cpu.GetS16();
				ea = g_sim.DebugReadWord(base) + cpu.GetY();
				break;

			case kModeDpIndLong:
				base = byte1;
				ea = g_sim.DebugRead24(byte1) + cpu.GetY();
				break;

			case kModeDpIndLongY:
				base = byte1;
				ea = g_sim.DebugRead24(byte1) + cpu.GetY();
				break;
		}

		bool write = false;
		switch(opid) {
		case kOpcodeSTA:
		case kOpcodeSTX:
		case kOpcodeSTY:
		case kOpcodeSTZ:
			write = true;
			break;
		}

		const char *name = NULL;
		
		if (dolabel)
			name = ATGetSymbolName(base, write);

		if (name)
			line.append(name);
		else if (addr24)
			line.append_sprintf("$%06X", base & 0xffffff);
		else if (addr16)
			line.append_sprintf("$%04X", base & 0xffff);
		else
			line.append_sprintf("$%02X", base & 0xff);

		switch(mode) {
			case kModeZpX:
			case kModeAbsX:
				line.append(",X");
				break;

			case kModeZpY:
			case kModeAbsY:
				line.append(",Y");
				break;

			case kModeInd:
			case kModeIndA:
				line.append(")");
				break;

			case kModeIndX:
			case kModeIndAX:
				line.append(",X)");
				break;

			case kModeIndY:
				line.append("),Y");
				break;

			case kModeBBit:
				{
					const char *name2 = ATGetSymbolName(ea2, false);

					line.append(",");

					if (name2)
						line.append(name2);
					else
						line.append_sprintf("$%04X", ea2);
				}
				break;

			case kModeStack:
				line.append(",S");
				break;

			case kModeStackIndY:
				line.append(",S),Y");
				break;

			case kModeIndAL:
			case kModeDpIndLong:
				line.append("]");
				break;

			case kModeDpIndLongY:
				line.append("],Y");
				break;
		}

		if (decodeReferences && mode != kModeRel && mode != kModeRel16) {
			if (line.size() < 30)
				line.resize(30, ' ');

			if (ea16)
				line.append_sprintf(" [$%04X] = $%02X", ea, g_sim.DebugReadByte(ea));
			else
				line.append_sprintf(" [$%02X] = $%02X", ea, g_sim.DebugReadByte(ea));
		}
	}

	return addr + opsize;
}

uint16 ATDisassembleInsn(uint16 addr) {
	char buf[256];
	addr = ATDisassembleInsn(buf, addr, true);
	ATConsoleWrite(buf);
	return addr;
}

void ATDisassembleRange(FILE *f, uint16 addr1, uint16 addr2) {
	char buf[256];
	while(addr1 < addr2) {
		addr1 = ATDisassembleInsn(buf, addr1, true);
		fputs(buf, f);
	}
}

uint16 ATDisassembleGetFirstAnchor(uint16 addr, uint16 target) {
	ATCPUEmulator& cpu = g_sim.GetCPU();
	ATCPUSubMode subMode = cpu.GetCPUSubMode();
	const uint8 (*const tbl)[2] = kModeTbl[subMode];
	const uint8 *const modetbl = kBytesPerModeTables[subMode];

	vdfastvector<uint8> results;

	uint16 testbase = addr;
	for(int i=0; i<4; ++i) {
		uint16 ip = testbase;
		for(;;) {
			if (ip == target)
				return testbase;

			uint32 offset = (uint16)(ip - addr);
			if (offset < results.size() && results[offset])
				break;

			uint8 opcode = g_sim.DebugReadByte(ip);
			uint8 mode = tbl[opcode][0];

			uint8 oplen = modetbl[mode];
			if (mode == kModeInvalid || (uint16)(target - ip) < oplen) {
				if (offset >= results.size())
					results.resize(offset+1, false);
				results[offset] = true;
				break;
			}

			ip += oplen;
		}

		++testbase;
		if (testbase == target)
			break;
	}

	return testbase;
}

int ATGetOpcodeLength(uint8 opcode) {
	ATCPUEmulator& cpu = g_sim.GetCPU();
	ATCPUSubMode subMode = cpu.GetCPUSubMode();
	const uint8 (*const tbl)[2] = kModeTbl[subMode];

	return kBytesPerModeTables[subMode][tbl[opcode][0]];
}
