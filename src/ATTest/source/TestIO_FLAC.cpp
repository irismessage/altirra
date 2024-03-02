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

DEFINE_TEST(IO_FLAC) {
	static constexpr const wchar_t *kTestFiles[] = {
		L"../../testdata/flac/silence2-4.flac",
		L"../../testdata/flac/chirp-8.flac",
		L"../../testdata/flac/chirpu8-5.flac",
		L"../../testdata/flac/chirps24-fixed.flac"
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
			throw AssertionException("Decoding failed: %s", e.c_str());
		}
	}

	return 0;
}
