//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2022 Avery Lee
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
#include <at/atcore/ksyms.h>
#include <at/atdebugger/symbols.h>
#include <at/atdebugger/internal/symstore.h>

bool ATCreateDefaultVariableSymbolStore(IATSymbolStore **ppStore) {
	vdrefptr<ATSymbolStore> symstore(new ATSymbolStore);

	symstore->Init(0x0000, 0x0400);

	using namespace ATKernelSymbols;

	static constexpr ATSymbolStore::SymbolInfo kSymbols[] = {
		{ CASINI, "CASINI", 2 },
		{ RAMLO , "RAMLO" , 2 },
		{ TRAMSZ, "TRAMSZ", 1 },
		{ WARMST, "WARMST", 1 },
		{ DOSVEC, "DOSVEC", 2 },
		{ DOSINI, "DOSINI", 2 },
		{ APPMHI, "APPMHI", 2 },
		{ POKMSK, "POKMSK", 1 },
		{ BRKKEY, "BRKKEY", 1 },
		{ RTCLOK, "RTCLOK", 3 },
		{ BUFADR, "BUFADR", 2 },
		{ ICHIDZ, "ICHIDZ", 1 },
		{ ICDNOZ, "ICDNOZ", 1 },
		{ ICCOMZ, "ICCOMZ", 1 },
		{ ICSTAZ, "ICSTAZ", 1 },
		{ ICBALZ, "ICBALZ", 1 },
		{ ICBAHZ, "ICBAHZ", 1 },
		{ ICBLLZ, "ICBLLZ", 1 },
		{ ICBLHZ, "ICBLHZ", 1 },
		{ ICAX1Z, "ICAX1Z", 1 },
		{ ICAX2Z, "ICAX2Z", 1 },
		{ ICAX3Z, "ICAX3Z", 1 },
		{ ICAX4Z, "ICAX4Z", 1 },
		{ ICAX5Z, "ICAX5Z", 1 },
		{ STATUS, "STATUS", 1 },
		{ CHKSUM, "CHKSUM", 1 },
		{ BUFRLO, "BUFRLO", 1 },
		{ BUFRHI, "BUFRHI", 1 },
		{ BFENLO, "BFENLO", 1 },
		{ BFENHI, "BFENHI", 1 },
		{ BUFRFL, "BUFRFL", 1 },
		{ RECVDN, "RECVDN", 1 },
		{ XMTDON, "XMTDON", 1 },
		{ CHKSNT, "CHKSNT", 1 },
		{ SOUNDR, "SOUNDR", 1 },
		{ CRITIC, "CRITIC", 1 },
		{ CKEY,   "CKEY"  , 1 },
		{ CASSBT, "CASSBT", 1 },
		{ ATRACT, "ATRACT", 1 },
		{ DRKMSK, "DRKMSK", 1 },
		{ COLRSH, "COLRSH", 1 },
		{ HOLD1 , "HOLD1" , 1 },
		{ LMARGN, "LMARGN", 1 },
		{ RMARGN, "RMARGN", 1 },
		{ ROWCRS, "ROWCRS", 1 },
		{ COLCRS, "COLCRS", 2 },
		{ OLDROW, "OLDROW", 1 },
		{ OLDCOL, "OLDCOL", 2 },
		{ OLDCHR, "OLDCHR", 1 },
		{ DINDEX, "DINDEX", 1 },
		{ SAVMSC, "SAVMSC", 2 },
		{ OLDADR, "OLDADR", 2 },
		{ PALNTS, "PALNTS", 1 },
		{ LOGCOL, "LOGCOL", 1 },
		{ ADRESS, "ADRESS", 2 },
		{ TOADR , "TOADR" , 2 },
		{ RAMTOP, "RAMTOP", 1 },
		{ BUFCNT, "BUFCNT", 1 },
		{ BUFSTR, "BUFSTR", 2 },
		{ BITMSK, "BITMSK", 1 },
		{ DELTAR, "DELTAR", 1 },
		{ DELTAC, "DELTAC", 2 },
		{ ROWINC, "ROWINC", 1 },
		{ COLINC, "COLINC", 1 },
		{ KEYDEF, "KEYDEF", 2 },	// XL/XE
		{ SWPFLG, "SWPFLG", 1 },
		{ COUNTR, "COUNTR", 2 },

		{ FR0, "FR0", 1 },
		{ FR1, "FR1", 1 },
		{ CIX, "CIX", 1 },

		{ INBUFF, "INBUFF", 1 },
		{ FLPTR, "FLPTR", 1 },

		{ VDSLST, "VDSLST", 2 },
		{ VPRCED, "VPRCED", 2 },
		{ VINTER, "VINTER", 2 },
		{ VBREAK, "VBREAK", 2 },
		{ VKEYBD, "VKEYBD", 2 },
		{ VSERIN, "VSERIN", 2 },
		{ VSEROR, "VSEROR", 2 },
		{ VSEROC, "VSEROC", 2 },
		{ VTIMR1, "VTIMR1", 2 },
		{ VTIMR2, "VTIMR2", 2 },
		{ VTIMR4, "VTIMR4", 2 },
		{ VIMIRQ, "VIMIRQ", 2 },
		{ CDTMV1, "CDTMV1", 2 },
		{ CDTMV2, "CDTMV2", 2 },
		{ CDTMV3, "CDTMV3", 2 },
		{ CDTMV4, "CDTMV4", 2 },
		{ CDTMV5, "CDTMV5", 2 },
		{ VVBLKI, "VVBLKI", 2 },
		{ VVBLKD, "VVBLKD", 2 },
		{ CDTMA1, "CDTMA1", 1 },
		{ CDTMA2, "CDTMA2", 1 },
		{ CDTMF3, "CDTMF3", 1 },
		{ CDTMF4, "CDTMF4", 1 },
		{ CDTMF5, "CDTMF5", 1 },
		{ SDMCTL, "SDMCTL", 1 },
		{ SDLSTL, "SDLSTL", 1 },
		{ SDLSTH, "SDLSTH", 1 },
		{ SSKCTL, "SSKCTL", 1 },
		{ LPENH , "LPENH" , 1 },
		{ LPENV , "LPENV" , 1 },
		{ BRKKY , "BRKKY" , 2 },
		{ VPIRQ , "VPIRQ" , 2 },	// XL/XE
		{ COLDST, "COLDST", 1 },
		{ PDVMSK, "PDVMSK", 1 },
		{ SHPDVS, "SHPDVS", 1 },
		{ PDMSK , "PDMSK" , 1 },	// XL/XE
		{ CHSALT, "CHSALT", 1 },	// XL/XE
		{ GPRIOR, "GPRIOR", 1 },
		{ PADDL0, "PADDL0", 1 },
		{ PADDL1, "PADDL1", 1 },
		{ PADDL2, "PADDL2", 1 },
		{ PADDL3, "PADDL3", 1 },
		{ PADDL4, "PADDL4", 1 },
		{ PADDL5, "PADDL5", 1 },
		{ PADDL6, "PADDL6", 1 },
		{ PADDL7, "PADDL7", 1 },
		{ STICK0, "STICK0", 1 },
		{ STICK1, "STICK1", 1 },
		{ STICK2, "STICK2", 1 },
		{ STICK3, "STICK3", 1 },
		{ PTRIG0, "PTRIG0", 1 },
		{ PTRIG1, "PTRIG1", 1 },
		{ PTRIG2, "PTRIG2", 1 },
		{ PTRIG3, "PTRIG3", 1 },
		{ PTRIG4, "PTRIG4", 1 },
		{ PTRIG5, "PTRIG5", 1 },
		{ PTRIG6, "PTRIG6", 1 },
		{ PTRIG7, "PTRIG7", 1 },
		{ STRIG0, "STRIG0", 1 },
		{ STRIG1, "STRIG1", 1 },
		{ STRIG2, "STRIG2", 1 },
		{ STRIG3, "STRIG3", 1 },
		{ JVECK , "JVECK" , 2 },
		{ TXTROW, "TXTROW", 1 },
		{ TXTCOL, "TXTCOL", 2 },
		{ TINDEX, "TINDEX", 1 },
		{ TXTMSC, "TXTMSC", 2 },
		{ TXTOLD, "TXTOLD", 2 },
		{ HOLD2 , "HOLD2" , 1 },
		{ DMASK , "DMASK" , 1 },
		{ ESCFLG, "ESCFLG", 1 },
		{ TABMAP, "TABMAP", 15 },
		{ LOGMAP, "LOGMAP", 4 },
		{ SHFLOK, "SHFLOK", 1 },
		{ BOTSCR, "BOTSCR", 1 },
		{ PCOLR0, "PCOLR0", 1 },
		{ PCOLR1, "PCOLR1", 1 },
		{ PCOLR2, "PCOLR2", 1 },
		{ PCOLR3, "PCOLR3", 1 },
		{ COLOR0, "COLOR0", 1 },
		{ COLOR1, "COLOR1", 1 },
		{ COLOR2, "COLOR2", 1 },
		{ COLOR3, "COLOR3", 1 },
		{ COLOR4, "COLOR4", 1 },
		{ DSCTLN, "DSCTLN", 1 },	// XL/XE
		{ KRPDEL, "KRPDEL", 1 },	// XL/XE
		{ KEYREP, "KEYREP", 1 },	// XL/XE
		{ NOCLIK, "NOCLIK", 1 },	// XL/XE
		{ HELPFG, "HELPFG", 1 },	// XL/XE
		{ DMASAV, "DMASAV", 1 },	// XL/XE
		{ RUNAD , "RUNAD" , 2 },
		{ INITAD, "INITAD", 2 },
		{ MEMTOP, "MEMTOP", 2 },
		{ MEMLO , "MEMLO" , 2 },
		{ DVSTAT, "DVSTAT", 4 },
		{ CBAUDL, "CBAUDL", 1 },
		{ CBAUDH, "CBAUDH", 1 },
		{ CRSINH, "CRSINH", 1 },
		{ KEYDEL, "KEYDEL", 1 },
		{ CH1   , "CH1"   , 1 },
		{ CHACT , "CHACT" , 1 },
		{ CHBAS , "CHBAS" , 1 },
		{ ATACHR, "ATACHR", 1 },
		{ CH    , "CH"    , 1 },
		{ FILDAT, "FILDAT", 1 },
		{ DSPFLG, "DSPFLG", 1 },
		{ DDEVIC, "DDEVIC", 1 },
		{ DUNIT , "DUNIT" , 1 },
		{ DCOMND, "DCOMND", 1 },
		{ DSTATS, "DSTATS", 1 },
		{ DBUFLO, "DBUFLO", 1 },
		{ DBUFHI, "DBUFHI", 1 },
		{ DTIMLO, "DTIMLO", 1 },
		{ DBYTLO, "DBYTLO", 1 },
		{ DBYTHI, "DBYTHI", 1 },
		{ DAUX1 , "DAUX1" , 1 },
		{ DAUX2 , "DAUX2" , 1 },
		{ TIMER1, "TIMER1", 2 },
		{ TIMER2, "TIMER2", 2 },
		{ TIMFLG, "TIMFLG", 1 },
		{ STACKP, "STACKP", 1 },
		{ HATABS, "HATABS", 38 },
		{ ICHID , "ICHID" , 1 },
		{ ICDNO , "ICDNO" , 1 },
		{ ICCMD , "ICCMD" , 1 },
		{ ICSTA , "ICSTA" , 1 },
		{ ICBAL , "ICBAL" , 1 },
		{ ICBAH , "ICBAH" , 1 },
		{ ICPTL , "ICPTL" , 1 },
		{ ICPTH , "ICPTH" , 1 },
		{ ICBLL , "ICBLL" , 1 },
		{ ICBLH , "ICBLH" , 1 },
		{ ICAX1 , "ICAX1" , 1 },
		{ ICAX2 , "ICAX2" , 1 },
		{ ICAX3 , "ICAX3" , 1 },
		{ ICAX4 , "ICAX4" , 1 },
		{ ICAX5 , "ICAX5" , 1 },
		{ ICAX6 , "ICAX6" , 1 },
		{ BASICF, "BASICF", 1 },
		{ GINTLK, "GINTLK", 1 },
		{ CASBUF, "CASBUF", 131 },
		{ LBUFF , "LBUFF" , 128 },
	};

	symstore->AddSymbols(kSymbols);

	*ppStore = symstore.release();
	return true;
}

bool ATCreateDefaultVariableSymbolStore5200(IATSymbolStore **ppStore) {
	vdrefptr<ATSymbolStore> symstore(new ATSymbolStore);

	symstore->Init(0x0000, 0x0400);

	using namespace ATKernelSymbols5200;

	static constexpr ATSymbolStore::SymbolInfo kSymbols[] = {
		{ POKMSK, "POKMSK", 1 },
		{ RTCLOK, "RTCLOK", 1 },
		{ CRITIC, "CRITIC", 1 },
		{ ATRACT, "ATRACT", 1 },
		{ SDMCTL, "SDMCTL", 1 },
		{ SDLSTL, "SDLSTL", 1 },
		{ SDLSTH, "SDLSTH", 1 },
		{ PCOLR0, "PCOLR0", 1 },
		{ PCOLR1, "PCOLR1", 1 },
		{ PCOLR2, "PCOLR2", 1 },
		{ PCOLR3, "PCOLR3", 1 },
		{ COLOR0, "COLOR0", 1 },
		{ COLOR1, "COLOR1", 1 },
		{ COLOR2, "COLOR2", 1 },
		{ COLOR3, "COLOR3", 1 },
		{ COLOR4, "COLOR4", 1 },

		{ VIMIRQ, "VIMIRQ", 2 },
		{ VVBLKI, "VVBLKI", 2 },
		{ VVBLKD, "VVBLKD", 2 },
		{ VDSLST, "VDSLST", 2 },
		{ VTRIGR, "VTRIGR", 2 },
		{ VBRKOP, "VBRKOP", 2 },
		{ VKYBDI, "VKYBDI", 2 },
		{ VKYBDF, "VKYBDF", 2 },
		{ VSERIN, "VSERIN", 2 },
		{ VSEROR, "VSEROR", 2 },
		{ VSEROC, "VSEROC", 2 },
		{ VTIMR1, "VTIMR1", 2 },
		{ VTIMR2, "VTIMR2", 2 },
		{ VTIMR4, "VTIMR4", 2 },
	};

	symstore->AddSymbols(kSymbols);

	*ppStore = symstore.release();
	return true;
}

bool ATCreateDefaultKernelSymbolStore(IATSymbolStore **ppStore) {
	using namespace ATKernelSymbols;

	vdrefptr<ATSymbolStore> symstore(new ATSymbolStore);

	symstore->Init(0xD800, 0x0D00);
	static constexpr ATSymbolStore::SymbolInfo kSymbols[] = {
		{ AFP, "AFP", 1 },
		{ FASC, "FASC", 1 },
		{ IPF, "IPF", 1 },
		{ FPI, "FPI", 1 },
		{ ZFR0, "ZFR0", 1 },
		{ ZF1, "ZF1", 1 },
		{ FADD, "FADD", 1 },
		{ FSUB, "FSUB", 1 },
		{ FMUL, "FMUL", 1 },
		{ FDIV, "FDIV", 1 },
		{ PLYEVL, "PLYEVL", 1 },
		{ FLD0R, "FLD0R", 1 },
		{ FLD0P, "FLD0P", 1 },
		{ FLD1R, "FLD1R", 1 },
		{ FLD1P, "FLD1P", 1 },
		{ FST0R, "FST0R", 1 },
		{ FST0P, "FST0P", 1 },
		{ FMOVE, "FMOVE", 1 },
		{ EXP, "EXP", 1 },
		{ EXP10, "EXP10", 1 },
		{ LOG, "LOG", 1 },
		{ LOG10, "LOG10", 1 },
		{ 0xE400, "EDITRV", 3 },
		{ 0xE410, "SCRENV", 3 },
		{ 0xE420, "KEYBDV", 3 },
		{ 0xE430, "PRINTV", 3 },
		{ 0xE440, "CASETV", 3 },
		{ 0xE450, "DISKIV", 3 },
		{ 0xE453, "DSKINV", 3 },
		{ 0xE456, "CIOV", 3 },
		{ 0xE459, "SIOV", 3 },
		{ 0xE45C, "SETVBV", 3 },
		{ 0xE45F, "SYSVBV", 3 },
		{ 0xE462, "XITVBV", 3 },
		{ 0xE465, "SIOINV", 3 },
		{ 0xE468, "SENDEV", 3 },
		{ 0xE46B, "INTINV", 3 },
		{ 0xE46E, "CIOINV", 3 },
		{ 0xE471, "BLKBDV", 3 },
		{ 0xE474, "WARMSV", 3 },
		{ 0xE477, "COLDSV", 3 },
		{ 0xE47A, "RBLOKV", 3 },
		{ 0xE47D, "CSOPIV", 3 },
		{ 0xE480, "VCTABL", 3 },
	};

	symstore->AddSymbols(kSymbols);

	*ppStore = symstore.release();
	return true;
}

namespace {
	struct HardwareSymbol {
		uint32 mOffset;
		const char *mpWriteName;
		const char *mpReadName;
	};

	static const HardwareSymbol kGTIASymbols[]={
		{ 0x00, "HPOSP0", "M0PF" },
		{ 0x01, "HPOSP1", "M1PF" },
		{ 0x02, "HPOSP2", "M2PF" },
		{ 0x03, "HPOSP3", "M3PF" },
		{ 0x04, "HPOSM0", "P0PF" },
		{ 0x05, "HPOSM1", "P1PF" },
		{ 0x06, "HPOSM2", "P2PF" },
		{ 0x07, "HPOSM3", "P3PF" },
		{ 0x08, "SIZEP0", "M0PL" },
		{ 0x09, "SIZEP1", "M1PL" },
		{ 0x0A, "SIZEP2", "M2PL" },
		{ 0x0B, "SIZEP3", "M3PL" },
		{ 0x0C, "SIZEM", "P0PL" },
		{ 0x0D, "GRAFP0", "P1PL" },
		{ 0x0E, "GRAFP1", "P2PL" },
		{ 0x0F, "GRAFP2", "P3PL" },
		{ 0x10, "GRAFP3", "TRIG0" },
		{ 0x11, "GRAFM", "TRIG1" },
		{ 0x12, "COLPM0", "TRIG2" },
		{ 0x13, "COLPM1", "TRIG3" },
		{ 0x14, "COLPM2", "PAL" },
		{ 0x15, "COLPM3", NULL },
		{ 0x16, "COLPF0" },
		{ 0x17, "COLPF1" },
		{ 0x18, "COLPF2" },
		{ 0x19, "COLPF3" },
		{ 0x1A, "COLBK" },
		{ 0x1B, "PRIOR" },
		{ 0x1C, "VDELAY" },
		{ 0x1D, "GRACTL" },
		{ 0x1E, "HITCLR" },
		{ 0x1F, "CONSOL", "CONSOL" },
	};

	static const HardwareSymbol kPOKEYSymbols[]={
		{ 0x00, "AUDF1", "POT0" },
		{ 0x01, "AUDC1", "POT1" },
		{ 0x02, "AUDF2", "POT2" },
		{ 0x03, "AUDC2", "POT3" },
		{ 0x04, "AUDF3", "POT4" },
		{ 0x05, "AUDC3", "POT5" },
		{ 0x06, "AUDF4", "POT6" },
		{ 0x07, "AUDC4", "POT7" },
		{ 0x08, "AUDCTL", "ALLPOT" },
		{ 0x09, "STIMER", "KBCODE" },
		{ 0x0A, "SKRES", "RANDOM" },
		{ 0x0B, "POTGO" },
		{ 0x0D, "SEROUT", "SERIN" },
		{ 0x0E, "IRQEN", "IRQST" },
		{ 0x0F, "SKCTL", "SKSTAT" },
	};

	static const HardwareSymbol kPIASymbols[]={
		{ 0x00, "PORTA", "PORTA" },
		{ 0x01, "PORTB", "PORTB" },
		{ 0x02, "PACTL", "PACTL" },
		{ 0x03, "PBCTL", "PBCTL" },
	};

	static const HardwareSymbol kANTICSymbols[]={
		{ 0x00, "DMACTL" },
		{ 0x01, "CHACTL" },
		{ 0x02, "DLISTL" },
		{ 0x03, "DLISTH" },
		{ 0x04, "HSCROL" },
		{ 0x05, "VSCROL" },
		{ 0x07, "PMBASE" },
		{ 0x09, "CHBASE" },
		{ 0x0A, "WSYNC" },
		{ 0x0B, NULL, "VCOUNT" },
		{ 0x0C, NULL, "PENH" },
		{ 0x0D, NULL, "PENV" },
		{ 0x0E, "NMIEN" },
		{ 0x0F, "NMIRES", "NMIST" },
	};

	void AddHardwareSymbols(ATSymbolStore *store, uint32 base, const HardwareSymbol *sym, uint32 n) {
		while(n--) {
			store->AddReadWriteRegisterSymbol(base + sym->mOffset, sym->mpWriteName, sym->mpReadName);
			++sym;
		}
	}

	template<size_t N>
	inline void AddHardwareSymbols(ATSymbolStore *store, uint32 base, const HardwareSymbol (&syms)[N]) {
		AddHardwareSymbols(store, base, syms, N);
	}
}

bool ATCreateDefaultHardwareSymbolStore(IATSymbolStore **ppStore) {
	vdrefptr<ATSymbolStore> symstore(new ATSymbolStore);

	symstore->Init(0xD000, 0x0500);
	AddHardwareSymbols(symstore, 0xD000, kGTIASymbols);
	AddHardwareSymbols(symstore, 0xD200, kPOKEYSymbols);
	AddHardwareSymbols(symstore, 0xD300, kPIASymbols);
	AddHardwareSymbols(symstore, 0xD400, kANTICSymbols);

	*ppStore = symstore.release();
	return true;
}

bool ATCreateDefault5200HardwareSymbolStore(IATSymbolStore **ppStore) {
	vdrefptr<ATSymbolStore> symstore(new ATSymbolStore);

	symstore->Init(0xC000, 0x3000);
	AddHardwareSymbols(symstore, 0xC000, kGTIASymbols);
	AddHardwareSymbols(symstore, 0xE800, kPOKEYSymbols);
	AddHardwareSymbols(symstore, 0xD400, kANTICSymbols);

	*ppStore = symstore.release();
	return true;
}
