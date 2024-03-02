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
#include <vd2/system/file.h>
#include <vd2/system/strutil.h>
#include <vd2/system/vdalloc.h>
#include <at/atcore/address.h>
#include <at/atdebugger/symbols.h>
#include "test.h"

DEFINE_TEST(Debugger_SymbolIO) {
	{
		static constexpr char kMADSTest1Lst[] = R"--(mads 2.1.0 build 8 (23 Dec 19)
Source: bank.s
     1 						org		$2000
     2 02,2000					lmb		#2
     3 						org		$2000
     4 FFFF> 02,2000-2003> 20 + 		jsr		foo
     5 = 02,2003				dta		=*
     6 02,2003 02				dta		[=*]
     7
     8
     9 03,2004					lmb		#3
    10 03,2004					org		$2000
    11 03,2000			.proc foo
    12 03,2000-2000> 60				rts
    13 				.endp
    14
    15 						.echo =foo
    15 				$0003
)--";

		vdrefptr<IATSymbolStore> symstore;
		VDMemoryStream stream(kMADSTest1Lst, sizeof(kMADSTest1Lst) - 1);
		ATLoadSymbols(L"mads_test1.lst", stream, ~symstore);

		ATSourceLineInfo lineInfo;
		TEST_ASSERT(!symstore->GetLineForOffset(0x002000, false, lineInfo));
		TEST_ASSERT(!symstore->GetLineForOffset(0x022002, false, lineInfo));
		TEST_ASSERT( symstore->GetLineForOffset(0x022003, false, lineInfo) && lineInfo.mLine == 6);
		TEST_ASSERT( symstore->GetLineForOffset(0x022004, false, lineInfo) && lineInfo.mLine == 6);
		TEST_ASSERT( symstore->GetLineForOffset(0x032000, false, lineInfo) && lineInfo.mLine == 12);
		TEST_ASSERT( symstore->GetLineForOffset(0x032001, false, lineInfo) && lineInfo.mLine == 12);
	}

	{
		static constexpr char kMADSTest1Lab[] = R"--(mads 2.1.0 build 8 (23 Dec 19)
Label table:
02	2003	DTA
03	2000	FOO
)--";

		vdrefptr<IATSymbolStore> symstore;
		VDMemoryStream stream(kMADSTest1Lab, sizeof(kMADSTest1Lab) - 1);
		ATLoadSymbols(L"mads_test1.lab", stream, ~symstore);

		ATSymbol symbol;
		TEST_ASSERT(symstore->LookupSymbol(0x022003, kATSymbol_Read, symbol) && !vdstricmp(symbol.mpName, "dta"));
		TEST_ASSERT(symstore->LookupSymbol(0x032000, kATSymbol_Read, symbol) && !vdstricmp(symbol.mpName, "foo"));
	}

	{
		static constexpr char kMADSTest2[] = R"--(mads 2.1.0 build 8 (23 Dec 19)
Source: bank2.s
     1 						;##BANK 2 cart $F8
     2 						;##BANK 3 cart $F9
     3 						org		$A000
     4 02,A000					lmb		#2
     5 						org		$A000
     6 FFFF> 02,A000-A003> 20 + 		jsr		foo
     7 = 02,A003				dta		=*
     8 02,A003 02				dta		[=*]
     9
    10
    11 03,A004					lmb		#3
    12 03,A004					org		$A000
    13 03,A000			.proc foo
    14 03,A000-A000> 60				rts
    15 				.endp
)--";

		vdrefptr<IATSymbolStore> symstore;
		VDMemoryStream stream(kMADSTest2, sizeof(kMADSTest2) - 1);
		ATLoadSymbols(L"mads_test2.lst", stream, ~symstore);

		ATSourceLineInfo lineInfo;
		TEST_ASSERT( symstore->GetLineForOffset(kATAddressSpace_CB + 0xF8A003, false, lineInfo) && lineInfo.mLine == 8);
		TEST_ASSERT( symstore->GetLineForOffset(kATAddressSpace_CB + 0xF9A000, false, lineInfo) && lineInfo.mLine == 14);
	}

	{
		static constexpr char kMADSTest3Lab[] = R"--(mads 2.1.0 build 8 (23 Dec 19)
Label table:
00	00F8	__ATBANK_02_CART
00	00F9	__ATBANK_03_CART
02	A003	DTA
03	A000	FOO
)--";

		vdrefptr<IATSymbolStore> symstore;
		VDMemoryStream stream(kMADSTest3Lab, sizeof(kMADSTest3Lab) - 1);
		ATLoadSymbols(L"mads_test3.lab", stream, ~symstore);

		ATSymbol symbol;
		TEST_ASSERT(symstore->LookupSymbol(kATAddressSpace_CB + 0xF8A003, kATSymbol_Read, symbol) && !vdstricmp(symbol.mpName, "dta"));
		TEST_ASSERT(symstore->LookupSymbol(kATAddressSpace_CB + 0xF9A000, kATSymbol_Read, symbol) && !vdstricmp(symbol.mpName, "foo"));
	}

	return 0;
}
