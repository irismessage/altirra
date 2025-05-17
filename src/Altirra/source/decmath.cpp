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

#include <stdafx.h>
#include <vd2/system/VDString.h>
#include <at/atcore/decmath.h>
#include <at/atcore/ksyms.h>
#include "decmath.h"
#include "cpu.h"
#include "cpumemory.h"
#include "console.h"
#include "debuggerlog.h"

ATDebuggerLogChannel g_ATLCFPAccel(false, false, "FPACCEL", "Floating-point acceleration");

ATDecFloat ATReadDecFloat(ATCPUEmulatorMemory& mem, uint16 addr) {
	ATDecFloat v;

	v.mSignExp		= mem.ReadByte(addr);
	v.mMantissa[0]	= mem.ReadByte(addr+1);
	v.mMantissa[1]	= mem.ReadByte(addr+2);
	v.mMantissa[2]	= mem.ReadByte(addr+3);
	v.mMantissa[3]	= mem.ReadByte(addr+4);
	v.mMantissa[4]	= mem.ReadByte(addr+5);
	return v;
}

ATDecFloat ATDebugReadDecFloat(ATCPUEmulatorMemory& mem, uint16 addr) {
	ATDecFloat v;

	v.mSignExp		= mem.DebugReadByte(addr);
	v.mMantissa[0]	= mem.DebugReadByte(addr+1);
	v.mMantissa[1]	= mem.DebugReadByte(addr+2);
	v.mMantissa[2]	= mem.DebugReadByte(addr+3);
	v.mMantissa[3]	= mem.DebugReadByte(addr+4);
	v.mMantissa[4]	= mem.DebugReadByte(addr+5);
	return v;
}

void ATWriteDecFloat(ATCPUEmulatorMemory& mem, uint16 addr, const ATDecFloat& v) {
	mem.WriteByte(addr, v.mSignExp);
	mem.WriteByte(addr+1, v.mMantissa[0]);
	mem.WriteByte(addr+2, v.mMantissa[1]);
	mem.WriteByte(addr+3, v.mMantissa[2]);
	mem.WriteByte(addr+4, v.mMantissa[3]);
	mem.WriteByte(addr+5, v.mMantissa[4]);
}

ATDecFloat ATReadFR0(ATCPUEmulatorMemory& mem) {
	return ATReadDecFloat(mem, ATKernelSymbols::FR0);
}

ATDecFloat ATReadFR1(ATCPUEmulatorMemory& mem) {
	return ATReadDecFloat(mem, ATKernelSymbols::FR1);
}

void ATWriteFR0(ATCPUEmulatorMemory& mem, const ATDecFloat& x) {
	return ATWriteDecFloat(mem, ATKernelSymbols::FR0, x);
}

double ATDebugReadDecFloatAsBinary(ATCPUEmulatorMemory& mem, uint16 addr) {
	return ATDebugReadDecFloat(mem, addr).ToDouble();
}

double ATReadDecFloatAsBinary(ATCPUEmulatorMemory& mem, uint16 addr) {
	return ATReadDecFloat(mem, addr).ToDouble();
}

double ATReadDecFloatAsBinary(const uint8 bytes[6]) {
	ATDecFloat v;

	v.mSignExp		= bytes[0];
	v.mMantissa[0]	= bytes[1];
	v.mMantissa[1]	= bytes[2];
	v.mMantissa[2]	= bytes[3];
	v.mMantissa[3]	= bytes[4];
	v.mMantissa[4]	= bytes[5];

	return v.ToDouble();
}

///////////////////////////////////////////////////////////////////////////////

void ATAccelAFP(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	uint16 buffer = mem.ReadByte(ATKernelSymbols::INBUFF) + ((uint16)mem.ReadByte(ATKernelSymbols::INBUFF+1) << 8);
	uint8 index = mem.ReadByte(ATKernelSymbols::CIX);

	// skip leading spaces
	while(mem.ReadByte(buffer+index) == ' ') {
		++index;
		if (!index) {
			cpu.SetFlagC();
			g_ATLCFPAccel("AFP -> error\n");
			return;
		}
	}

	// check for a minus sign
	uint8 bias = 0x40;
	switch(mem.ReadByte(buffer+index)) {
		case '-':
			bias = 0xc0;
			// fall through
		case '+':
			++index;
			break;
	}

	// count number of leading digits
	ATDecFloat v;
	v.SetZero();

	int digits = 0;
	int leading = 0;
	bool period = false;
	bool nonzero = false;
	bool anydigits = false;
	for(;;) {
		uint8 c = mem.ReadByte(buffer+index);

		if (c == '.') {
			if (period)
				break;

			period = true;
		} else if ((uint32)(c-'0') < 10) {
			anydigits = true;

			if (c != '0')
				nonzero = true;

			if (nonzero) {
				if (!period)
					++leading;

				if (digits < 10) {
					int mantIndex = digits >> 1;

					if (digits & 1)
						v.mMantissa[mantIndex] += (c - '0');
					else
						v.mMantissa[mantIndex] += (c - '0') << 4;

					++digits;
				}
			} else if (period)
				--leading;
		} else
			break;

		// we need to check for wrapping to prevent an infinite loop, since this is HLE
		if (!++index) {
			mem.WriteByte(ATKernelSymbols::CIX, index);
			cpu.SetFlagC();
			g_ATLCFPAccel("AFP -> error\n");
			return;
		}
	}

	// if we couldn't get any digits, it's an error
	if (!anydigits) {
		mem.WriteByte(ATKernelSymbols::CIX, index);
		cpu.SetFlagC();
		g_ATLCFPAccel("AFP -> error\n");
		return;
	}

	// check for exponential notation -- note that this must be an uppercase E
	uint8 c = mem.ReadByte(buffer+index);
	if (c == 'E') {
		int index0 = index;

		// check for sign
		++index;

		c = mem.ReadByte(buffer+index);
		bool negexp = false;
		if (c == '+' || c == '-') {
			if (c == '-')
				negexp = true;

			++index;
			c = mem.ReadByte(buffer+index);
		}

		// check for first digit -- note if this fails, it is NOT an error; we
		// need to roll back to the E
		uint8 xd = c - '0';
		if (xd >= 10) {
			index = index0;
		} else {
			int exp = xd;

			++index;
			c = mem.ReadByte(buffer+index);
			uint8 xd2 = c - '0';
			if (xd2 < 10) {
				exp = exp*10+xd2;
				++index;
			}

			// zero is not a valid exponent
			if (!exp) {
				index = index0;
			} else {
				if (negexp)
					exp = -exp;

				leading += exp;
			}
		}
	}

	if (v.mMantissa[0]) {
		if (leading & 1) {
			v.mMantissa[4] = (v.mMantissa[4] >> 4) + (v.mMantissa[3] << 4);
			v.mMantissa[3] = (v.mMantissa[3] >> 4) + (v.mMantissa[2] << 4);
			v.mMantissa[2] = (v.mMantissa[2] >> 4) + (v.mMantissa[1] << 4);
			v.mMantissa[1] = (v.mMantissa[1] >> 4) + (v.mMantissa[0] << 4);
			v.mMantissa[0] = (v.mMantissa[0] >> 4);
		}

		int exponent100 = ((leading - 1) >> 1);

		if (exponent100 <= -49) {
			// underflow
			v.SetZero();
		} else if (exponent100 >= 49) {
			// overflow
			cpu.SetFlagC();
			g_ATLCFPAccel("AFP -> error\n");
			return;
		} else {
			v.mSignExp = exponent100 + bias;
		}
	}

	ATWriteFR0(mem, v);

	mem.WriteByte(ATKernelSymbols::CIX, index);
	cpu.ClearFlagC();

	if (g_ATLCFPAccel.IsEnabled())
		g_ATLCFPAccel("AFP -> %s\n", v.ToString().c_str());
}

void ATAccelFASC(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	// Some test cases:
	// 1
	// -1
	// 10
	// 1000000000
	// 1E+10
	// 1.0E+11
	// 1E+12
	// 0.01
	// 1.0E-03
	// 1E-04
	// 0.11
	// 0.011
	// 1.1E-03
	// 1.1E-04
	// 1.1E-05

	char buf[21];
	char *s = buf;
	const ATDecFloat v(ATReadFR0(mem));

	if (!v.mSignExp || !v.mMantissa[0])
		*s++ = '0';
	else {
		if (v.mSignExp & 0x80)
			*s++ = '-';

		int exp = ((v.mSignExp & 0x7f) - 0x40)*2;
		int expodd = 0;

		if (exp == -2) {
			*s++ = '0';
			*s++ = '.';
		}

		if (v.mMantissa[0] >= 16)
			expodd = 1;

		if (expodd || exp == -2) {
			*s++ = '0' + (v.mMantissa[0] >> 4);
		}

		if ((exp >= 10 || exp < -2) && expodd)
			*s++ = '.';

		*s++ = '0' + (v.mMantissa[0] & 15);

		if ((exp >= 10 || exp < -2) && !expodd)
			*s++ = '.';

		for(int i=1; i<5; ++i) {
			uint8 m = v.mMantissa[i];

			if (exp == i*2-2)
				*s++ = '.';

			*s++ = '0' + (m >> 4);
			*s++ = '0' + (m & 15);
		}

		int omittableDigits;

		if (exp < -2 || exp >= 10)
			omittableDigits = 8;
		else if (exp >= 0)
			omittableDigits = 8-exp;
		else
			omittableDigits = 8+expodd;

		for(int i=0; i<omittableDigits; ++i) {
			if (s[-1] != '0')
				break;
			--s;
		}

		if (s[-1] == '.')
			--s;

		exp += expodd;
		if (exp >= 10 || exp <= -3) {
			*s++ = 'E';
			*s++ = (exp < 0 ? '-' : '+');

			int absexp = abs(exp);
			*s++ = '0' + (absexp / 10);
			*s++ = '0' + (absexp % 10);
		}
	}

	mem.WriteByte(ATKernelSymbols::INBUFF, (uint8)ATKernelSymbols::LBUFF);
	mem.WriteByte(ATKernelSymbols::INBUFF+1, (uint8)(ATKernelSymbols::LBUFF >> 8));

	int len = (int)(s - buf);
	bool needPeriod = true;
	for(int i=0; i<len - 1; ++i) {
		uint8 c = buf[i];

		if (c == '.' || c == 'E')
			needPeriod = false;

		mem.WriteByte(ATKernelSymbols::LBUFF+i, c);
	}

	mem.WriteByte(ATKernelSymbols::LBUFF+len-1, (uint8)(s[-1] | 0x80));
	*s = 0;

	// SysInfo 2.19 looks for a period after non-zero numbers without checking the termination flag.
	if (needPeriod)
		mem.WriteByte(ATKernelSymbols::LBUFF+len, '.');

	if (g_ATLCFPAccel.IsEnabled())
		g_ATLCFPAccel("FASC(%s) -> %s\n", v.ToString().c_str(), buf);
}

void ATAccelIPF(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	const int value0 = mem.ReadByte(ATKernelSymbols::FR0) + ((uint32)mem.ReadByte(ATKernelSymbols::FR0+1) << 8);
	int value = value0;
	ATDecFloat r;

	if (!value) {
		r.SetZero();
	} else {
		r.mSignExp = 0x42;

		int d0 = value % 10;	value /= 10;
		int d1 = value % 10;	value /= 10;
		int d2 = value % 10;	value /= 10;
		int d3 = value % 10;	value /= 10;
		int d4 = value;

		uint8 d01 = (uint8)((d1 << 4) + d0);
		uint8 d23 = (uint8)((d3 << 4) + d2);
		uint8 d45 = (uint8)d4;

		while(!d45) {
			d45 = d23;
			d23 = d01;
			d01 = 0;
			--r.mSignExp;
		}

		r.mMantissa[0] = d45;
		r.mMantissa[1] = d23;
		r.mMantissa[2] = d01;
		r.mMantissa[3] = 0;
		r.mMantissa[4] = 0;
	}

	ATWriteFR0(mem, r);

	if (g_ATLCFPAccel.IsEnabled())
		g_ATLCFPAccel("IPF($%04X) -> %s\n", value0, r.ToString().c_str());
}

void ATAccelFPI(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	const ATDecFloat x = ATReadFR0(mem);

	// 40 01 00 00 00 00 = 1.0
	// 42 06 55 35 00 00 = 65535.5
	// 3F 50 00 00 00 00 = 0.5
	uint32 value = 0;

	uint8 exp = x.mSignExp;
	if (exp >= 0x43)
		value = 0x10000;
	else {
		if (exp >= 0x3F) {
			uint8 roundbyte = x.mMantissa[exp - 0x3F];
			
			for(int i=0; i<exp-0x3F; ++i) {
				value *= 100;

				const uint8 c = x.mMantissa[i];
				value += (c >> 4)*10 + (c & 15);
			}

			if (roundbyte >= 0x50)
				++value;
		}
	}

	if (value >= 0x10000) {
		cpu.SetFlagC();

		if (g_ATLCFPAccel.IsEnabled())
			g_ATLCFPAccel("FPI(%s) -> error\n", x.ToString().c_str());
	} else {
		mem.WriteByte(0xD4, (uint8)value);
		mem.WriteByte(0xD5, (uint8)(value >> 8));
		cpu.ClearFlagC();

		if (g_ATLCFPAccel.IsEnabled())
			g_ATLCFPAccel("FPI(%s) -> $%04X\n", x.ToString().c_str(), value);
	}
}

void ATAccelFADD(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	ATDecFloat fp0(ATReadFR0(mem));
	ATDecFloat fp1(ATReadFR1(mem));
	ATDecFloat fpr;

	if (ATDecFloatAdd(fpr, fp0, fp1)) {
		double r0 = fp0.ToDouble();
		double r1 = fp1.ToDouble();
		[[maybe_unused]] double rr = fpr.ToDouble();

		if (fabs(r0) > 1e-5 && fabs(r1) > 1e-5) {
			if (r0 > r1) {
				VDASSERT((rr - (r0 + r1)) / r0 < 1e-5);
			} else {
				VDASSERT((rr - (r0 + r1)) / r1 < 1e-5);
			}
		}

		if (g_ATLCFPAccel.IsEnabled())
			g_ATLCFPAccel("FADD(%s, %s) -> %s\n", fp0.ToString().c_str(), fp1.ToString().c_str(), fpr.ToString().c_str());

		ATWriteFR0(mem, fpr);
		cpu.ClearFlagC();
	} else {
		cpu.SetFlagC();

		if (g_ATLCFPAccel.IsEnabled())
			g_ATLCFPAccel("FADD(%s, %s) -> error\n", fp0.ToString().c_str(), fp1.ToString().c_str());
	}
}

void ATAccelFSUB(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	ATDecFloat fp0(ATReadFR0(mem));
	ATDecFloat fp1(ATReadFR1(mem));
	ATDecFloat fpr;

	if (ATDecFloatAdd(fpr, fp0, -fp1)) {
		ATWriteFR0(mem, fpr);
		cpu.ClearFlagC();

		if (g_ATLCFPAccel.IsEnabled())
			g_ATLCFPAccel("FSUB(%s, %s) -> %s\n", fp0.ToString().c_str(), fp1.ToString().c_str(), fpr.ToString().c_str());
	} else {
		cpu.SetFlagC();

		if (g_ATLCFPAccel.IsEnabled())
			g_ATLCFPAccel("FSUB(%s, %s) -> error\n", fp0.ToString().c_str(), fp1.ToString().c_str());
	}
}

void ATAccelFMUL(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	ATDecFloat fp0(ATReadFR0(mem));
	ATDecFloat fp1(ATReadFR1(mem));
	ATDecFloat fpr;

	if (ATDecFloatMul(fpr, fp0, fp1)) {
		ATWriteFR0(mem, fpr);
		cpu.ClearFlagC();

		if (g_ATLCFPAccel.IsEnabled())
			g_ATLCFPAccel("FMUL(%s, %s) -> %s\n", fp0.ToString().c_str(), fp1.ToString().c_str(), fpr.ToString().c_str());
	} else {
		cpu.SetFlagC();

		if (g_ATLCFPAccel.IsEnabled())
			g_ATLCFPAccel("FMUL(%s, %s) -> error\n", fp0.ToString().c_str(), fp1.ToString().c_str());
	}
}

void ATAccelFDIV(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	ATDecFloat fp0(ATReadFR0(mem));
	ATDecFloat fp1(ATReadFR1(mem));
	ATDecFloat fpr;

	if (ATDecFloatDiv(fpr, fp0, fp1)) {
		ATWriteFR0(mem, fpr);
		cpu.ClearFlagC();

		if (g_ATLCFPAccel.IsEnabled())
			g_ATLCFPAccel("FDIV(%s, %s) -> %s\n", fp0.ToString().c_str(), fp1.ToString().c_str(), fpr.ToString().c_str());
	} else {
		cpu.SetFlagC();

		if (g_ATLCFPAccel.IsEnabled())
			g_ATLCFPAccel("FDIV(%s, %s) -> error\n", fp0.ToString().c_str(), fp1.ToString().c_str());
	}
}

void ATAccelLOG(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	const ATDecFloat x0 = ATReadFR0(mem);
	double x = x0.ToDouble();
	if (x < 0.0) {
		cpu.SetFlagC();
		return;
	}

	double r = log(x);
	ATDecFloat fpr;
	if (!fpr.SetDouble(r)) {
		if (g_ATLCFPAccel.IsEnabled())
			g_ATLCFPAccel("LOG(%s) -> error\n", x0.ToString().c_str());

		cpu.SetFlagC();
		return;
	}

	if (g_ATLCFPAccel.IsEnabled())
		g_ATLCFPAccel("LOG(%s) -> %s\n", x0.ToString().c_str(), fpr.ToString().c_str());

	ATWriteFR0(mem, fpr);
	cpu.ClearFlagC();
}

void ATAccelLOG10(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	const ATDecFloat x0 = ATReadFR0(mem);
	double x = x0.ToDouble();
	if (x < 0.0) {
		cpu.SetFlagC();
		return;
	}

	double r = log10(x);
	ATDecFloat fpr;
	if (!fpr.SetDouble(r)) {
		if (g_ATLCFPAccel.IsEnabled())
			g_ATLCFPAccel("LOG10(%s) -> error\n", x0.ToString().c_str());

		cpu.SetFlagC();
		return;
	}

	if (g_ATLCFPAccel.IsEnabled())
		g_ATLCFPAccel("LOG10(%s) -> %s\n", x0.ToString().c_str(), fpr.ToString().c_str());

	ATWriteFR0(mem, fpr);
	cpu.ClearFlagC();
}

void ATAccelEXP(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	const ATDecFloat x0 = ATReadFR0(mem);
	double x = x0.ToDouble();

	static constexpr double kLn1E100 = 230.25850929940456840179914546844;

	if (x >= kLn1E100) {
		g_ATLCFPAccel("EXP(%s) -> error\n", x0.ToString().c_str());
		cpu.SetFlagC();
		return;
	}

	const double r = exp(x);

	ATDecFloat fpr;
	if (!fpr.SetDouble(r)) {
		g_ATLCFPAccel("EXP(%s) -> error\n", x0.ToString().c_str());
		cpu.SetFlagC();
		return;
	}

	g_ATLCFPAccel("EXP(%s) -> %s\n", x0.ToString().c_str(), fpr.ToString().c_str());
	ATWriteFR0(mem, fpr);
	cpu.ClearFlagC();
}

void ATAccelEXP10(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	const ATDecFloat x0 = ATReadFR0(mem);
	double x = x0.ToDouble();

	if (x >= 100) {
		g_ATLCFPAccel("EXP10(%s) -> error\n", x0.ToString().c_str());
		cpu.SetFlagC();
		return;
	}

	double r = pow(10.0, x);
	ATDecFloat fpr;
	if (!fpr.SetDouble(r)) {
		g_ATLCFPAccel("EXP10(%s) -> error\n", x0.ToString().c_str());
		cpu.SetFlagC();
		return;
	}

	g_ATLCFPAccel("EXP10(%s) -> %s\n", x0.ToString().c_str(), fpr.ToString().c_str());
	ATWriteFR0(mem, fpr);
	cpu.ClearFlagC();
}

void ATAccelSKPSPC(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	uint16 buffer = mem.ReadByte(ATKernelSymbols::INBUFF) + ((uint16)mem.ReadByte(ATKernelSymbols::INBUFF+1) << 8);
	uint8 index = mem.ReadByte(ATKernelSymbols::CIX);
	uint8 ch;

	for(;;) {
		ch = mem.ReadByte(buffer + index);
		
		if (ch != ' ')
			break;

		++index;
		if (!index)
			break;
	}

	mem.WriteByte(ATKernelSymbols::CIX, index);
	cpu.SetY(index);
}

void ATAccelISDIGT(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	uint16 buffer = mem.ReadByte(ATKernelSymbols::INBUFF) + ((uint16)mem.ReadByte(ATKernelSymbols::INBUFF+1) << 8);
	uint8 index = mem.ReadByte(ATKernelSymbols::CIX);

	uint8 c = mem.ReadByte(buffer + index);
	if ((uint8)(c - '0') >= 10)
		cpu.SetFlagC();
	else
		cpu.ClearFlagC();

	cpu.SetA((uint8)(c - '0'));
	cpu.SetY(index);
}

void ATAccelNORMALIZE(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	ATDecFloat fr0(ATReadFR0(mem));
	int count = 0;

	while(count < 5 && !fr0.mMantissa[count])
		++count;

	if (count) {
		if (count >= 5)
			fr0.SetZero();
		else {
			for(int i=0; i<5-count; ++i)
				fr0.mMantissa[i] = fr0.mMantissa[i+count];

			for(int i=5-count; i<5; ++i)
				fr0.mMantissa[5-i] = 0;

			if ((fr0.mSignExp & 0x7f) < 64 - 49 + count)
				fr0.SetZero();
			else
				fr0.mSignExp -= count;
		}

		ATWriteFR0(mem, fr0);
	}

	cpu.ClearFlagC();
}

void ATAccelPLYEVL(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	const uint16 addr0 = ((uint16)cpu.GetY() << 8) + cpu.GetX();
	const uint8 coeffs0 = cpu.GetA();
	uint16 addr = addr0;
	uint8 coeffs = coeffs0;

	ATDecFloat z(ATReadFR0(mem));
	ATDecFloat accum;
	ATDecFloat t;

	accum.SetZero();

	for(;;) {
		if (!ATDecFloatAdd(t, accum, ATReadDecFloat(mem, addr))) {
			cpu.SetFlagC();
			return;
		}

		addr += 6;

		if (!--coeffs)
			break;

		if (!ATDecFloatMul(accum, t, z)) {
			cpu.SetFlagC();
			if (g_ATLCFPAccel.IsEnabled())
				g_ATLCFPAccel("PLYEVL(%s,$%04X,%u) -> error\n", z.ToString().c_str(), addr0, coeffs0);
			return;
		}
	}

	ATWriteFR0(mem, t);
	cpu.ClearFlagC();

	if (g_ATLCFPAccel.IsEnabled())
		g_ATLCFPAccel("PLYEVL(%s,$%04X,%u) -> %s\n", z.ToString().c_str(), addr0, coeffs0, t.ToString().c_str());
}

void ATAccelZFR0(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	ATDecFloat z;
	z.SetZero();
	ATWriteFR0(mem, z);

	// Note: must preserve C for Basic XE compatibility.
	cpu.SetA(0);
	cpu.SetX((uint8)(ATKernelSymbols::FR0 + 6));
	cpu.Ldy(0);

	g_ATLCFPAccel("ZFR0\n");
}

void ATAccelZF1(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	uint8 addr = cpu.GetX();

	for(int i=0; i<6; ++i)
		mem.WriteByte(addr++, 0);

	cpu.SetA(0);
	cpu.SetX(addr);
	cpu.Ldy(0);

	g_ATLCFPAccel("ZF1\n");
}

void ATAccelZFL(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	uint8 addr = cpu.GetX();
	uint8 len = cpu.GetY();

	do {
		mem.WriteByte(addr++, 0);
	} while(--len);

	cpu.SetA(0);
	cpu.SetX(addr);
	cpu.Ldy(0);

	g_ATLCFPAccel("ZFL\n");
}

void ATAccelLDBUFA(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	mem.WriteByte(ATKernelSymbols::INBUFF+1, (uint8)(ATKernelSymbols::LBUFF >> 8));
	mem.WriteByte(ATKernelSymbols::INBUFF, (uint8)ATKernelSymbols::LBUFF);
}

void ATAccelFLD0R(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	mem.WriteByte(ATKernelSymbols::FLPTR+1, cpu.GetY());
	mem.WriteByte(ATKernelSymbols::FLPTR, cpu.GetX());
	ATAccelFLD0P(cpu, mem);
}

void ATAccelFLD0P(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	const uint16 addr = ((uint16)mem.ReadByte(ATKernelSymbols::FLPTR+1) << 8) + mem.ReadByte(ATKernelSymbols::FLPTR);
	const ATDecFloat x = ATReadDecFloat(mem, addr);

	ATWriteFR0(mem, x);

	cpu.SetY(0);
	cpu.SetA(x.mSignExp);
	cpu.SetP((cpu.GetP() & ~AT6502::kFlagZ) | AT6502::kFlagN);

	if (g_ATLCFPAccel.IsEnabled())
		g_ATLCFPAccel("FLD0P($%04X) -> %s\n", addr, x.ToString().c_str());
}

void ATAccelFLD1R(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	mem.WriteByte(ATKernelSymbols::FLPTR+1, cpu.GetY());
	mem.WriteByte(ATKernelSymbols::FLPTR, cpu.GetX());
	ATAccelFLD1P(cpu, mem);
}

void ATAccelFLD1P(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	const uint16 addr = ((uint16)mem.ReadByte(ATKernelSymbols::FLPTR+1) << 8) + mem.ReadByte(ATKernelSymbols::FLPTR);

	const ATDecFloat x = ATReadDecFloat(mem, addr);

	ATWriteDecFloat(mem, ATKernelSymbols::FR1, x);

	cpu.SetY(0);
	cpu.SetA(mem.ReadByte(ATKernelSymbols::FR1));

	// This is critical for Atari Basic to work, even though it's not guaranteed.
	cpu.SetP((cpu.GetP() & ~AT6502::kFlagZ) | AT6502::kFlagN);

	if (g_ATLCFPAccel.IsEnabled())
		g_ATLCFPAccel("FLD1P($%04X) -> %s\n", addr, x.ToString().c_str());
}

void ATAccelFST0R(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	mem.WriteByte(ATKernelSymbols::FLPTR+1, cpu.GetY());
	mem.WriteByte(ATKernelSymbols::FLPTR, cpu.GetX());
	ATAccelFST0P(cpu, mem);
}

void ATAccelFST0P(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	uint16 addr = ((uint16)mem.ReadByte(ATKernelSymbols::FLPTR+1) << 8) + mem.ReadByte(ATKernelSymbols::FLPTR);

	for(int i=0; i<6; ++i)
		mem.WriteByte(addr+i, mem.ReadByte(ATKernelSymbols::FR0+i));
}

void ATAccelFMOVE(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	ATDecFloat x = ATReadFR0(mem);

	ATWriteDecFloat(mem, ATKernelSymbols::FR1, x);

	if (g_ATLCFPAccel.IsEnabled())
		g_ATLCFPAccel("FMOVE(%s)\n", x.ToString().c_str());
}

void ATAccelREDRNG(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	ATDecFloat one;
	one.SetOne();

	ATDecFloat x = ATReadFR0(mem);
	ATDecFloat y = ATReadDecFloat(mem, cpu.GetY()*256U + cpu.GetX());

	ATDecFloat num;
	ATDecFloat den;
	ATDecFloat res;

	cpu.ClearFlagC();
	if (!ATDecFloatAdd(num, x, -y) ||
		!ATDecFloatAdd(den, x, y) ||
		!ATDecFloatDiv(res, num, den)) {
		cpu.SetFlagC();
	} else {
		ATWriteFR0(mem, res);
	}
}
