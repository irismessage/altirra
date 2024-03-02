#ifndef AT_KERNELDB_H
#define AT_KERNELDB_H

#include "cpu.h"
#include "ksyms.h"

struct ATMemoryAdapter {
	ATCPUEmulatorMemory *mpMem;
};

struct ATByteVAdapter {
public:
	ATByteVAdapter(ATCPUEmulatorMemory *mem, uint16 addr) : mpMem(mem), mAddress(addr) {}

	operator uint8() const {
		return mpMem->ReadByte(mAddress);
	}

	void operator=(uint8 v) {
		mpMem->WriteByte(mAddress, v);
	}

	uint8 operator&=(uint8 mask) {
		uint8 c = mpMem->ReadByte(mAddress) & mask;
		mpMem->WriteByte(mAddress, c);
		return c;
	}

	uint8 operator|=(uint8 mask) {
		uint8 c = mpMem->ReadByte(mAddress) | mask;
		mpMem->WriteByte(mAddress, c);
		return c;
	}

	uint16 r16() const {
		return (uint16)((uint32)mpMem->ReadByte(mAddress) + 256*(uint32)mpMem->ReadByte(mAddress + 1));
	}

	void w16(uint16 v) {
		uint8 b0 = v & 0xff;
		uint8 b1 = v >> 8;

		mpMem->WriteByte(mAddress, b0);
		mpMem->WriteByte(mAddress+1, b1);
	}

private:
	ATCPUEmulatorMemory *mpMem;
	uint16 mAddress;
};

template<uint16 kAddress>
struct ATByteAdapter {
public:
	operator uint8() const {
		return mpMem->ReadByte(kAddress);
	}

	void operator=(uint8 v) {
		mpMem->WriteByte(kAddress, v);
	}

	uint8 operator++() {
		uint8 c = mpMem->ReadByte(kAddress) + 1;
		mpMem->WriteByte(kAddress, c);
		return c;
	}

	uint8 operator--() {
		uint8 c = mpMem->ReadByte(kAddress) - 1;
		mpMem->WriteByte(kAddress, c);
		return c;
	}

	ATByteVAdapter operator[](uint16 offset) const {
		return ATByteVAdapter(mpMem, kAddress + offset);
	}

	uint8 operator&=(uint8 mask) {
		uint8 c = mpMem->ReadByte(kAddress) & mask;
		mpMem->WriteByte(kAddress, c);
		return c;
	}

private:
	ATCPUEmulatorMemory *mpMem;
};

template<uint16 kAddress>
struct ATWordAdapter {
public:
	operator uint16() const {
		return mpMem->ReadByte(kAddress) + ((uint16)mpMem->ReadByte(kAddress + 1) << 8);
	}

	void operator=(uint16 v) {
		mpMem->WriteByte(kAddress, (uint8)v);
		mpMem->WriteByte(kAddress + 1, (uint8)(v >> 8));
	}

	uint16 operator++() {
		uint8 lo = mpMem->ReadByte(kAddress);
		uint8 hi = mpMem->ReadByte(kAddress + 1);
		mpMem->WriteByte(kAddress, ++lo);

		if (!lo)
			mpMem->WriteByte(kAddress + 1, ++hi);

		return (uint16)(lo + ((uint32)hi << 8));
	}

	uint16 operator--() {
		uint8 lo = mpMem->ReadByte(kAddress);
		uint8 hi = mpMem->ReadByte(kAddress + 1);

		mpMem->WriteByte(kAddress, --lo);

		if (lo == 0xFF)
			mpMem->WriteByte(kAddress + 1, --hi);

		return (uint16)(lo + ((uint32)hi << 8));
	}

private:
	ATCPUEmulatorMemory *mpMem;
};

struct ATKernelDatabase {
	ATKernelDatabase(ATCPUEmulatorMemory *mem) {
		mAdapter.mpMem = mem;
	}

	union {
		ATMemoryAdapter mAdapter;

		// page zero
		ATByteAdapter<ATKernelSymbols::TRAMSZ> TRAMSZ;
		ATByteAdapter<ATKernelSymbols::WARMST> WARMST;
		ATWordAdapter<ATKernelSymbols::DOSVEC> DOSVEC;
		ATWordAdapter<ATKernelSymbols::DOSINI> DOSINI;
		ATByteAdapter<ATKernelSymbols::POKMSK> POKMSK;
		ATByteAdapter<ATKernelSymbols::BRKKEY> BRKKEY;
		ATByteAdapter<ATKernelSymbols::RTCLOK> RTCLOK;
		ATByteAdapter<ATKernelSymbols::ICHIDZ> ICHIDZ;
		ATByteAdapter<ATKernelSymbols::ICDNOZ> ICDNOZ;
		ATByteAdapter<ATKernelSymbols::ICCOMZ> ICCOMZ;
		ATByteAdapter<ATKernelSymbols::ICBALZ> ICBALZ;
		ATByteAdapter<ATKernelSymbols::ICBAHZ> ICBAHZ;
		ATWordAdapter<ATKernelSymbols::ICBALZ> ICBAZ;
		ATByteAdapter<ATKernelSymbols::ICAX1Z> ICAX1Z;
		ATByteAdapter<ATKernelSymbols::ICAX2Z> ICAX2Z;
		ATByteAdapter<ATKernelSymbols::ICAX3Z> ICAX3Z;
		ATByteAdapter<ATKernelSymbols::ICAX4Z> ICAX4Z;
		ATByteAdapter<ATKernelSymbols::ICAX5Z> ICAX5Z;
		ATByteAdapter<ATKernelSymbols::ICIDNO> ICIDNO;
		ATByteAdapter<ATKernelSymbols::CIOCHR> CIOCHR;
		ATByteAdapter<ATKernelSymbols::STATUS> STATUS;
		ATByteAdapter<ATKernelSymbols::CHKSUM> CHKSUM;
		ATByteAdapter<ATKernelSymbols::BUFRLO> BUFRLO;
		ATByteAdapter<ATKernelSymbols::BUFRHI> BUFRHI;
		ATByteAdapter<ATKernelSymbols::BFENLO> BFENLO;
		ATByteAdapter<ATKernelSymbols::BFENHI> BFENHI;
		ATByteAdapter<ATKernelSymbols::BUFRFL> BUFRFL;
		ATByteAdapter<ATKernelSymbols::CHKSNT> CHKSNT;
		ATByteAdapter<ATKernelSymbols::BPTR  > BPTR  ;
		ATByteAdapter<ATKernelSymbols::FTYPE > FTYPE ;
		ATByteAdapter<ATKernelSymbols::FEOF  > FEOF  ;
		ATByteAdapter<ATKernelSymbols::CRITIC> CRITIC;
		ATByteAdapter<ATKernelSymbols::ATRACT> ATRACT;
		ATByteAdapter<ATKernelSymbols::DRKMSK> DRKMSK;
		ATByteAdapter<ATKernelSymbols::COLRSH> COLRSH;
		ATByteAdapter<ATKernelSymbols::LMARGN> LMARGN;
		ATByteAdapter<ATKernelSymbols::RMARGN> RMARGN;
		ATByteAdapter<ATKernelSymbols::ROWCRS> ROWCRS;
		ATWordAdapter<ATKernelSymbols::COLCRS> COLCRS;
		ATByteAdapter<ATKernelSymbols::DINDEX> DINDEX;
		ATWordAdapter<ATKernelSymbols::SAVMSC> SAVMSC;
		ATByteAdapter<ATKernelSymbols::OLDROW> OLDROW;
		ATWordAdapter<ATKernelSymbols::OLDCOL> OLDCOL;
		ATByteAdapter<ATKernelSymbols::OLDCHR> OLDCHR;
		ATWordAdapter<ATKernelSymbols::OLDADR> OLDADR;
		ATByteAdapter<ATKernelSymbols::LOGCOL> LOGCOL;
		ATByteAdapter<ATKernelSymbols::RAMTOP> RAMTOP;
		ATByteAdapter<ATKernelSymbols::BUFCNT> BUFCNT;
		ATWordAdapter<ATKernelSymbols::BUFADR> BUFADR;
		ATByteAdapter<ATKernelSymbols::SWPFLG> SWPFLG;
		ATWordAdapter<ATKernelSymbols::RAMLO > RAMLO ;
		ATByteAdapter<ATKernelSymbols::CIX   > CIX   ;
		ATWordAdapter<ATKernelSymbols::INBUFF> INBUFF;
		ATWordAdapter<ATKernelSymbols::FLPTR > FLPTR ;

		ATWordAdapter<ATKernelSymbols::VDSLST> VDSLST;
		ATWordAdapter<ATKernelSymbols::VPRCED> VPRCED;
		ATWordAdapter<ATKernelSymbols::VINTER> VINTER;
		ATWordAdapter<ATKernelSymbols::VBREAK> VBREAK;
		ATWordAdapter<ATKernelSymbols::VKEYBD> VKEYBD;
		ATWordAdapter<ATKernelSymbols::VSERIN> VSERIN;
		ATWordAdapter<ATKernelSymbols::VSEROR> VSEROR;
		ATWordAdapter<ATKernelSymbols::VSEROC> VSEROC;
		ATWordAdapter<ATKernelSymbols::VTIMR1> VTIMR1;
		ATWordAdapter<ATKernelSymbols::VTIMR2> VTIMR2;
		ATWordAdapter<ATKernelSymbols::VTIMR4> VTIMR4;
		ATWordAdapter<ATKernelSymbols::VIMIRQ> VIMIRQ;
		ATWordAdapter<ATKernelSymbols::CDTMV1> CDTMV1;
		ATWordAdapter<ATKernelSymbols::CDTMV2> CDTMV2;
		ATWordAdapter<ATKernelSymbols::CDTMV3> CDTMV3;
		ATWordAdapter<ATKernelSymbols::CDTMV4> CDTMV4;
		ATWordAdapter<ATKernelSymbols::CDTMV5> CDTMV5;
		ATWordAdapter<ATKernelSymbols::VVBLKI> VVBLKI;
		ATWordAdapter<ATKernelSymbols::VVBLKD> VVBLKD;
		ATWordAdapter<ATKernelSymbols::CDTMA1> CDTMA1;
		ATWordAdapter<ATKernelSymbols::CDTMA2> CDTMA2;
		ATWordAdapter<ATKernelSymbols::CDTMF3> CDTMF3;
		ATWordAdapter<ATKernelSymbols::CDTMF4> CDTMF4;
		ATWordAdapter<ATKernelSymbols::CDTMF5> CDTMF5;
		ATByteAdapter<ATKernelSymbols::SDMCTL> SDMCTL;
		ATByteAdapter<ATKernelSymbols::SDLSTL> SDLSTL;
		ATByteAdapter<ATKernelSymbols::SDLSTH> SDLSTH;
		ATByteAdapter<ATKernelSymbols::COLDST> COLDST;
		ATByteAdapter<ATKernelSymbols::GPRIOR> GPRIOR;
		ATByteAdapter<ATKernelSymbols::JVECK > JVECK ;
		ATByteAdapter<ATKernelSymbols::WMODE > WMODE ;
		ATByteAdapter<ATKernelSymbols::BLIM  > BLIM  ;
		ATByteAdapter<ATKernelSymbols::TXTROW> TXTROW;
		ATWordAdapter<ATKernelSymbols::TXTCOL> TXTCOL;
		ATByteAdapter<ATKernelSymbols::TINDEX> TINDEX;
		ATWordAdapter<ATKernelSymbols::TXTMSC> TXTMSC;
		ATByteAdapter<ATKernelSymbols::TXTOLD> TXTOLD;
		ATByteAdapter<ATKernelSymbols::LOGMAP> LOGMAP;
		ATByteAdapter<ATKernelSymbols::BOTSCR> BOTSCR;
		ATByteAdapter<ATKernelSymbols::PCOLR0> PCOLR0;
		ATByteAdapter<ATKernelSymbols::PCOLR1> PCOLR1;
		ATByteAdapter<ATKernelSymbols::PCOLR2> PCOLR2;
		ATByteAdapter<ATKernelSymbols::PCOLR3> PCOLR3;
		ATByteAdapter<ATKernelSymbols::COLOR0> COLOR0;
		ATByteAdapter<ATKernelSymbols::COLOR1> COLOR1;
		ATByteAdapter<ATKernelSymbols::COLOR2> COLOR2;
		ATByteAdapter<ATKernelSymbols::COLOR3> COLOR3;
		ATByteAdapter<ATKernelSymbols::COLOR4> COLOR4;
		ATWordAdapter<ATKernelSymbols::MEMTOP> MEMTOP;
		ATByteAdapter<ATKernelSymbols::MEMLO > MEMLO;
		ATByteAdapter<ATKernelSymbols::CHACT > CHACT;
		ATByteAdapter<ATKernelSymbols::CHBAS > CHBAS;
		ATByteAdapter<ATKernelSymbols::DSPFLG> DSPFLG;
		ATByteAdapter<ATKernelSymbols::DDEVIC> DDEVIC;
		ATByteAdapter<ATKernelSymbols::DUNIT > DUNIT;
		ATByteAdapter<ATKernelSymbols::HATABS> HATABS;
		ATByteAdapter<ATKernelSymbols::ICCMD > ICCMD;
		ATByteAdapter<ATKernelSymbols::ICSTA > ICSTA;
		ATByteAdapter<ATKernelSymbols::ICBAL > ICBAL;
		ATByteAdapter<ATKernelSymbols::ICBLL > ICBLL;

		ATByteAdapter<ATKernelSymbols::COLPM0> COLPM0;
		ATByteAdapter<ATKernelSymbols::COLPM1> COLPM1;
		ATByteAdapter<ATKernelSymbols::COLPM2> COLPM2;
		ATByteAdapter<ATKernelSymbols::COLPM3> COLPM3;
		ATByteAdapter<ATKernelSymbols::COLPF0> COLPF0;
		ATByteAdapter<ATKernelSymbols::COLPF1> COLPF1;
		ATByteAdapter<ATKernelSymbols::COLPF2> COLPF2;
		ATByteAdapter<ATKernelSymbols::COLPF3> COLPF3;
		ATByteAdapter<ATKernelSymbols::COLBK > COLBK ;
		ATByteAdapter<ATKernelSymbols::PRIOR > PRIOR ;
		ATByteAdapter<ATKernelSymbols::CONSOL> CONSOL;
		ATByteAdapter<ATKernelSymbols::IRQST > IRQST ;
		ATByteAdapter<ATKernelSymbols::IRQEN > IRQEN ;

		// POKEY (D2xx)
		ATByteAdapter<ATKernelSymbols::AUDC1 > AUDC1 ;
		ATByteAdapter<ATKernelSymbols::AUDF1 > AUDF1 ;
		ATByteAdapter<ATKernelSymbols::AUDCTL> AUDCTL;
		ATByteAdapter<ATKernelSymbols::SKCTL > SKCTL ;

		// PIAs (D3xx)
		ATByteAdapter<ATKernelSymbols::PORTA > PORTA ;
		ATByteAdapter<ATKernelSymbols::PORTB > PORTB ;
		ATByteAdapter<ATKernelSymbols::PACTL > PACTL ;
		ATByteAdapter<ATKernelSymbols::PBCTL > PBCTL ;

		// ANTIC (D4xx)
		ATByteAdapter<ATKernelSymbols::DMACTL> DMACTL;
		ATByteAdapter<ATKernelSymbols::CHACTL> CHACTL;
		ATByteAdapter<ATKernelSymbols::DLISTL> DLISTL;
		ATByteAdapter<ATKernelSymbols::DLISTH> DLISTH;
		ATByteAdapter<ATKernelSymbols::CHBASE> CHBASE;
		ATByteAdapter<ATKernelSymbols::NMIEN > NMIEN ;
		ATByteAdapter<ATKernelSymbols::NMIRES> NMIRES;
	};
};

#endif
