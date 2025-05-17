//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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
#include <vd2/system/time.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/zip.h>
#include <test.h>

namespace {
	class TempStream final : public IVDStream {
	public:
		const wchar_t *GetNameForError() { return L""; }
		sint64	Pos() { return mPos; }

		void	Read(void *buffer, sint32 bytes) {
			if (ReadData(buffer, bytes) != bytes)
				throw MyError("unexpected EOF");
		}

		sint32	ReadData(void *buffer, sint32 bytes) {
			if (bytes <= 0)
				return 0;

			size_t avail = mBuf.size() - mPos;
			if ((size_t)bytes > avail)
				bytes = (sint32)avail;

			memcpy(buffer, mBuf.data() + mPos, bytes);
			mPos += bytes;

			return bytes;
		}

		void Write(const void *buffer, sint32 bytes) {
			if (bytes <= 0)
				return;

			size_t lim = mPos + (size_t)bytes;
			if (lim < mPos)
				throw MyError("stream overflow");

			if (mBuf.size() < lim)
				mBuf.resize(lim);

			memcpy(mBuf.data() + mPos, buffer, bytes);
			mPos += (size_t)bytes;
		}

		void Reset() { mPos = 0; }

		vdfastvector<char> mBuf;
		size_t mPos = 0;
	};
}

DEFINE_TEST_NONAUTO(System_Zip) {
	vdblock<char> buf(65536);
	vdblock<char> buf2(65536);

	int iterations = 0;
	for(;;) {
		int rle = 0;
		char rlec;
		for(int i=0; i<65536; ++i) {
			if (!rle--) {
				rle = (rand() & 511) + 1;
				rlec = (char)rand();
			}

			buf[i] = rlec;
		}

		TempStream ms;

		{
			VDDeflateStream ds(ms);
			ds.Write(buf.data(), 65536);
			ds.Finalize();
		}

		ms.Reset();

		vdautoptr is(new VDInflateStream<false>);
		is->Init(&ms, ms.mBuf.size(), false);

		is->Read(buf2.data(), buf2.size());

		TEST_ASSERT(!memcmp(buf.data(), buf2.data(), 65536));

		if (!(++iterations % 1000))
			printf("%u iterations completed\n", iterations);
	}

	return 0;
}

DEFINE_TEST_NONAUTO(System_ZipBench) {
	const wchar_t *args = ATTestGetArguments();

	if (!*args)
		throw ATTestAssertionException("No filename specified on command line.");

	VDStringW fnbuf(args);
	bool summary = false;

	if (fnbuf.size() >= 3 && fnbuf.subspan(fnbuf.size()-2, 2) == L"/s") {
		fnbuf.pop_back();
		fnbuf.pop_back();
		summary = true;
	}

	VDDirectoryIterator dirIt(fnbuf.c_str());

	while(dirIt.Next()) {
		const VDStringW path(dirIt.GetFullPath());
		VDFileStream fs(path.c_str());
		VDZipArchive za;
		za.Init(&fs);

		vdblock<char> buf(65536);

		const sint32 n = za.GetFileCount();
		double totalTime = 0;
		sint64 totalInBytes = 0;
		sint64 totalOutBytes = 0;

		for(sint32 i=0; i<n; ++i) {
			vdautoptr<IVDInflateStream> is(za.OpenDecodedStream(i, true));
			const auto& fi = za.GetFileInfo(i);
			sint32 len = 0;
			sint64 left = fi.mUncompressedSize;

			is->EnableCRC();
			is->SetExpectedCRC(fi.mCRC32);

			const uint64 startTick = VDGetPreciseTick();
			while(left > 0) {
				sint32 tc = (sint32)std::min<sint64>(left, 65536);
				left -= tc;

				sint32 actual = is->ReadData(buf.data(), tc);

				if (actual == 0)
					break;

				if (actual < 0)
					throw ATTestAssertionException("Decompression error: %ls", za.GetFileInfo(i).mDecodedFileName.c_str());

				len += actual;
			}

			is->VerifyCRC();

			const uint64 endTick = VDGetPreciseTick();
			const double seconds = (double)(endTick - startTick) * VDGetPreciseSecondsPerTick();

			totalTime += seconds;
			totalInBytes += fi.mCompressedSize;
			totalOutBytes += len;

			if (!summary) {
				printf("%6.1fMB  %6.1fMB/sec -> %7.1fMB  %6.1fMB/sec | %ls\n"
					, (double)fi.mCompressedSize / (1024.0 * 1024.0)
					, (double)fi.mCompressedSize / seconds / (1024.0 * 1024.0)
					, (double)len / (1024.0 * 1024.0)
					, (double)len / seconds / (1024.0 * 1024.0)
					, fi.mDecodedFileName.c_str());
			}
		}

		if (n > 1 || summary) {
			printf("%6.1fMB  %6.1fMB/sec -> %7.1fMB  %6.1fMB/sec | <Total> %ls\n"
				, (double)totalInBytes / (1024.0 * 1024.0)
				, (double)totalInBytes / totalTime / (1024.0 * 1024.0)
				, (double)totalOutBytes / (1024.0 * 1024.0)
				, (double)totalOutBytes / totalTime / (1024.0 * 1024.0)
				, path.c_str()
			);
		}
	}
	return 0;
}
