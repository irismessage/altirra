//	Altirra - Atari 800/800XL/5200 emulator
//	Test module
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
#include <at/atio/cassetteimage.h>
#include "test.h"

AT_DEFINE_TEST(IO_TapeWrite) {
	vdrefptr<IATCassetteImage> tape;
	ATCreateNewCassetteImage(~tape);

	AT_TEST_ASSERT(tape->GetDataLength() == 0);

	ATCassetteWriteCursor writeCursor;
	uint32 bitSum;

	tape->WriteBlankData(writeCursor, 100, false);
	AT_TEST_ASSERT(writeCursor.mPosition == 100);
	AT_TEST_ASSERT(tape->GetDataLength() == 100);
	bitSum = tape->GetBitSum(0, 100, false); AT_TEST_ASSERT(bitSum == 100);

	tape->WriteBlankData(writeCursor, 100, false);
	AT_TEST_ASSERT(writeCursor.mPosition == 200);
	AT_TEST_ASSERT(tape->GetDataLength() == 200);
	bitSum = tape->GetBitSum(0, 200, false); AT_TEST_ASSERT(bitSum == 200);

	writeCursor.mPosition = 400;
	tape->WriteBlankData(writeCursor, 100, false);
	AT_TEST_ASSERT(writeCursor.mPosition == 500);
	AT_TEST_ASSERT(tape->GetDataLength() == 500);
	bitSum = tape->GetBitSum(0, 500, false); AT_TEST_ASSERT(bitSum == 500);

	writeCursor.mPosition = 300;
	tape->WriteBlankData(writeCursor, 100, false);
	AT_TEST_ASSERT(writeCursor.mPosition == 400);
	AT_TEST_ASSERT(tape->GetDataLength() == 500);
	bitSum = tape->GetBitSum(0, 500, false); AT_TEST_ASSERT(bitSum == 500);

	writeCursor.mPosition = 150;
	tape->WritePulse(writeCursor, false, 100, false, true);
	AT_TEST_ASSERT(writeCursor.mPosition == 250);
	AT_TEST_ASSERT(tape->GetDataLength() == 500);
	bitSum = tape->GetBitSum(0, 500, false); AT_TEST_ASSERT(bitSum == 400);

	writeCursor.mPosition = 50;
	tape->WriteStdData(writeCursor, 0xAA, 600, false);
	constexpr uint32 kByteLen600 = (uint32)(kATCassetteDataSampleRate / 60.0f + 0.5f);
	AT_TEST_ASSERT(writeCursor.mPosition >= kByteLen600 + 49 && writeCursor.mPosition <= kByteLen600 + 51);
	AT_TEST_ASSERT(tape->GetDataLength() == writeCursor.mPosition);
	constexpr uint32 kApproxExpectedBitSum600 = kByteLen600 / 2 + 50;
	bitSum = tape->GetBitSum(0, 50, false); AT_TEST_ASSERT(bitSum == 50);
	bitSum = tape->GetBitSum(50, writeCursor.mPosition - 50, false); AT_TEST_ASSERT(bitSum >= kByteLen600 / 2 - 2 && bitSum <= kByteLen600 / 2 + 2);

	return 0;
}

AT_DEFINE_TEST_NONAUTO(IO_TapeWriteStress) {
	vdrefptr<IATCassetteImage> tape;
	ATCreateNewCassetteImage(~tape);

	srand(1);

	ATCassetteWriteCursor writeCursor;

	printf("Stress testing tape...\n");

	vdrefptr<IATTapeImageClip> clip;

	for(int i=0; i<100; ++i) {
		for(int j=0; j<10000; ++j) {
			const uint32 tapeLength0 = tape->GetDataLength();

			writeCursor.mPosition = ((uint32)rand() + ((uint32)rand() << 15)) % (tapeLength0 + 65536);

			const uint32 pos0 = writeCursor.mPosition;
			uint32 len;
			uint32 baud;
			uint32 estLen;
			uint32 expectedLen;
			uint32 actualLen;

			uint32 chkpos = std::max<uint32>(pos0, 43) - 43;
			uint32 chklen = pos0 - chkpos;
			uint32 chkval = tape->GetBitSum(chkpos, chklen, false);

			uint32 chk2pos;
			uint32 chk2len;
			uint32 chk2val;

			uint32 chk3val;
			uint32 chk3len;
			bool polarity;

			switch(rand() % 10) {
				case 0:
					len = rand();
					chk2pos = std::min<uint32>(pos0 + len, tapeLength0);
					chk2len = std::min<uint32>(43, tapeLength0 - chk2pos);
					chk2val = tape->GetBitSum(chk2pos, chk2len, false);
					tape->WriteBlankData(writeCursor, len, false);
					AT_TEST_ASSERT(writeCursor.mPosition == pos0 + len);
					AT_TEST_ASSERT(tape->GetDataLength() == std::max<uint32>(pos0 + len, tapeLength0));
					AT_TEST_ASSERT(chk2val == tape->GetBitSum(chk2pos, chk2len, false));
					break;

				case 4:
					len = rand();
					chk2pos = std::min<uint32>(pos0 + len, tapeLength0);
					chk2len = std::min<uint32>(43, tapeLength0 - chk2pos);
					chk2val = tape->GetBitSum(chk2pos, chk2len, false);
					tape->WriteBlankData(writeCursor, len, true);
					AT_TEST_ASSERT(writeCursor.mPosition == pos0 + len);
					AT_TEST_ASSERT(tape->GetDataLength() == std::max<uint32>(pos0, tapeLength0) + len);
					AT_TEST_ASSERT(chk2val == tape->GetBitSum(chk2pos + len, chk2len, false));
					break;

				case 1:
					len = rand();
					chk2pos = std::min<uint32>(pos0 + len, tapeLength0);
					chk2len = std::min<uint32>(43, tapeLength0 - chk2pos);
					chk2val = tape->GetBitSum(chk2pos, chk2len, false);
					polarity = (rand() & 1) != 0;
					tape->WritePulse(writeCursor, polarity, len, false, true);
					AT_TEST_ASSERT(writeCursor.mPosition == pos0 + len);
					AT_TEST_ASSERT(tape->GetDataLength() == std::max<uint32>(pos0 + len, tapeLength0));
					AT_TEST_ASSERT(tape->GetBitSum(pos0, len, false) == (polarity ? len : 0));
					AT_TEST_ASSERT(chk2val == tape->GetBitSum(chk2pos, chk2len, false));
					break;

				case 7:
					len = rand() & 31;
					chk2pos = std::min<uint32>(pos0 + len, tapeLength0);
					chk2len = std::min<uint32>(43, tapeLength0 - chk2pos);
					chk2val = tape->GetBitSum(chk2pos, chk2len, false);
					polarity = (rand() & 1) != 0;
					tape->WritePulse(writeCursor, polarity, len, false, true);
					AT_TEST_ASSERT(writeCursor.mPosition == pos0 + len);
					AT_TEST_ASSERT(tape->GetDataLength() == std::max<uint32>(pos0 + len, tapeLength0));
					AT_TEST_ASSERT(tape->GetBitSum(pos0, len, false) == (polarity ? len : 0));
					AT_TEST_ASSERT(chk2val == tape->GetBitSum(chk2pos, chk2len, false));
					break;

				case 5:
					len = rand();
					chk2pos = std::min<uint32>(pos0 + len, tapeLength0);
					chk2len = std::min<uint32>(43, tapeLength0 - chk2pos);
					chk2val = tape->GetBitSum(chk2pos, chk2len, false);
					polarity = (rand() & 1) != 0;
					tape->WritePulse(writeCursor, polarity, len, true, true);
					AT_TEST_ASSERT(writeCursor.mPosition == pos0 + len);
					AT_TEST_ASSERT(tape->GetDataLength() == std::max<uint32>(pos0, tapeLength0) + len);
					AT_TEST_ASSERT(tape->GetBitSum(pos0, len, false) == (polarity ? len : 0));
					AT_TEST_ASSERT(chk2val == tape->GetBitSum(chk2pos + len, chk2len, false));
					break;

				case 2:
					baud = rand() % 600 + 300;
					estLen = (uint32)(0.5f + kATCassetteDataSampleRate * 10.0f / (float)baud);
					tape->WriteStdData(writeCursor, rand() & 255, baud, false);
					expectedLen = std::max<uint32>(pos0 + estLen, tapeLength0);
					actualLen = tape->GetDataLength();
					AT_TEST_ASSERT(actualLen >= expectedLen - 1 && actualLen <= expectedLen + 1);
					break;

				case 6:
					baud = rand() % 600 + 300;
					estLen = (uint32)(0.5f + kATCassetteDataSampleRate * 10.0f / (float)baud);
					tape->WriteStdData(writeCursor, rand() & 255, baud, true);
					expectedLen = std::max<uint32>(tapeLength0, pos0) + estLen;
					actualLen = tape->GetDataLength();
					AT_TEST_ASSERT(actualLen >= expectedLen - 1 && actualLen <= expectedLen + 1);
					break;

				case 3:
					len = rand() + rand()*3;
					chk2pos = std::min<uint32>(pos0 + len, tapeLength0);
					chk2len = std::min<uint32>(43, tapeLength0 - chk2pos);
					chk2val = tape->GetBitSum(chk2pos, chk2len, false);
					tape->DeleteRange(pos0, pos0 + len);
					AT_TEST_ASSERT(tape->GetDataLength() == std::min<uint32>(pos0, tapeLength0) + (tapeLength0 - std::min<uint32>(pos0 + len, tapeLength0)));
					AT_TEST_ASSERT(chk2val == tape->GetBitSum(pos0, chk2len, false));
					break;

				case 8: {
					len = rand();
					uint32 pos2 = ((uint32)rand() + ((uint32)rand() << 15)) % (tapeLength0 + 65536);
					chk2pos = std::min<uint32>(pos0 + len, tapeLength0);
					chk2len = std::min<uint32>(43, tapeLength0 - chk2pos);
					chk2val = tape->GetBitSum(chk2pos, chk2len, false);
					chk3len = std::min<uint32>(len, std::max(tapeLength0, pos2) - pos2);
					chk3val = tape->GetBitSum(pos2, chk3len, false);

					tape->ReplaceRange(writeCursor.mPosition, *tape->CopyRange(pos2, len));
					AT_TEST_ASSERT(tape->GetDataLength() == std::max<uint32>(pos0 + chk3len, tapeLength0));
					AT_TEST_ASSERT(chk2val == tape->GetBitSum(chk2pos, chk2len, false));
					AT_TEST_ASSERT(chk3val == tape->GetBitSum(pos0, chk3len, false));
					break;
				}

				case 9: {
					len = rand();
					uint32 pos2 = ((uint32)rand() + ((uint32)rand() << 15)) % (tapeLength0 + 65536);
					chk2pos = std::min<uint32>(pos0, tapeLength0);
					chk2len = std::min<uint32>(43, tapeLength0 - chk2pos);
					chk2val = tape->GetBitSum(chk2pos, chk2len, false);
					chk3len = std::min<uint32>(len, std::max(tapeLength0, pos2) - pos2);
					chk3val = tape->GetBitSum(pos2, chk3len, false);

					auto clip = tape->CopyRange(pos2, len);
					tape->InsertRange(writeCursor.mPosition, *clip);
					AT_TEST_ASSERT(tape->GetDataLength() == std::max(pos0, tapeLength0) + chk3len);
					AT_TEST_ASSERT(chk2val == tape->GetBitSum(chk2pos + chk3len, chk2len, false));
					AT_TEST_ASSERT(chk3val == tape->GetBitSum(pos0, chk3len, false));
					break;
				}
			}

			TEST_ASSERT(chkval == tape->GetBitSum(chkpos, chklen, false));
		}

		printf("...%d%% %u (%.1fs)\n", i, tape->GetDataLength(), (float)tape->GetDataLength() / kATCassetteDataSampleRate);
	}

	return 0;
}
