//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
//	Beginning with 1.6.0, the VirtualDub system library is licensed
//	differently than the remainder of VirtualDub.  This particular file is
//	thus licensed as follows (the "zlib" license):
//
//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.

#include <stdafx.h>
#include <numeric>
#include <vd2/system/vdtypes.h>
#include <vd2/system/zip.h>
#include <vd2/system/binary.h>
#include <vd2/system/date.h>
#include <vd2/system/error.h>
#include <vd2/system/function.h>

//#define VDDEBUG_DEFLATE VDDEBUG2
#define VDDEBUG_DEFLATE(...) ((void)0)

//#define VDDEBUG_INFLATE VDDEBUG2
#define VDDEBUG_INFLATE(...) ((void)0)

//#define VDDEBUG_INFLATE_ERROR VDDEBUG2
#define VDDEBUG_INFLATE_ERROR VDFAIL

namespace nsVDDeflate {
	const unsigned len_tbl[32]={
		3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,
		131,163,195,227,258
	};

	const unsigned len_pack_tbl[32]={
		3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,
		131,163,195,227,258,~(unsigned)0
	};

	const unsigned char hclen_tbl[]={
		16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
	};

	const unsigned char len_bits_tbl[32]={
		0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
	};

	const unsigned char dist_bits_tbl[]={
		0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
	};

	const unsigned dist_tbl[]={
		1,2,3,4,	// +0 bits
		5,7,		// +1 bits
		9,13,		// +2 bits
		17,25,		// +3 bits
		33,49,		// +4 bits
		65,97,		// +5 bits
		129,193,	// +6 bits
		257,385,	// +7 bits
		513,769,	// +8 bits
		1025,1537,	// +9 bits
		2049,3073,	// +10 bits
		4097,6145,	// +11 bits
		8193,12289,	// +12 bits
		16385,24577,// +13 bits
		32769
	};
};

bool VDDeflateBitReader::refill() {
	sint32 tc = mBytesLeft>kBufferSize?kBufferSize:(sint32)mBytesLeft;

	if (!tc)
		return false;

	mpSrc->Read(mBuffer+kBufferSize-tc, tc);	// might throw

	mBufferPt = -tc;

	mBytesLeftLimited = mBytesLeft > kBigAvailThreshold ? kBigAvailThreshold : (unsigned)mBytesLeft;
	mBytesLeft -= tc;

	return true;
}

void VDDeflateBitReader::readbytes(void *dst, unsigned len) {
	// LAME: OPTIMIZE LATER
	uint8 *dst2 = (uint8 *)dst;
	while(len-->0)
		*dst2++ = getbits(8);
}

///////////////////////////////////////////////////////////////////////////

void VDCRCTable::Init(uint32 crc) {
	InitConst(crc);
}

uint32 VDCRCTable::Process(uint32 crc, const void *src0, size_t count) const {
	const uint8 *src = (const uint8 *)src0;

	// This code is from the PNG spec.
	while(count--)
		crc = mTable[(uint8)crc ^ *src++] ^ (crc >> 8);

	return crc;
}

constexpr void VDCRCTable::InitConst(uint32 crc) {
	for(int i=0; i<256; ++i) {
		unsigned v = i;
		for(int j=0; j<8; ++j)
			v = (v>>1) ^ (crc & -(sint32)(v&1));

		mTable[i] = v;
	}
}

constexpr VDCRCTable VDCRCTable::MakeConst(uint32 crc) {
	VDCRCTable table;

	table.InitConst(crc);

	return table;
}

constexpr VDCRCTable VDCRCTable::CRC32 = MakeConst(VDCRCTable::kCRC32);

///////////////////////////////////////////////////////////////////////////

void VDCRCChecker::Process(const void *src, sint32 count) {
	if (count > 0)
		mValue = mTable.Process(mValue, src, count);
}

struct VDHuffmanHistoSorterData {
	VDHuffmanHistoSorterData(const int pHisto[288]) {
		for(int i=0; i<288; ++i) {
			mHisto[i] = (pHisto[i] << 9) + 287 - i;
		}
	}

	int mHisto[288];
};

///////////////////////////////////////////////////////////////////////////

class VDDeflateHuffmanTable {
public:
	VDDeflateHuffmanTable();

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

VDDeflateHuffmanTable::VDDeflateHuffmanTable() {
	Init();
}

void VDDeflateHuffmanTable::Init() {
	std::fill(mHistogram, mHistogram+288, 0);
}

void VDDeflateHuffmanTable::BuildCode(int depth_limit) {
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
	std::sort(&mDHT[16], &mDHT[16+288], [&](int f1, int f2) { return mHistogram[f1] > mHistogram[f2]; });
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

uint32 VDDeflateHuffmanTable::GetOutputSize() const {
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

uint32 VDDeflateHuffmanTable::GetCodeCount(int limit) const {
	return std::accumulate(mHistogram2, mHistogram2+limit, 0);
}

uint32 VDDeflateHuffmanTable::GetStaticOutputSize() const {
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

void VDDeflateHuffmanTable::BuildEncodingTable(uint16 *p, int *l, int limit) {
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

void VDDeflateHuffmanTable::BuildStaticLengthEncodingTable(uint16 *p, int *l) {
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

void VDDeflateHuffmanTable::BuildStaticDistanceEncodingTable(uint16 *p, int *l) {
	memset(mDHT, 0, sizeof(mDHT[0])*16);
	mDHT[4] = 32;

	for(int i=0; i<32; ++i)
		mDHT[i+16] = i;

	BuildEncodingTable(p, l, 32);
}

class VDDeflateEncoder {
	VDDeflateEncoder(const VDDeflateEncoder&) = delete;
	VDDeflateEncoder& operator=(const VDDeflateEncoder&) = delete;
public:
	VDDeflateEncoder() = default;

	void Init(bool quick, vdfunction<void(const void *, uint32)> preProcessFn, vdfunction<void(const void *, uint32)> writeFn);
	void Write(const void *src, size_t len);
	void ForceNewBlock();
	void Finish();

protected:
	void EndBlock(bool term);
	void Compress(bool flush);
	void VDFORCEINLINE PutBits(uint32 encoding, int enclen);
	void FlushBits();
	void FlushOutput();
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
	uint32	mPreprocessPos = 0;

	vdfunction<void(const void *, uint32)> mpPreProcessFn;
	vdfunction<void(const void *, uint32)> mpOutputFn;
	uint32	mOutputLevel = 0;
	uint8	mOutputBuf[4096 + 4];

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

void VDDeflateEncoder::Init(bool quick, vdfunction<void(const void *, uint32)> preProcessFn, vdfunction<void(const void *, uint32)> writeFn) {
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

	mpOutputFn = std::move(writeFn);
	mpPreProcessFn = std::move(preProcessFn);

	mOutputBuf[0] = 0x78;		// 32K window, Deflate
	mOutputBuf[1] = 0xDA;		// maximum compression, no dictionary, check offset = 0x1A
	mOutputLevel = 0;
}

void VDDeflateEncoder::Write(const void *src, size_t len) {
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

void VDDeflateEncoder::ForceNewBlock() {
	Compress(false);
	EndBlock(false);
}

#define HASH(pos) ((((uint32)hist[(pos)  ] << 8) + ((uint32)hist[(pos)+1] << 4) + ((uint32)hist[(pos)+2] << 0)) & 0xffff)

void VDDeflateEncoder::EndBlock(bool term) {
	if (mpCode > mCodeBuf) {
		if (mPendingLen) {
			const uint8 *hist = mHistoryBuffer - mHistoryBase;
			int bestlen = mPendingLen - 1;
			mPendingLen = 0;

			while(bestlen-- > 0) {
				int hval = HASH(mHistoryPos);
				mHashNext[mHistoryPos & 0x7fff] = mHashTable[hval];
				mHashTable[hval] = mHistoryPos;
				++mHistoryPos;
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

void VDDeflateEncoder::Compress(bool flush) {
	using namespace nsVDDeflate;

	uint8	*lenptr = mpLen;
	uint16	*codeptr = mpCode;
	uint16	*distptr = mpDist;

	const uint8 *hist = mHistoryBuffer - mHistoryBase;

	uint32 pos = mHistoryPos;
	const uint32 len = mHistoryBase + mHistoryTail;
	const uint32 maxpos = flush ? len : len > 258+3 ? len - (258+3) : 0;		// +6 is for the 3-byte hash.

	if (mPreprocessPos < len) {
		mpPreProcessFn(mHistoryBuffer + (mPreprocessPos - mHistoryBase), len - mPreprocessPos);
		mPreprocessPos = len;
	}

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
		uint32 hcode = HASH(pos);

		sint32 hpos = mHashTable[hcode];
		uint32 limit = 258;
		if (limit > len-pos)
			limit = len-pos;

		sint32 hlimit = pos - mWindowLimit;		// note that our initial hash table values are low enough to avoid colliding with this.
		if (hlimit < 0)
			hlimit = 0;

		uint32 minmatch = mPendingLen > 3 ? mPendingLen : 3;
		uint32 bestlen = minmatch - 1;
		uint32 bestoffset = 0;

		if (hpos >= hlimit && limit >= minmatch) {
			sint32 hstart = hpos;
			const unsigned char *s2 = hist + pos;
			const uint16 matchWord1 = *(const uint16 *)s2;
			const uint8 matchWord2 = *(const uint8 *)(s2 + 2);
			uint32 hoffset = 0;

			do {
				const unsigned char *s1 = hist + hpos - hoffset;

				VDDEBUG_DEFLATE("testing %u %u (%02X%02X%02X %02X%02X%02X %02X %02X)\n", hpos, bestlen
					, hist[hpos]
					, hist[hpos+1]
					, hist[hpos+2]
					, s2[hoffset]
					, s2[hoffset+1]
					, s2[hoffset+2]
					, HASH(hpos)
					, HASH(pos + hoffset)
				);

				if (s1[bestlen] == s2[bestlen] && *(const uint16 *)s1 == matchWord1 && s1[2] == matchWord2) {
					uint32 mlen = 3;
					while(mlen < limit && s1[mlen] == s2[mlen])
						++mlen;

					// Check for a suboptimal match.
					//
					// The Deflate format requires additional raw bits for distance as the distance
					// increases. This means that it is not cost-effective to encode long distance
					// matches with too short of a length. The breakpoints are as follows:
					//
					//		Distance	Extra bits
					//			5			1
					//			9			2
					//			17			3
					//			33			4
					//			65			5
					//			129			6
					//			257			7
					//			513			8
					//			1025		9
					//			2049		10
					//			4097		11
					//			8193		12
					//			16385		13
					//
					// At least 1 bit is also needed for the distance encoding.
					//
					// This is also true for lengths, but with a given set of Huffman trees it is never
					// advantageous to code a shorter length. (It may be more efficient if it concentrates
					// the Huffman tree nodes, but we don't have visibility of that at this point.)
					//
					// If we assume we have a mostly full literal tree, any literals will cost 8-9 bits.
					// Therefore, we can apply a cost penalty of 1 byte for >=513.

					uint32 offset = (uint32)(s2 - s1);
					uint32 penalty = (offset >= 513) ? 1 : 0;

					if (mlen > bestlen + penalty) {
						bestoffset = offset;
						bestlen = mlen;

						if (mlen >= limit)
							break;

						if (mlen > 3) {
							// hop hash chains!
#if 1
							const uint32 diff = (mlen - 3) - hoffset;

							hlimit += diff;
							hpos += diff;
							if (hpos == pos)
								hpos = hstart;
							else
								hpos = mHashNext[hpos & 0x7fff];

							hoffset = mlen - 3;
#else
							const uint32 diff = (mlen - 2) - hoffset;

							hlimit += diff;
							hoffset = mlen - 2;
							hpos = mHashTable[HASH(pos + hoffset)];
#endif

						} else {
							hoffset = 1;
							++hlimit;

							hpos = mHashTable[HASH(pos + 1)];
						}
						continue;
					}
				}

				hpos = mHashNext[hpos & 0x7fff];
			} while(hpos >= hlimit);
		}

		if (bestoffset) {
			// check for an illegal match
			VDASSERT((uint32)(bestoffset-1) < 32768U);
			VDASSERT(bestlen < 259);
			VDASSERT(!memcmp(hist+pos, hist+pos-bestoffset, bestlen));
			VDASSERT(pos >= bestoffset);
			VDASSERT(pos+bestlen <= len);
			VDASSERT(pos-bestoffset >= mHistoryBase);

			unsigned lcode = 0;
			while(bestlen >= len_pack_tbl[lcode+1])
				++lcode;

			*codeptr++ = lcode + 257;
			*distptr++ = bestoffset;
			*lenptr++ = bestlen - 3;
			mLenExtraBits += len_bits_tbl[lcode];

			VDDEBUG_DEFLATE("%u match: (%u, %u)\n", pos, bestoffset, bestlen);
		} else {
			VDDEBUG_DEFLATE("%u literal %02X\n", pos, c);
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
			// no pending match -- make the new match pending if we have one
			if (bestlen > 1) {
				mPendingLen = bestlen;
				bestlen = 1;
			}
		} else {
			// 
			if (bestlen > mPendingLen) {
				// new match is better than the pending match -- truncate the previous
				// match in favor of the new one
				codeptr[-2] = hist[pos - 1];
				distptr[-2] = distptr[-1];
				--distptr;
				lenptr[-2] = lenptr[-1];
				--lenptr;
				mPendingLen = bestlen;
				bestlen = 1;
			} else {
				// pending match is better -- keep that and discard the one we just found
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

			while(--bestlen) {
				uint32 hcode = HASH(pos);
				mHashNext[pos & 0x7fff] = mHashTable[hcode];
				mHashTable[hcode] = pos;
				++pos;
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

void VDDeflateEncoder::Finish() {
	while(mHistoryPos != mHistoryBase + mHistoryTail)
		Compress(true);

	VDASSERT(mpCode != mCodeBuf);
	EndBlock(true);

	FlushBits();
	FlushOutput();
}

void VDFORCEINLINE VDDeflateEncoder::PutBits(uint32 encoding, int enclen) {
	mAccum >>= enclen;
	mAccum += encoding;
	mAccBits += enclen;
	VDASSERT(mAccBits >= -16 && mAccBits < 32);

	if (mAccBits >= 16) {
		mAccBits -= 16;

		if (vdcountof(mOutputBuf) - mOutputLevel < 2) {
			mpOutputFn(mOutputBuf, mOutputLevel);
			mOutputLevel = 0;
		}

		mOutputBuf[mOutputLevel++] = mAccum >> (16-mAccBits);
		mOutputBuf[mOutputLevel++] = mAccum >> (24-mAccBits);
	}		
}

void VDDeflateEncoder::FlushBits() {
	if (vdcountof(mOutputBuf) - mOutputLevel < 4)
		FlushOutput();

	while(mAccBits > 0) {
		mOutputBuf[mOutputLevel++] = (uint8)(mAccum >> (32-mAccBits));
		mAccBits -= 8;
	}
}

void VDDeflateEncoder::FlushOutput() {
	if (mOutputLevel) {
		mpOutputFn(mOutputBuf, mOutputLevel);
		mOutputLevel = 0;
	}
}

uint32 VDDeflateEncoder::Flush(int n, int ndists, bool term, bool test) {
	using namespace nsVDDeflate;

	const uint16 *codes = mCodeBuf;
	const uint8 *lens = mLenBuf;
	const uint16 *dists = mDistBuf;

	VDDeflateHuffmanTable htcodes, htdists, htlens;
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
		FlushOutput();

		const uint8 *base = &mHistoryBuffer[mHistoryBlockStart - mHistoryBase];
		if (blockSize)
			mpOutputFn(base, blockSize);
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

				VDASSERT(len >= len_pack_tbl[code-257]);
				VDASSERT(len < len_pack_tbl[code-256]);

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

///////////////////////////////////////////////////////////////////////////

void VDInflateStream::Init(IVDStream *pSrc, uint64 limit, bool bStored) {
	mBits.init(pSrc, limit);
	mBlockType = kNoBlock;
	mReadPt = mWritePt = mBufferLevel = 0;
	mStoredBytesLeft = 0;
	mbNoMoreBlocks = false;

	if (bStored) {
		mStoredBytesLeft = (uint32)limit;
		mbNoMoreBlocks = true;
		mBlockType = kStoredBlock;
	}
}

const wchar_t *VDInflateStream::GetNameForError() {
	return mBits.stream()->GetNameForError();
}

sint64 VDInflateStream::Pos() {
	return mPos;
}

void VDInflateStream::Read(void *buffer, sint32 bytes) {
	if (bytes != ReadData(buffer, bytes))
		throw MyError("Read error on compressed data");
}

sint32 VDInflateStream::ReadData(void *dst0, sint32 bytes) {
	sint32 actual = 0;

	uint8 *dst = (uint8 *)dst0;

	while(bytes > 0) {
		if (mBufferLevel > 0) {
			unsigned tc = std::min<unsigned>(mBufferLevel, bytes);
			unsigned bp = 65536 - mReadPt;

			if (bp < tc) {
				memcpy(dst, mBuffer+mReadPt, bp);
				memcpy(dst+bp, mBuffer, tc-bp);
				mReadPt = tc-bp;
			} else {
				memcpy(dst, mBuffer+mReadPt, tc);
				mReadPt += tc;
			}
			mBufferLevel -= tc;
			dst += tc;
			bytes -= tc;
			actual += tc;
		} else {
			uint32 origWritePt = mWritePt;
			uint32 origBufferLevel = mBufferLevel;

			if (!Inflate())
				break;

			if (mbCRCEnabled && mBufferLevel != origBufferLevel) {
				if (mWritePt <= origWritePt) {
					mCRCChecker.Process(mBuffer+origWritePt, 65536 - origWritePt);
					mCRCChecker.Process(mBuffer, mWritePt);
				} else {
					mCRCChecker.Process(mBuffer+origWritePt, mWritePt - origWritePt);
				}
			}
		}
	}

	mPos += actual;
	return actual;
}

void VDInflateStream::Write(const void *buffer, sint32 bytes) {
	throw MyError("Zip streams are read-only.");
}

bool VDInflateStream::Inflate() {
	using namespace nsVDDeflate;

	if (mBlockType == kNoBlock)
		if (mbNoMoreBlocks || !ParseBlockHeader())
			return false;

	if (mBlockType == kStoredBlock) {
		while(mBufferLevel < 65536) {
			if (mStoredBytesLeft <= 0) {
				mBlockType = kNoBlock;
				break;
			}
			uint32 tc = std::min<uint32>(65536 - mWritePt, std::min<uint32>(65536 - mBufferLevel, mStoredBytesLeft));

			mBits.readbytes(mBuffer + mWritePt, tc);

			mWritePt = (mWritePt + tc) & 65535;
			mStoredBytesLeft -= tc;
			mBufferLevel += tc;
		}
	} else {
		while(mBufferLevel < 65024) {
			unsigned code, bits;

			code	= mCodeDecode[mBits.peek() & 0x7fff];
			bits	= mCodeLengths[code];

			if (!mBits.consume(bits))
				return false;

			if (code == 256) {
				mBlockType = kNoBlock;
				break;
			} else if (code >= 257) {
				unsigned	dist, len;

				code -= 257;

				len = len_tbl[code] + mBits.getbits(len_bits_tbl[code]);

				if (len < 3)
					return false;	// can happen with a bad static block

				code = mDistDecode[mBits.peek() & 0x7fff];
				bits = mCodeLengths[code + 288];

				if (!mBits.consume(bits))
					return false;

				dist = dist_tbl[code] + mBits.getbits(dist_bits_tbl[code]);

				VDDEBUG_INFLATE("copy (%u, %u)\n", dist, len);

				unsigned copysrc = (mWritePt - dist) & 65535;

				mBufferLevel += len;

				// NOTE: This can be a self-replicating copy.  It must be ascending and it must
				//		 be by bytes.
				do {
					mBuffer[mWritePt++] = mBuffer[copysrc++];
					mWritePt &= 65535;
					copysrc &= 65535;
				} while(--len);
			} else {
				VDDEBUG_INFLATE("literal %u\n", code);
				mBuffer[mWritePt++] = code;
				mWritePt &= 65535;
				++mBufferLevel;
			}
		}
	}

	return true;
}

namespace {
	static unsigned revword8(unsigned x) {
		x = (unsigned char )((x << 4) + (x >> 4));
		x = ((x << 2) & 0xcc) + ((x >> 2) & 0x33);
		return ((x << 1) & 0xaa) + ((x >> 1) & 0x55);
	}

	static unsigned revword15(unsigned x) {
		x = ((x << 8) & 0xff00) + ((x >> 8) & 0x00ff);
		x = ((x << 4) & 0xf0f0) + ((x >> 4) & 0x0f0f);
		x = ((x << 2) & 0xcccc) + ((x >> 2) & 0x3333);
		return (x & 0x5555) + ((x >> 2) & 0x2aaa);
	}

	static bool InflateExpandTable256(unsigned char *dst, unsigned char *lens, unsigned codes) {
		unsigned	k;
		unsigned	ki;
		unsigned	base=0;

		for(unsigned i=1; i<16; ++i) {
			ki = 1<<i;

			for(unsigned j=0; j<codes; ++j) {
				if (lens[j] == i) {
					for(k=base; k<0x100; k+=ki)
						dst[k] = j;

					base = revword8((revword8(base)+(0x100 >> i)) & 0xff);
				}
			}
		}

		return !base;
	}

	static bool InflateExpandTable32K(unsigned short *dst, unsigned char *lens, unsigned codes) {
		unsigned	k;
		unsigned	ki;
		unsigned	base=0;

		for(int i=1; i<16; ++i) {
			ki = 1<<i;

			for(unsigned j=0; j<codes; ++j) {
				if (lens[j] == i) {
					for(k=base; k<0x8000; k+=ki)
						dst[k] = j;

					base = revword15(revword15(base)+(0x8000 >> i));
				}
			}
		}

		return !base;
	}
}

bool VDInflateStream::ParseBlockHeader() {
	unsigned char ltbl_lengths[20];
	unsigned char ltbl_decode[256];

	if (mBits.getbit())
		mbNoMoreBlocks = true;

	unsigned type = mBits.getbits(2);

	switch(type) {
	case 0:		// stored
		{
			mBits.align();
			if (mBits.avail() < 32)
				return false;

			mStoredBytesLeft = mBits.getbits(16);

			uint32 invCount = mBits.getbits(16);

			if ((uint16)~invCount != mStoredBytesLeft) {
				VDDEBUG_INFLATE_ERROR("Stored block header incorrect\n");
				return false;
			}

			if (mBits.bytesleft() < mStoredBytesLeft) {
				VDDEBUG_INFLATE_ERROR("Stored block header incorrect\n");
				return false;
			}

			mBlockType = kStoredBlock;
		}
		break;
	case 1:		// static trees
		{
			int i;

			for(i=0; i<144; ++i) mCodeLengths[i] = 8;
			for(   ; i<256; ++i) mCodeLengths[i] = 9;
			for(   ; i<280; ++i) mCodeLengths[i] = 7;
			for(   ; i<288; ++i) mCodeLengths[i] = 8;
			for(i=0; i< 32; ++i) mCodeLengths[i+288] = 5;

			if (!InflateExpandTable32K(mCodeDecode, mCodeLengths, 288)) {
				VDDEBUG_INFLATE_ERROR("Static code table bad\n");
				return false;
			}
			if (!InflateExpandTable32K(mDistDecode, mCodeLengths+288, 32)) {
				VDDEBUG_INFLATE_ERROR("Static distance table bad\n");
				return false;
			}

			mBlockType = kDeflatedBlock;
		}
		break;
	case 2:		// dynamic trees
		{
			if (mBits.avail() < 16) {
				VDDEBUG_INFLATE_ERROR("Dynamic tree header bad\n");
				return false;
			}

			const unsigned	code_count	= mBits.getbits(5) + 257;
			const unsigned	dist_count	= mBits.getbits(5) + 1;
			const unsigned	total_count	= code_count + dist_count;
			const unsigned	ltbl_count	= mBits.getbits(4) + 4;

			// decompress length table tree

			if (mBits.bitsleft() < 3*ltbl_count) {
				VDDEBUG_INFLATE_ERROR("Error redaing length table tree\n");
				return false;
			}

			memset(ltbl_lengths, 0, sizeof ltbl_lengths);

			static const unsigned char hclen_tbl[]={
				16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
			};

			for(unsigned i=0; i<ltbl_count; ++i) {
				ltbl_lengths[hclen_tbl[i]] = mBits.getbits(3);
			}

			if (!InflateExpandTable256(ltbl_decode, ltbl_lengths, 20)) {
				VDDEBUG_INFLATE_ERROR("Length tree table bad\n");
				return false;
			}

			// decompress length table

			unsigned j=0;
			unsigned last = 0;
			while(j < total_count) {
				unsigned k = ltbl_decode[0xff & mBits.peek()];
				unsigned run = 1;

				if (!mBits.consume(ltbl_lengths[k])) {
					VDDEBUG_INFLATE_ERROR("EOF while reading length table\n");
					return false;
				}

				switch(k) {
				case 16:	// last run of 3-6
					if (mBits.avail() < 2)
						return false;
					run = mBits.getbits(2) + 3;
					break;
				case 17:	// zero run of 3-10
					if (mBits.avail() < 3)
						return false;
					run = mBits.getbits(3) + 3;
					last = 0;
					break;
				case 18:	// zero run of 11-138
					if (mBits.avail() < 7)
						return false;
					run = mBits.getbits(7) + 11;
					last = 0;
					break;
				default:
					last = k;
				}

				if (run+j > total_count) {
					VDDEBUG_INFLATE_ERROR("Length tree bad\n");
					return false;
				}

				do {
					mCodeLengths[j++] = last;
				} while(--run);
			}

			memmove(mCodeLengths + 288, mCodeLengths + code_count, dist_count);

			if (!InflateExpandTable32K(mCodeDecode, mCodeLengths, code_count)) {
				VDDEBUG_INFLATE_ERROR("Code tree bad\n");
				return false;
			}
			if (!InflateExpandTable32K(mDistDecode, mCodeLengths+288, dist_count)) {
				VDDEBUG_INFLATE_ERROR("Distance tree bad\n");
				return false;
			}
			mBlockType = kDeflatedBlock;
		}
		break;
	default:
		return false;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////

#pragma pack(push, 2)

namespace {
	enum {
		kZipMethodStore		= 0,
		kZipMethodDeflate	= 8,
		kZipMethodEnhancedDeflate	= 9
	};

	struct ZipFileHeader {
		enum { kSignature = 0x04034b50 };
		uint32		signature;
		uint16		version_required;
		uint16		flags;
		uint16		method;
		uint16		mod_time;
		uint16		mod_date;
		uint32		crc32;
		uint32		compressed_size;
		uint32		uncompressed_size;
		uint16		filename_len;
		uint16		extrafield_len;
	};

	struct ZipDataDescriptor {
		uint32		crc32;
		uint32		compressed_size;
		uint32		uncompressed_size;
	};

	struct ZipFileEntry {
		enum { kSignature = 0x02014b50 };
		uint32		signature;
		uint16		version_create;
		uint16		version_required;
		uint16		flags;
		uint16		method;
		uint16		mod_time;
		uint16		mod_date;
		uint32		crc32;
		uint32		compressed_size;
		uint32		uncompressed_size;
		uint16		filename_len;
		uint16		extrafield_len;
		uint16		comment_len;
		uint16		diskno;
		uint16		internal_attrib;
		uint32		external_attrib;
		uint32		reloff_localhdr;
	};

	struct ZipCentralDir {
		enum { kSignature = 0x06054b50 };

		uint32		signature;
		uint16		diskno;
		uint16		diskno_dir;
		uint16		dirents;
		uint16		dirents_total;
		uint32		dirsize;
		uint32		diroffset;
		uint16		comment_len;
	};
}

#pragma pack(pop)

VDZipArchive::VDZipArchive() {
}

VDZipArchive::~VDZipArchive() {
}

void VDZipArchive::Init(IVDRandomAccessStream *pSrc) {
	mpStream = pSrc;

	// First, see if the central directory is at the end (common case).
	const sint64 streamLen = mpStream->Length();
	mpStream->Seek(streamLen - sizeof(ZipCentralDir));

	ZipCentralDir cdirhdr;

	mpStream->Read(&cdirhdr, sizeof cdirhdr);
	if (cdirhdr.signature != ZipCentralDir::kSignature) {
		// Okay, the central directory isn't at the end. Read the last 64K of the file
		// and see if we can spot it. 
		uint32 buflen = 65536 + sizeof(ZipCentralDir);

		if ((sint64)buflen > streamLen)
			buflen = (uint32)streamLen;

		vdfastvector<uint8> buf(buflen);
		const uint8 *bufp = buf.data();

		const sint64 bufOffset = streamLen - buflen;
		mpStream->Seek(bufOffset);
		mpStream->Read(buf.data(), buflen);

		// Search for valid end-of-central-dir signature.
		const uint32 kNativeEndSig = VDFromLE32(ZipCentralDir::kSignature);
		const uint32 kNativeStartSig = VDFromLE32(ZipFileEntry::kSignature);

		for(uint32 i=0; i<buflen-4; ++i) {
			if (VDReadUnalignedU32(bufp + i) == kNativeEndSig) {
				const uint32 diroffset = VDReadUnalignedLEU32(bufp + i + offsetof(ZipCentralDir, diroffset));
				const uint32 dirsize = VDReadUnalignedLEU32(bufp + i + offsetof(ZipCentralDir, dirsize));

				if (diroffset + dirsize == bufOffset + i) {
					uint32 testsig;
					mpStream->Seek(diroffset);
					mpStream->Read(&testsig, 4);

					if (testsig == kNativeStartSig) {
						memcpy(&cdirhdr, bufp + i, sizeof(ZipCentralDir));
						goto found_directory;
					}
				}
			}
		}

		throw MyError("Zip file has missing or bad central directory");
	}

found_directory:
	mDirectory.resize(cdirhdr.dirents_total);

	mpStream->Seek(cdirhdr.diroffset);

	for(int i=0; i<cdirhdr.dirents_total; ++i) {
		FileInfoInternal& fii = mDirectory[i];
		ZipFileEntry ent;

		mpStream->Read(&ent, sizeof ent);
		if (ent.signature != ZipFileEntry::kSignature)
			throw MyError("Zip directory is bad");

		if (ent.method != kZipMethodStore && ent.method != kZipMethodDeflate) {
			if (ent.method == kZipMethodEnhancedDeflate)
				throw MyError("The Enhanced Deflate compression method in .zip files is not currently supported.");
			else
				throw MyError("Unsupported compression method in zip archive");
		}

		fii.mDataStart			= ent.reloff_localhdr;
		fii.mCompressedSize		= ent.compressed_size;
		fii.mUncompressedSize	= ent.uncompressed_size;
		fii.mCRC32				= ent.crc32;
		fii.mbPacked			= ent.method == kZipMethodDeflate;
		fii.mFileName.resize(ent.filename_len);

		mpStream->Read(&*fii.mFileName.begin(), ent.filename_len);
		
		mpStream->Seek(mpStream->Pos() + ent.extrafield_len + ent.comment_len);
	}
}

sint32 VDZipArchive::GetFileCount() {
	return mDirectory.size();
}

const VDZipArchive::FileInfo& VDZipArchive::GetFileInfo(sint32 idx) {
	VDASSERT((size_t)idx < mDirectory.size());
	return mDirectory[idx];
}

IVDStream *VDZipArchive::OpenRawStream(sint32 idx) {
	const FileInfoInternal& fi = mDirectory[idx];

	mpStream->Seek(fi.mDataStart);

	ZipFileHeader hdr;
	mpStream->Read(&hdr, sizeof hdr);

	if (hdr.signature != ZipFileHeader::kSignature)
		throw MyError("Bad header for file in zip archive");

	mpStream->Seek(fi.mDataStart + sizeof(hdr) + hdr.filename_len + hdr.extrafield_len);

	return mpStream;
}

///////////////////////////////////////////////////////////////////////////

void VDGUnzipStream::Init(IVDStream *pSrc, uint64 limit) {
	// See RFC1952 for a description of the gzip header format.
	uint8 hdr[10];

	uint32 gzipContainerBytes = 10 + 8;	// header + footer
	pSrc->Read(hdr, 10);
	if (hdr[0] != 0x1f || hdr[1] != 0x8b)
		throw MyError("Source stream is not in gzip format.");

	if (hdr[2] != 0x08)
		throw MyError("Gzip stream uses an unsupported compression method.");

	enum {
		FLG_FTEXT		= 0x01,
		FLG_FHCRC		= 0x02,
		FLG_FEXTRA		= 0x04,
		FLG_FNAME		= 0x08,
		FLG_FCOMMENT	= 0x10
	};

	const uint8 flg = hdr[3];

	if (flg & FLG_FEXTRA) {
		uint8 xlendat[2];
		pSrc->Read(xlendat, 2);

		uint32 xlen = VDReadUnalignedLEU16(xlendat);
		uint8 buf[256];

		gzipContainerBytes += xlen + 2;

		while(xlen) {
			uint32 tc = xlen > 256 ? 256 : xlen;
			pSrc->Read(buf, tc);
			xlen -= tc;
		}
	}

	if (flg & FLG_FNAME) {
		// ugh
		uint8 c;
		for(;;) {
			pSrc->Read(&c, 1);
			++gzipContainerBytes;

			if (!c)
				break;

			mFilename += c;
		} 
	}

	if (flg & FLG_FCOMMENT) {
		// ugh
		uint8 c;
		do {
			pSrc->Read(&c, 1);
			++gzipContainerBytes;
		} while(c);
	}

	if (flg & FLG_FHCRC) {
		uint16 crc16;

		pSrc->Read(&crc16, 2);
		gzipContainerBytes += 2;
	}

	if (gzipContainerBytes > limit)
		throw MyError("The gzip compressed data is invalid.");

	limit -= gzipContainerBytes;

	VDInflateStream::Init(pSrc, limit, false);
}

///////////////////////////////////////////////////////////////////////////

VDDeflateStream::VDDeflateStream(IVDStream& dest)
	: mDestStream(dest)
	, mpEncoder(nullptr)
	, mPos(0)
	, mCRCChecker(VDCRCTable::CRC32)
{
	Reset();
}

VDDeflateStream::~VDDeflateStream() {
	delete mpEncoder;
}

void VDDeflateStream::Reset() {
	mPos = 0;
	
	mCRCChecker.Init();

	delete mpEncoder;
	mpEncoder = nullptr;
	mpEncoder = new VDDeflateEncoder;
	mpEncoder->Init(false,
		[this](const void *p, uint32 n) { PreProcessInput(p, n); },
		[this](const void *p, uint32 n) { WriteOutput(p, n); }
	);
}

void VDDeflateStream::Finalize() {
	mpEncoder->Finish();
}

const wchar_t *VDDeflateStream::GetNameForError() {
	return mDestStream.GetNameForError();
}

void VDDeflateStream::Read(void *buffer, sint32 bytes) {
	throw MyError("Deflate streams are write-only.");
}

sint32 VDDeflateStream::ReadData(void *buffer, sint32 bytes) {
	throw MyError("Deflate streams are write-only.");
}

void VDDeflateStream::Write(const void *buffer, sint32 bytes) {
	if (bytes <= 0)
		return;

	mPos += bytes;
	mpEncoder->Write(buffer, (uint32)bytes);
}

void VDDeflateStream::PreProcessInput(const void *p, uint32 n) {
	mCRCChecker.Process(p, n);
}

void VDDeflateStream::WriteOutput(const void *p, uint32 n) {
	mDestStream.Write(p, n);
}

///////////////////////////////////////////////////////////////////////////

class VDZipArchiveWriter final : public IVDZipArchiveWriter {
public:
	VDZipArchiveWriter(IVDStream& dest);
	~VDZipArchiveWriter();

	VDDeflateStream& BeginFile(const wchar_t *path);
	void EndFile();

	void Finalize();

private:
	struct DirEnt {
		VDStringA mPath;
		sint64 mPos;
		sint64 mCompressedSize;
		sint64 mUncompressedSize;
		uint32 mCRC32;
		uint16 mFlags;
		uint8 mMethod;
	};

	IVDStream& mDestStream;
	sint64 mFileStart;
	sint64 mFileEnd;
	uint16 mFileDate;
	uint16 mFileTime;

	vdvector<DirEnt> mDirectory;

	// large -- put at end
	VDDeflateStream mDeflateStream;
};

VDZipArchiveWriter::VDZipArchiveWriter(IVDStream& dest)
	: mDestStream(dest)
	, mDeflateStream(dest)
{
	// Currently, we use a single timestamp from the beginning of the archive creation for
	// all files within the archive. The local date and time need to be encoded to MS-DOS
	// format for the basic Zip headers.

	const VDExpandedDate localDate = VDGetLocalDate(VDGetCurrentDate());

	mFileDate = (((localDate.mYear - 1980) & 127) << 9)
			+ (localDate.mMonth << 5)
			+ localDate.mDay;

	mFileTime = (localDate.mHour << 11)
		+ (localDate.mMinute << 5)
		+ (localDate.mSecond >> 1);
}

VDZipArchiveWriter::~VDZipArchiveWriter() {
}

VDDeflateStream& VDZipArchiveWriter::BeginFile(const wchar_t *path) {
	DirEnt& de = mDirectory.emplace_back();
	de.mPos = mDestStream.Pos();

	// Normalize the zip path by removing leading slashes, repeated slashes, and
	// converting backslashes to forward slashes.
	const VDStringA& rawPath = VDTextWToU8(VDStringSpanW(path));
	char last = '/';
	char extCheck = 0;
	for(char c : rawPath) {
		if (c == '\\')
			c = '/';

		if (c != last || last != '/')
			de.mPath.push_back(c);

		last = c;
		extCheck |= c;
	}

	//de.mMethod = 0;				// stored
	de.mMethod = 8;				// Deflate

	// bit 3: local header size/CRC32 are not filled out, use data descriptor
	de.mFlags = 0x0008;

	// bit 11: language encoding flag (EFS) - use UTF-8
	//
	// To avoid tempting fate, this bit is only set if we have non-ASCII characters in
	// the path. Info-Zip unzip versions through 6.00 unfortunately do not handle this
	// properly, either using OEM CP437 or ISO 8859-1 decoding for the filename, making
	// it a bit of a lost cause. Windows 10 File Explorer and 7-Zip 16 do handle UTF-8
	// filenames.

	if (extCheck & (char)0x80)
		de.mFlags |= 0x800;

	ZipFileHeader zhdr {};
	zhdr.signature = zhdr.kSignature;
	zhdr.version_required = 20;
	zhdr.flags = de.mFlags;
	zhdr.method = de.mMethod;
	zhdr.mod_time = mFileTime;
	zhdr.mod_date = mFileDate;
	zhdr.crc32 = 0;
	zhdr.compressed_size = 0;
	zhdr.uncompressed_size = 0;
	zhdr.filename_len = de.mPath.size();
	zhdr.extrafield_len = 0;

	mDestStream.Write(&zhdr, sizeof zhdr);
	mDestStream.Write(de.mPath.data(), de.mPath.size());

	mFileStart = mDestStream.Pos();

	mDeflateStream.Reset();

	return mDeflateStream;
}

void VDZipArchiveWriter::EndFile() {
	mDeflateStream.Finalize();
	
	mFileEnd = mDestStream.Pos();

	DirEnt& de = mDirectory.back();
	de.mCompressedSize = mFileEnd - mFileStart;
	de.mUncompressedSize = mDeflateStream.Pos();
	de.mCRC32 = mDeflateStream.GetCRC();

	// write data descriptor
	ZipDataDescriptor zdesc {};
	zdesc.crc32 = de.mCRC32;
	zdesc.compressed_size =  de.mCompressedSize > 0xFFFFFFFF ? 0xFFFFFFFF : (uint32)de.mCompressedSize;
	zdesc.uncompressed_size =  de.mUncompressedSize > 0xFFFFFFFF ? 0xFFFFFFFF : (uint32)de.mUncompressedSize;

	mDestStream.Write(&zdesc, sizeof zdesc);
}

void VDZipArchiveWriter::Finalize() {
	ZipFileEntry zfe;

	const sint64 dirStartPos = mDestStream.Pos();

	for(const DirEnt& de : mDirectory) {
		zfe = {};
		zfe.signature = zfe.kSignature;
		zfe.version_create = 20;
		zfe.version_required = 20;	// pkzip 2.0 compatible
		zfe.flags = de.mFlags;
		zfe.method = de.mMethod;
		zfe.mod_time = mFileTime;
		zfe.mod_date = mFileDate;
		zfe.crc32 = de.mCRC32;
		zfe.compressed_size = de.mCompressedSize;
		zfe.uncompressed_size = de.mUncompressedSize;
		zfe.filename_len = de.mPath.size();
		zfe.extrafield_len = 0;
		zfe.comment_len = 0;
		zfe.diskno = 0;
		zfe.internal_attrib = 0;	// binary data
		zfe.external_attrib = 0;
		zfe.reloff_localhdr = de.mPos != (uint32)de.mPos ? 0xFFFFFFFF : (uint32)de.mPos;

		mDestStream.Write(&zfe, sizeof zfe);
		mDestStream.Write(de.mPath.data(), de.mPath.size());
	}
	
	const sint64 dirEndPos = mDestStream.Pos();

	ZipCentralDir zdir {};
	zdir.signature = zdir.kSignature;
	zdir.diskno = 0;
	zdir.diskno_dir = 0;
	zdir.dirents = mDirectory.size();
	zdir.dirents_total = mDirectory.size();
	zdir.dirsize = (uint32)(dirEndPos - dirStartPos);
	zdir.diroffset = (uint32)dirStartPos;
	zdir.comment_len = 0;

	mDestStream.Write(&zdir, sizeof zdir);
}

IVDZipArchiveWriter *VDCreateZipArchiveWriter(IVDStream& stream) {
	return new VDZipArchiveWriter(stream);
}
