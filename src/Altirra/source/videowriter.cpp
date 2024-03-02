#include <stdafx.h>

#define INITGUID

#include <numeric>

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
	#include <emmintrin.h>
#endif

#include <vd2/system/cpuaccel.h>
#include <vd2/system/error.h>
#include <vd2/system/fraction.h>
#include <vd2/system/int128.h>
#include <vd2/system/math.h>
#include <vd2/system/vdalloc.h>
#include <vd2/Kasumi/blitter.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/resample.h>

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
	#include <at/atcore/intrin_sse2.h>
#endif

#include <at/atio/wav.h>
#include "videowriter.h"
#include "audiofilters.h"
#include "audiooutput.h"
#include "aviwriter.h"
#include "gtia.h"
#include "uirender.h"

#include <vd2/system/w32assist.h>

#include <windows.h>
#include <mfidl.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <uuids.h>

///////////////////////////////////////////////////////////////////////////////

class IATVideoEncoder {
public:
	virtual ~IATVideoEncoder() {}

	virtual void Compress(const VDPixmap& px, bool intra, bool encodeAll) = 0;

	virtual uint32 GetEncodedLength() const = 0;
	virtual const void *GetEncodedData() const = 0;
};

///////////////////////////////////////////////////////////////////////////////

class ATVideoEncoderRaw : public IATVideoEncoder {
public:
	ATVideoEncoderRaw(uint32 w, uint32 h, int format);
	void Compress(const VDPixmap& px, bool intra, bool encodeAll);

	uint32 GetEncodedLength() const { return mEncodedLength; }
	const void *GetEncodedData() const { return mBuffer.data(); }

protected:
	vdfastvector<uint8, vdaligned_alloc<uint8> > mBuffer;
	vdfastvector<uint8, vdaligned_alloc<uint8> > mBufferRef;
	VDPixmapLayout mLayout;
	uint32 mEncodedLength;
};

ATVideoEncoderRaw::ATVideoEncoderRaw(uint32 w, uint32 h, int format) {
	uint32 size = VDPixmapCreateLinearLayout(mLayout, format, w, h, 4);
	VDPixmapLayoutFlipV(mLayout);

	mBuffer.resize(size);
	mBufferRef.resize(size);
}

void ATVideoEncoderRaw::Compress(const VDPixmap& px, bool intra, bool encodeAll) {
	mBufferRef.swap(mBuffer);

	VDPixmap pxbuf = VDPixmapFromLayout(mLayout, mBuffer.data());
	VDPixmap pxref = VDPixmapFromLayout(mLayout, mBufferRef.data());

	VDPixmapBlt(pxbuf, px);

	if (!intra && !encodeAll) {
		const uint8 *src = (const uint8 *)pxbuf.data;
		const uint8 *ref = (const uint8 *)pxref.data;
		const uint32 w = pxbuf.w;
		const uint32 h = pxbuf.h;
		const uint32 bpr = mLayout.format == nsVDPixmap::kPixFormat_RGB888 ? 3*w : w;

		mEncodedLength = 0;
		for(uint32 y=0; y<h; ++y) {
			if (memcmp(src, ref, bpr)) {
				mEncodedLength = (uint32)mBuffer.size();
				break;
			}

			src += pxbuf.pitch;
			ref += pxbuf.pitch;
		}
	} else {
		mEncodedLength = (uint32)mBuffer.size();
	}
}

///////////////////////////////////////////////////////////////////////////////

class ATVideoEncoderRLE : public IATVideoEncoder {
public:
	ATVideoEncoderRLE(uint32 w, uint32 h);
	void Compress(const VDPixmap& px, bool intra, bool encodeAll);

	uint32 GetEncodedLength() const { return mEncodedLength; }
	const void *GetEncodedData() const { return mPackBuffer.data(); }

protected:
	void CompressIntra8();
	void CompressInter8(bool encodeAll);

	uint32 mWidth;
	uint32 mHeight;
	uint32 mEncodedLength;

	vdfastvector<uint8> mPackBuffer;
	VDPixmapBuffer	mBuffer;
	VDPixmapBuffer	mBufferRef;
};

ATVideoEncoderRLE::ATVideoEncoderRLE(uint32 w, uint32 h) {
	mWidth = w;
	mHeight = h;

	mPackBuffer.resize(w * h * 2);

	VDPixmapLayout layout;
	VDPixmapCreateLinearLayout(layout, nsVDPixmap::kPixFormat_Pal8, w, h, 16);
	mBuffer.init(layout);
	mBufferRef.init(layout);
}

void ATVideoEncoderRLE::Compress(const VDPixmap& px, bool intra, bool encodeAll) {
	mBuffer.swap(mBufferRef);
	VDPixmapBlt(mBuffer, px);

	if (intra)
		CompressIntra8();
	else
		CompressInter8(encodeAll);
}

void ATVideoEncoderRLE::CompressIntra8() {
	uint8 *dst0 = mPackBuffer.data();
	uint8 *dst = dst0;

	const uint32 w = mWidth;
	const uint32 h = mHeight;
	const uint8 *src = (const uint8 *)mBuffer.data + mBuffer.pitch * (h - 1);

	for(uint32 y = 0; y < h; ++y) {
		uint32 x = 0;

		// check if we can skip the scan line
		while(x < w) {
			uint32 x2 = x;
			bool rle = false;

			while(x2 < w) {
				if (src[x2] == src[x2+1] && src[x2+1] == src[x2+2] && x2 + 2 < w) {
					rle = true;
					break;
				}

				++x2;
			}

			uint32 literalLen = x2 - x;
			if (literalLen) {
				if (literalLen < 3) {
					*dst++ = 1;
					*dst++ = src[x++];
					if (literalLen == 2) {
						*dst++ = 1;
						*dst++ = src[x++];
					}
				} else {
					while(literalLen) {
						uint32 tc = literalLen;
						if (tc > 255) {
							if (tc > 256)
								tc = 254;	// not an error - avoid wasting a byte
							else
								tc = 252;
						}

						literalLen -= tc;

						*dst++ = 0;
						*dst++ = (uint8)tc;
						memcpy(dst, &src[x], tc);
						dst += tc;
						x += tc;

						if (tc & 1)
							*dst++ = 0;
					}
				}
			}

			if (rle) {
				uint8 c = src[x2];

				x2 += 3;
				while(x2 < w && src[x2] == c)
					++x2;

				uint32 runLen = x2 - x;
				while(runLen) {
					uint32 tc = runLen;
					if (tc > 255) {
						if (tc > 256)
							tc = 254;	// not an error - avoid wasting a byte
						else
							tc = 252;
					}

					runLen -= tc;

					*dst++ = (uint8)tc;
					*dst++ = c;
				}

				x = x2;
			}
		}

		// write EOL or EOF
		*dst++ = 0;
		*dst++ = (y == h - 1) ? 1 : 0;

		src -= mBuffer.pitch;
	}

	// write frame
	mEncodedLength = (uint32)(dst - dst0);
}

void ATVideoEncoderRLE::CompressInter8(bool encodeAll) {
	uint8 *dst0 = mPackBuffer.data();
	uint8 *dst = dst0;

	const uint32 w = mWidth;
	const uint32 h = mHeight;
	const uint8 *src = (const uint8 *)mBuffer.data + mBuffer.pitch * (h - 1);
	const uint8 *ref = (const uint8 *)mBufferRef.data + mBufferRef.pitch * (h - 1);

	uint32 lastx = 0;
	uint32 lasty = 0;

	for(uint32 y = 0; y < h; ++y) {
		uint32 x = 0;
		uint32 xl = w;

		// determine right border
		while(xl > 0 && src[xl-1] == ref[xl - 1])
			--xl;

		// check if we can skip the scan line
		while(x < xl) {
			uint32 x2 = x;
			bool rle = false;
			bool copy = false;

			while(x2 < xl) {
				if (src[x2] == src[x2+1] && src[x2+1] == src[x2+2] && x2 + 2 < xl) {
					rle = true;
					break;
				}

				if (src[x2] == ref[x2] && (x2 + 1 >= xl || (src[x2+1] == ref[x2+1] && (x2 + 2 >= xl || src[x2+2] == ref[x2+2])))) {
					copy = true;
					break;
				}

				++x2;
			}

			uint32 literalLen = x2 - x;
			if ((literalLen || rle) && (y != lasty || x != lastx)) {
				// check if we need to encode an EOL
				if (x < lastx) {
					*dst++ = 0;
					*dst++ = 0;
					lastx = 0;
					++lasty;
				}

				// encode a skip
				while(x != lastx || y != lasty) {
					uint32 dx = x - lastx;
					uint32 dy = y - lasty;

					if (dx > 255)
						dx = 255;

					if (dy > 255)
						dy = 255;

					*dst++ = 0;
					*dst++ = 2;
					*dst++ = (uint8)dx;
					*dst++ = (uint8)dy;

					lastx += dx;
					lasty += dy;
				}
			}

			if (literalLen) {
				if (literalLen < 3) {
					*dst++ = 1;
					*dst++ = src[x++];
					if (literalLen == 2) {
						*dst++ = 1;
						*dst++ = src[x++];
					}
				} else {
					while(literalLen) {
						uint32 tc = literalLen;
						if (tc > 255) {
							if (tc > 256)
								tc = 254;	// not an error - avoid wasting a byte
							else
								tc = 252;
						}

						literalLen -= tc;

						*dst++ = 0;
						*dst++ = (uint8)tc;
						memcpy(dst, &src[x], tc);
						dst += tc;
						x += tc;

						if (tc & 1)
							*dst++ = 0;
					}
				}

				lastx = x;
			}

			if (rle) {
				uint8 c = src[x2];

				x2 += 3;
				while(x2 < xl && src[x2] == c)
					++x2;

				uint32 runLen = x2 - x;
				while(runLen) {
					uint32 tc = runLen;
					if (tc > 255) {
						if (tc > 256)
							tc = 254;	// not an error - avoid wasting a byte
						else
							tc = 252;
					}

					runLen -= tc;

					*dst++ = (uint8)tc;
					*dst++ = c;
				}

				lastx = x2;
				x = x2;
			} else if (copy) {
				x = x2;
				while(src[x] == ref[x] && x < xl)
					++x;
			}
		}

		src -= mBuffer.pitch;
		ref -= mBufferRef.pitch;
	}

	if (dst != dst0 || encodeAll) {
		// write EOF
		*dst++ = 0;
		*dst++ = 1;
	}

	mEncodedLength = (uint32)(dst - dst0);
}

///////////////////////////////////////////////////////////////////////////////

namespace {
	const unsigned len_tbl[32]={
		3,4,5,6,7,8,9,10,
		11,13,15,17,19,23,27,31,
		35,43,51,59,67,83,99,115,
		131,163,195,227,258,259
	};

	const unsigned char len_bits_tbl[32]={
		0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
	};

	const unsigned char dist_bits_tbl[]={
		0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
	};

	const unsigned dist_tbl[]={
		1,2,3,4,5,7,9,13,
		17,25,33,49,65,97,129,193,
		257,385,513,769,1025,1537,2049,3073,
		4097,6145,8193,12289,16385,24577,
		32769
	};

	const unsigned char hclen_tbl[]={
		16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
	};

	struct VDHuffmanHistoSorterData {
		VDHuffmanHistoSorterData(const int pHisto[288]) {
			for(int i=0; i<288; ++i) {
				mHisto[i] = (pHisto[i] << 9) + 287 - i;
			}
		}

		int mHisto[288];
	};

	struct VDHuffmanHistoSorter {
		VDHuffmanHistoSorter(const VDHuffmanHistoSorterData& data) : mpHisto(data.mHisto) {}

		// We want to sort by descending probability first, then by ascending code point.
		bool operator()(int f1, int f2) const {
			return mpHisto[f1] > mpHisto[f2];
		}

		const int *mpHisto;
	};

	class VDZMBVHuffmanTable {
	public:
		VDZMBVHuffmanTable();

		void Init();

		inline void Tally(int c) {
			++mHistogram[c];
		}

		inline void Tally(int c, int count) {
			mHistogram[c] += count;
		}

		void BuildCode(int depth_limit = 15);
		void BuildEncodingTable(uint16 *p, int *l, int limit);
		void BuildStaticLengthEncodingTable(uint16 *p, int *l);
		void BuildStaticDistanceEncodingTable(uint16 *p, int *l);

		uint32 GetCodeCount(int limit) const;
		uint32 GetOutputSize() const;
		uint32 GetStaticOutputSize() const;

		const uint16 *GetDHTSegment() { return mDHT; }
		int GetDHTSegmentLen() const { return mDHTLength; }

	private:
		int mHistogram[288];
		int mHistogram2[288];
		uint16 mDHT[288+16];
		int mDHTLength;
	};

	VDZMBVHuffmanTable::VDZMBVHuffmanTable() {
		Init();
	}

	void VDZMBVHuffmanTable::Init() {
		std::fill(mHistogram, mHistogram+288, 0);
	}

	void VDZMBVHuffmanTable::BuildCode(int depth_limit) {
		int i;
		int nonzero_codes = 0;

		for(i=0; i<288; ++i) {
			mDHT[i+16] = i;
			if (mHistogram[i])
				++nonzero_codes;
			mHistogram2[i] = mHistogram[i];
		}

		// Codes are stored in the second half of the DHT segment in decreasing
		// order of frequency.

		std::sort(&mDHT[16], &mDHT[16+288], VDHuffmanHistoSorter(VDHuffmanHistoSorterData(mHistogram)));
		mDHTLength = 16 + nonzero_codes;

		// Sort histogram in increasing order.

		std::sort(mHistogram, mHistogram+288);

		int *A = mHistogram+288 - nonzero_codes;

		// Begin merging process (from "In-place calculation of minimum redundancy codes" by A. Moffat and J. Katajainen)
		//
		// There are three merging possibilities:
		//
		// 1) Leaf node with leaf node.
		// 2) Leaf node with internal node.
		// 3) Internal node with internal node.

		int leaf = 2;					// Next, smallest unattached leaf node.
		int internal = 0;				// Next, smallest unattached internal node.

		// Merging always creates one internal node and eliminates one node from
		// the total, so we will always be doing N-1 merges.

		A[0] += A[1];		// First merge is always two leaf nodes.
		for(int next=1; next<nonzero_codes-1; ++next) {		// 'next' is the value that receives the next unattached internal node.
			int a, b;

			// Pick first node.
			if (leaf < nonzero_codes && A[leaf] <= A[internal]) {
				A[next] = a=A[leaf++];			// begin new internal node with P of smallest leaf node
			} else {
				A[next] = a=A[internal];		// begin new internal node with P of smallest internal node
				A[internal++] = next;					// hook smallest internal node as child of new node
			}

			// Pick second node.
			if (internal >= next || (leaf < nonzero_codes && A[leaf] <= A[internal])) {
				A[next] += b=A[leaf++];			// complete new internal node with P of smallest leaf node
			} else {
				A[next] += b=A[internal];		// complete new internal node with P of smallest internal node
				A[internal++] = next;					// hook smallest internal node as child of new node
			}
		}

		// At this point, we have a binary tree composed entirely of pointers to
		// parents, partially sorted such that children are always before their
		// parents in the array.  Traverse the array backwards, replacing each
		// node with its depth in the tree.

		A[nonzero_codes-2] = 0;		// root has height 0 (0 bits)
		for(i = nonzero_codes-3; i>=0; --i)
			A[i] = A[A[i]]+1;		// child height is 1+height(parent).

		// Compute canonical tree bit depths for first part of DHT segment.
		// For each internal node at depth N, add two counts at depth N+1
		// and subtract one count at depth N.  Essentially, we are splitting
		// as we go.  We traverse backwards to ensure that no counts will drop
		// below zero at any time.

		std::fill(mDHT, mDHT+16, 0);

		int overallocation = 0;

		mDHT[0] = 2;		// 2 codes at depth 1 (1 bit)
		for(i = nonzero_codes-3; i>=0; --i) {
			int depth = A[i];

			// The optimal Huffman tree for N nodes can have a depth of N-1,
			// but we have to constrain ourselves at depth 15.  We simply
			// pile up counts at depth 15.  This causes us to overallocate the
			// codespace, but we will compensate for that later.

			if (depth >= depth_limit) {
				++mDHT[depth_limit-1];
			} else {
				--mDHT[depth-1];
				++mDHT[depth];
				++mDHT[depth];
			}
		}

		// Remove the extra code point.
		for(i=15; i>=0; --i) {
			if (mDHT[i])
				overallocation += mDHT[i] * (0x8000 >> i);
		}
		overallocation -= 0x10000;

		// We may have overallocated the codespace if we were forced to shorten
		// some codewords.

		if (overallocation > 0) {
			// Codespace is overallocated.  Begin lengthening codes from bit depth
			// 15 down until we are under the limit.

			i = depth_limit-2;
			while(overallocation > 0) {
				if (mDHT[i]) {
					--mDHT[i];
					++mDHT[i+1];
					overallocation -= 0x4000 >> i;
					if (i < depth_limit-2)
						++i;
				} else
					--i;
			}

			// We may be undercommitted at this point.  Raise codes from bit depth
			// 1 up until we are at the desired limit.

			int underallocation = -overallocation;

			i = 1;
			while(underallocation > 0) {
				if (mDHT[i] && (0x8000>>i) <= underallocation) {
					underallocation -= (0x8000>>i);
					--mDHT[i];
					--i;
					++mDHT[i];
				} else {
					++i;
				}
			}
		}
	}

	uint32 VDZMBVHuffmanTable::GetOutputSize() const {
		const uint16 *pCodes = mDHT+16;

		uint32 size = 0;

		for(int len=0; len<16; ++len) {
			int count = mDHT[len];

			uint32 points = 0;
			while(count--) {
				int code = *pCodes++;

				points += mHistogram2[code];
			}

			size += points * (len + 1);
		}

		return size;
	}

	uint32 VDZMBVHuffmanTable::GetCodeCount(int limit) const {
		return std::accumulate(mHistogram2, mHistogram2+limit, 0);
	}

	uint32 VDZMBVHuffmanTable::GetStaticOutputSize() const {
		uint32 sum7 = 0;
		uint32 sum8 = 0;
		uint32 sum9 = 0;
		sum8 = std::accumulate(mHistogram2+  0, mHistogram2+144, sum8);
		sum9 = std::accumulate(mHistogram2+144, mHistogram2+256, sum9);
		sum7 = std::accumulate(mHistogram2+256, mHistogram2+280, sum7);
		sum8 = std::accumulate(mHistogram2+280, mHistogram2+288, sum8);

		return 7*sum7 + 8*sum8 + 9*sum9;
	}

	static unsigned revword15(unsigned x) {
		unsigned y = 0;
		for(int i=0; i<15; ++i) {
			y = y + y + (x&1);
			x >>= 1;
		}
		return y;
	}

	void VDZMBVHuffmanTable::BuildEncodingTable(uint16 *p, int *l, int limit) {
		const uint16 *pCodes = mDHT+16;

		uint16 total = 0;
		uint16 inc = 0x4000;

		for(int len=0; len<16; ++len) {
			int count = mDHT[len];

			while(count--) {
				int code = *pCodes++;

				l[code] = len+1;
			}

			for(int k=0; k<limit; ++k) {
				if (l[k] == len+1) {
					p[k] = revword15(total) << (16 - (len+1));
					total += inc;
				}
			}
			inc >>= 1;
		}
	}

	void VDZMBVHuffmanTable::BuildStaticLengthEncodingTable(uint16 *p, int *l) {
		memset(mDHT, 0, sizeof(mDHT[0])*16);
		mDHT[6] = 24;
		mDHT[7] = 152;
		mDHT[8] = 112;

		uint16 *dst = mDHT + 16;
		for(int i=256; i<280; ++i)
			*dst++ = i;
		for(int i=0; i<144; ++i)
			*dst++ = i;
		for(int i=280; i<288; ++i)
			*dst++ = i;
		for(int i=144; i<256; ++i)
			*dst++ = i;

		BuildEncodingTable(p, l, 288);
	}

	void VDZMBVHuffmanTable::BuildStaticDistanceEncodingTable(uint16 *p, int *l) {
		memset(mDHT, 0, sizeof(mDHT[0])*16);
		mDHT[4] = 32;

		for(int i=0; i<32; ++i)
			mDHT[i+16] = i;

		BuildEncodingTable(p, l, 32);
	}

	class VDZMBVDeflateEncoder {
	public:
		VDZMBVDeflateEncoder();
		VDZMBVDeflateEncoder(const VDZMBVDeflateEncoder&);
		~VDZMBVDeflateEncoder();

		VDZMBVDeflateEncoder& operator=(const VDZMBVDeflateEncoder&);

		void Init(bool quick);
		void Write(const void *src, size_t len);
		void ForceNewBlock();
		void Finish();

		void SyncBegin();
		void SyncEnd();

		uint32 EstimateOutputSize();

		vdfastvector<uint8>& GetOutput() { return mOutput; }

	protected:
		void EndBlock(bool term);
		void Compress(bool flush);
		void VDFORCEINLINE PutBits(uint32 encoding, int enclen);
		void FlushBits();
		uint32 Flush(int n, int ndists, bool term, bool test);

		uint32	mAccum;
		int		mAccBits;
		uint32	mHistoryPos;
		uint32	mHistoryTail;
		uint32	mHistoryBase;
		uint32	mHistoryBlockStart;
		uint32	mLenExtraBits;
		uint32	mPendingLen;
		uint8	*mpLen;
		uint16	*mpCode;
		uint16	*mpDist;

		uint32	mWindowLimit;

		vdfastvector<uint8> mOutput;

		// Block coding tables
		uint16	mCodeEnc[288];
		int		mCodeLen[288];

		uint16	mDistEnc[32];
		int		mDistLen[32];

		uint8	mHistoryBuffer[65536+6];
		sint32	mHashNext[32768];
		sint32	mHashTable[65536];
		uint8	mLenBuf[32769];
		uint16	mCodeBuf[32769];
		uint16	mDistBuf[32769];
	};

	VDZMBVDeflateEncoder::VDZMBVDeflateEncoder()
	{
	}

	VDZMBVDeflateEncoder::VDZMBVDeflateEncoder(const VDZMBVDeflateEncoder& src) {
		*this = src;
	}

	VDZMBVDeflateEncoder::~VDZMBVDeflateEncoder() {
	}

	VDZMBVDeflateEncoder& VDZMBVDeflateEncoder::operator=(const VDZMBVDeflateEncoder& src) {
		if (this != &src) {
			mAccum			= src.mAccum;
			mAccBits		= src.mAccBits;
			mHistoryPos		= src.mHistoryPos;
			mHistoryTail	= src.mHistoryTail;
			mHistoryBase	= src.mHistoryBase;
			mHistoryBlockStart = src.mHistoryBlockStart;
			mLenExtraBits = src.mLenExtraBits;
			mPendingLen		= src.mPendingLen;
			mpLen			= mLenBuf + (src.mpLen - src.mLenBuf);
			mpCode			= mCodeBuf + (src.mpCode - src.mCodeBuf);
			mpDist			= mDistBuf + (src.mpDist - src.mDistBuf);
			mWindowLimit	= src.mWindowLimit;
			mOutput			= src.mOutput;

			memcpy(mHistoryBuffer, src.mHistoryBuffer, mHistoryTail);
			memcpy(mHashNext, src.mHashNext, sizeof mHashNext);
			memcpy(mHashTable, src.mHashTable, sizeof mHashTable);
			memcpy(mLenBuf, src.mLenBuf, sizeof(mLenBuf[0]) * (src.mpLen - src.mLenBuf));
			memcpy(mCodeBuf, src.mCodeBuf, sizeof(mCodeBuf[0]) * (src.mpCode - src.mCodeBuf));
			memcpy(mDistBuf, src.mDistBuf, sizeof(mDistBuf[0]) * (src.mpDist - src.mDistBuf));
		}
		return *this;
	}

	void VDZMBVDeflateEncoder::Init(bool quick) {
		std::fill(mHashNext, mHashNext+32768, -0x20000);
		std::fill(mHashTable, mHashTable+65536, -0x20000);

		mWindowLimit = quick ? 1024 : 32768;

		mpLen = mLenBuf;
		mpCode = mCodeBuf;
		mpDist = mDistBuf;
		mHistoryPos = 0;
		mHistoryTail = 0;
		mHistoryBase = 0;
		mHistoryBlockStart = 0;
		mLenExtraBits = 0;
		mPendingLen = 0;
		mAccum = 0;
		mAccBits = 0;

		mOutput.push_back(0x78);	// 32K window, Deflate
		mOutput.push_back(0xDA);	// maximum compression, no dictionary, check offset = 0x1A
	}

	void VDZMBVDeflateEncoder::Write(const void *src, size_t len) {
		while(len > 0) {
			uint32 tc = sizeof mHistoryBuffer - mHistoryTail;

			if (!tc) {
				Compress(false);
				continue;
			}

			if ((size_t)tc > len)
				tc = (uint32)len;

			memcpy(mHistoryBuffer + mHistoryTail, src, tc);

			mHistoryTail += tc;
			src = (const char *)src + tc;
			len -= tc;
		}
	}

	void VDZMBVDeflateEncoder::ForceNewBlock() {
		Compress(false);
		EndBlock(false);
	}

	#define HASH3(pos) (((uint32)hist[(pos)] ^ ((uint32)hist[(pos)+1] << 4) ^ ((uint32)hist[(pos)+2] << 8)) & 0xffff)
//	#define HASH6(pos) (((uint32)hist[(pos)] ^ ((uint32)hist[(pos)+1] << 2) ^ ((uint32)hist[(pos)+2] << 4) ^ ((uint32)hist[(pos)+3] << 6) ^ ((uint32)hist[(pos)+4] << 7) ^ ((uint32)hist[(pos)+5] << 8)) & 0xffff)
	#define HASH6(pos) ((uint32)((*(const uint32 *)&hist[(pos)] + *(const uint16 *)&hist[(pos)+4]) * 0xc17d3f45) >> 16)

	const bool usehash6 = true;

	void VDZMBVDeflateEncoder::EndBlock(bool term) {
		if (mpCode > mCodeBuf) {
			if (mPendingLen) {
				const uint8 *hist = mHistoryBuffer - mHistoryBase;
				int bestlen = mPendingLen - 1;
				mPendingLen = 0;

				if (usehash6) {
					while(bestlen-- > 0) {
						int hval = HASH6(mHistoryPos);
						mHashNext[mHistoryPos & 0x7fff] = mHashTable[hval];
						mHashTable[hval] = mHistoryPos;
						++mHistoryPos;
					}
				} else {
					while(bestlen-- > 0) {
						int hval = HASH3(mHistoryPos);
						mHashNext[mHistoryPos & 0x7fff] = mHashTable[hval];
						mHashTable[hval] = mHistoryPos;
						++mHistoryPos;
					}
				}
			}

			*mpCode++ = 256;
			Flush((int)(mpCode - mCodeBuf), (int)(mpDist - mDistBuf), term, false);
			mpCode = mCodeBuf;
			mpDist = mDistBuf;
			mpLen = mLenBuf;
			mHistoryBlockStart = mHistoryPos;
			mLenExtraBits = 0;
		}
	}

	void VDZMBVDeflateEncoder::Compress(bool flush) {
		uint8	*lenptr = mpLen;
		uint16	*codeptr = mpCode;
		uint16	*distptr = mpDist;

		const uint8 *hist = mHistoryBuffer - mHistoryBase;
		const int maxmatch = 100;

		uint32 pos = mHistoryPos;
		uint32 len = mHistoryBase + mHistoryTail;
		uint32 hashlen = usehash6 ? 6 : 3;
		uint32 maxpos = flush ? len : len > 258+hashlen ? len - (258+hashlen) : 0;		// +6 is for the 6-byte hash.
		while(pos < maxpos) {
			if (codeptr >= mCodeBuf + 32768) {
				mpCode = codeptr;
				mpDist = distptr;
				mpLen = lenptr;
				mHistoryPos = pos;
				EndBlock(false);
				pos = mHistoryPos;
				codeptr = mpCode;
				distptr = mpDist;
				lenptr = mpLen;

				// Note that it's possible for the EndBlock() to have flushed out a pending
				// run and pushed us all the way to maxpos.
				VDASSERT(pos <= mHistoryBase + mHistoryTail);
				continue;
			}

			uint8 c = hist[pos];
			uint32 hcode = usehash6 ? HASH6(pos) : HASH3(pos);

			sint32 hpos = mHashTable[hcode];
			uint32 limit = 258;
			if (limit > len-pos)
				limit = len-pos;

			sint32 hlimit = pos - mWindowLimit;		// note that our initial hash table values are low enough to avoid colliding with this.
			if (hlimit < 0)
				hlimit = 0;

			uint32 bestlen = hashlen - 1;
			uint32 bestoffset = 0;

			if (hpos >= hlimit && limit >= hashlen) {
				sint32 hstart = hpos;
				const unsigned char *s2 = hist + pos;
				int matchlimit = maxmatch;

				if (usehash6) {
					uint32 matchWord1 = *(const uint32 *)s2;
					uint16 matchWord2 = *(const uint16 *)(s2 + 4);
					int hashoffset = 0;

					do {
						const unsigned char *s1 = hist + hpos - hashoffset;
						uint32 mlen = 0;

						if (s1[bestlen] == s2[bestlen] && *(const uint32 *)s1 == matchWord1 && *(const uint16 *)(s1 + 4) == matchWord2) {
							mlen = 6;
							while(mlen < limit && s1[mlen] == s2[mlen])
								++mlen;

							if (mlen > bestlen) {
								if (mlen > limit)
									mlen = limit;

								bestoffset = pos - hpos + hashoffset;

								// hop hash chains if we can
								sint32 hashoffsetnew = mlen - 5;
								uint32 hposnew = hpos - hashoffset + hashoffsetnew;
								if (hposnew <= pos) {
									hlimit += (hashoffsetnew - hashoffset);
									hashoffset = hashoffsetnew;
									hpos = hposnew;

									if (hpos == pos)
										hpos = hstart;
									else
										hpos = mHashNext[hpos & 0x7fff];
								}

								bestlen = mlen;

								if (bestlen >= limit)
									break;
								continue;
							}
						}

						if (!--matchlimit)
							break;

						hpos = mHashNext[hpos & 0x7fff];
					} while(hpos >= hlimit);
				} else {
					uint8 matchByte0 = s2[0];
					uint8 matchByte1 = s2[1];
					uint8 matchByte2 = s2[2];

					do {
						const unsigned char *s1 = hist + hpos - bestlen + 2;
						uint32 mlen = 0;

						if (s1[bestlen] == s2[bestlen] && s1[0] == matchByte0 && s1[1] == matchByte1 && s1[2] == matchByte2) {
							mlen = 3;
							while(mlen < limit && s1[mlen] == s2[mlen])
								++mlen;

							if (mlen > bestlen) {
								uint32 moffset = pos - hpos + bestlen - 2;

								if (mlen > 5 || moffset < 4096) {
									bestoffset = moffset;
									// hop hash chains!
									hpos += mlen - bestlen;
									if (hpos > (sint32)pos)
										hpos = mHashTable[HASH3(hpos)];
									else if (hpos == pos)
										hpos = hstart;
									else
										hpos = mHashNext[(hpos + mlen - bestlen) & 0x7fff];
									hlimit += (mlen - bestlen);

									bestlen = mlen;
									continue;
								}
							}
						}

						if (!--matchlimit)
							break;

						hpos = mHashNext[hpos & 0x7fff];
					} while(hpos >= hlimit);
				}
			}

			// Normally, we'd accept any match of longer 3 or greater. However, the savings for this aren't
			// enough to match the decrease in the effectiveness of the Huffman encoding, so it's usually
			// better to keep only longer matches. We follow the lead of zlib's Z_FILTERED and only accept
			// matches of length 6 or longer. It turns out that we can greatly speed up compression when
			// this is the case since we can use a longer hash -- the PNG filtering often means a very
			// skewed distribution which hinders the effectiveness of a 3-byte hash.
			if (bestlen >= hashlen) {
				// check for an illegal match
				VDASSERT((uint32)(bestoffset-1) < 32768U);
				VDASSERT(bestlen < 259);
				VDASSERT(!memcmp(hist+pos, hist+pos-bestoffset, bestlen));
				VDASSERT(pos >= bestoffset);
				VDASSERT(pos+bestlen <= len);
				VDASSERT(pos-bestoffset >= mHistoryBase);

				unsigned lcode = 0;
				while(bestlen >= len_tbl[lcode+1])
					++lcode;
				*codeptr++ = lcode + 257;
				*distptr++ = bestoffset;
				*lenptr++ = bestlen - 3;
				mLenExtraBits += len_bits_tbl[lcode];
			} else {
				*codeptr++ = c;
				bestlen = 1;
			}

			// Lazy matching.
			//
			//	prev	current		compare		action
			//	======================================
			//	lit		lit						append
			//	lit		match					stash
			//	match	lit						retire
			//	match	match		shorter		retire
			//	match	match		longer		obsolete
			VDASSERT(pos+bestlen <= mHistoryBase + mHistoryTail);

			if (!mPendingLen) {
				if (bestlen > 1) {
					mPendingLen = bestlen;
					bestlen = 1;
				}
			} else {
				if (bestlen > mPendingLen) {
					codeptr[-2] = hist[pos - 1];
					distptr[-2] = distptr[-1];
					--distptr;
					lenptr[-2] = lenptr[-1];
					--lenptr;
					mPendingLen = bestlen;
					bestlen = 1;
				} else {
					--codeptr;
					if (bestlen > 1) {
						--distptr;
						--lenptr;
					}

					bestlen = mPendingLen - 1;
					mPendingLen = 0;
				}
			}

			VDASSERT(pos+bestlen <= mHistoryBase + mHistoryTail);

			if (bestlen > 0) {
				mHashNext[pos & 0x7fff] = mHashTable[hcode];
				mHashTable[hcode] = pos;
				++pos;

				if (false) {
					--bestlen;

					pos += bestlen;
				} else {
					if (usehash6) {
						while(--bestlen) {
							uint32 hcode = HASH6(pos);
							mHashNext[pos & 0x7fff] = mHashTable[hcode];
							mHashTable[hcode] = pos;
							++pos;
						}
					} else {
						while(--bestlen) {
							uint32 hcode = HASH3(pos);
							mHashNext[pos & 0x7fff] = mHashTable[hcode];
							mHashTable[hcode] = pos;
							++pos;
						}
					}
				}
			}
		}

		// shift down by 32K
		if (pos - mHistoryBase >= 49152) {
			uint32 delta = (pos - 32768) - mHistoryBase;
			memmove(mHistoryBuffer, mHistoryBuffer + delta, mHistoryTail - delta);
			mHistoryBase += delta;
			mHistoryTail -= delta;
		}

		mHistoryPos = pos;
		mpLen = lenptr;
		mpCode = codeptr;
		mpDist = distptr;
	}

	void VDZMBVDeflateEncoder::Finish() {
		while(mHistoryPos != mHistoryBase + mHistoryTail)
			Compress(true);

		VDASSERT(mpCode != mCodeBuf);
		EndBlock(true);

		FlushBits();
	}

	void VDZMBVDeflateEncoder::SyncBegin() {
		while(mHistoryPos != mHistoryBase + mHistoryTail)
			Compress(true);

		VDASSERT(mpCode != mCodeBuf);
		EndBlock(false);

		// sync to byte with an empty stored block
		PutBits(0, 1);
		PutBits(0, 2);
		PutBits(0, -mAccBits & 7);
		PutBits(0x00000000, 16);
		PutBits(0xFFFF0000, 16);
		FlushBits();
	}

	void VDZMBVDeflateEncoder::SyncEnd() {
		mOutput.clear();
		std::fill(mHashNext, mHashNext+32768, -0x20000);
		std::fill(mHashTable, mHashTable+65536, -0x20000);

		mpLen = mLenBuf;
		mpCode = mCodeBuf;
		mpDist = mDistBuf;
		mHistoryPos = 0;
		mHistoryTail = 0;
		mHistoryBase = 0;
		mHistoryBlockStart = 0;
		mLenExtraBits = 0;
		mPendingLen = 0;
		mAccum = 0;
		mAccBits = 0;
	}

	uint32 VDZMBVDeflateEncoder::EstimateOutputSize() {
		Compress(false);

		return (uint32)mOutput.size() * 8 + mAccBits + Flush((int)(mpCode - mCodeBuf), (int)(mpDist - mDistBuf), false, true);
	}

	void VDFORCEINLINE VDZMBVDeflateEncoder::PutBits(uint32 encoding, int enclen) {
		mAccum >>= enclen;
		mAccum += encoding;
		mAccBits += enclen;

		if (mAccBits >= 16) {
			mAccBits -= 16;
	//		uint8 c[2] = { mAccum >> (16-mAccBits), mAccum >> (24-mAccBits) };

	//		mOutput.insert(mOutput.end(), c, c+2);
			mOutput.push_back(mAccum >> (16-mAccBits));
			mOutput.push_back(mAccum >> (24-mAccBits));
		}		
	}

	void VDZMBVDeflateEncoder::FlushBits() {
		while(mAccBits > 0) {
			mOutput.push_back(0xff & (mAccum >> (32-mAccBits)));
			mAccBits -= 8;
		}
	}

	uint32 VDZMBVDeflateEncoder::Flush(int n, int ndists, bool term, bool test) {
		const uint16 *codes = mCodeBuf;
		const uint8 *lens = mLenBuf;
		const uint16 *dists = mDistBuf;

		VDZMBVHuffmanTable htcodes, htdists, htlens;
		int i;

		memset(mCodeLen, 0, sizeof mCodeLen);
		memset(mDistLen, 0, sizeof mDistLen);

		for(i=0; i<n; ++i)
			htcodes.Tally(codes[i]);

		htcodes.BuildCode(15);

		for(i=0; i<ndists; ++i) {
			int c=0;
			while(dists[i] >= dist_tbl[c+1])
				++c;

			htdists.Tally(c);
		}

		htdists.BuildCode(15);

		int totalcodes = 286;
		int totaldists = 30;
		int totallens = totalcodes + totaldists;

		htcodes.BuildEncodingTable(mCodeEnc, mCodeLen, 288);
		htdists.BuildEncodingTable(mDistEnc, mDistLen, 32);

		// RLE the length table
		uint8 lenbuf[286+30+1];
		uint8 *lendst = lenbuf;
		uint8 rlebuf[286+30+1];
		uint8 *rledst = rlebuf;

		for(i=0; i<totalcodes; ++i)
			*lendst++ = mCodeLen[i];

		for(i=0; i<totaldists; ++i)
			*lendst++ = mDistLen[i];

		*lendst = 255;		// avoid match

		int last = -1;
		uint32 treeExtraBits = 0;
		i=0;
		while(i<totallens) {
			if (!lenbuf[i] && !lenbuf[i+1] && !lenbuf[i+2]) {
				int j;
				for(j=3; j<138 && !lenbuf[i+j]; ++j)
					;
				if (j < 11) {
					*rledst++ = 17;
					*rledst++ = j-3;
					treeExtraBits += 3;
				} else {
					*rledst++ = 18;
					*rledst++ = j-11;
					treeExtraBits += 7;
				}
				htlens.Tally(rledst[-2]);
				i += j;
				last = 0;
			} else if (lenbuf[i] == last && lenbuf[i+1] == last && lenbuf[i+2] == last) {
				int j;
				for(j=3; j<6 && lenbuf[i+j] == last; ++j)
					;
				*rledst++ = 16;
				htlens.Tally(16);
				*rledst++ = j-3;
				treeExtraBits += 2;
				i += j;
			} else {
				htlens.Tally(*rledst++ = lenbuf[i++]);
				last = lenbuf[i-1];
			}
		}

		htlens.BuildCode(7);

		// compute bits for dynamic encoding
		uint32 blockSize = mHistoryPos - mHistoryBlockStart;
		uint32 alignBits = -(mAccBits+3) & 7;
		uint32 dynamicBlockBits = htcodes.GetOutputSize() + htdists.GetOutputSize() + mLenExtraBits + htlens.GetOutputSize() + 14 + 19*3 + treeExtraBits;
		uint32 staticBlockBits = htcodes.GetStaticOutputSize() + htdists.GetCodeCount(32)*5 + mLenExtraBits;
		uint32 storeBlockBits = blockSize*8 + 32 + alignBits;

		if (storeBlockBits < dynamicBlockBits && storeBlockBits < staticBlockBits) {
			if (test)
				return storeBlockBits;

			PutBits((term ? 0x20000000 : 0) + (0 << 30), 3);

			// align to byte boundary
			PutBits(0, alignBits);

			// write block size
			PutBits((blockSize << 16) & 0xffff0000, 16);
			PutBits((~blockSize << 16) & 0xffff0000, 16);

			// write the block.
			FlushBits();

			const uint8 *base = &mHistoryBuffer[mHistoryBlockStart - mHistoryBase];
			mOutput.insert(mOutput.end(), base, base+blockSize);
		} else {
			if (dynamicBlockBits < staticBlockBits) {
				if (test)
					return dynamicBlockBits;

				PutBits((term ? 0x20000000 : 0) + (2 << 30), 3);

				PutBits((totalcodes - 257) << 27, 5);	// code count - 257
				PutBits((totaldists - 1) << 27, 5);	// dist count - 1
				PutBits(0xf0000000, 4);	// ltbl count - 4

				uint16 hlenc[19];
				int hllen[19]={0};
				htlens.BuildEncodingTable(hlenc, hllen, 19);

				for(i=0; i<19; ++i) {
					int k = hclen_tbl[i];

					PutBits(hllen[k] << 29, 3);
				}

				uint8 *rlesrc = rlebuf;
				while(rlesrc < rledst) {
					uint8 c = *rlesrc++;
					PutBits((uint32)hlenc[c] << 16, hllen[c]);

					if (c == 16)
						PutBits((uint32)*rlesrc++ << 30, 2);
					else if (c == 17)
						PutBits((uint32)*rlesrc++ << 29, 3);
					else if (c == 18)
						PutBits((uint32)*rlesrc++ << 25, 7);
				}
			} else {
				if (test)
					return staticBlockBits;

				PutBits((term ? 0x20000000 : 0) + (1 << 30), 3);

				memset(mCodeLen, 0, sizeof(mCodeLen));
				memset(mDistLen, 0, sizeof(mDistLen));
				htcodes.BuildStaticLengthEncodingTable(mCodeEnc, mCodeLen);
				htdists.BuildStaticDistanceEncodingTable(mDistEnc, mDistLen);
			}

			for(i=0; i<n; ++i) {
				unsigned code = *codes++;
				unsigned clen = mCodeLen[code];

				PutBits((uint32)mCodeEnc[code] << 16, clen);

				if (code >= 257) {
					unsigned extralenbits = len_bits_tbl[code-257];
					unsigned len = *lens++ + 3;

					VDASSERT(len >= len_tbl[code-257]);
					VDASSERT(len < len_tbl[code-256]);

					if (extralenbits)
						PutBits((len - len_tbl[code-257]) << (32 - extralenbits), extralenbits);

					unsigned dist = *dists++;
					int dcode=0;
					while(dist >= dist_tbl[dcode+1])
						++dcode;

					PutBits((uint32)mDistEnc[dcode] << 16, mDistLen[dcode]);

					unsigned extradistbits = dist_bits_tbl[dcode];

					if (extradistbits)
						PutBits((dist - dist_tbl[dcode]) << (32 - extradistbits), extradistbits);
				}
			}
		}

		return 0;
	}
}

class ATVideoEncoderZMBV : public IATVideoEncoder {
public:
	ATVideoEncoderZMBV(uint32 w, uint32 h, bool rgb32);
	void Compress(const VDPixmap& px, bool intra, bool encodeAll);

	uint32 GetEncodedLength() const { return mEncodedLength; }
	const void *GetEncodedData() const { return mPackBuffer.data(); }

protected:
	void CompressIntra8(const VDPixmap& px);
	void CompressInter8(bool encodeAll);

	uint32 mWidth;
	uint32 mHeight;
	bool mbRgb32;
	uint32 mEncodedLength;

	vdfastvector<uint8, vdaligned_alloc<uint8> > mPackBuffer;
	vdfastvector<uint8, vdaligned_alloc<uint8> > mBuffer;
	vdfastvector<uint8, vdaligned_alloc<uint8> > mBufferRef;

	struct MotionVector {
		sint8 x;
		sint8 y;

		bool operator==(const MotionVector& v) const {
			return !((x ^ v.x) | (y ^ v.y));
		}

		bool operator!=(const MotionVector& v) const {
			return !!((x ^ v.x) | (y ^ v.y));
		}

		MotionVector offset(sint8 dx, sint8 dy) const {
			MotionVector v = {x+dx, y+dy};
			return v;
		}
	};

	vdfastvector<MotionVector> mVecBuffer;
	vdfastvector<MotionVector> mVecBufferPrev;

	VDPixmapLayout	mLayout;

	VDZMBVDeflateEncoder mEncoder;
};

ATVideoEncoderZMBV::ATVideoEncoderZMBV(uint32 w, uint32 h, bool rgb32) {
	mWidth = w;
	mHeight = h;
	mbRgb32 = rgb32;

	mPackBuffer.resize(rgb32 ? w * h * 8 : w * h * 2);

	mLayout.format = rgb32 ? nsVDPixmap::kPixFormat_XRGB8888 : nsVDPixmap::kPixFormat_Pal8;
	mLayout.w = w;
	mLayout.h = h;
	mLayout.palette = NULL;
	mLayout.pitch = (w + 47) & ~15;

	if (rgb32)
		mLayout.pitch *= 4;

	mLayout.data = mLayout.pitch * 16 + (rgb32 ? 64 : 16);
	mLayout.data2 = 0;
	mLayout.data3 = 0;
	mLayout.pitch2 = 0;
	mLayout.pitch3 = 0;

	uint32 size = (uint32)mLayout.pitch * (mLayout.h + 32);
	mBuffer.resize(size, 0);
	mBufferRef.resize(size, 0);

	uint32 blkw = (w + 15) >> 4;
	uint32 blkh = (h + 15) >> 4;

	MotionVector v0 = { 0, 0 };
	mVecBuffer.resize(blkw * (blkh + 1) + 1, v0);
	mVecBufferPrev.resize(blkw * (blkh + 1) + 1, v0);
}

void ATVideoEncoderZMBV::Compress(const VDPixmap& px, bool intra, bool encodeAll) {
	mBuffer.swap(mBufferRef);
	mVecBuffer.swap(mVecBufferPrev);

	const VDPixmap& pxdst = VDPixmapFromLayout(mLayout, mBuffer.data());
	VDPixmapBlt(pxdst, px);

	if (mbRgb32) {
		uint8 *dstrow = (uint8 *)pxdst.data;
		for(uint32 y = 0; y < mHeight; ++y) {
			uint8 *dst = dstrow;

			for(uint32 x = 0; x < mWidth; ++x) {
				dst[3] = dst[2];
				dst += 4;
			}

			dstrow += pxdst.pitch;
		}
	}

	if (intra)
		CompressIntra8(px);
	else
		CompressInter8(encodeAll);
}

void ATVideoEncoderZMBV::CompressIntra8(const VDPixmap& px) {
	uint8 *dst0 = mPackBuffer.data();
	uint8 *dst = dst0;

	const uint32 w = mWidth;
	const uint32 h = mHeight;
	const uint8 *src = mBuffer.data() + mLayout.data;

	*dst++ = 0x01;	// intra
	*dst++ = 0x00;	// major
	*dst++ = 0x01;	// minor
	*dst++ = 0x01;	// zlib compressed
	*dst++ = mbRgb32 ? 0x08 : 0x04;	// 8-bit / 32-bit
	*dst++ = 16;	// 8x8 blocks
	*dst++ = 16;

	uint8 *base = dst;

	if (mbRgb32) {
		VDMemcpyRect(dst, w*4, src, mLayout.pitch, w * 4, h);
		dst += w * h * 4;
	} else {
		for(int i=0; i<256; ++i) {
			const uint32 c = px.palette[i];

			*dst++ = (uint8)(c >> 16);
			*dst++ = (uint8)(c >>  8);
			*dst++ = (uint8)(c >>  0);
		}

		VDMemcpyRect(dst, w, src, mLayout.pitch, w, h);
		dst += w * h;
	}

	// zlib compress frame
	mEncoder.Init(true);
	mEncoder.Write(base, dst - base);
	mEncoder.SyncBegin();
	const vdfastvector<uint8>& zdata = mEncoder.GetOutput();
	memcpy(base, zdata.data(), zdata.size());
	dst = base + zdata.size();
	mEncoder.SyncEnd();

	// write frame
	mEncodedLength = (uint32)(dst - dst0);
}

namespace {
	static const uint32 kMasks[28]={
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x000000ff, 0x0000ffff, 0x00ffffff, 0xffffffff,
		0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
		0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,   
		0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	};

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
	int BlockDiff16_8_SSE2(const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		static const VDALIGN(16) uint32 _m0[4] = { 0x55555555, 0x55555555, 0x55555555, 0x55555555 };
		static const VDALIGN(16) uint32 _m1[4] = { 0x33333333, 0x33333333, 0x33333333, 0x33333333 };
		static const VDALIGN(16) uint32 _m2[4] = { 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f };
		__m128i m0 = *(const __m128i *)_m0;
		__m128i m1 = *(const __m128i *)_m1;
		__m128i m2 = *(const __m128i *)_m2;
		__m128i zero = _mm_setzero_si128();
		__m128i err = zero;

		for(uint32 y=0; y<h; ++y) {
			__m128i a = *(__m128i *)src;
			__m128i b0 = _mm_loadl_epi64((const __m128i *)ref);
			__m128i b1 = _mm_loadl_epi64((const __m128i *)(ref + 8));
			__m128i b = _mm_unpacklo_epi64(b0, b1);
			__m128i e = _mm_xor_si128(a, b);

			e = _mm_sub_epi8(e, _mm_and_si128(_mm_srli_epi16(e, 1), m0));
			e = _mm_add_epi8(_mm_and_si128(e, m1), _mm_and_si128(_mm_srli_epi16(e, 2), m1));
			e = _mm_add_epi8(_mm_and_si128(e, m2), _mm_and_si128(_mm_srli_epi16(e, 4), m2));
			err = _mm_add_epi8(e, err);

			ref += pitch;
			src += pitch;
		}

		err = _mm_sad_epu8(err, zero);
		err = _mm_add_epi32(err, _mm_srli_si128(err, 8));

		return _mm_cvtsi128_si32(err);
	}

	int BlockDiff16_32_SSE2(const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		static const VDALIGN(16) uint32 _m0[4] = { 0x55555555, 0x55555555, 0x55555555, 0x55555555 };
		static const VDALIGN(16) uint32 _m1[4] = { 0x33333333, 0x33333333, 0x33333333, 0x33333333 };
		static const VDALIGN(16) uint32 _m2[4] = { 0x000f0f0f, 0x000f0f0f, 0x000f0f0f, 0x000f0f0f };	// not an error - drop dummy alpha
		__m128i m0 = *(const __m128i *)_m0;
		__m128i m1 = *(const __m128i *)_m1;
		__m128i m2 = *(const __m128i *)_m2;
		__m128i zero = _mm_setzero_si128();
		__m128i err = zero;

		for(uint32 y=0; y<h; ++y) {
			__m128i a0 = *(__m128i *)(src + 0);
			__m128i a1 = *(__m128i *)(src + 16);
			__m128i a2 = *(__m128i *)(src + 32);
			__m128i a3 = *(__m128i *)(src + 48);
			__m128i b0 = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)(ref +  0)), _mm_loadl_epi64((const __m128i *)(ref +  8)));
			__m128i b1 = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)(ref + 16)), _mm_loadl_epi64((const __m128i *)(ref + 24)));
			__m128i b2 = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)(ref + 32)), _mm_loadl_epi64((const __m128i *)(ref + 40)));
			__m128i b3 = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)(ref + 48)), _mm_loadl_epi64((const __m128i *)(ref + 56)));
			__m128i e0 = _mm_xor_si128(a0, b0);
			__m128i e1 = _mm_xor_si128(a1, b1);
			__m128i e2 = _mm_xor_si128(a2, b2);
			__m128i e3 = _mm_xor_si128(a3, b3);

			e0 = _mm_sub_epi8(e0, _mm_and_si128(m0, _mm_srli_epi16(e0, 1)));
			e1 = _mm_sub_epi8(e1, _mm_and_si128(m0, _mm_srli_epi16(e1, 1)));
			e2 = _mm_sub_epi8(e2, _mm_and_si128(m0, _mm_srli_epi16(e2, 1)));
			e3 = _mm_sub_epi8(e3, _mm_and_si128(m0, _mm_srli_epi16(e3, 1)));

			e0 = _mm_add_epi8(_mm_and_si128(m1, e0), _mm_and_si128(_mm_srli_epi16(e0, 2), m1));
			e1 = _mm_add_epi8(_mm_and_si128(m1, e1), _mm_and_si128(_mm_srli_epi16(e1, 2), m1));
			e2 = _mm_add_epi8(_mm_and_si128(m1, e2), _mm_and_si128(_mm_srli_epi16(e2, 2), m1));
			e3 = _mm_add_epi8(_mm_and_si128(m1, e3), _mm_and_si128(_mm_srli_epi16(e3, 2), m1));
																						
			e0 = _mm_add_epi8(_mm_and_si128(m2, e0), _mm_and_si128(_mm_srli_epi16(e0, 4), m2));
			e1 = _mm_add_epi8(_mm_and_si128(m2, e1), _mm_and_si128(_mm_srli_epi16(e1, 4), m2));
			e2 = _mm_add_epi8(_mm_and_si128(m2, e2), _mm_and_si128(_mm_srli_epi16(e2, 4), m2));
			e3 = _mm_add_epi8(_mm_and_si128(m2, e3), _mm_and_si128(_mm_srli_epi16(e3, 4), m2));

			__m128i e = _mm_adds_epu8(_mm_adds_epu8(e0, e1), _mm_adds_epu8(e2, e3));

			e = _mm_sad_epu8(e, zero);

			err = _mm_add_epi32(err, e);

			ref += pitch;
			src += pitch;
		}

		err = _mm_add_epi32(err, _mm_srli_si128(err, 8));

		return _mm_cvtsi128_si32(err);
	}
#endif

	int BlockDiff_8(const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		int err = 0;
		uint32 diff;
		uint32 mask0 = kMasks[w + 11];
		uint32 mask1 = kMasks[w + 7];
		uint32 mask2 = kMasks[w + 3];
		uint32 mask3 = kMasks[w - 1];

		for(uint32 y=0; y<h; ++y) {
			diff = (*(const uint32 *)&src[ 0] ^ *(const uint32 *)&ref[ 0]) & mask0;
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			diff = (*(const uint32 *)&src[ 4] ^ *(const uint32 *)&ref[ 4]) & mask1;
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			diff = (*(const uint32 *)&src[ 8] ^ *(const uint32 *)&ref[ 8]) & mask2;
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			diff = (*(const uint32 *)&src[12] ^ *(const uint32 *)&ref[12]) & mask3;
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			ref += pitch;
			src += pitch;
		}

		return err;
	}

	int BlockDiff_32(const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		int err = 0;
		uint32 diff;
		uint32 bytes4 = w*4;

		for(uint32 y=0; y<h; ++y) {
			for(uint32 x=0; x<bytes4; x+=4) {
				diff = (*(const uint32 *)&src[x] ^ *(const uint32 *)&ref[x]);
				diff -= (diff >> 1) & 0x55555555;
				diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
				diff = (diff + (diff >> 4)) & 0x000f0f0f;		// not an error - drop dummy alpha
				err += (diff * 0x01010101) >> 24;
			}

			ref += pitch;
			src += pitch;
		}

		return err;
	}

	int BlockDiff16_8(const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		int err = 0;
		uint32 diff;

		for(uint32 y=0; y<h; ++y) {
			diff = *(const uint32 *)&src[ 0] ^ *(const uint32 *)&ref[ 0];
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			diff = *(const uint32 *)&src[ 4] ^ *(const uint32 *)&ref[ 4];
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			diff = *(const uint32 *)&src[ 8] ^ *(const uint32 *)&ref[ 8];
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			diff = *(const uint32 *)&src[12] ^ *(const uint32 *)&ref[12];
			diff -= (diff >> 1) & 0x55555555;
			diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
			diff = (diff + (diff >> 4)) & 0x0f0f0f0f;
			err += (diff * 0x01010101) >> 24;

			ref += pitch;
			src += pitch;
		}

		return err;
	}

	int BlockDiff16_32(const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		int err = 0;
		uint32 diff;

		for(uint32 y=0; y<h; ++y) {
			for(uint32 x=0; x<64; x+=4) {
				diff = *(const uint32 *)&src[x] ^ *(const uint32 *)&ref[x];
				diff -= (diff >> 1) & 0x55555555;
				diff = ((diff & 0xcccccccc) >> 2) + (diff & 0x33333333);
				diff = (diff + (diff >> 4)) & 0x000f0f0f;
				err += (diff * 0x01010101) >> 24;
			}

			ref += pitch;
			src += pitch;
		}

		return err;
	}

	void ComputeXor(uint8 *dst, const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		for(uint32 y=0; y<h; ++y) {
			for(uint32 x=0; x<w; ++x)
				*dst++ = src[x] ^ ref[x];

			src += pitch;
			ref += pitch;
		}
	}

	void ComputeXor16_8(uint8 *dst, const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		for(uint32 y=0; y<h; ++y) {
			*(uint32 *)&dst[ 0] = *(const uint32 *)&src[ 0] ^ *(const uint32 *)&ref[ 0];
			*(uint32 *)&dst[ 4] = *(const uint32 *)&src[ 4] ^ *(const uint32 *)&ref[ 4];
			*(uint32 *)&dst[ 8] = *(const uint32 *)&src[ 8] ^ *(const uint32 *)&ref[ 8];
			*(uint32 *)&dst[12] = *(const uint32 *)&src[12] ^ *(const uint32 *)&ref[12];

			dst += 16;
			src += pitch;
			ref += pitch;
		}
	}

	void ComputeXor16_32(uint8 *dst, const uint8 *src, const uint8 *ref, ptrdiff_t pitch, uint32 w, uint32 h) {
		for(uint32 y=0; y<h; ++y) {
			*(uint32 *)&dst[ 0] = (*(const uint32 *)&src[ 0] ^ *(const uint32 *)&ref[ 0]) & 0x00ffffff;
			*(uint32 *)&dst[ 4] = (*(const uint32 *)&src[ 4] ^ *(const uint32 *)&ref[ 4]) & 0x00ffffff;
			*(uint32 *)&dst[ 8] = (*(const uint32 *)&src[ 8] ^ *(const uint32 *)&ref[ 8]) & 0x00ffffff;
			*(uint32 *)&dst[12] = (*(const uint32 *)&src[12] ^ *(const uint32 *)&ref[12]) & 0x00ffffff;
			*(uint32 *)&dst[16] = (*(const uint32 *)&src[16] ^ *(const uint32 *)&ref[16]) & 0x00ffffff;
			*(uint32 *)&dst[20] = (*(const uint32 *)&src[20] ^ *(const uint32 *)&ref[20]) & 0x00ffffff;
			*(uint32 *)&dst[24] = (*(const uint32 *)&src[24] ^ *(const uint32 *)&ref[24]) & 0x00ffffff;
			*(uint32 *)&dst[28] = (*(const uint32 *)&src[28] ^ *(const uint32 *)&ref[28]) & 0x00ffffff;
			*(uint32 *)&dst[32] = (*(const uint32 *)&src[32] ^ *(const uint32 *)&ref[32]) & 0x00ffffff;
			*(uint32 *)&dst[36] = (*(const uint32 *)&src[36] ^ *(const uint32 *)&ref[36]) & 0x00ffffff;
			*(uint32 *)&dst[40] = (*(const uint32 *)&src[40] ^ *(const uint32 *)&ref[40]) & 0x00ffffff;
			*(uint32 *)&dst[44] = (*(const uint32 *)&src[44] ^ *(const uint32 *)&ref[44]) & 0x00ffffff;
			*(uint32 *)&dst[48] = (*(const uint32 *)&src[48] ^ *(const uint32 *)&ref[48]) & 0x00ffffff;
			*(uint32 *)&dst[52] = (*(const uint32 *)&src[52] ^ *(const uint32 *)&ref[52]) & 0x00ffffff;
			*(uint32 *)&dst[56] = (*(const uint32 *)&src[56] ^ *(const uint32 *)&ref[56]) & 0x00ffffff;
			*(uint32 *)&dst[60] = (*(const uint32 *)&src[60] ^ *(const uint32 *)&ref[60]) & 0x00ffffff;

			dst += 64;
			src += pitch;
			ref += pitch;
		}
	}
}

void ATVideoEncoderZMBV::CompressInter8(bool encodeAll) {
	uint8 *dst0 = mPackBuffer.data();
	uint8 *dst = dst0;

	const uint32 w = mWidth;
	const uint32 h = mHeight;
	const uint8 *src = mBuffer.data() + mLayout.data;
	const uint8 *ref = mBufferRef.data() + mLayout.data;

	const uint32 bw = (w + 15) >> 4;
	const uint32 bh = (h + 15) >> 4;
	const uint32 bcount = bw * bh;
	const uint32 bxedge = w >> 4;
	const uint32 byedge = h >> 4;

	*dst++ = 0x00;	// inter

	uint8 *base = dst;

	uint8 *blkdst = dst;
	dst += bcount*2;

	if (bcount & 1) {
		*dst++ = 0;
		*dst++ = 0;
	}

	MotionVector *mvp = mVecBufferPrev.data() + bw + 1;
	MotionVector *mvc = mVecBuffer.data() + bw + 1;
	MotionVector mvcand[16];
	const ptrdiff_t pitch = mLayout.pitch;
	bool delta = false;

	const bool rgb32 = mbRgb32;
	int (*blockDiff)(const uint8 *, const uint8 *, ptrdiff_t, uint32, uint32) = rgb32 ? BlockDiff_32 : BlockDiff_8;
	int (*blockDiff16)(const uint8 *, const uint8 *, ptrdiff_t, uint32, uint32) = rgb32 ? BlockDiff16_32 : BlockDiff16_8;
	void (*computeXor)(uint8 *, const uint8 *, const uint8 *, ptrdiff_t, uint32, uint32) = ComputeXor;
	void (*computeXor16)(uint8 *, const uint8 *, const uint8 *, ptrdiff_t, uint32, uint32) = rgb32 ? ComputeXor16_32 : ComputeXor16_8;

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
	if (SSE2_enabled)
		blockDiff16 = rgb32 ? BlockDiff16_32_SSE2 : BlockDiff16_8_SSE2;
#endif

	for(uint32 by = 0; by < bh; ++by) {
		const uint8 *src2 = src;
		const uint8 *ref2 = ref;
		const uint32 blockh = (by == byedge) ? h & 15 : 16;

		for(uint32 bx = 0; bx < bw; ++bx) {
			const uint32 blockw = (bx == bxedge) ? w & 15 : 16;
			int (*bd)(const uint8 *, const uint8 *, ptrdiff_t, uint32, uint32) = (blockw == 16) ? blockDiff16 : blockDiff;
			MotionVector mvbest = {0, 0};
			int errbest = bd(src2, ref2, pitch, blockw, blockh);

			if (errbest) {
				int mvn = 0;
				mvcand[mvn++] = mvc[-1];
				mvcand[mvn++] = mvc[-(int)bw];
				mvcand[mvn++] = mvp[0];

				uint8 triedMasks[33*5] = {0};

				for(int pass = 0; pass < 20; ++pass) {
					bool improved = false;

					for(int i=0; i<mvn && errbest; ++i) {
						const MotionVector& mv = mvcand[i];

						if (abs(mv.x) > 16 || abs(mv.y) > 16)
							continue;

						int idx = (mv.y + 16) + ((unsigned)(mv.x + 16) >> 3)*5;
						uint8 bit = 1 << ((mv.x + 16) & 7);
						if (triedMasks[idx] & bit)
							continue;

						triedMasks[idx] |= bit;

						int err = bd(src2, ref2 + mv.y * pitch + (rgb32 ? mv.x*4 : mv.x), pitch, blockw, blockh);

						if (err < errbest) {
							mvbest = mv;
							errbest = err;
							improved = true;
						}
					}

					if (!errbest || (pass && !improved))
						break;

					mvn = 0;
					mvcand[mvn++] = mvbest.offset(-1,0);
					mvcand[mvn++] = mvbest.offset(+1,0);
					mvcand[mvn++] = mvbest.offset(0,-1);
					mvcand[mvn++] = mvbest.offset(0,+1);
					mvcand[mvn++] = mvbest.offset(-1,-1);
					mvcand[mvn++] = mvbest.offset(+1,-1);
					mvcand[mvn++] = mvbest.offset(-1,+1);
					mvcand[mvn++] = mvbest.offset(+1,+1);
					mvcand[mvn++] = mvbest.offset(-2,0);
					mvcand[mvn++] = mvbest.offset(+2,0);
					mvcand[mvn++] = mvbest.offset(0,-2);
					mvcand[mvn++] = mvbest.offset(0,+2);
				}
			}

			if (errbest) {
				blkdst[0] = mvbest.x + mvbest.x + 1;
				blkdst[1] = mvbest.y + mvbest.y;

				if (rgb32) {
					if (blockw == 16)
						computeXor16(dst, src2, ref2 + mvbest.y * pitch + mvbest.x*4, pitch, blockw*4, blockh);
					else
						computeXor(dst, src2, ref2 + mvbest.y * pitch + mvbest.x*4, pitch, blockw*4, blockh);

					dst += blockw*blockh*4;
				} else {
					if (blockw == 16)
						computeXor16(dst, src2, ref2 + mvbest.y * pitch + mvbest.x, pitch, blockw, blockh);
					else
						computeXor(dst, src2, ref2 + mvbest.y * pitch + mvbest.x, pitch, blockw, blockh);

					dst += blockw*blockh;
				}

			} else {
				blkdst[0] = mvbest.x + mvbest.x;
				blkdst[1] = mvbest.y + mvbest.y;
			}

			mvc[0] = mvbest;

			if (mvbest.x || mvbest.y || errbest)
				delta = true;

			if (rgb32) {
				src2 += 64;
				ref2 += 64;
			} else {
				src2 += 16;
				ref2 += 16;
			}

			blkdst += 2;
			++mvp;
			++mvc;
		}

		src += mLayout.pitch * 16;
		ref += mLayout.pitch * 16;
	}

	if (!delta && !encodeAll) {
		mEncodedLength = 0;
		return;
	}

	// zlib compress frame
	mEncoder.Write(base, dst - base);
	mEncoder.SyncBegin();
	const vdfastvector<uint8>& zdata = mEncoder.GetOutput();
	memcpy(base, zdata.data(), zdata.size());
	dst = base + zdata.size();
	mEncoder.SyncEnd();

	mEncodedLength = (uint32)(dst - dst0);
}

///////////////////////////////////////////////////////////////////////////////

class IATMediaEncoder {
public:
	virtual ~IATMediaEncoder() = default;

	virtual sint64 GetCurrentSize() = 0;

	virtual void WriteVideo(const VDPixmap& px) = 0;
	virtual void BeginAudioFrame(uint32 bytes, uint32 samples) = 0;
	virtual void WriteAudio(const sint16 *data, uint32 bytes) = 0;
	virtual void EndAudioFrame() = 0;
	virtual bool Finalize(MyError& e) = 0;
};

class ATAVIEncoder final : public IATMediaEncoder {
public:
	ATAVIEncoder(const wchar_t *filename, ATVideoEncoding venc, uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, double samplingRate, bool stereo, bool encodeAllFrames);

	sint64 GetCurrentSize() override;

	void WriteVideo(const VDPixmap& px) override;
	void BeginAudioFrame(uint32 bytes, uint32 samples) override;
	void WriteAudio(const sint16 *data, uint32 bytes) override;
	void EndAudioFrame() override;

	bool Finalize(MyError& e) override;

private:
	uint32 mKeyCounter = 0;
	uint32 mKeyInterval = 0;
	bool mbEncodeAllFrames = false;

	vdautoptr<IVDMediaOutputAVIFile> mFile;
	vdautoptr<IATVideoEncoder> mpVideoEncoder;
	IVDMediaOutputStream *mVideoStream = nullptr;
	IVDMediaOutputStream *mAudioStream = nullptr;
};

ATAVIEncoder::ATAVIEncoder(const wchar_t *filename, ATVideoEncoding venc, uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, double samplingRate, bool stereo, bool encodeAllFrames) {
	mbEncodeAllFrames = encodeAllFrames;
	mKeyCounter = 0;
	mKeyInterval = 60;

	mFile = VDCreateMediaOutputAVIFile();

	mVideoStream = mFile->createVideoStream();
	mAudioStream = mFile->createAudioStream();

	struct {
		VDAVIBitmapInfoHeader hdr;
		uint32 pal[256];
	} bmf;
	bmf.hdr.biSize			= sizeof bmf.hdr;
	bmf.hdr.biWidth			= w;
	bmf.hdr.biHeight		= h;
	bmf.hdr.biPlanes		= 1;
	bmf.hdr.biXPelsPerMeter	= 3150;
	bmf.hdr.biYPelsPerMeter	= 3150;
	bmf.hdr.biClrUsed		= venc != kATVideoEncoding_ZMBV && palette ? 256 : 0;
	bmf.hdr.biClrImportant	= bmf.hdr.biClrUsed;

	switch(venc) {
		case kATVideoEncoding_Raw:
			bmf.hdr.biBitCount		= palette ? 8 : 24;
			bmf.hdr.biCompression	= VDAVIBitmapInfoHeader::kCompressionRGB;
			bmf.hdr.biSizeImage		= w * h * (palette ? 1 : 3);
			break;

		case kATVideoEncoding_RLE:
			bmf.hdr.biBitCount		= 8;
			bmf.hdr.biCompression	= VDAVIBitmapInfoHeader::kCompressionRLE8;
			bmf.hdr.biSizeImage		= w * h * 2;
			break;

		case kATVideoEncoding_ZMBV:
			bmf.hdr.biCompression	= VDMAKEFOURCC('Z', 'M', 'B', 'V');
			bmf.hdr.biSizeImage		= palette ? w * h * 2 : w * h * 8;
			bmf.hdr.biBitCount = 0;
			break;
	}

	if (palette && venc != kATVideoEncoding_ZMBV) {
		for(int i=0; i<256; ++i)
			bmf.pal[i] = palette[i] & 0xffffff;
		mVideoStream->setFormat(&bmf, sizeof bmf);
	} else
		mVideoStream->setFormat(&bmf.hdr, sizeof bmf.hdr);

	AVIStreamHeader_fixed hdr;
	hdr.fccType					= VDMAKEFOURCC('v', 'i', 'd', 's');
    hdr.dwFlags					= 0;
    hdr.wPriority				= 0;
    hdr.wLanguage				= 0;
    hdr.dwInitialFrames			= 0;
    hdr.dwScale					= frameRate.getLo();
    hdr.dwRate					= frameRate.getHi();
    hdr.dwStart					= 0;
    hdr.dwLength				= 0;
    hdr.dwSuggestedBufferSize	= 0;
    hdr.dwQuality				= (uint32)-1;
    hdr.dwSampleSize			= 0;
	hdr.rcFrame.left			= 0;
	hdr.rcFrame.top				= 0;
	hdr.rcFrame.right			= w;
	hdr.rcFrame.bottom			= h;

	switch(venc) {
		case kATVideoEncoding_Raw:
			hdr.fccHandler				= VDMAKEFOURCC('D', 'I', 'B', ' ');
			break;

		case kATVideoEncoding_RLE:
			hdr.fccHandler				= VDMAKEFOURCC('m', 'r', 'l', 'e');
			break;

		case kATVideoEncoding_ZMBV:
			hdr.fccHandler				= VDMAKEFOURCC('Z', 'M', 'B', 'V');
			break;
	}

	mVideoStream->setStreamInfo(hdr);

	nsVDWinFormats::WaveFormatEx wf;
	wf.mFormatTag = nsVDWinFormats::kWAVE_FORMAT_PCM;
	wf.mChannels = stereo ? 2 : 1;
	wf.SetSamplesPerSec(48000);
	wf.mBlockAlign = 2 * wf.mChannels;
	wf.SetAvgBytesPerSec(48000 * wf.mBlockAlign);
	wf.mBitsPerSample = 16;
	wf.mSize = 0;

	mAudioStream->setFormat(&wf, offsetof(nsVDWinFormats::WaveFormatEx, mSize));
	hdr.fccType					= VDMAKEFOURCC('a', 'u', 'd', 's');
    hdr.fccHandler				= 0;
    hdr.dwFlags					= 0;
    hdr.wPriority				= 0;
    hdr.wLanguage				= 0;
    hdr.dwInitialFrames			= 0;
	hdr.dwScale					= wf.mBlockAlign;
	hdr.dwRate					= wf.GetAvgBytesPerSec();
    hdr.dwStart					= 0;
    hdr.dwLength				= 0;
    hdr.dwSuggestedBufferSize	= 0;
    hdr.dwQuality				= (uint32)-1;
	hdr.dwSampleSize			= wf.mBlockAlign;
	hdr.rcFrame.left			= 0;
	hdr.rcFrame.top				= 0;
	hdr.rcFrame.right			= 0;
	hdr.rcFrame.bottom			= 0;

	mAudioStream->setStreamInfo(hdr);

	mFile->setBuffering(4194304, 524288, IVDFileAsync::kModeAsynchronous);
	mFile->init(filename);

	switch(venc) {
		case kATVideoEncoding_Raw:
			mpVideoEncoder = new ATVideoEncoderRaw(w, h, palette ? nsVDPixmap::kPixFormat_Pal8 : nsVDPixmap::kPixFormat_RGB888);
			break;

		case kATVideoEncoding_RLE:
			mpVideoEncoder = new ATVideoEncoderRLE(w, h);
			break;

		case kATVideoEncoding_ZMBV:
			mpVideoEncoder = new ATVideoEncoderZMBV(w, h, palette == NULL);
			break;
	}
}

sint64 ATAVIEncoder::GetCurrentSize() {
	return mFile->GetCurrentSize();
}

void ATAVIEncoder::WriteVideo(const VDPixmap& px) {
	bool intra = false;

	if (!mKeyCounter) {
		mKeyCounter = mKeyInterval;
		intra = true;
	}

	--mKeyCounter;

	mpVideoEncoder->Compress(px, intra, mbEncodeAllFrames);

	uint32 len = mpVideoEncoder->GetEncodedLength();
	mVideoStream->write(len && intra ? IVDMediaOutputStream::kFlagKeyFrame : 0, mpVideoEncoder->GetEncodedData(), len, 1);
}

void ATAVIEncoder::BeginAudioFrame(uint32 bytes, uint32 samples) {
	mAudioStream->partialWriteBegin(IVDMediaOutputStream::kFlagKeyFrame, bytes, samples);
}

void ATAVIEncoder::WriteAudio(const sint16 *data, uint32 bytes) {
	mAudioStream->partialWrite(data, bytes);
}

void ATAVIEncoder::EndAudioFrame() {
	mAudioStream->partialWriteEnd();
}

bool ATAVIEncoder::Finalize(MyError& error) {
	if (mVideoStream) {
		try {
			mVideoStream->finish();
		} catch(MyError e) {
			if (e.empty())
				error.TransferFrom(e);
		}

		mVideoStream = nullptr;
	}

	if (mFile) {
		try {
			mFile->finalize();
		} catch(MyError e) {
			if (e.empty())
				error.TransferFrom(e);
		}

		mFile.reset();
	}

	mpVideoEncoder.reset();

	return error.empty();
}

///////////////////////////////////////////////////////////////////////////////

class ATMFSampleAllocatorW32 final : public IMFSinkWriterCallback {
public:
	LPVOID AddSample(IMFSample *sampleAdoptRef);
	bool AllocateCachedSample(IMFSample **sample);
	HRESULT WaitForFinalize();
	void Shutdown();

public: // IUnknown
	DWORD STDMETHODCALLTYPE AddRef() override;
	DWORD STDMETHODCALLTYPE Release() override;
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **ppvObj) override;

public:	// IMFSinkWriterCallback
	HRESULT STDMETHODCALLTYPE OnFinalize(HRESULT hrStatus) override;
	HRESULT STDMETHODCALLTYPE OnMarker(DWORD dwStreamIndex, LPVOID pvContext) override;

private:
	uint32 mSampleQueueMax = 32;

	VDAtomicInt mRefCount { 0 };
	VDSignal mFinalized;
	HRESULT mFinalizationResult = S_OK;

	VDCriticalSection mMutex;
	uint32 mSampleQueueBeginId = 0;
	uint32 mSampleQueueNextInUseId = 0;
	uint32 mSampleQueueEndId = 0;
	vdfastdeque<IMFSample *> mSamples;
};

LPVOID ATMFSampleAllocatorW32::AddSample(IMFSample *sampleAdoptRef) {
	sampleAdoptRef->AddRef();

	IMFSample *sampleToDrop = nullptr;
	uint32 fenceId = 0;
	vdsynchronized(mMutex) {
		VDASSERT(mSampleQueueEndId == (uint32)(mSampleQueueBeginId + mSamples.size()));
		VDASSERT((uint32)(mSampleQueueNextInUseId - mSampleQueueBeginId) <= (uint32)(mSampleQueueEndId - mSampleQueueBeginId));

		mSamples.push_back(sampleAdoptRef);

		if (mSamples.size() > mSampleQueueMax) {
			sampleToDrop = mSamples.front();
			mSamples.pop_front();

			++mSampleQueueBeginId;
			++mSampleQueueNextInUseId;
		}

		fenceId = ++mSampleQueueEndId;
	}

	if (sampleToDrop)
		sampleToDrop->Release();

	return (LPVOID)(uintptr)fenceId;
}

bool ATMFSampleAllocatorW32::AllocateCachedSample(IMFSample **sample) {
	IMFSample *reclaimedSample = nullptr;

	vdsynchronized(mMutex) {
		VDASSERT(mSampleQueueEndId == (uint32)(mSampleQueueBeginId + mSamples.size()));
		VDASSERT((uint32)(mSampleQueueNextInUseId - mSampleQueueBeginId) <= (uint32)(mSampleQueueEndId - mSampleQueueBeginId));

		if (mSampleQueueNextInUseId != mSampleQueueBeginId) {
			reclaimedSample = mSamples.front();
			mSamples.pop_front();

			++mSampleQueueBeginId;
		}
	}

	if (!reclaimedSample)
		return false;

	*sample = reclaimedSample;
	return true;
}

HRESULT ATMFSampleAllocatorW32::WaitForFinalize() {
	WaitForSingleObjectEx((HANDLE)mFinalized.getHandle(), INFINITE, TRUE);

	return mFinalizationResult;
}

void ATMFSampleAllocatorW32::Shutdown() {
	vdfastdeque<IMFSample *> samples;

	vdsynchronized(mMutex) {
		samples.swap(mSamples);
	}

	for(IMFSample *sample : samples)
		sample->Release();
}

DWORD STDMETHODCALLTYPE ATMFSampleAllocatorW32::AddRef() {
	VDASSERT(mRefCount < 1000000);
	return ++mRefCount;
}

DWORD STDMETHODCALLTYPE ATMFSampleAllocatorW32::Release() {
	uint32 rc = --mRefCount;
	VDASSERT(rc >= 0 && rc < 1000000);

	if (!rc)
		delete this;

	return rc;
}

HRESULT STDMETHODCALLTYPE ATMFSampleAllocatorW32::QueryInterface(REFIID iid, void **ppvObj) {
	if (!ppvObj)
		return E_POINTER;

	if (iid == IID_IUnknown) {
		*ppvObj = static_cast<IUnknown *>(this);
	} else if (iid == __uuidof(IMFSinkWriterCallback)) {
		*ppvObj = static_cast<IMFSinkWriterCallback *>(this);
	} else {
		*ppvObj = nullptr;
		return E_NOINTERFACE;
	}

	AddRef();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATMFSampleAllocatorW32::OnFinalize(HRESULT hrStatus) {
	mFinalizationResult = hrStatus;

	mFinalized.signal();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATMFSampleAllocatorW32::OnMarker(DWORD dwStreamIndex, LPVOID pvContext) {
	uint32 sampleFenceId = (uint32)(uintptr)pvContext;

	vdsynchronized(mMutex) {
		uint32 newOffset = sampleFenceId - mSampleQueueBeginId;

		// It is legitimate for the fence offset to be outside of the current sample queue window.
		// This happens if Media Foundation decides to buffer more samples than we care to track in
		// our queue, which has a safety limit to prevent a perpetual memory leak if fencing fails.
		// In that case, we will have preemptively advanced the beginning of the queue past submitted
		// fences.

		if (newOffset <= mSampleQueueEndId - mSampleQueueBeginId) {
			uint32 oldOffset = mSampleQueueNextInUseId - mSampleQueueBeginId;

			if (newOffset > oldOffset) {
				mSampleQueueNextInUseId = sampleFenceId;
				VDASSERT((uint32)(mSampleQueueNextInUseId - mSampleQueueBeginId) <= (uint32)(mSampleQueueEndId - mSampleQueueBeginId));
			}
		}
	}

	return S_OK;
}

///////////////////////////////////////////////////////////////////////////////

class ATMediaFoundationEncoderW32 final : public IATMediaEncoder {
public:
	ATMediaFoundationEncoderW32(const wchar_t *filename, ATVideoEncoding venc, uint32 videoBitRate, uint32 audioBitRate, uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, double samplingRate, bool stereo, bool useYUV);
	~ATMediaFoundationEncoderW32();

	sint64 GetCurrentSize() override;

	void WriteVideo(const VDPixmap& px) override;
	void BeginAudioFrame(uint32 bytes, uint32 samples) override;
	void WriteAudio(const sint16 *data, uint32 bytes) override;
	void EndAudioFrame() override;

	bool Finalize(MyError& e) override;

private:
	void Init(const wchar_t *filename, ATVideoEncoding venc, uint32 videoBitRate, uint32 audioBitRate, uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, double samplingRate, bool stereo, bool useYUV);
	void Shutdown();

	bool mbMFInited = false;
	vdrefptr<IMFSinkWriter> mpSinkWriter;

	VDPixmapLayout mVideoFrameLayout {};
	uint32 mVideoFrameSize = 0;
	uint32 mVideoFrameCount = 0;
	VDFraction mVideoFrameRate { 0, 0 };
	LONGLONG mVideoNextSampleTime = 0;

	VDPixmapCachedBlitter mVideoBlitter;

	vdrefptr<IMFMediaBuffer> mpAudioBuffer;
	BYTE *mpAudioDst = nullptr;
	BYTE *mpAudioDstEnd = nullptr;
	uint64 mAudioSamplesWritten = 0;
	LONGLONG mAudioNextSampleTime = 0;
	bool mbAudioConvertToStereo = false;
	uint32 mAudioSampleSize = 0;

	vdrefptr<ATMFSampleAllocatorW32> mpVideoSampleAllocator;

	DWORD mVideoStreamIndex = 0;
	DWORD mAudioStreamIndex = 0;

	HMODULE mhmodMFPlat = nullptr;
	HMODULE mhmodMFReadWrite = nullptr;

	decltype(&MFStartup) mpfnMFStartup = nullptr;
	decltype(&MFShutdown) mpfnMFShutdown = nullptr;
	decltype(&MFCreateMemoryBuffer) mpfnMFCreateMemoryBuffer = nullptr;
	decltype(&MFCreateAlignedMemoryBuffer) mpfnMFCreateAlignedMemoryBuffer = nullptr;
	decltype(&MFCreateMediaType) mpfnMFCreateMediaType = nullptr;
	decltype(&MFCreateSample) mpfnMFCreateSample = nullptr;
	decltype(&MFCreateAttributes) mpfnMFCreateAttributes = nullptr;
	decltype(&MFCreateSinkWriterFromURL) mpfnMFCreateSinkWriterFromURL = nullptr;

	class HRVerify {
	public:
		void operator+=(HRESULT hr) const {
			if (FAILED(hr))
				throw MyWin32Error("Media encoding failed: %%s", hr);
		}
	};
};

ATMediaFoundationEncoderW32::ATMediaFoundationEncoderW32(const wchar_t *filename, ATVideoEncoding venc, uint32 videoBitRate, uint32 audioBitRate, uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, double samplingRate, bool stereo, bool useYUV) {
	try {
		Init(filename, venc, videoBitRate, audioBitRate, w, h, frameRate, palette, samplingRate, stereo, useYUV);
	} catch(...) {
		MyError e;
		Finalize(e);
		throw;
	}
}

ATMediaFoundationEncoderW32::~ATMediaFoundationEncoderW32() {
	Shutdown();
}

void ATMediaFoundationEncoderW32::Init(const wchar_t *filename, ATVideoEncoding venc,
	uint32 videoBitRate, uint32 audioBitRate,
	uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, double samplingRate, bool stereo,
	bool useYUV)
{
	HRVerify verify;

	if (!VDIsAtLeastVistaW32())
		throw MyError("Cannot encode in this format as Media Foundation services are not available on this version of Windows.");

	mhmodMFPlat = VDLoadSystemLibraryW32("MFPlat.dll");
	if (!mhmodMFPlat)
		throw MyWin32Error("Unable to load MFPlat.dll: %%s", GetLastError());

	mhmodMFReadWrite = VDLoadSystemLibraryW32("MFReadWrite.dll");
	if (!mhmodMFReadWrite)
		throw MyWin32Error("Unable to load MFReadWrite.dll: %%s", GetLastError());

	const auto ResolveImport = [](auto*& fnptr, HMODULE hmod, const char *name) {
		fnptr = (std::remove_reference_t<decltype(fnptr)>)GetProcAddress(hmod, name);
		if (!fnptr)
			throw MyError("Unable to initialize Media Foundation: could not resolve function %s().", name);
	};
	
	ResolveImport(mpfnMFStartup,					mhmodMFPlat, "MFStartup");
	ResolveImport(mpfnMFShutdown,					mhmodMFPlat, "MFShutdown");
	ResolveImport(mpfnMFCreateMemoryBuffer,			mhmodMFPlat, "MFCreateMemoryBuffer");
	ResolveImport(mpfnMFCreateAlignedMemoryBuffer,	mhmodMFPlat, "MFCreateAlignedMemoryBuffer");
	ResolveImport(mpfnMFCreateMediaType,			mhmodMFPlat, "MFCreateMediaType");
	ResolveImport(mpfnMFCreateSample,				mhmodMFPlat, "MFCreateSample");
	ResolveImport(mpfnMFCreateAttributes,			mhmodMFPlat, "MFCreateAttributes");
	ResolveImport(mpfnMFCreateSinkWriterFromURL,	mhmodMFReadWrite, "MFCreateSinkWriterFromURL");

	mVideoFrameRate = frameRate;

	if (useYUV) {
		// We alias NV12 as Y8 in our layout -- we'll be using a custom blitter for this anyway.
		mVideoFrameSize = VDPixmapCreateLinearLayout(mVideoFrameLayout, nsVDPixmap::kPixFormat_Y8, w, h + (h >> 1), 1);

		// Round the frame size up to a multiple of 16 and add another 32 bytes. This lets us
		// safely overwrite up to a whole xmmword during the chroma conversion process for speed.
		mVideoFrameSize = (mVideoFrameSize + 15 + 32) & ~15;
	} else {
		mVideoFrameSize = VDPixmapCreateLinearLayout(mVideoFrameLayout, nsVDPixmap::kPixFormat_XRGB8888, w, h, 4);
		VDPixmapLayoutFlipV(mVideoFrameLayout);
	}

	verify += mpfnMFStartup(MF_VERSION, MFSTARTUP_LITE);
	mbMFInited = true;

	vdrefptr<IMFAttributes> sinkWriterAttributes;

	verify += mpfnMFCreateAttributes(~sinkWriterAttributes, 1);

	// Enable hardware encoders if possible as they are MUCH faster. Note that it is critical that we pass
	// YUV input in this case. The RGB32>YV12 converter in msvproc.dll is somewhat OK, but the RGB32>NV12
	// converter is unvectorized and very slow, and it is hit if the Intel hardware encoder is used. We
	// can do this much faster as we can convert to YUV pre-upscale with SSE2 code.
	//
	verify += sinkWriterAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 1);

	// force the container type regardless of extension
	verify += sinkWriterAttributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE,
		venc == kATVideoEncoding_H264_AAC || venc == kATVideoEncoding_H264_MP3 ? MFTranscodeContainerType_MPEG4 : MFTranscodeContainerType_ASF);

	// create our sample allocator and bind it as a callback
	mpVideoSampleAllocator = new ATMFSampleAllocatorW32;

	verify += sinkWriterAttributes->SetUnknown(MF_SINK_WRITER_ASYNC_CALLBACK, mpVideoSampleAllocator);

	// create a sink writer
	verify += mpfnMFCreateSinkWriterFromURL(filename, nullptr, sinkWriterAttributes, ~mpSinkWriter);
	sinkWriterAttributes.clear();

	vdrefptr<IMFMediaType> mediaTypeOut;
	verify += mpfnMFCreateMediaType(~mediaTypeOut);

	verify += mediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);

	switch(venc) {
		case kATVideoEncoding_WMV7:
			verify += mediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_WMV1);
			break;

		case kATVideoEncoding_WMV9:
			verify += mediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_WMV3);
			break;

		case kATVideoEncoding_H264_AAC:
		case kATVideoEncoding_H264_MP3:
			verify += mediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
			break;
	}

	verify += mediaTypeOut->SetUINT32(MF_MT_AVG_BITRATE, std::clamp<uint32>(videoBitRate, 500000, 8000000));
	verify += mediaTypeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	verify += MFSetAttributeSize(mediaTypeOut, MF_MT_FRAME_SIZE, w, h);
	verify += MFSetAttributeRatio(mediaTypeOut, MF_MT_FRAME_RATE, frameRate.getHi(), frameRate.getLo());
	verify += MFSetAttributeRatio(mediaTypeOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

	verify += mpSinkWriter->AddStream(mediaTypeOut, &mVideoStreamIndex);
	mediaTypeOut.clear();

	vdrefptr<IMFMediaType> mediaTypeIn;
	verify += mpfnMFCreateMediaType(~mediaTypeIn);

	verify += mediaTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);

	if (useYUV) {
		verify += mediaTypeIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
		verify += mediaTypeIn->SetUINT32(MF_MT_VIDEO_CHROMA_SITING, MFVideoChromaSubsampling_MPEG2);
		verify += mediaTypeIn->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
		verify += mediaTypeIn->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
	} else {
		verify += mediaTypeIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	}

	verify += mediaTypeIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	verify += mediaTypeIn->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);

	// This shouldn't be necessary since RGB32 defaults to bottom-up, but the WMV encoder flips the
	// video if it is not explicitly set.
	verify += mediaTypeIn->SetUINT32(MF_MT_DEFAULT_STRIDE, useYUV ? w : (UINT32)0 - w*4);

	verify += MFSetAttributeSize(mediaTypeIn, MF_MT_FRAME_SIZE, w, h);
	verify += MFSetAttributeRatio(mediaTypeIn, MF_MT_FRAME_RATE, frameRate.getHi(), frameRate.getLo());
	verify += MFSetAttributeRatio(mediaTypeIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

	verify += mpSinkWriter->SetInputMediaType(mVideoStreamIndex, mediaTypeIn, nullptr);
	mediaTypeIn.clear();

	////////////////////////////////////////

	// WMAv8 has few mono-only modes, so force stereo
	mbAudioConvertToStereo = false;

	if (venc != kATVideoEncoding_H264_AAC && venc != kATVideoEncoding_H264_MP3 && !stereo) {
		mbAudioConvertToStereo = true;
		stereo = true;
	}

	const uint32 samplesPerSecond = 48000;
	const uint32 numChannels = stereo ? 2 : 1;

	mAudioSampleSize = numChannels * sizeof(sint16);

	verify += mpfnMFCreateMediaType(~mediaTypeOut);

	verify += mediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	verify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, samplesPerSecond);
	verify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, numChannels);

	if (venc == kATVideoEncoding_H264_AAC) {
		verify += mediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
		verify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);

		// The AAC encoder only accepts 12000, 16000, 20000, and 24000.
		verify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, ((std::clamp<uint32>(audioBitRate, 96000, 192000) + 16000) / 32000) * 4000);
	} else if (venc == kATVideoEncoding_H264_MP3) {
		verify += mediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_MP3);

		// The MP3 Encoder MF filter only supports up to 128kbps in mono.
		const uint32 bitrate = ((std::clamp<uint32>(audioBitRate, 64000, numChannels > 1 ? 256000 : 128000) + 16000) / 32000) * 32000;
		verify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bitrate >> 3);

		MPEGLAYER3WAVEFORMAT wf;

		wf.wID = MPEGLAYER3_ID_MPEG;
		wf.fdwFlags = MPEGLAYER3_FLAG_PADDING_OFF;
		wf.nBlockSize = (144 * bitrate) / samplesPerSecond;
		wf.nFramesPerBlock = 1;
		wf.nCodecDelay = 0;

		verify += mediaTypeOut->SetBlob(MF_MT_USER_DATA, (const UINT8 *)&wf.wID, 12);
		verify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_PREFER_WAVEFORMATEX, 1);
		verify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
	} else {
		verify += mediaTypeOut->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_WMAudioV8);
		verify += mediaTypeOut->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
		verify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);

		// The WMA codecs are very picky about byte rate and block alignment. These values were
		// determined by checking the output types from the filter. Unfortunately, we can't do this
		// with Sink Writer as it creates the transform and sets the output type first on stream add.
		static constexpr struct WMAProfile {
			uint32 mByteRate;
			uint32 mBlockAlignment;
		} kWMAProfiles[] = {
			{ 24000, 8192 },
			{ 20001, 6827 },
			{ 16002, 5462 },
			{ 12000, 4096 },
		};

		const auto& profile = kWMAProfiles[(std::clamp<uint32>(audioBitRate, 96000, 192000) - 96000 + 16000) / 32000];

		verify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, profile.mByteRate);
		verify += mediaTypeOut->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, profile.mBlockAlignment);
	}

	verify += mpSinkWriter->AddStream(mediaTypeOut, &mAudioStreamIndex);
	mediaTypeOut.clear();

	verify += mpfnMFCreateMediaType(~mediaTypeIn);

	verify += mediaTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	verify += mediaTypeIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	verify += mediaTypeIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	verify += mediaTypeIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, samplesPerSecond);
	verify += mediaTypeIn->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, samplesPerSecond * mAudioSampleSize);
	verify += mediaTypeIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, numChannels);
	verify += mediaTypeIn->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, mAudioSampleSize);

	if (venc == kATVideoEncoding_H264_MP3)
		verify += mediaTypeIn->SetUINT32(MF_MT_AUDIO_PREFER_WAVEFORMATEX, 1);

	verify += mpSinkWriter->SetInputMediaType(mAudioStreamIndex, mediaTypeIn, nullptr);
	mediaTypeIn.clear();

	////////////////////////////////////////

	verify += mpSinkWriter->BeginWriting();
}

void ATMediaFoundationEncoderW32::Shutdown() {
	if (mpSinkWriter) {
		HRESULT hr = mpSinkWriter->Finalize();

		// Since the video sample allocator registers as a callback on the sink writer, the finalize
		// is synchronous and we must wait for it.
		if (SUCCEEDED(hr) && mpVideoSampleAllocator)
			mpVideoSampleAllocator->WaitForFinalize();

		mpSinkWriter.clear();
	}

	mpAudioBuffer.clear();

	if (mbMFInited) {
		mbMFInited = false;
		mpfnMFShutdown();
	}

	if (mhmodMFReadWrite) {
		FreeLibrary(mhmodMFReadWrite);
		mhmodMFReadWrite = nullptr;
	}

	if (mhmodMFPlat) {
		FreeLibrary(mhmodMFPlat);
		mhmodMFPlat = nullptr;
	}
}

bool ATMediaFoundationEncoderW32::Finalize(MyError& e) {
	if (mpSinkWriter) {
		try {
			HRVerify verify;
			verify += mpSinkWriter->Finalize();

			// Since the video sample allocator registers as a callback on the sink writer, the finalize
			// is synchronous and we must wait for it.
			if (mpVideoSampleAllocator)
				verify += mpVideoSampleAllocator->WaitForFinalize();

		} catch(MyError error) {
			e.TransferFrom(error);
		}

		mpSinkWriter.clear();
	}

	return e.empty();
}

sint64 ATMediaFoundationEncoderW32::GetCurrentSize() {
	if (mpSinkWriter) {
		MF_SINK_WRITER_STATISTICS stats { sizeof(MF_SINK_WRITER_STATISTICS) };

		HRESULT hr = mpSinkWriter->GetStatistics(MF_SINK_WRITER_ALL_STREAMS, &stats);
		if (SUCCEEDED(hr))
			return stats.qwByteCountProcessed;
	}

	return 0;
}

namespace {
	void BlitChroma444ToNV12_Reference(void *uvdst, ptrdiff_t uvpitch, const void *usrc, ptrdiff_t upitch, const void *vsrc, ptrdiff_t vpitch, uint32 w, uint32 h) {
		uint8 *uvdst8 = (uint8 *)uvdst;
		const uint8 *usrc8 = (uint8 *)usrc;
		const uint8 *vsrc8 = (uint8 *)vsrc;

		while(h--) {
			uint8 *VDRESTRICT uvdst2 = uvdst8;
			uvdst8 += uvpitch;

			const uint8 *VDRESTRICT usrca2 = usrc8;
			usrc8 += upitch;
			const uint8 *VDRESTRICT usrcb2 = usrc8;
			usrc8 += upitch;
			const uint8 *VDRESTRICT vsrca2 = vsrc8;
			vsrc8 += vpitch;
			const uint8 *VDRESTRICT vsrcb2 = vsrc8;
			vsrc8 += vpitch;

			*uvdst2++ = (uint8)(((usrca2[0] + usrcb2[0])*3 + usrca2[1] + usrcb2[1] + 4) >> 3);
			usrca2 += 1;
			usrcb2 += 1;

			*uvdst2++ = (uint8)(((vsrca2[0] + vsrcb2[0])*3 + vsrca2[1] + vsrcb2[1] + 4) >> 3);
			vsrca2 += 1;
			vsrcb2 += 1;

			for(uint32 i=1; i<w; ++i) {
				*uvdst2++ = (uint8)(((usrca2[0] + usrcb2[0]) + (usrca2[1] + usrcb2[1])*2 + (usrca2[2] + usrcb2[2]) + 4) >> 3);
				usrca2 += 2;
				usrcb2 += 2;

				*uvdst2++ = (uint8)(((vsrca2[0] + vsrcb2[0]) + (vsrca2[1] + vsrcb2[1])*2 + (vsrca2[2] + vsrcb2[2]) + 4) >> 3);
				vsrca2 += 2;
				vsrcb2 += 2;
			}
		}
	}

#if VD_CPU_X86 || VD_CPU_X64
	void BlitChroma444ToNV12_SSE2(void *uvdst, ptrdiff_t uvpitch, const void *usrc, ptrdiff_t upitch, const void *vsrc, ptrdiff_t vpitch, uint32 w, uint32 h) {
		uint8 *uvdst8 = (uint8 *)uvdst;
		const uint8 *usrc8 = (uint8 *)usrc;
		const uint8 *vsrc8 = (uint8 *)vsrc;

		uint32 w8 = w >> 3;

		__m128i umask = _mm_set1_epi16(0x00FF);
		__m128i rightAndMask = ATIntrinGetEndMask_SSE2(2 * (w & 7));
		__m128i rightOrMask = _mm_andnot_si128(rightAndMask, _mm_set1_epi8(-0x80));

		while(h--) {
			uint8 *VDRESTRICT uvdst2 = uvdst8;
			uvdst8 += uvpitch;

			const uint8 *VDRESTRICT usrca2 = usrc8;
			usrc8 += upitch;
			const uint8 *VDRESTRICT usrcb2 = usrc8;
			usrc8 += upitch;
			const uint8 *VDRESTRICT vsrca2 = vsrc8;
			vsrc8 += vpitch;
			const uint8 *VDRESTRICT vsrcb2 = vsrc8;
			vsrc8 += vpitch;

			// We take advantage of our known layout here, which is 16-byte aligned chroma scanlines with 16 bytes before
			// and after. This means that the first and last segments might just need some extra masking.
			__m128i prevU = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(usrca2 - 1)), _mm_loadu_si128((const __m128i *)(usrcb2 - 1)));
			__m128i prevV = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(vsrca2 - 2)), _mm_loadu_si128((const __m128i *)(vsrcb2 - 2)));
			prevU = _mm_insert_epi16(prevU, 0x8080, 0);
			prevV = _mm_insert_epi16(prevV, 0x8080, 0);

			__m128i curU  = _mm_avg_epu8(_mm_load_si128 ((const __m128i *)(usrca2 + 0)), _mm_load_si128 ((const __m128i *)(usrcb2 + 0)));
			__m128i curV  = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(vsrca2 - 1)), _mm_loadu_si128((const __m128i *)(vsrcb2 - 1)));
			__m128i nextU = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(usrca2 + 1)), _mm_loadu_si128((const __m128i *)(usrcb2 + 1)));
			__m128i nextV = _mm_avg_epu8(_mm_load_si128 ((const __m128i *)(vsrca2 + 0)), _mm_load_si128 ((const __m128i *)(vsrcb2 + 0)));
			usrca2 += 16;
			usrcb2 += 16;
			vsrca2 += 16;
			vsrcb2 += 16;

			__m128i u = _mm_avg_epu8(_mm_avg_epu8(prevU, nextU), curU);
			__m128i v = _mm_avg_epu8(_mm_avg_epu8(prevV, nextV), curV);

			__m128i uv = _mm_or_si128(_mm_and_si128(umask, u), _mm_andnot_si128(umask, v));

			_mm_store_si128((__m128i *)uvdst2, uv);
			uvdst2 += 16;

			for(uint32 i = 1; i < w8; ++i) {
				__m128i prevU = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(usrca2 - 1)), _mm_loadu_si128((const __m128i *)(usrcb2 - 1)));
				__m128i prevV = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(vsrca2 - 2)), _mm_loadu_si128((const __m128i *)(vsrcb2 - 2)));
				__m128i curU  = _mm_avg_epu8(_mm_load_si128 ((const __m128i *)(usrca2 + 0)), _mm_load_si128 ((const __m128i *)(usrcb2 + 0)));
				__m128i curV  = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(vsrca2 - 1)), _mm_loadu_si128((const __m128i *)(vsrcb2 - 1)));
				__m128i nextU = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(usrca2 + 1)), _mm_loadu_si128((const __m128i *)(usrcb2 + 1)));
				__m128i nextV = _mm_avg_epu8(_mm_load_si128 ((const __m128i *)(vsrca2 + 0)), _mm_load_si128 ((const __m128i *)(vsrcb2 + 0)));
				usrca2 += 16;
				usrcb2 += 16;
				vsrca2 += 16;
				vsrcb2 += 16;

				__m128i u = _mm_avg_epu8(_mm_avg_epu8(prevU, nextU), curU);
				__m128i v = _mm_avg_epu8(_mm_avg_epu8(prevV, nextV), curV);

				__m128i uv = _mm_or_si128(_mm_and_si128(umask, u), _mm_andnot_si128(umask, v));

				_mm_store_si128((__m128i *)uvdst2, uv);
				uvdst2 += 16;
			}

			// do leftover bytes
			if (w & 7) {
				__m128i prevU = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(usrca2 - 1)), _mm_loadu_si128((const __m128i *)(usrcb2 - 1)));
				__m128i prevV = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(vsrca2 - 2)), _mm_loadu_si128((const __m128i *)(vsrcb2 - 2)));
				__m128i curU  = _mm_avg_epu8(_mm_load_si128 ((const __m128i *)(usrca2 + 0)), _mm_load_si128 ((const __m128i *)(usrcb2 + 0)));
				__m128i curV  = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(vsrca2 - 1)), _mm_loadu_si128((const __m128i *)(vsrcb2 - 1)));
				__m128i nextU = _mm_avg_epu8(_mm_loadu_si128((const __m128i *)(usrca2 + 1)), _mm_loadu_si128((const __m128i *)(usrcb2 + 1)));
				__m128i nextV = _mm_avg_epu8(_mm_load_si128 ((const __m128i *)(vsrca2 + 0)), _mm_load_si128 ((const __m128i *)(vsrcb2 + 0)));

				nextU = _mm_or_si128(_mm_and_si128(nextU, rightAndMask), rightOrMask);
				nextV = _mm_or_si128(_mm_and_si128(nextV, rightAndMask), rightOrMask);

				usrca2 += 16;
				usrcb2 += 16;
				vsrca2 += 16;
				vsrcb2 += 16;

				__m128i u = _mm_avg_epu8(_mm_avg_epu8(prevU, nextU), curU);
				__m128i v = _mm_avg_epu8(_mm_avg_epu8(prevV, nextV), curV);

				__m128i uv = _mm_or_si128(_mm_and_si128(umask, u), _mm_andnot_si128(umask, v));

				_mm_store_si128((__m128i *)uvdst2, uv);
				uvdst2 += 16;
			}
		}
	}
#endif

	void BlitChroma420ToNV12_Reference(void *uvdst, ptrdiff_t uvpitch, const void *usrc, ptrdiff_t upitch, const void *vsrc, ptrdiff_t vpitch, uint32 w, uint32 h) {
		uint8 *uvdst8 = (uint8 *)uvdst;
		const uint8 *usrc8 = (uint8 *)usrc;
		const uint8 *vsrc8 = (uint8 *)vsrc;

		while(h--) {
			uint8 *VDRESTRICT uvdst2 = (uint8 *)uvdst8;
			uvdst8 += uvpitch;

			const uint8 *VDRESTRICT usrc2 = (const uint8 *)usrc8;
			usrc8 += upitch;
			const uint8 *VDRESTRICT vsrc2 = (const uint8 *)usrc8;
			vsrc8 += vpitch;

			for(uint32 i=w; i; --i) {
				*uvdst2++ = *usrc2++;
				*uvdst2++ = *vsrc2++;
			}
		}
	}

#if VD_CPU_X86 || VD_CPU_X64
	void BlitChroma420ToNV12_SSE2(void *uvdst, ptrdiff_t uvpitch, const void *usrc, ptrdiff_t upitch, const void *vsrc, ptrdiff_t vpitch, uint32 w, uint32 h) {
		uint8 *uvdst8 = (uint8 *)uvdst;
		const uint8 *usrc8 = (uint8 *)usrc;
		const uint8 *vsrc8 = (uint8 *)vsrc;

		uint32 w16 = (w + 15) >> 4;

		while(h--) {
			__m128i *VDRESTRICT uvdst2 = (__m128i *)uvdst8;
			uvdst8 += uvpitch;

			const __m128i *VDRESTRICT usrc2 = (const __m128i *)usrc8;
			usrc8 += upitch;
			const __m128i *VDRESTRICT vsrc2 = (const __m128i *)vsrc8;
			vsrc8 += vpitch;

			for(uint32 i=w16; i; --i) {
				__m128i u = *usrc2++;
				__m128i v = *vsrc2++;

				// This may overwrite up to 30 bytes, but we've guaranteed that we have this
				// padding at the end of the buffer.
				_mm_storeu_si128(uvdst2++, _mm_unpacklo_epi8(u, v));
				_mm_storeu_si128(uvdst2++, _mm_unpackhi_epi8(u, v));
			}
		}
	}
#endif

	void BlitChroma444ToNV12(void *uvdst, ptrdiff_t uvpitch, const void *usrc, ptrdiff_t upitch, const void *vsrc, ptrdiff_t vpitch, uint32 w, uint32 h) {
#if VD_CPU_X86 || VD_CPU_X64
		if (w >= 32 && SSE2_enabled) {
			BlitChroma444ToNV12_SSE2(uvdst, uvpitch, usrc, upitch, vsrc, vpitch, w, h);
			return;
		}
#endif

		BlitChroma444ToNV12_Reference(uvdst, uvpitch, usrc, upitch, vsrc, vpitch, w, h);
	}

	void BlitChroma420ToNV12(void *uvdst, ptrdiff_t uvpitch, const void *usrc, ptrdiff_t upitch, const void *vsrc, ptrdiff_t vpitch, uint32 w, uint32 h) {
#if VD_CPU_X86 || VD_CPU_X64
		if (SSE2_enabled) {
			BlitChroma420ToNV12_SSE2(uvdst, uvpitch, usrc, upitch, vsrc, vpitch, w, h);
			return;
		}
#endif

		BlitChroma420ToNV12_Reference(uvdst, uvpitch, usrc, upitch, vsrc, vpitch, w, h);
	}
}

void ATMediaFoundationEncoderW32::WriteVideo(const VDPixmap& px) {
	// sanity check the input buffer
	if (mVideoFrameLayout.format == nsVDPixmap::kPixFormat_Y8) {
		if (px.format != nsVDPixmap::kPixFormat_YUV444_Planar_709 && px.format != nsVDPixmap::kPixFormat_YUV420_Planar_709)
			return;

		if (px.w != mVideoFrameLayout.w || px.h + (px.h >> 1) != mVideoFrameLayout.h)
			return;
	}

	HRVerify verify;

	vdrefptr<IMFSample> sample;
	vdrefptr<IMFMediaBuffer> buf;

	// try to reclaim a sample from the allocator, to reduce memory allocation overhead -- this
	// is significant due to VM remapping
	if (mpVideoSampleAllocator->AllocateCachedSample(~sample)) {
		// we got one -- sanitize it
		DWORD bufferCount = 0;
		verify += sample->GetBufferCount(&bufferCount);

		if (bufferCount > 0) {
			verify += sample->GetBufferByIndex(0, ~buf);

			if (buf) {
				DWORD existingMaxLen = 0;
				verify += buf->GetMaxLength(&existingMaxLen);

				if (existingMaxLen < mVideoFrameSize)
					buf.clear();
			}
		}

		// wipe the sample of buffers and attributes so we start clean -- note that we are
		// holding the buffer so we can reuse it
		sample->RemoveAllBuffers();
		sample->DeleteAllItems();
	}

	if (!buf)
		verify += mpfnMFCreateAlignedMemoryBuffer(mVideoFrameSize, MF_64_BYTE_ALIGNMENT, ~buf);

	BYTE *data = nullptr;
	verify += buf->Lock(&data, nullptr, nullptr);

	if (mVideoFrameLayout.format == nsVDPixmap::kPixFormat_Y8) {
		// Special case for YUV444/YUV420 -> NV12 -- blit the luma plane, then use a custom blitter for the chroma plane.
		VDMemcpyRect(data + mVideoFrameLayout.data, mVideoFrameLayout.pitch, px.data, px.pitch, mVideoFrameLayout.w, mVideoFrameLayout.h);

		if (px.format == nsVDPixmap::kPixFormat_YUV444_Planar_709) {
			BlitChroma444ToNV12(
				data + mVideoFrameLayout.data + mVideoFrameLayout.pitch * ((mVideoFrameLayout.h * 2) / 3),
				mVideoFrameLayout.pitch,
				px.data2,
				px.pitch2,
				px.data3,
				px.pitch3,
				mVideoFrameLayout.w >> 1,
				mVideoFrameLayout.h / 3
			);
		} else {
			BlitChroma420ToNV12(
				data + mVideoFrameLayout.data + mVideoFrameLayout.pitch * ((mVideoFrameLayout.h * 2) / 3),
				mVideoFrameLayout.pitch,
				px.data2,
				px.pitch2,
				px.data3,
				px.pitch3,
				mVideoFrameLayout.w >> 1,
				mVideoFrameLayout.h / 3
			);
		}
	} else {
		mVideoBlitter.Blit(VDPixmapFromLayout(mVideoFrameLayout, data), px);
	}

	verify += buf->Unlock();

	verify += buf->SetCurrentLength(mVideoFrameSize);

	if (!sample)
		verify += mpfnMFCreateSample(~sample);

	verify += sample->AddBuffer(buf);
	buf.clear();

	++mVideoFrameCount;
	LONGLONG endTime = (LONGLONG)(sint64)((vduint128((uint64)mVideoFrameCount * 10000000U) * vduint128(mVideoFrameRate.getLo()) + vduint128(mVideoFrameRate.getHi() >> 1)) / mVideoFrameRate.getHi());

	verify += sample->SetSampleTime(mVideoNextSampleTime);
	verify += sample->SetSampleDuration(endTime - mVideoNextSampleTime);

	mVideoNextSampleTime = endTime;

	verify += mpSinkWriter->WriteSample(mVideoStreamIndex, sample);

	LPVOID fenceId = mpVideoSampleAllocator->AddSample(sample);
	sample.release();

	mpSinkWriter->PlaceMarker(mVideoStreamIndex, fenceId);
}

void ATMediaFoundationEncoderW32::BeginAudioFrame(uint32 bytes, uint32 samples) {
	HRVerify verify;

	VDASSERT(!mpAudioDst);

	if (mbAudioConvertToStereo)
		bytes *= 2;

	if (bytes > 0) {
		verify += mpfnMFCreateMemoryBuffer(bytes, ~mpAudioBuffer);
		verify += mpAudioBuffer->SetCurrentLength(bytes);
		verify += mpAudioBuffer->Lock(&mpAudioDst, nullptr, nullptr);
		mpAudioDstEnd = mpAudioDst + bytes;
	}

	mAudioSamplesWritten += samples;
}

void ATMediaFoundationEncoderW32::WriteAudio(const sint16 *data, uint32 bytes) {
	if (mbAudioConvertToStereo) {
		VDASSERT((bytes & 1) == 0);

		const sint16 *VDRESTRICT src = data;
		sint16 *VDRESTRICT dst = (sint16 *)mpAudioDst;

		for(uint32 i = bytes >> 1; i; --i) {
			dst[0] = dst[1] = *src++;
			dst += 2;
		}

		mpAudioDst = (BYTE *)dst;
	} else {
		memcpy(mpAudioDst, data, bytes);
		mpAudioDst += bytes;
	}
}

void ATMediaFoundationEncoderW32::EndAudioFrame() {
	if (!mpAudioDst)
		return;

	HRVerify verify;

	VDASSERT(mpAudioDst == mpAudioDstEnd);

	verify += mpAudioBuffer->Unlock();

	vdrefptr<IMFSample> sample;

	verify += mpfnMFCreateSample(~sample);

	verify += sample->AddBuffer(mpAudioBuffer);
	mpAudioBuffer.clear();

	LONGLONG sampleEndTime = (sint64)(((vduint128(mAudioSamplesWritten) * vduint128(10000000)) + vduint128(24000)) / (uint32)48000);

	verify += sample->SetSampleTime(mAudioNextSampleTime);
	verify += sample->SetSampleDuration(sampleEndTime - mAudioNextSampleTime);

	mAudioNextSampleTime = sampleEndTime;

	verify += mpSinkWriter->WriteSample(mAudioStreamIndex, sample);

	mpAudioDst = nullptr;
	mpAudioDstEnd = nullptr;
}

///////////////////////////////////////////////////////////////////////////////

class ATVideoWriter final : public IATVideoWriter, public IATGTIAVideoTap, public IATAudioTap {
public:
	ATVideoWriter();
	~ATVideoWriter();

	IATGTIAVideoTap *AsVideoTap() override { return this; }
	IATAudioTap *AsAudioTap() override { return this; }

	void CheckExceptions() override;

	void Init(const wchar_t *filename, ATVideoEncoding venc,
		uint32 videoBitRate, uint32 audioBitRate,
		uint32 w, uint32 h, const VDFraction& frameRate, double pixelAspectRatio,
		ATVideoRecordingResamplingMode resamplingMode,
		ATVideoRecordingScalingMode scalingMode,
		const uint32 *palette, double samplingRate, bool stereo, double timestampRate, bool halfRate, bool encodeAllFrames, IATUIRenderer *r) override;
	void Shutdown() override;

	void WriteFrame(const VDPixmap& px, uint64 timestampStart, uint64 timestampEnd) override;
	void WriteRawAudio(const float *left, const float *right, uint32 count, uint32 timestamp);

protected:
	void RaiseError(const MyError& e);

	bool mbStereo;
	bool mbHalfRate;
	bool mbErrorState;

	bool	mbVideoTimestampSet;
	bool	mbAudioPreskipSet;
	uint64	mFirstVideoTimestamp;
	sint32	mAudioPreskip;
	uint32	mVideoPreskip;
	double	mFrameRate;
	double	mSamplingRate;
	double	mTimestampRate;

	IATUIRenderer	*mpUIRenderer;

	vdautoptr<IATMediaEncoder> mpMediaEncoder;

	VDPixmapCachedBlitter mVideoColorConversionBlitter;
	vdautoptr<IVDPixmapResampler> mpVideoResampler;
	VDPixmapCachedBlitter mVideoPostResampleCcBlitter;
	VDPixmapBuffer mVideoColorConversionBuffer;
	VDPixmapBuffer mVideoResampleBuffer;
	VDPixmapBuffer mVideoPostResampleCcBuffer;

	MyError	mError;

	enum { kResampleBufferSize = 4096 };

	uint32	mResampleLevel;
	uint64	mResampleAccum;
	uint64	mResampleRate;
	float	mResampleBuffers[2][4096];
};

ATVideoWriter::ATVideoWriter() {
}

ATVideoWriter::~ATVideoWriter() {
}

void ATVideoWriter::CheckExceptions() {
	if (!mbErrorState)
		return;

	if (!mError.empty()) {
		MyError e;

		e.TransferFrom(mError);
		throw e;
	}
}

void ATVideoWriter::Init(const wchar_t *filename, ATVideoEncoding venc,
	uint32 videoBitRate,
	uint32 audioBitRate,
	uint32 w, uint32 h, const VDFraction& frameRate, double pixelAspectRatio,
	ATVideoRecordingResamplingMode resamplingMode,
	ATVideoRecordingScalingMode scalingMode,
	const uint32 *palette, double samplingRate, bool stereo, double timestampRate, bool halfRate, bool encodeAllFrames, IATUIRenderer *r)
{
	mbStereo = stereo;
	mbHalfRate = halfRate;
	mbErrorState = false;
	mbVideoTimestampSet = false;
	mbAudioPreskipSet = false;
	mFrameRate = frameRate.asDouble();
	mSamplingRate = samplingRate;
	mTimestampRate = timestampRate;
	mAudioPreskip = 0;
	mVideoPreskip = 0;

	mResampleLevel = 0;
	mResampleAccum = 0;
	mResampleRate = VDRoundToInt64(4294967296.0 / 48000.0 * samplingRate);

	mpUIRenderer = r;

	VDFraction encodingFrameRate = frameRate;

	if (halfRate)
		encodingFrameRate /= 2;

	float aspectCorrectionRatio = pixelAspectRatio;

	if (fabsf(aspectCorrectionRatio - 1.0f) < 1e-4f)
		aspectCorrectionRatio = 1.0f;

	float dstwf = (float)w * aspectCorrectionRatio;
	float dsthf = (float)h;
	uint32 framew = w;
	uint32 frameh = h;

	switch(scalingMode) {
		case ATVideoRecordingScalingMode::None:
			framew = (uint32)VDCeilToInt(dstwf);
			frameh = (uint32)VDCeilToInt(dsthf);
			break;

		case ATVideoRecordingScalingMode::Scale480Narrow:
			framew = 640;
			frameh = 480;
			break;

		case ATVideoRecordingScalingMode::Scale480Wide:
			framew = 854;
			frameh = 480;
			break;

		case ATVideoRecordingScalingMode::Scale720Narrow:
			framew = 960;
			frameh = 720;
			break;

		case ATVideoRecordingScalingMode::Scale720Wide:
			framew = 1280;
			frameh = 720;
			break;
	}
	
	bool useYUV = false;

	switch(venc) {
		case kATVideoEncoding_WMV7:
		case kATVideoEncoding_WMV9:
		case kATVideoEncoding_H264_AAC:
		case kATVideoEncoding_H264_MP3:
			useYUV = true;
			break;

		default:
			break;
	}

	if (useYUV) {
		// Ensure even/odd frame size for 4:2:0 since odd support is not guaranteed in MF (much less defined, really).
		framew = (framew + 1) & ~1;
		frameh = (frameh + 1) & ~1;
	}

	if (framew != w || frameh != h || (uint32)(0.5f + dstwf) != w || (uint32)(0.5f + dsthf) != h) {
		mpVideoResampler = VDCreatePixmapResampler();

		if (useYUV) {
			VDPixmapLayout layout;
			VDPixmapCreateLinearLayout(layout, nsVDPixmap::kPixFormat_YUV444_Planar_709, framew, frameh, 16);
			mVideoResampleBuffer.init(layout, 16);
		} else {
			mVideoResampleBuffer.init(framew, frameh, nsVDPixmap::kPixFormat_XRGB8888);
		}

		memset(mVideoResampleBuffer.base(), 0, mVideoResampleBuffer.size());

		if (useYUV) {
			VDMemset8Rect(mVideoResampleBuffer.data2, mVideoResampleBuffer.pitch2, 0x80, framew, frameh);
			VDMemset8Rect(mVideoResampleBuffer.data3, mVideoResampleBuffer.pitch3, 0x80, framew, frameh);
		}

		float scale = 1.0f;
		
		if (scalingMode != ATVideoRecordingScalingMode::None)
			scale = std::min<float>((float)framew / dstwf, (float)frameh / dsthf);

		dstwf *= scale;
		dsthf *= scale;

		const float dstxf = ((float)framew - dstwf) * 0.5f;
		const float dstyf = ((float)frameh - dsthf) * 0.5f;
		vdrect32f dstrect(dstxf, dstyf, (float)framew - dstxf, (float)frameh - dstyf);

		IVDPixmapResampler::FilterMode filterMode;
		switch(resamplingMode) {
			case ATVideoRecordingResamplingMode::Nearest:
				filterMode = IVDPixmapResampler::kFilterPoint;
				break;

			case ATVideoRecordingResamplingMode::SharpBilinear:
				filterMode = IVDPixmapResampler::kFilterSharpLinear;
				mpVideoResampler->SetSharpnessFactors(2.0f, 2.0f);
				break;

			case ATVideoRecordingResamplingMode::Bilinear:
				filterMode = IVDPixmapResampler::kFilterLinear;
				break;
		}

		mpVideoResampler->SetFilters(filterMode, filterMode, false);
		VDVERIFY(mpVideoResampler->Init(dstrect, framew, frameh, mVideoResampleBuffer.format, vdrect32f(0, 0, (float)w, (float)h), w, h, mVideoResampleBuffer.format));

		w = framew;
		h = frameh;

		palette = nullptr;
	} else if (useYUV) {
		mVideoPostResampleCcBuffer.init(w, h, nsVDPixmap::kPixFormat_YUV420_Planar_709);
	}

	if (!palette && venc == kATVideoEncoding_RLE)
		throw MyError("RLE encoding is not available as the current emulation video and recording settings require 24-bit video.");

	switch(venc) {
		case kATVideoEncoding_Raw:
		case kATVideoEncoding_RLE:
		case kATVideoEncoding_ZMBV:
			mpMediaEncoder = new ATAVIEncoder(filename, venc, w, h, encodingFrameRate, palette, samplingRate, stereo, encodeAllFrames);
			break;

		case kATVideoEncoding_WMV7:
		case kATVideoEncoding_WMV9:
		case kATVideoEncoding_H264_AAC:
		case kATVideoEncoding_H264_MP3:
			mpMediaEncoder = new ATMediaFoundationEncoderW32(filename, venc, videoBitRate, audioBitRate, w, h, encodingFrameRate, palette, samplingRate, stereo, useYUV);
			break;

		default:
			throw MyError("Unimplemented compression mode.");
	}
}

void ATVideoWriter::Shutdown() {
	if (mpUIRenderer) {
		mpUIRenderer->SetRecordingPosition();
		mpUIRenderer = NULL;
	}

	if (mpMediaEncoder) {
		MyError e;
		if (!mpMediaEncoder->Finalize(e))
			RaiseError(e);

		mpMediaEncoder.reset();
	}
}

void ATVideoWriter::WriteFrame(const VDPixmap& px, uint64 timestamp, uint64 timestampEnd) {
	if (mbErrorState)
		return;

	if (!mbAudioPreskipSet) {
		mbVideoTimestampSet = true;
		mFirstVideoTimestamp = timestamp;
		return;
	}

	if (mVideoPreskip) {
		--mVideoPreskip;
		return;
	}

	if (mpUIRenderer)
		mpUIRenderer->SetRecordingPosition((float)((double)(timestamp - mFirstVideoTimestamp) / mTimestampRate), mpMediaEncoder->GetCurrentSize());

	const VDPixmap *pxlast = &px;

	try {
		if (mpVideoResampler) {
			if (pxlast->format != mVideoResampleBuffer.format) {
				if (!mVideoColorConversionBuffer.format)
					mVideoColorConversionBuffer.init(pxlast->w, pxlast->h, mVideoResampleBuffer.format);

				mVideoColorConversionBlitter.Blit(mVideoColorConversionBuffer, *pxlast);
				pxlast = &mVideoColorConversionBuffer;
			}

			mpVideoResampler->Process(mVideoResampleBuffer, *pxlast);
			pxlast = &mVideoResampleBuffer;
		}

		if (mVideoPostResampleCcBuffer.format) {
			mVideoPostResampleCcBlitter.Blit(mVideoPostResampleCcBuffer, *pxlast);
			pxlast = &mVideoPostResampleCcBuffer;
		}

		mpMediaEncoder->WriteVideo(*pxlast);

		if (mbHalfRate)
			mVideoPreskip = 1;
	} catch(const MyError& e) {
		RaiseError(e);
	}
}

void ATVideoWriter::WriteRawAudio(const float *left, const float *right, uint32 count, uint32 timestamp) {
	if (mbErrorState)
		return;

	if (!mbAudioPreskipSet) {
		if (!mbVideoTimestampSet)
			return;

		mbAudioPreskipSet = true;

		// Compute how much audio we need to skip to get the streams in sync. We do this by computing the
		// error between the next video frame time and the current audio timestamp. If this is negative,
		// we increase the video frame skip and try again (to be safe wrt. roundoff). Note that we are
		// doing this at input sampling rate, not at output sampling rate.
		double offset = (double)(sint32)(mFirstVideoTimestamp - timestamp) / mTimestampRate + 1.0f / mFrameRate;
		for(;;) {
			mAudioPreskip = VDRoundToInt32(offset * mSamplingRate);

			if (mAudioPreskip >= 0)
				break;

			++mVideoPreskip;
			offset += 1.0f / mFrameRate;
		}
	}

	if (mAudioPreskip) {
		uint32 toSkip = mAudioPreskip;

		if (toSkip >= count) {
			mAudioPreskip -= count;
			return;
		}

		mAudioPreskip = 0;

		left += toSkip;
		if (right)
			right += toSkip;

		count -= toSkip;
	}

	uint32 outputSamples = 0;
	uint32 newLevel = mResampleLevel + count;

	if (newLevel >= 8) {
		uint64 newMaxValid = ((uint64)(newLevel - 7) << 32) - 1;

		if (newMaxValid > mResampleAccum)
			outputSamples = (uint32)((newMaxValid - mResampleAccum) / mResampleRate);
	}

	sint16 buf[1024];
	try {
		if (outputSamples)
			mpMediaEncoder->BeginAudioFrame(outputSamples*(mbStereo ? 4 : 2), outputSamples);

		uint32 outputSamplesLeft = outputSamples;
		for(;;) {
			// copy in samples
			if (count) {
				uint32 tcIn = kResampleBufferSize - mResampleLevel;

				if (tcIn > count)
					tcIn = count;

				count -= tcIn;

				if (mbStereo) {
					if (right) {
						for(uint32 i=0; i<tcIn; ++i) {
							mResampleBuffers[0][mResampleLevel] = *left++;
							mResampleBuffers[1][mResampleLevel++] = *right++;
						}
					} else {
						for(uint32 i=0; i<tcIn; ++i) {
							mResampleBuffers[0][mResampleLevel] = mResampleBuffers[1][mResampleLevel] = *left++;
							++mResampleLevel;
						}
					}
				} else {
					if (right) {
						for(uint32 i=0; i<tcIn; ++i) {
							mResampleBuffers[0][mResampleLevel++] = 0.5f * (*left++ + *right++);
						}
					} else {
						memcpy(&mResampleBuffers[0][mResampleLevel], left, sizeof(float) * tcIn);
						mResampleLevel += tcIn;
						left += tcIn;
					}
				}
			}

			if (!outputSamplesLeft)
				break;

			// process out samples
			while(mResampleLevel >= 8) {
				uint64 maxValidPoint = ((uint64)(mResampleLevel - 7) << 32) - 1;

				if (maxValidPoint <= mResampleAccum)
					break;

				uint32 tcOut = (uint32)((maxValidPoint - mResampleAccum) / mResampleRate);

				if (!tcOut)
					break;

				if (mbStereo) {
					if (tcOut > 512)
						tcOut = 512;

					mResampleAccum = ATFilterResampleStereo16(buf, mResampleBuffers[0], mResampleBuffers[1], tcOut, mResampleAccum, mResampleRate);

					mpMediaEncoder->WriteAudio(buf, 2*sizeof(sint16)*tcOut);
				} else {
					if (tcOut > 1024)
						tcOut = 1024;

					mResampleAccum = ATFilterResampleMono16(buf, mResampleBuffers[0], tcOut, mResampleAccum, mResampleRate);
					mpMediaEncoder->WriteAudio(buf, sizeof(sint16)*tcOut);
				}

				outputSamplesLeft -= tcOut;
			}

			// shift resampling buffer if required
			uint32 baseIdx = (uint32)(mResampleAccum >> 32);
			if (baseIdx >= (kResampleBufferSize >> 1)) {
				size_t bytesToMove = sizeof(float) * (mResampleLevel - baseIdx);

				memmove(mResampleBuffers[0], &mResampleBuffers[0][baseIdx], bytesToMove);

				if (mbStereo)
					memmove(mResampleBuffers[1], &mResampleBuffers[1][baseIdx], bytesToMove);

				mResampleAccum = (uint32)mResampleAccum;
				mResampleLevel -= baseIdx;
			}
		}

		if (outputSamples)
			mpMediaEncoder->EndAudioFrame();

		VDASSERT(!count);

	} catch(const MyError& e) {
		RaiseError(e);
	}
}

void ATVideoWriter::RaiseError(const MyError& e) {
	if (!mbErrorState) {
		mbErrorState = true;
		mError.assign(e);
	}
}

void ATCreateVideoWriter(IATVideoWriter **w) {
	*w = new ATVideoWriter;
}
