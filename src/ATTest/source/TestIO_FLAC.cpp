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
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/vdalloc.h>
#include <at/atio/audioreader.h>
#include "test.h"

AT_DEFINE_TEST(IO_FLAC) {
	static constexpr const wchar_t *kTestFiles[] = {
		L"../../testdata/flac/silence2-4.flac",
		L"../../testdata/flac/chirp-8.flac",
		L"../../testdata/flac/chirpu8-5.flac",
		L"../../testdata/flac/chirps24-fixed.flac"
	};

	sint16 buf[1024];

	for(const wchar_t *fn : kTestFiles) {
		AT_TEST_TRACEF("testing %ls", fn);

		try {
			VDFileStream fs(fn);
			vdautoptr dec(ATCreateAudioReaderFLAC(fs, true));

			while(dec->ReadStereo16(buf, 512))
				;
		} catch(const MyError& e) {
			throw AssertionException(L"Decoding failed: %ls", e.wc_str());
		}
	}

	return 0;
}

AT_DEFINE_TEST_NONAUTO(IO_FLAC_OfficialTestFiles) {
	static constexpr const wchar_t *kTestFiles[] = {
		L"01 - blocksize 4096.flac",
		L"02 - blocksize 4608.flac",
		L"03 - blocksize 16.flac",
		L"04 - blocksize 192.flac",
		L"05 - blocksize 254.flac",
		L"06 - blocksize 512.flac",
		L"07 - blocksize 725.flac",
		L"08 - blocksize 1000.flac",
		L"09 - blocksize 1937.flac",
		L"10 - blocksize 2304.flac",
		L"11 - partition order 8.flac",
		L"12 - qlp precision 15 bit.flac",
		L"13 - qlp precision 2 bit.flac",
		L"14 - wasted bits.flac",
		L"15 - only verbatim subframes.flac",
		L"16 - partition order 8 containing escaped partitions.flac",
		L"17 - all fixed orders.flac",
		L"18 - precision search.flac",
		L"19 - samplerate 35467Hz.flac",
		L"20 - samplerate 39kHz.flac",
		L"21 - samplerate 22050Hz.flac",
		L"22 - 12 bit per sample.flac",
		L"23 - 8 bit per sample.flac",
		L"24 - variable blocksize file created with flake revision 264.flac",
		L"25 - variable blocksize file created with flake revision 264, modified to create smaller blocks.flac",
		L"26 - variable blocksize file created with CUETools.Flake 2.1.6.flac",
		L"27 - old format variable blocksize file created with Flake 0.11.flac",
		L"28 - high resolution audio, default settings.flac",
		L"29 - high resolution audio, blocksize 16384.flac",
		L"30 - high resolution audio, blocksize 13456.flac",
		L"31 - high resolution audio, using only 32nd order predictors.flac",
		L"32 - high resolution audio, partition order 8 containing escaped partitions.flac",
		L"33 - samplerate 192kHz.flac",
		L"34 - samplerate 192kHz, using only 32nd order predictors.flac",
		L"35 - samplerate 134560Hz.flac",
		L"36 - samplerate 384kHz.flac",
		L"37 - 20 bit per sample.flac",
		L"38 - 3 channels (3.0).flac",
		L"39 - 4 channels (4.0).flac",
		L"40 - 5 channels (5.0).flac",
		L"41 - 6 channels (5.1).flac",
		L"42 - 7 channels (6.1).flac",
		L"43 - 8 channels (7.1).flac",
		L"44 - 8-channel surround, 192kHz, 24 bit, using only 32nd order predictors.flac",
		L"45 - no total number of samples set.flac",
		L"46 - no min-max framesize set.flac",
		L"47 - only STREAMINFO.flac",
		L"48 - Extremely large SEEKTABLE.flac",
		L"49 - Extremely large PADDING.flac",
		L"50 - Extremely large PICTURE.flac",
		L"51 - Extremely large VORBISCOMMENT.flac",
		L"52 - Extremely large APPLICATION.flac",
		L"53 - CUESHEET with very many indexes.flac",
		L"54 - 1000x repeating VORBISCOMMENT.flac",
		L"55 - file 48-53 combined.flac",
		L"56 - JPG PICTURE.flac",
		L"57 - PNG PICTURE.flac",
		L"58 - GIF PICTURE.flac",
		L"59 - AVIF PICTURE.flac",
		L"60 - mono audio.flac",
		L"61 - predictor overflow check, 16-bit.flac",
		L"62 - predictor overflow check, 20-bit.flac",
		L"63 - predictor overflow check, 24-bit.flac",
		L"64 - rice partitions with escape code zero.flac",
	};

	sint16 buf[1024];

	for(const wchar_t *fn : kTestFiles) {
		printf("testing %ls\n", fn);

		try {
			VDFileStream fs(fn);
			vdautoptr dec(ATCreateAudioReaderFLAC(fs, true));

			while(dec->ReadStereo16(buf, 512))
				;
		} catch(const MyError& e) {
			throw AssertionException(L"Decoding failed: %ls", e.wc_str());
		}
	}

	return 0;
}

AT_DEFINE_TEST_NONAUTO(IO_FLAC_OfficialTestFileStress) {
	static constexpr const wchar_t *kTestFiles[] = {
		L"44 - 8-channel surround, 192kHz, 24 bit, using only 32nd order predictors.flac",
	};

	sint16 buf[1024];

	for(;;) {
		for(const wchar_t *fn : kTestFiles) {
			printf("testing %ls\n", fn);

			try {
				VDFileStream fs(fn);
				vdautoptr dec(ATCreateAudioReaderFLAC(fs, true));

				while(dec->ReadStereo16(buf, 512))
					;
			} catch(const MyError& e) {
				throw AssertionException(L"Decoding failed: %ls", e.wc_str());
			}
		}
	}

	return 0;
}
