//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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
#include <vd2/system/binary.h>
#include <vd2/system/constexpr.h>
#include <vd2/system/file.h>
#include <at/atcpu/history.h>
#include <at/atdebugger/target.h>
#include "trace.h"
#include "cputracer.h"

vdrefptr<ATTraceCollection> ATLoadTraceFromAtari800(const wchar_t *file) {
	VDTextInputFile textInput(file);

	vdrefptr<ATTraceCollection> traceColl(new ATTraceCollection);
	vdautoptr<ATCPUTraceProcessor> proc(new ATCPUTraceProcessor);

	// Atari800WinPLus format:
	//
	//           1         2         3         4         5         6         7
	// 01234567890123456789012345678901234567890123456789012345678901234567890123456789
	// 261 101 09B5 LDA $D20E ;IRQST      ; 4cyc ; A=ff S=e5 X=02 Y=00 P=NV*B-I--
	//
	// Atari800 5.x format:
	//
	//           1         2         3         4         5         6         7
	// 01234567890123456789012345678901234567890123456789012345678901234567890123456789
	//   0  52 A=F7 X=8B Y=FC S=FF P=N-*--I-- PC=C2AE: D0 FD     BNE $C2AD
	//
	// We ignore any lines that don't conform to this format, as it's going to include
	// various other commands and command outputs.
	//

	enum class OperandModes : uint8 {
		None,		// no operand
		Imm8,		// #$xx
		Addr8,		// $xx
		Addr8X,		// $xx,X
		Addr8Y,		// $xx,Y
		Addr16,		// $xxxx
		Addr16X,	// $xxxx,X
		Addr16Y,	// $xxxx,Y
		Ind16,		// ($xxxx)
		Ind8X,		// ($xx,X)
		Ind8Y,		// ($xx),Y
	};

	#define Encode(mode, op) (((uint32)mode << 24) + ((uint32)(#op)[2] << 16) + ((uint32)(#op)[1] << 8) + ((uint32)(#op)[0] << 0))
	#define Ip(op) Encode(OperandModes::None, op)
	#define Re(op) Encode(OperandModes::Addr16, op)
	#define Im(op) Encode(OperandModes::Imm8, op)
	#define Zp(op) Encode(OperandModes::Addr8, op)
	#define Zx(op) Encode(OperandModes::Addr8X, op)
	#define Zy(op) Encode(OperandModes::Addr8Y, op)
	#define Ab(op) Encode(OperandModes::Addr16, op)
	#define Ax(op) Encode(OperandModes::Addr16X, op)
	#define Ay(op) Encode(OperandModes::Addr16Y, op)
	#define Ia(op) Encode(OperandModes::Ind16, op)
	#define Ix(op) Encode(OperandModes::Ind8X, op)
	#define Iy(op) Encode(OperandModes::Ind8Y, op)

	static constexpr uint32 kOpcodeTable[256]={
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

	#undef Encode
	#undef Ip
	#undef Re
	#undef Im
	#undef Zp
	#undef Zx
	#undef Zy
	#undef Ab
	#undef Ax
	#undef Ay
	#undef Ia
	#undef Ix
	#undef Iy

	static constexpr uint32 kInsnHashTableSize = 2669;

	static constexpr auto kInsnHashTable = [] {
		VDCxArray<uint8, kInsnHashTableSize> ht {};
		bool used[kInsnHashTableSize] {};

		for(int i = 0; i < 256; ++i) {
			const uint32 opdat = kOpcodeTable[i];
			uint32 htidx = opdat % kInsnHashTableSize;

			// compile-time check that we can do a perfect hash
			if (used[htidx] && kOpcodeTable[ht.v[htidx]] != opdat) {
				used[-i-1000] = false;	// deliberate UB - force constexpr eval error with index in message
				throw;
			}

			used[htidx] = true;
			ht.v[htidx] = i;
		}

		return ht;
	}();

	static constexpr uint8 kFlagDigit = 0x01;
	static constexpr uint8 kFlagUCaseLetter = 0x02;
	static constexpr uint8 kFlagDigitOrSpace = 0x04;
	static constexpr uint8 kFlagSpace = 0x08;
	static constexpr uint8 kFlagXDigit = 0x10;
	static constexpr uint8 kFlagXDigitOrSpace = 0x20;
	static constexpr uint8 kFlagPrintable = 0x40;
	static constexpr uint8 kFlagMatchExact = 0x80;

	// home grown ctype, tailored and to avoid locale
	static constexpr auto kClassifier =
		[]() -> VDCxArray<uint8, 256> {
			VDCxArray<uint8, 256> ctab {};

			for(int i = 0x20; i <= 0x7E; ++i)
				ctab.v[i] = kFlagPrintable;

			ctab.v[0x20] |= kFlagSpace | kFlagDigitOrSpace | kFlagXDigitOrSpace;

			for(int i = 0x30; i <= 0x39; ++i)
				ctab.v[i] |= kFlagDigit | kFlagDigitOrSpace | kFlagXDigit | kFlagXDigitOrSpace;

			for(int i = 0x41; i <= 0x5A; ++i)
				ctab.v[i] |= kFlagUCaseLetter;

			for(int i = 0x41; i <= 0x46; ++i)
				ctab.v[i] |= kFlagXDigit | kFlagXDigitOrSpace;

			for(int i = 0x61; i <= 0x66; ++i)
				ctab.v[i] |= kFlagXDigit | kFlagXDigitOrSpace;

			return ctab;
		}();

	static constexpr auto translatePattern =
		[]<size_t N>(const char (&s)[N]) consteval -> VDCxArray<uint8, N-1> {
			VDCxArray<uint8, N-1> pat;

			for(uint32 i = 0; i < N-1; ++i) {
				const char c = s[i];
				uint8 flags = 0;

				switch(c) {
					case 'd':
						flags = kFlagDigitOrSpace;
						break;

					case 'D':
						flags = kFlagDigit;
						break;

					case 'h':
						flags = kFlagXDigitOrSpace;
						break
							;
					case 'H':
						flags = kFlagXDigit;
						break;

					case 'U':
						flags = kFlagUCaseLetter;
						break;

					case '?':
						flags = kFlagPrintable;
						break;

					default:
						flags = kFlagMatchExact | (uint8)c;
						break;
				}

				pat.v[i] = flags;
			}

			return pat;
		};


	static constexpr char kTemplateA8WP[] =
	//	 261 101 09B5 LDA $D20E ;IRQST      ; 4cyc ; A=ff S=e5 X=02 Y=00 P=NV*B-I--
		"ddD ddD HHHH UUU ??????????????????; ?cyc ; A=HH S=HH X=HH Y=HH P=??*?????";

	static constexpr char kTemplateA85[] =
	//     0  52 A=F7 X=8B Y=FC S=FF P=N-*--I-- PC=C2AE: D0 FD     BNE $C2AD
		"ddD ddD A=HH X=HH Y=HH S=HH P=??*????? PC=HHHH: HH hh hh";

	static constexpr auto kPatternA8WP = translatePattern(kTemplateA8WP);
	static constexpr auto kPatternA85 = translatePattern(kTemplateA85);

	static constexpr uint8 kHexLookup[32] {
		 0,10,11,12,13,14,15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,
	};

	// We need to parse instructions until we see the beam wrap around to be
	// able to determine whether PAL or NTSC is being used. This takes up to
	// 17.8K instructions.
	static constexpr size_t kInsnsPerBlock = 20480;
	vdblock<ATCPUHistoryEntry> hebuf(kInsnsPerBlock);
	vdblock<const ATCPUHistoryEntry *> heptrs(256);
	uint32 numInsnsBuffered = 0;
	uint32 highVPos = 0;
	uint32 lastVPos = 0;
	uint64 baseCycle = 0;
	bool firstInsnBatch = true;
	ATCPUTimestampDecoder tsdecoder;
	uint32 framePeriod = 0;

	ATTraceContext ctx {};
	ctx.mBaseTime = 0;
	ctx.mBaseTickScale = 4.0f / 7159090.0f;		// will be fixed up from autodetection
	ctx.mpCollection = traceColl;

	struct SIOCommandEvent {
		uint64 mStartTick;
		uint64 mEndTick;
	};

	vdfastvector<SIOCommandEvent> sioCommandEvents;
	uint64 sioCommandStartTime = 0;
	bool sioCommandAsserted = false;

	struct SIODataEvent {
		uint64 mStartTick;
		uint64 mEndTick;
		uint8 mData;
	};

	// We can't easily determine the serial baud rate and don't have actual
	// transmit timing info, so currently we just assume it is 19200 baud and
	// the byte is sent either immediately or after the end of the last byte,
	// whichever is later.
	static const uint32 kStandardBytePeriod = 932;

	vdfastvector<SIODataEvent> sioSendEvents;
	vdfastvector<SIODataEvent> sioReceiveEvents;

	uint64 sioLastSendTick = 0;
	uint64 sioLastReceiveTick = 0;

	const auto flushInsns = [&] {
		if (!numInsnsBuffered)
			return;

		if (firstInsnBatch) {
			firstInsnBatch = false;

			tsdecoder.mFrameCountBase = 0;
			tsdecoder.mFrameTimestampBase = 0;
			tsdecoder.mCyclesPerFrame = highVPos > 288 ? 312*114 : 262*114;

			framePeriod = tsdecoder.mCyclesPerFrame;

			if (highVPos > 288)
				ctx.mBaseTickScale = 1.0f / 1773447.0f;

			proc->Init(hebuf[0].mS, kATDebugDisasmMode_6502, 1, &ctx, true, false, nullptr, tsdecoder, true);
		}

		const uint64 endTick64 = baseCycle - (uint32)baseCycle + hebuf[numInsnsBuffered - 1].mCycle;

		// the tracer processes pairs of instructions, so we must always stop one short and carry
		// the last insn over
		uint32 insnsToProcess = numInsnsBuffered - 1;
		const ATCPUHistoryEntry *src = hebuf.data();

		heptrs[0] = src++;
		while(insnsToProcess) {
			uint32 blockSize = std::min<uint32>(255, insnsToProcess);

			for(uint32 i=1; i<=blockSize; ++i)
				heptrs[i] = src++;

			proc->ProcessInsns(endTick64, heptrs.data(), blockSize, tsdecoder);

			heptrs[0] = heptrs[blockSize];
			insnsToProcess -= blockSize;
		}

		hebuf[0] = hebuf[numInsnsBuffered - 1];
		numInsnsBuffered = 1;
	};

	int lastS = -1;
	uint8 lastOpcode = 0;

	enum class PendingValueType : uint8 {
		None,
		SerIn,
	};

	enum class PendingValueLocation : uint8 {
		A,
		X,
		Y
	};

	struct PendingValue {
		PendingValueType mType;
		PendingValueLocation mLocation;
	};

	PendingValue pendingValues[256] {};

	while(const char *line = textInput.GetNextLine()) {
		const size_t lineLen = strlen(line);

		// check whether the line is A8WP or A85 format
		static constexpr size_t kMinLineLen = std::min(kPatternA8WP.size(), kPatternA85.size());
		if (lineLen < kMinLineLen)
			continue;

		bool useA85 = false;
		if (line[9] == '=') {
			if (lineLen < kPatternA85.size())
				continue;

			useA85 = true;
		} else {
			if (lineLen < kPatternA8WP.size())
				continue;
		}

		// validate line
		const uint8 *const pattern = useA85 ? kPatternA85.data() : kPatternA8WP.data();
		const size_t patternLen = useA85 ? kPatternA85.size() : kPatternA8WP.size();
		bool valid = true;

		for(size_t i=0; i<patternLen; ++i) {
			const uint8 flags = pattern[i];
			const uint8 c = (uint8)line[i];

			if (flags & kFlagMatchExact) {
				if (c != flags - kFlagMatchExact) {
					valid = false;
					break;
				}
			} else {
				if (!(kClassifier.v[c] & flags)) {
					valid = false;
					break;
				}
			}
		}

		if (!valid)
			continue;

		// parse out fields
		uint32 vpos;
		uint32 hpos;
		uint32 pc;
		uint8 ra;
		uint8 rs;
		uint8 rx;
		uint8 ry;
		uint8 rp;

		if (useA85) {
			vpos= ((uint32)(uint8)line[0] & 0x0f) * 100
				+ ((uint32)(uint8)line[1] & 0x0f) * 10
				+ ((uint32)(uint8)line[2] & 0x0f);

			hpos= ((uint32)(uint8)line[4] & 0x0f) * 100
				+ ((uint32)(uint8)line[5] & 0x0f) * 10
				+ ((uint32)(uint8)line[6] & 0x0f);

			ra	= ((uint32)kHexLookup[line[10] & 0x1F] << 4)
				+ ((uint32)kHexLookup[line[11] & 0x1F] << 0);

			rx	= ((uint32)kHexLookup[line[15] & 0x1F] << 4)
				+ ((uint32)kHexLookup[line[16] & 0x1F] << 0);

			ry	= ((uint32)kHexLookup[line[20] & 0x1F] << 4)
				+ ((uint32)kHexLookup[line[21] & 0x1F] << 0);

			rs	= ((uint32)kHexLookup[line[25] & 0x1F] << 4)
				+ ((uint32)kHexLookup[line[26] & 0x1F] << 0);

			rp	= (line[30] == '-' ? 0 : 0x80)	// N
				+ (line[31] == '-' ? 0 : 0x40)	// V
				+ 0x30							// *B
				+ (line[34] == '-' ? 0 : 0x08)	// D
				+ (line[35] == '-' ? 0 : 0x04)	// I
				+ (line[36] == '-' ? 0 : 0x02)	// Z
				+ (line[37] == '-' ? 0 : 0x01)	// C
				;

			pc	= ((uint32)kHexLookup[line[42] & 0x1F] << 12)
				+ ((uint32)kHexLookup[line[43] & 0x1F] <<  8)
				+ ((uint32)kHexLookup[line[44] & 0x1F] <<  4)
				+ ((uint32)kHexLookup[line[45] & 0x1F] <<  0);
		} else {
			vpos= ((uint32)(uint8)line[0] & 0x0f) * 100
				+ ((uint32)(uint8)line[1] & 0x0f) * 10
				+ ((uint32)(uint8)line[2] & 0x0f);

			hpos= ((uint32)(uint8)line[4] & 0x0f) * 100
				+ ((uint32)(uint8)line[5] & 0x0f) * 10
				+ ((uint32)(uint8)line[6] & 0x0f);

			pc	= ((uint32)kHexLookup[line[ 8] & 0x1F] << 12)
				+ ((uint32)kHexLookup[line[ 9] & 0x1F] <<  8)
				+ ((uint32)kHexLookup[line[10] & 0x1F] <<  4)
				+ ((uint32)kHexLookup[line[11] & 0x1F] <<  0);

			ra	= ((uint32)kHexLookup[line[46] & 0x1F] << 4)
				+ ((uint32)kHexLookup[line[47] & 0x1F] << 0);

			rs	= ((uint32)kHexLookup[line[51] & 0x1F] << 4)
				+ ((uint32)kHexLookup[line[52] & 0x1F] << 0);

			rx	= ((uint32)kHexLookup[line[56] & 0x1F] << 4)
				+ ((uint32)kHexLookup[line[57] & 0x1F] << 0);

			ry	= ((uint32)kHexLookup[line[61] & 0x1F] << 4)
				+ ((uint32)kHexLookup[line[62] & 0x1F] << 0);

			rp	= (line[66] == '-' ? 0 : 0x80)	// N
				+ (line[67] == '-' ? 0 : 0x40)	// V
				+ 0x30							// *B
				+ (line[70] == '-' ? 0 : 0x08)	// D
				+ (line[71] == '-' ? 0 : 0x04)	// I
				+ (line[72] == '-' ? 0 : 0x02)	// Z
				+ (line[73] == '-' ? 0 : 0x01)	// C
				;
		}

		if (vpos < lastVPos) {
			highVPos = lastVPos;
			baseCycle += lastVPos > 288 ? 312*114 : 262*114;
		}

		lastVPos = vpos;

		const uint64 cycle = baseCycle + hpos + vpos * 114;

		ATCPUHistoryEntry& he = hebuf[numInsnsBuffered];
		he = {};
		he.mCycle = (uint32)cycle;
		he.mUnhaltedCycle = he.mCycle;
		he.mA = ra;
		he.mS = rs;
		he.mX = rx;
		he.mY = ry;
		he.mP = rp;
		he.mPC = pc;
		he.mEA = UINT32_C(0xFFFFFFFF);

		if (useA85) {
			// A8 5.x -- opcode bytes are provided directly
			he.mOpcode[0] = ((uint32)kHexLookup[line[48] & 0x1F] << 4)
				+ ((uint32)kHexLookup[line[49] & 0x1F] << 0);

			he.mOpcode[1] = ((uint32)kHexLookup[line[51] & 0x1F] << 4)
				+ ((uint32)kHexLookup[line[52] & 0x1F] << 0);

			he.mOpcode[2] = ((uint32)kHexLookup[line[54] & 0x1F] << 4)
				+ ((uint32)kHexLookup[line[55] & 0x1F] << 0);
		} else {
			// A8WP - try to parse the instruction as we need to reassemble it
			uint32 insnEncoding = VDReadUnalignedLEU32(&line[13]) & 0xFFFFFF;
			uint8 op1 = 0;
			uint8 op2 = 0;

			if (line[17] == ' ')
				insnEncoding += (uint32)OperandModes::None << 24;
			else if (line[17] == '#') {
				insnEncoding += (uint32)OperandModes::Imm8 << 24;

				op1 = ((uint32)kHexLookup[line[19] & 0x1F] << 4)
					+ ((uint32)kHexLookup[line[20] & 0x1F] << 0);
			} else if (line[17] == '(') {
				op1 = ((uint32)kHexLookup[line[19] & 0x1F] << 4)
					+ ((uint32)kHexLookup[line[20] & 0x1F] << 0);

				if (line[21] == ')') {
					insnEncoding += (uint32)OperandModes::Ind8Y << 24;
				} else if (line[21] == ',') {
					insnEncoding += (uint32)OperandModes::Ind8X << 24;
				} else {
					insnEncoding += (uint32)OperandModes::Ind16 << 24;

					op2 = op1;
					op1 = ((uint32)kHexLookup[line[21] & 0x1F] << 4)
						+ ((uint32)kHexLookup[line[22] & 0x1F] << 0);
				}
			} else if (line[17] == '$') {
				op1 = ((uint32)kHexLookup[line[18] & 0x1F] << 4)
					+ ((uint32)kHexLookup[line[19] & 0x1F] << 0);

				if (line[20] == ' ')
					insnEncoding += (uint32)OperandModes::Addr8 << 24;
				else if (line[20] == ',') {
					if (line[21] == 'X')
						insnEncoding += (uint32)OperandModes::Addr8X << 24;
					else if (line[21] == 'Y')
						insnEncoding += (uint32)OperandModes::Addr8Y << 24;
				} else if (line[22] == ' ') {
					op2 = op1;
					op1 = ((uint32)kHexLookup[line[20] & 0x1F] << 4)
						+ ((uint32)kHexLookup[line[21] & 0x1F] << 0);

					insnEncoding += (uint32)OperandModes::Addr16 << 24;
				} else if (line[22] == ',') {
					op2 = op1;
					op1 = ((uint32)kHexLookup[line[20] & 0x1F] << 4)
						+ ((uint32)kHexLookup[line[21] & 0x1F] << 0);

					if (line[23] == 'X')
						insnEncoding += (uint32)OperandModes::Addr16X << 24;
					else if (line[23] == 'Y')
						insnEncoding += (uint32)OperandModes::Addr16Y << 24;
				}
			}

			uint8 opcode = kInsnHashTable.v[insnEncoding % kInsnHashTableSize];

			if (kOpcodeTable[opcode] == insnEncoding) {
				he.mOpcode[0] = opcode;

				// special handling for branch instructions
				if ((opcode & 0x1F) == 0x10) {
					he.mOpcode[1] = (uint8)(op1 - (pc + 2));
				} else {
					he.mOpcode[1] = op1;
					he.mOpcode[2] = op2;
				}
			}
		}

		// check if the last instruction was not TXS or BRK and the stack
		// pointer dropped by 3 -- this will be an interrupt
		if (lastOpcode && lastOpcode != 0x9A && lastS >= 0 && ((lastS - he.mS) & 0xFF) == 0x03) {
			// set the interrupt flag
			// if the instruction is a BIT NMIST, assume it is a VBI or DLI
			if (he.mOpcode[0] == 0x2C && he.mOpcode[1] == 0x0F && he.mOpcode[2] == 0xD4)
				he.mbNMI = true;
			else
				he.mbIRQ = true;
		}

		lastOpcode = he.mOpcode[0];
		lastS = he.mS;

		PendingValue& pv = pendingValues[he.mS];

		if (pv.mType != PendingValueType::None) {
			uint8 readVal;

			switch(pv.mLocation) {
				case PendingValueLocation::A:
				default:
					readVal = he.mA;
					break;

				case PendingValueLocation::X:
					readVal = he.mX;
					break;

				case PendingValueLocation::Y:
					readVal = he.mY;
					break;
			}

			if (pv.mType == PendingValueType::SerIn) {
				// SIO receive
				const uint64 receiveStartTick = std::max<uint64>(std::max<uint64>(cycle, kStandardBytePeriod * 2) - kStandardBytePeriod * 2, sioLastReceiveTick);
				sioLastReceiveTick = std::min<uint64>(cycle, receiveStartTick + kStandardBytePeriod);

				if (receiveStartTick < sioLastReceiveTick)
					sioReceiveEvents.emplace_back(receiveStartTick, sioLastReceiveTick, readVal);
			}
		}

		pv.mType = PendingValueType::None;

		// check for interesting hardware accesses
		if (he.mOpcode[0] >= 0x8C && he.mOpcode[0] <= 0x8E) {		// STY/STA/STX
			// get byte that was written
			const uint8 writeVal = he.mOpcode[0] == 0x8D ? he.mA : he.mOpcode[0] == 0x8E ? he.mX : he.mY;

			// check for writes to PBCTL that change CB2 (SIO command)
			if ((he.mOpcode[1] & 0x03) == 3 && he.mOpcode[2] == 0xD3) {

				// the typical output modes are 110 or 111; anything other than
				// 110 is going to either passively or actively pull up
				const bool asserted = (writeVal & 0x38) == 0x30;
				if (sioCommandAsserted != asserted) {
					sioCommandAsserted = asserted;

					if (asserted)
						sioCommandStartTime = cycle;
					else
						sioCommandEvents.emplace_back(sioCommandStartTime, cycle);
				}
			} else if (he.mOpcode[1] == 0x0D && he.mOpcode[2] == 0xD2) {
				// SEROUT -- queue a send

				uint64 byteStart = std::max<uint64>(cycle, sioLastSendTick);
				sioLastSendTick = byteStart + kStandardBytePeriod;

				sioSendEvents.emplace_back(byteStart, sioLastSendTick, writeVal);
			}
		} else if (he.mOpcode[0] >= 0xAC && he.mOpcode[0] <= 0xAE) {	// LDY/LDA/LDX
			if (he.mOpcode[1] == 0x0D && he.mOpcode[2] == 0xD2) {	// SERIN
				pv.mType = PendingValueType::SerIn;
				pv.mLocation = he.mOpcode[0] == 0xAC ? PendingValueLocation::Y
					: he.mOpcode[0] == 0xAD ? PendingValueLocation::A
					: PendingValueLocation::X;
			}
		}

		// flush to CPU tracer if the buffer is full
		if (++numInsnsBuffered >= kInsnsPerBlock) {
			flushInsns();
		}
	}

	flushInsns();

	proc->Shutdown();

	if (framePeriod) {
		// create frame channel
		ATTraceChannelSimple *frameChannel = ctx.mpCollection->AddGroup(L"Frames", kATTraceGroupType_Frames)->AddSimpleChannel(ctx.mBaseTime, ctx.mBaseTickScale, L"Frames");
			
		uint64 tick = 0;
		uint64 endTick64 = baseCycle - (uint32)baseCycle + hebuf[0].mCycle;

		while(tick < endTick64) {
			frameChannel->AddTickEvent(tick, tick + framePeriod, L"Frame", 0xFFFFFF);
			tick += framePeriod;
		}
	}

	ATTraceGroup *group = traceColl->AddGroup(L"SIO Bus");

	ATTraceChannelSimple *sioCommandChannel = group->AddSimpleChannel(ctx.mBaseTime, ctx.mBaseTickScale, L"Command");
	for(const auto& event : sioCommandEvents)
		sioCommandChannel->AddTickEvent(event.mStartTick, event.mEndTick, L"", 0xFFC02020);

	ATTraceChannelFormatted *sioSendChannel = group->AddFormattedChannel(ctx.mBaseTime, ctx.mBaseTickScale, L"Send");
	for(const auto& event : sioSendEvents) {
		sioSendChannel->AddTickEvent(
			event.mStartTick,
			event.mEndTick,
			[ch = (uint8)event.mData](VDStringW& s) {
				s.sprintf(L"%02X", ch);
			},
			kATTraceColor_IO_Write);
	}

	ATTraceChannelFormatted *sioReceiveChannel = group->AddFormattedChannel(ctx.mBaseTime, ctx.mBaseTickScale, L"Receive");
	for(const auto& event : sioReceiveEvents) {
		sioReceiveChannel->AddTickEvent(
			event.mStartTick,
			event.mEndTick,
			[ch = (uint8)event.mData](VDStringW& s) {
				s.sprintf(L"%02X", ch);
			},
			kATTraceColor_IO_Read);
	}

	return traceColl;
}

