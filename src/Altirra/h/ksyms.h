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

#ifndef AT_KSYMS_H
#define AT_KSYMS_H

namespace ATKernelSymbols {
	// page zero
	enum {
		TRAMSZ = 0x0006,
		WARMST = 0x0008,
		DOSVEC = 0x000A,
		DOSINI = 0x000C,
		POKMSK = 0x0010,
		BRKKEY = 0x0011,
		RTCLOK = 0x0012,
		ICHIDZ = 0x0020,
		ICDNOZ = 0x0021,
		ICCOMZ = 0x0022,
		ICSTAZ = 0x0023,
		ICBALZ = 0x0024,
		ICBAHZ = 0x0025,
		ICBLLZ = 0x0028,
		ICBLHZ = 0x0029,
		ICAX1Z = 0x002A,
		ICAX2Z = 0x002B,
		ICAX3Z = 0x002C,
		ICAX4Z = 0x002D,
		ICAX5Z = 0x002E,
		ICIDNO = 0x002E,
		CIOCHR = 0x002F,
		STATUS = 0x0030,
		CHKSUM = 0X0031,
		BUFRLO = 0X0032,
		BUFRHI = 0X0033,
		BFENLO = 0X0034,
		BFENHI = 0X0035,
		BUFRFL = 0X0038,
		RECVDN = 0X0039,
		CHKSNT = 0x003B,
		BPTR   = 0x003D,
		FTYPE  = 0x003E,
		FEOF   = 0x003F,
		CRITIC = 0x0042,
		ATRACT = 0x004D,
		DRKMSK = 0x004E,
		COLRSH = 0x004F,
		LMARGN = 0x0052,
		RMARGN = 0x0053,
		ROWCRS = 0x0054,
		COLCRS = 0x0055,
		DINDEX = 0x0057,
		SAVMSC = 0x0058,
		OLDROW = 0x005A,
		OLDCOL = 0x005B,
		OLDCHR = 0x005D,
		OLDADR = 0x005E,
		LOGCOL = 0x0063,
		RAMTOP = 0x006A,
		BUFCNT = 0x006B,
		BUFADR = 0x006C,
		SWPFLG = 0x007B,
		RAMLO  = 0x0085,
		FR0	   = 0x00D4,
		FR1    = 0x00E0,
		CIX    = 0x00F2,
		INBUFF = 0x00F3,
		FLPTR  = 0x00FC
	};

	// page 2/3 symbols
	enum {
		VDSLST = 0x0200,
		VPRCED = 0x0202,
		VINTER = 0x0204,
		VBREAK = 0x0206,
		VKEYBD = 0x0208,
		VSERIN = 0x020A,
		VSEROR = 0x020C,
		VSEROC = 0x020E,
		VTIMR1 = 0x0210,
		VTIMR2 = 0x0212,
		VTIMR4 = 0x0214,
		VIMIRQ = 0x0216,
		CDTMV1 = 0x0218,
		CDTMV2 = 0x021A,
		CDTMV3 = 0x021C,
		CDTMV4 = 0x021E,
		CDTMV5 = 0x0220,
		VVBLKI = 0x0222,
		VVBLKD = 0x0224,
		CDTMA1 = 0x0226,
		CDTMA2 = 0x0228,
		CDTMF3 = 0x022A,
		CDTMF4 = 0x022C,
		CDTMF5 = 0x022E,
		SDMCTL = 0x022F,
		SDLSTL = 0x0230,
		SDLSTH = 0x0231,
		COLDST = 0x0244,
		GPRIOR = 0x026F,
		JVECK  = 0x028C,
		WMODE  = 0x0289,
		BLIM   = 0x028A,
		TXTROW = 0x0290,
		TXTCOL = 0x0291,
		TINDEX = 0x0293,
		TXTMSC = 0x0294,
		TXTOLD = 0x0296,
		LOGMAP = 0x02B2,
		BOTSCR = 0x02BF,
		PCOLR0 = 0x02C0,
		PCOLR1 = 0x02C1,
		PCOLR2 = 0x02C2,
		PCOLR3 = 0x02C3,
		COLOR0 = 0x02C4,
		COLOR1 = 0x02C5,
		COLOR2 = 0x02C6,
		COLOR3 = 0x02C7,
		COLOR4 = 0x02C8,
		MEMTOP = 0x02E5,
		MEMLO  = 0x02E7,
		KEYDEL = 0x02F1,
		CH1    = 0x02F2,
		CHACT  = 0x02F3,
		CHBAS  = 0x02F4,
		CH     = 0x02FC,
		DSPFLG = 0x02FE,
		DDEVIC = 0x0300,
		DUNIT  = 0x0301,
		TIMFLG = 0x0317,
		HATABS = 0x031A,
		ICHID  = 0x0340,
		ICCMD  = 0x0342,
		ICSTA  = 0x0343,
		ICBAL  = 0x0344,
		ICBAH  = 0x0345,
		ICPTL  = 0x0346,
		ICPTH  = 0x0347,
		ICBLL  = 0x0348,
		CASBUF = 0x03FD,
		LBUFF  = 0x0580
	};

	// hardware symbols
	enum {
		COLPM0 = 0xD012,
		COLPM1 = 0xD013,
		COLPM2 = 0xD014,
		COLPM3 = 0xD015,
		COLPF0 = 0xD016,
		COLPF1 = 0xD017,
		COLPF2 = 0xD018,
		COLPF3 = 0xD019,
		COLBK  = 0xD01A,
		PRIOR  = 0xD01B,
		CONSOL = 0xD01F,
		AUDF1  = 0xD200,
		AUDC1  = 0xD201,
		AUDC2  = 0xD203,
		AUDC3  = 0xD205,
		AUDC4  = 0xD207,
		AUDCTL = 0xD208,
		IRQST  = 0xD20E,
		IRQEN  = 0xD20E,
		SKCTL  = 0xD20F,
		PORTA  = 0xD300,
		PORTB  = 0xD301,
		PACTL  = 0xD302,
		PBCTL  = 0xD303,
		DMACTL = 0xD400,
		CHACTL = 0xD401,
		DLISTL = 0xD402,
		DLISTH = 0xD403,
		CHBASE = 0xD409,
		NMIEN  = 0xD40E,
		NMIRES = 0xD40F,
	};

	// floating-point library symbols
	enum {
		AFP    = 0xD800,
		FASC   = 0xD8E6,
		IPF    = 0xD9AA,
		FPI    = 0xD9D2,	// __ftol
		ZFR0   = 0xDA44,
		ZF1    = 0xDA46,
		LDBUFA = 0xDA51,	// undocumented (used by Atari Basic) - mwa #ldbuf inbuff
		FADD   = 0xDA66,
		FSUB   = 0xDA60,
		FMUL   = 0xDADB,
		FDIV   = 0xDB28,
		SKPSPC = 0xDBA1,	// undocumented (used by Atari Basic) - skip spaces starting at INBUFF[CIX]
		ISDIGT = 0xDBAF,	// undocumented (used by Atari Basic) - set carry if INBUFF[CIX] is not a digit
		PLYEVL = 0xDD40,
		FLD0R  = 0xDD89,
		FLD0P  = 0xDD8D,
		FLD1R  = 0xDD98,
		FLD1P  = 0xDD9C,
		FST0R  = 0xDDA7,
		FST0P  = 0xDDAB,
		FMOVE  = 0xDDB6,
		EXP    = 0xDDC0,
		EXP10  = 0xDDCC,
		LOG    = 0xDECD,
		LOG10  = 0xDED1
	};

	// kernel symbols
	enum {
		CASETV = 0xE440,
		DSKINV = 0xE453,
		CIOV   = 0xE456,
		SIOV   = 0xE459,
		SYSVBV = 0xE45F,
		XITVBV = 0xE462,
		CSOPIV = 0xE47D
	};
}


#endif
