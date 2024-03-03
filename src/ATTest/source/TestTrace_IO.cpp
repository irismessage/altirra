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
#include "tracefileencoding.h"
#include "test.h"

AT_DEFINE_TEST(Trace_IO) {
	ATTraceFmtRowMask mask;
	mask = ATTraceFmtRowMask(15);
	mask.MarkCount(0, 1);
	AT_TEST_ASSERT(mask.mMask[0] == 0b1);
	mask.MarkCount(2, 1);
	AT_TEST_ASSERT(mask.mMask[0] == 0b101);

	mask = ATTraceFmtRowMask(15);
	mask.MarkCount(0, 2);
	AT_TEST_ASSERT(mask.mMask[0] == 0b11);

	mask = ATTraceFmtRowMask(15);
	mask.MarkCount(1, 1);
	AT_TEST_ASSERT(mask.mMask[0] == 0b10);

	mask = ATTraceFmtRowMask(15);
	mask.MarkCount(2, 7);
	AT_TEST_ASSERT(mask.mMask[0] == 0b111111100);

	mask = ATTraceFmtRowMask(96);
	mask.MarkCount(31, 1);
	AT_TEST_ASSERT(mask.mMask[0] == 0x80000000U);
	AT_TEST_ASSERT(mask.mMask[1] == 0x00000000U);
	AT_TEST_ASSERT(mask.mMask[2] == 0x00000000U);

	mask = ATTraceFmtRowMask(96);
	mask.MarkCount(32, 1);
	AT_TEST_ASSERT(mask.mMask[0] == 0x00000000U);
	AT_TEST_ASSERT(mask.mMask[1] == 0x00000001U);
	AT_TEST_ASSERT(mask.mMask[2] == 0x00000000U);

		mask = ATTraceFmtRowMask(96);
	mask.MarkCount(33, 1);
	AT_TEST_ASSERT(mask.mMask[0] == 0x00000000U);
	AT_TEST_ASSERT(mask.mMask[1] == 0x00000002U);
	AT_TEST_ASSERT(mask.mMask[2] == 0x00000000U);

	mask = ATTraceFmtRowMask(96);
	mask.MarkCount(63, 1);
	AT_TEST_ASSERT(mask.mMask[0] == 0x00000000U);
	AT_TEST_ASSERT(mask.mMask[1] == 0x80000000U);
	AT_TEST_ASSERT(mask.mMask[2] == 0x00000000U);

	mask = ATTraceFmtRowMask(96);
	mask.MarkCount(64, 1);
	AT_TEST_ASSERT(mask.mMask[0] == 0x00000000U);
	AT_TEST_ASSERT(mask.mMask[1] == 0x00000000U);
	AT_TEST_ASSERT(mask.mMask[2] == 0x00000001U);

	mask = ATTraceFmtRowMask(96);
	mask.MarkCount(65, 1);
	AT_TEST_ASSERT(mask.mMask[0] == 0x00000000U);
	AT_TEST_ASSERT(mask.mMask[1] == 0x00000000U);
	AT_TEST_ASSERT(mask.mMask[2] == 0x00000002U);

	mask = ATTraceFmtRowMask(96);
	mask.MarkCount(0, 32);
	AT_TEST_ASSERT(mask.mMask[0] == 0xFFFFFFFFU);
	AT_TEST_ASSERT(mask.mMask[1] == 0x00000000U);
	AT_TEST_ASSERT(mask.mMask[2] == 0x00000000U);

	mask = ATTraceFmtRowMask(96);
	mask.MarkCount(32, 32);
	AT_TEST_ASSERT(mask.mMask[0] == 0x00000000U);
	AT_TEST_ASSERT(mask.mMask[1] == 0xFFFFFFFFU);
	AT_TEST_ASSERT(mask.mMask[2] == 0x00000000U);

	mask = ATTraceFmtRowMask(96);
	mask.MarkCount(31, 32);
	AT_TEST_ASSERT(mask.mMask[0] == 0x80000000U);
	AT_TEST_ASSERT(mask.mMask[1] == 0x7FFFFFFFU);
	AT_TEST_ASSERT(mask.mMask[2] == 0x00000000U);

	mask = ATTraceFmtRowMask(96);
	mask.MarkCount(33, 32);
	AT_TEST_ASSERT(mask.mMask[0] == 0x00000000U);
	AT_TEST_ASSERT(mask.mMask[1] == 0xFFFFFFFEU);
	AT_TEST_ASSERT(mask.mMask[2] == 0x00000001U);

	mask = ATTraceFmtRowMask(96);
	mask.MarkCount(32, 31);
	AT_TEST_ASSERT(mask.mMask[0] == 0x00000000U);
	AT_TEST_ASSERT(mask.mMask[1] == 0x7FFFFFFFU);
	AT_TEST_ASSERT(mask.mMask[2] == 0x00000000U);

	mask = ATTraceFmtRowMask(96);
	mask.MarkCount(33, 31);
	AT_TEST_ASSERT(mask.mMask[0] == 0x00000000U);
	AT_TEST_ASSERT(mask.mMask[1] == 0xFFFFFFFEU);
	AT_TEST_ASSERT(mask.mMask[2] == 0x00000000U);

	mask = ATTraceFmtRowMask(96);
	mask.MarkCount(0, 64);
	AT_TEST_ASSERT(mask.mMask[0] == 0xFFFFFFFFU);
	AT_TEST_ASSERT(mask.mMask[1] == 0xFFFFFFFFU);
	AT_TEST_ASSERT(mask.mMask[2] == 0x00000000U);

	mask = ATTraceFmtRowMask(96);
	mask.MarkCount(16, 32);
	AT_TEST_ASSERT(mask.mMask[0] == 0xFFFF0000U);
	AT_TEST_ASSERT(mask.mMask[1] == 0x0000FFFFU);
	AT_TEST_ASSERT(mask.mMask[2] == 0x00000000U);

	mask = ATTraceFmtRowMask(96);
	mask.MarkCount(0, 1);
	mask.MarkCount(94, 1);
	mask.MarkCount(16, 64);
	AT_TEST_ASSERT(mask.mMask[0] == 0xFFFF0001U);
	AT_TEST_ASSERT(mask.mMask[1] == 0xFFFFFFFFU);
	AT_TEST_ASSERT(mask.mMask[2] == 0x4000FFFFU);

	mask = ATTraceFmtRowMask(32);
	auto mask2 = ATTraceFmtRowMask(32);

	mask.MarkCount(0, 1);
	mask2.MarkCount(2, 1);
	AT_TEST_ASSERT(!mask.Overlaps(mask2));
	mask.Merge(mask2);
	AT_TEST_ASSERT(mask.mMask[0] == 0x00000005U);
	AT_TEST_ASSERT(mask.Overlaps(mask2));

	return 0;
}
