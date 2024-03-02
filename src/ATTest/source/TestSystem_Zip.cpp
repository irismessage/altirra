#include <stdafx.h>
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

		vdautoptr is(new VDInflateStream);
		is->Init(&ms, ms.mBuf.size(), false);

		is->Read(buf2.data(), buf2.size());

		TEST_ASSERT(!memcmp(buf.data(), buf2.data(), 65536));

		if (!(++iterations % 1000))
			printf("%u iterations completed\n", iterations);
	}

	return 0;
}
