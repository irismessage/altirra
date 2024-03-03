//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include "tracecpu.h"
#include "test.h"

DEFINE_TEST(Trace_CPU) {
	vdrefptr traceChannel { new ATTraceChannelCPUHistory(0, 1.0, L"Test", kATDebugDisasmMode_6502, 1, nullptr, false) };
	vdfastvector<ATCPUHistoryEntry> hbuf;

	hbuf.resize(4096);

	for(int i=0; i<4096; ++i) {
		auto& he = hbuf[i];

		he = {};
		he.mCycle = i;
		he.mUnhaltedCycle = i;
	}

	const auto equal = [](const ATCPUHistoryEntry& he1, const ATCPUHistoryEntry& he2) {
		return he1.mCycle == he2.mCycle
			&& he1.mUnhaltedCycle == he2.mUnhaltedCycle;
	};

	traceChannel->BeginEvents();

	for(int i=0; i<4096; ++i) {
		traceChannel->AddEvent(i, hbuf[i]);

		// test partial readback of tail -- note that we can't do an exhaustive
		// test of this or it would be atrociously slow
		int pos = i;
		int inc = 1;

		while(pos > 0) {
			const ATCPUHistoryEntry *he = nullptr;
			traceChannel->StartHistoryIteration(pos, 0);
			AT_TEST_ASSERT(1 == traceChannel->ReadHistoryEvents(&he, 0, 1));
			AT_TEST_ASSERT(he != nullptr);
			AT_TEST_ASSERT(equal(*he, hbuf[pos]));

			pos -= inc;
			++inc;
		}
	}

	traceChannel->EndEvents();

	AT_TEST_ASSERT(traceChannel->GetEventCount() == 4096);

	// test serial reading
	traceChannel->StartHistoryIteration(0, 0);
	for(int i=0; i<4096; ++i) {
		const ATCPUHistoryEntry *he = nullptr;
		AT_TEST_ASSERT(1 == traceChannel->ReadHistoryEvents(&he, i, 1));
		AT_TEST_ASSERT(he != nullptr);
		AT_TEST_ASSERT(equal(*he, hbuf[i]));
	}

	// test random reading using 12-bit LFSR
	uint32 pos = 1;
	for(int i=0; i<4096; ++i) {
		const ATCPUHistoryEntry *he = nullptr;
		AT_TEST_ASSERT(1 == traceChannel->ReadHistoryEvents(&he, pos, 1));
		AT_TEST_ASSERT(he != nullptr);
		AT_TEST_ASSERT(equal(*he, hbuf[pos]));

		pos = (pos >> 1) ^ (pos & 1 ? 0xE08 : 0);
	}

	return 0;
}
