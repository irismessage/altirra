//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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
#include <bit>
#include <vd2/system/binary.h>
#include <vd2/system/vdstl.h>
#include "printerttfencoder.h"

consteval ATTrueTypeEncoder::TTFTypeId::TTFTypeId(const char *s)
	: mId(0)
{
	const uint8 v[4] {
		(uint8)s[0],
		(uint8)s[1],
		(uint8)s[2],
		(uint8)s[3],
	};

	if (s[4])
		throw;

	mId = VDFromBEU32(std::bit_cast<uint32>(v));
}

ATTrueTypeEncoder::ATTrueTypeEncoder() {
	static_assert(sizeof(TTFPoint) == 4);

	mTableBuffer.resize(12, 0);

	// For determinism, we default to a fixed timestamp (1/1/2024 midnight GMT).
	mCreatedDate = mModifiedDate = VDDate { 133485408000000000 };
}

ATTrueTypeEncoder::~ATTrueTypeEncoder() {
}

void ATTrueTypeEncoder::SetTimestamps(const VDDate& created, const VDDate& modified) {
	mCreatedDate = created;
	mModifiedDate = modified;
}

void ATTrueTypeEncoder::SetDefaultAdvanceWidth(sint32 advance) {
	mDefaultAdvanceWidth = advance;
}

void ATTrueTypeEncoder::SetDefaultChar(uint16 defaultCh) {
	mDefaultChar = defaultCh;
}

void ATTrueTypeEncoder::SetBreakChar(uint16 breakCh) {
	mBreakChar = breakCh;
}

ATTrueTypeGlyphIndex ATTrueTypeEncoder::BeginSimpleGlyph() {
	ATTrueTypeGlyphIndex glyphIndex = (ATTrueTypeGlyphIndex)mGlyphs.size();

	TTFGlyphInfo& glyphInfo = mGlyphs.emplace_back();
	glyphInfo.mStartIndex = (uint32)mGlyphPoints.size();

	return glyphIndex;
}

void ATTrueTypeEncoder::AddGlyphPoint(sint32 x, sint32 y, bool onCurve) {
	TTFPoint pt;
	pt.mX = x;
	pt.mY = y;
	pt.mbOnCurve = onCurve;
	pt.mbEndContour = false;
	mGlyphPoints.push_back(pt);
}

void ATTrueTypeEncoder::EndContour() {
	mGlyphPoints.back().mbEndContour = true;
	++mGlyphs.back().mNumContours;
}

void ATTrueTypeEncoder::EndSimpleGlyph() {
	TTFGlyphInfo& glyphInfo = mGlyphs.back();
	auto it = mGlyphPoints.begin() + glyphInfo.mStartIndex;
	auto itEnd = mGlyphPoints.end();

	glyphInfo.mCount = (uint32)(itEnd - it);

	mMaxPoints = std::max<uint32>(mMaxPoints, glyphInfo.mCount);
	mMaxContours = std::max<uint32>(mMaxContours, glyphInfo.mNumContours);

	glyphInfo.mBBoxX1 = 0x7FFFFFFF;
	glyphInfo.mBBoxY1 = 0x7FFFFFFF;
	glyphInfo.mBBoxX2 = -0x7FFFFFFF - 1;
	glyphInfo.mBBoxY2 = -0x7FFFFFFF - 1;

	for(const TTFPoint& pt : vdspan(it, itEnd)) {
		glyphInfo.mBBoxX1 = std::min<sint32>(glyphInfo.mBBoxX1, pt.mX);
		glyphInfo.mBBoxY1 = std::min<sint32>(glyphInfo.mBBoxY1, pt.mY);
		glyphInfo.mBBoxX2 = std::max<sint32>(glyphInfo.mBBoxX2, pt.mX);
		glyphInfo.mBBoxY2 = std::max<sint32>(glyphInfo.mBBoxY2, pt.mY);
	}
}

ATTrueTypeGlyphIndex ATTrueTypeEncoder::BeginCompositeGlyph() {
	ATTrueTypeGlyphIndex glyphIndex = (ATTrueTypeGlyphIndex)mGlyphs.size();

	TTFGlyphInfo& glyphInfo = mGlyphs.emplace_back();
	glyphInfo.mNumContours = -1;
	glyphInfo.mStartIndex = (uint32)mGlyphReferences.size();
	glyphInfo.mCount = 0;
	glyphInfo.mBBoxX1 = 0x7FFFFFFF;
	glyphInfo.mBBoxY1 = 0x7FFFFFFF;
	glyphInfo.mBBoxX2 = -0x7FFFFFFF - 1;
	glyphInfo.mBBoxY2 = -0x7FFFFFFF - 1;

	return glyphIndex;
}

void ATTrueTypeEncoder::AddGlyphReference(ATTrueTypeGlyphIndex index, sint32 offsetX, sint32 offsetY) {
	const TTFGlyphInfo& srcGlyphInfo = mGlyphs[(size_t)index];
	VDASSERT(srcGlyphInfo.mNumContours >= 0);

	if (srcGlyphInfo.mNumContours <= 0)
		return;

	TTFGlyphInfo& compGlyphInfo = mGlyphs.back();
	
	compGlyphInfo.mBBoxX1 = std::min<sint32>(compGlyphInfo.mBBoxX1, srcGlyphInfo.mBBoxX1 + offsetX);
	compGlyphInfo.mBBoxY1 = std::min<sint32>(compGlyphInfo.mBBoxY1, srcGlyphInfo.mBBoxY1 + offsetY);
	compGlyphInfo.mBBoxX2 = std::max<sint32>(compGlyphInfo.mBBoxX2, srcGlyphInfo.mBBoxX2 + offsetX);
	compGlyphInfo.mBBoxY2 = std::max<sint32>(compGlyphInfo.mBBoxY2, srcGlyphInfo.mBBoxY2 + offsetY);

	auto& ref = mGlyphReferences.emplace_back();
	ref.mSrcGlyph = index;
	ref.mOffsetX = offsetX;
	ref.mOffsetY = offsetY;
}

void ATTrueTypeEncoder::EndCompositeGlyph() {
	TTFGlyphInfo& compGlyphInfo = mGlyphs.back();

	compGlyphInfo.mCount = (uint32)mGlyphReferences.size() - compGlyphInfo.mStartIndex;

	uint32 numPoints = 0;
	uint32 numContours = 0;

	for(const TTFGlyphReference& ref : vdspan(mGlyphReferences).subspan(compGlyphInfo.mStartIndex)) {
		const TTFGlyphInfo& srcGlyph = mGlyphs[(size_t)ref.mSrcGlyph];

		numPoints += srcGlyph.mCount;

		for(const TTFPoint& pt : vdspan(mGlyphPoints).subspan(srcGlyph.mStartIndex, srcGlyph.mCount)) {
			if (pt.mbEndContour)
				++numContours;
		}
	}

	mMaxCompositePoints = std::max<uint32>(mMaxCompositePoints, numPoints);
	mMaxCompositeContours = std::max<uint32>(mMaxCompositeContours, numContours);
	mMaxCompositeElements = std::max<uint32>(mMaxCompositeElements, (uint32)(mGlyphReferences.size() - compGlyphInfo.mStartIndex));
	mMaxCompositeDepth = 1;
}

void ATTrueTypeEncoder::MapCharacter(uint32 ch, ATTrueTypeGlyphIndex glyphIndex) {
	mCharMappings.emplace_back(ch, glyphIndex);
}

void ATTrueTypeEncoder::MapCharacterRange(uint32 chBase, ATTrueTypeGlyphIndex glyphIndexBase, uint32 numChars) {
	for(uint32 i=0; i<numChars; ++i) {
		mCharMappings.emplace_back(chBase + i, ATTrueTypeGlyphIndex((uint32)glyphIndexBase + i));
	}
}

void ATTrueTypeEncoder::SetName(ATTrueTypeName name, const char *str) {
	size_t len = strlen(str);

	mNames.emplace_back((uint16)name, (uint16)mNameChars.size(), (uint16)len);

	for(size_t i=0; i<len; ++i)
		mNameChars.push_back(VDToBE16((unsigned char)str[i]));
}

vdspan<const uint8> ATTrueTypeEncoder::Finalize() {
	// compute bounding box
	mBBoxX1 = 0x7FFFFFFF;
	mBBoxY1 = 0x7FFFFFFF;
	mBBoxX2 = -0x7FFFFFFF-1;
	mBBoxY2 = -0x7FFFFFFF-1;

	for(const TTFGlyphInfo& glyphInfo : mGlyphs) {
		if (glyphInfo.mNumContours == 0)
			continue;

		mBBoxX1 = std::min<sint32>(mBBoxX1, glyphInfo.mBBoxX1);
		mBBoxY1 = std::min<sint32>(mBBoxY1, glyphInfo.mBBoxY1);
		mBBoxX2 = std::max<sint32>(mBBoxX2, glyphInfo.mBBoxX2);
		mBBoxY2 = std::max<sint32>(mBBoxY2, glyphInfo.mBBoxY2);
	}

	std::sort(mCharMappings.begin(), mCharMappings.end(),
		[](const TTFCharMapping& a, const TTFCharMapping& b) {
			return a.mCh < b.mCh;
		}
	);

	WriteTableHead();
	WriteTableHhea();
	WriteTableMaxp();
	WriteTableOS2();
	WriteTableHmtx();
	WriteTableCmap();
	WriteTablePost();
	WriteTableGlyfLoca();
	WriteTableName();

	size_t numTables = mTableDirectory.size();

	struct TTFHeader {
		vdbe<uint32> mSfntVersion = 0x00010000;
		vdbe<uint16> mNumTables;
		vdbe<uint16> mSearchRange;
		vdbe<uint16> mEntrySelector;
		vdbe<uint16> mRangeShift;
	} header;
	static_assert(sizeof(TTFHeader) == 12);

	int tableBits = 0;

	while(((size_t)2 << tableBits) <= numTables)
		++tableBits;

	int searchRange = 16 << tableBits;
	header.mNumTables = (uint16)numTables;
	header.mSearchRange = (uint16)searchRange;
	header.mEntrySelector = (uint16)tableBits;
	header.mRangeShift = (uint16)((numTables << 4) - searchRange);

	// copy header into reserved space
	memcpy(mTableBuffer.data(), &header, sizeof header);

	// checksum the header
	uint32 chk = 0;
	chk += VDReadUnalignedBEU32(&mTableBuffer[0]);
	chk += VDReadUnalignedBEU32(&mTableBuffer[4]);
	chk += VDReadUnalignedBEU32(&mTableBuffer[8]);

	// checksum, offset, and endian swap the table directory
	uint32 tableOffset = (uint32)(16 * mTableDirectory.size());

	std::sort(mTableDirectory.begin(), mTableDirectory.end(),
		[](const TTFTableEntry& a, const TTFTableEntry& b) {
			return a.mId < b.mId;
		}
	);

	for(TTFTableEntry& te : mTableDirectory) {
		te.mPos += tableOffset;

		chk += te.mId;
		chk += te.mChecksum * 2;	// once for the checksum field, once for the data
		chk += te.mPos;
		chk += te.mLength;

		te.mId = VDToBEU32(te.mId);
		te.mChecksum = VDToBEU32(te.mChecksum);
		te.mPos = VDToBEU32(te.mPos);
		te.mLength = VDToBEU32(te.mLength);
	}

	// insert table directory after TTF header
	mTableBuffer.insert(mTableBuffer.begin() + 12, (uint8 *)mTableDirectory.data(), (uint8 *)(mTableDirectory.data() + mTableDirectory.size()));

	// patch the checksum in the 'head' chunk -- this is always the first chunk
	// in our TTF
	chk = 0xB1B0AFBAU - chk;

	VDWriteUnalignedBEU32(&mTableBuffer[12 + tableOffset + 8], chk);

	return mTableBuffer;
}

void ATTrueTypeEncoder::WriteTableHead() {
	struct HeadTable {
		vdbe<uint16> mVersionMajor		= 1;
		vdbe<uint16> mVersionMinor		= 0;
		vdbe<uint32> mFontRevision		= 0x00010000;	// must be consistent with Version name
		vdbe<uint32> mChecksum			= 0;
		vdbe<uint32> mMagic				= 0x5F0F3CF5;
		vdbe<uint16> mFlags				= 0;
		vdbe<uint16> mUnitsPerEm		= 1024;
		vdbe<uint64> mCreated			= 0;
		vdbe<uint64> mModified			= 0;
		vdbe<sint16> mBBoxX1			= 0;
		vdbe<sint16> mBBoxY1			= 0;
		vdbe<sint16> mBBoxX2			= 0;
		vdbe<sint16> mBBoxY2			= 0;
		vdbe<uint16> mMacStyle			= 0;
		vdbe<uint16> mLowestRecPPEM		= 8;			// lowest readable size in pixels; FontValidator warns 1 is implausible
		vdbe<uint16> mFontDirectionHint	= 2;
		vdbe<uint16> mIndexToLocFormat	= 0;
		vdbe<uint16> mGlyphDataFormat	= 0;
	} ht;

	// Timestamps in TTF are seconds since midnight 1/1/1904 UTC, so we need to adjust the epoch and then
	// divide by 10000000 to go from 100ns units to seconds.
	const auto dateToTTF = [](const VDDate& date) -> uint64 {
		return ((date.mTicks - 95616288000000000) + 5000000) / 10000000;
	};

	ht.mCreated = dateToTTF(mCreatedDate);
	ht.mModified = dateToTTF(mModifiedDate);

	ht.mBBoxX1 = (sint16)mBBoxX1;
	ht.mBBoxY1 = (sint16)mBBoxY1;
	ht.mBBoxX2 = (sint16)mBBoxX2;
	ht.mBBoxY2 = (sint16)mBBoxY2;

	BeginTable("head");
	WriteRaw(ht);
	EndTable();
}

void ATTrueTypeEncoder::WriteTableHhea() {
	struct HheaTable {
		vdbe<uint16> mMajorVersion = 1;
		vdbe<uint16> mMinorVersion = 0;
		vdbe<sint16> mAscender = 0;
		vdbe<sint16> mDescender = 0;
		vdbe<sint16> mLineGap = 0;
		vdbe<sint16> mAdvanceWidthMax = 0;
		vdbe<sint16> mMinLeftSideBearing = 0;
		vdbe<sint16> mMinRightSideBearing = 0;
		vdbe<sint16> mXMaxExtent = 0;
		vdbe<sint16> mCaretSlopeRise = 1;
		vdbe<sint16> mCaretSlopeRun = 0;
		vdbe<sint16> mCaretOffset = 0;
		vdbe<sint16> mReserved1 = 0;
		vdbe<sint16> mReserved2 = 0;
		vdbe<sint16> mReserved3 = 0;
		vdbe<sint16> mReserved4 = 0;
		vdbe<sint16> mMetricDataFormat = 0;
		vdbe<uint16> mNumberOfHMetrics = 0;
	} table;

	table.mAscender = mBBoxY2;
	table.mDescender = (sint16)std::min<sint32>(-1, mBBoxY1);
	table.mAdvanceWidthMax = mDefaultAdvanceWidth;
	table.mMinLeftSideBearing = 0;
	table.mMinRightSideBearing = mDefaultAdvanceWidth - mBBoxX2;
	table.mXMaxExtent = mBBoxX2;
	table.mNumberOfHMetrics = (uint16)mGlyphs.size();

	BeginTable("hhea");
	WriteRaw(table);
	EndTable();
}

void ATTrueTypeEncoder::WriteTableHmtx() {
	// we only support monospaced fonts, so just set the default font with
	// no per-glyph metrics
	BeginTable("hmtx");

	uint16 v[2] {
		VDToBEU16(mDefaultAdvanceWidth),
		0
	};

	for([[maybe_unused]] const auto& glyph: mGlyphs)
		WriteRaw(v);

	EndTable();
}

void ATTrueTypeEncoder::WriteTableMaxp() {
	struct MaxpTable {
		vdbe<uint32> mVersion16Dot16 = 0x00010000;
		vdbe<uint16> mNumGlyphs = 0;
		vdbe<uint16> mMaxPoints = 0;
		vdbe<uint16> mMaxContours = 0;
		vdbe<uint16> mMaxCompositePoints = 0;
		vdbe<uint16> mMaxCompositeContours = 0;
		vdbe<uint16> mMaxZones = 0;
		vdbe<uint16> mMaxTwilightPoints = 0;
		vdbe<uint16> mMaxStorage = 0;
		vdbe<uint16> mMaxFunctionDefs = 0;
		vdbe<uint16> mMaxInstructionDefs = 0;
		vdbe<uint16> mMaxStackElements = 0;
		vdbe<uint16> mMaxSizeOfInstructions = 0;
		vdbe<uint16> mMaxComponentElements = 0;
		vdbe<uint16> mMaxComponentDepth = 0;
	} table;

	table.mNumGlyphs = (uint16)mGlyphs.size();
	table.mMaxPoints = (uint16)mMaxPoints;
	table.mMaxContours = (uint16)mMaxContours;
	table.mMaxCompositePoints = (uint16)mMaxCompositePoints;
	table.mMaxCompositeContours = (uint16)mMaxCompositeContours;
	table.mMaxComponentElements = (uint16)mMaxCompositeElements;
	table.mMaxComponentDepth = (uint16)mMaxCompositeDepth;

	BeginTable("maxp");
	WriteRaw(table);
	EndTable();
}

void ATTrueTypeEncoder::WriteTablePost() {
	struct PostTable {
		vdbe<uint32> mVersion16Dot16 = 0x00030000;
		vdbe<sint16> mItalicAngle = 0;
		vdbe<sint16> mUnderlinePosition = 0;
		vdbe<sint16> mUnderlineThickness = 0;
		vdbe<uint32> mIsFixedPitch = 1;
		vdbe<uint32> mMinMemType42 = 0;
		vdbe<uint32> mMaxMemType42 = 0;
		vdbe<uint32> mMinMemType1 = 0;
		vdbe<uint32> mMaxMemType1 = 0;
		vdbe<uint16> mNumGlyphs = 0;
	} table;

	BeginTable("post");
	WriteRaw(table);
	EndTable();
}

void ATTrueTypeEncoder::WriteTableOS2() {
	sint16 subWidth = (mBBoxX2 - mBBoxX1) >> 1;
	sint16 subHeight = (mBBoxY2 - mBBoxY1) >> 1;

	struct OS2Table {
		vdbe<uint16> mVersion = 5;
		vdbe<sint16> mXAvgCharWidth = 0;
		vdbe<uint16> mUsWeightClass = 400;		// normal weight
		vdbe<uint16> mUsWidthClass = 5;			// medium width
		vdbe<uint16> mFsTtype = 8;				// editable embedding
		vdbe<sint16> mYSubscriptXSize = 0;
		vdbe<sint16> mYSubscriptYSize = 0;
		vdbe<sint16> mYSubscriptXOffset = 0;
		vdbe<sint16> mYSubscriptYOffset = 0;
		vdbe<sint16> mYSuperscriptXSize = 0;
		vdbe<sint16> mYSuperscriptYSize = 0;
		vdbe<sint16> mYSuperscriptXOffset = 0;
		vdbe<sint16> mYSuperscriptYOffset = 0;
		vdbe<sint16> mYStrikeoutSize = 0;
		vdbe<sint16> mYStrikeoutPosition = 0;
		vdbe<sint16> mSFamilyClass = 0;
		uint8 mPanose[10] {};
		vdbe<uint32> mUlUnicodeRange1 = 0;
		vdbe<uint32> mUlUnicodeRange2 = 0;
		vdbe<uint32> mUlUnicodeRange3 = 0;
		vdbe<uint32> mUlUnicodeRange4 = 0;
		vdbe<uint32> mAchVendID = 0x20202020;
		vdbe<uint16> mFsSelection = 0;
		vdbe<uint16> mUsFirstCharIndex = 0;
		vdbe<uint16> mUsLastCharIndex = 0;
		vdbe<sint16> mSTypoAscender = 0;
		vdbe<sint16> mSTypoDescender = 0;
		vdbe<sint16> mSTypoLineGap = 0;
		vdbe<uint16> mUsWinAscent = 0;
		vdbe<uint16> mUsWinDescent = 0;
		vdbe<uint32> mUlCodePageRange1 = 0;
		vdbe<uint32> mUlCodePageRange2 = 0;
		vdbe<sint16> mSxHeight = 0;
		vdbe<sint16> mSCapHeight = 0;
		vdbe<uint16> mUsDefaultChar = 0;
		vdbe<uint16> mUsBreakChar = 0;
		vdbe<uint16> mUsMaxContext = 0;
		vdbe<uint16> mUsLowerOpticalPointSize = 0;
		vdbe<uint16> mUsUpperOpticalPointSize = 0;
	} table;

	table.mXAvgCharWidth = mDefaultAdvanceWidth;
	table.mYSubscriptXSize = subWidth;
	table.mYSubscriptYSize = subHeight;
	table.mYSuperscriptXSize = subWidth;
	table.mYSuperscriptYSize = subHeight;
	table.mYStrikeoutPosition = subHeight;
	table.mSTypoAscender = mBBoxY2;
	table.mSTypoDescender = (sint16)std::min<sint32>(-1, mBBoxY1);
	table.mUsWinAscent = (uint16)mBBoxY2;
	table.mUsWinDescent = (uint16)std::max<sint32>(1, -mBBoxY1);

	table.mUsFirstCharIndex = mCharMappings.front().mCh;
	table.mUsLastCharIndex = mCharMappings.back().mCh;
	table.mUsDefaultChar = mDefaultChar;
	table.mUsBreakChar = mBreakChar;

	BeginTable("OS/2");
	WriteRaw(table);
	EndTable();
}

void ATTrueTypeEncoder::WriteTableCmap() {
	// RLE coding:
	//	- Split into contiguous ranges of character codes. Any gap in character
	//	  codes requires a new segment.
	//	- If subrange has a consistent char-to-glyph offset, it can be
	//	  encoded statically (8 bytes).
	//	- Otherwise, N glyphs must be encoded explicitly as 8+2N bytes.
	//	- Splitting a segment to encode it as a run of M glyphs saves 2M-16
	//	  bytes. Therefore, it is always a win at M>8 in the middle, and M>4
	//	  at an end.

	vdfastvector<vdbe<uint16>> endCode;
	vdfastvector<vdbe<uint16>> startCode;
	vdfastvector<vdbe<uint16>> idDelta;
	vdfastvector<vdbe<uint16>> idRangeOffset;
	vdfastvector<vdbe<uint16>> glyphIdArray;

	vdspan<TTFCharMapping> remainingMappings(mCharMappings);

	while(!remainingMappings.empty()) {
		// determine length of contiguous span
		size_t n = remainingMappings.size();
		size_t m = 1;

		while(m < n && remainingMappings[m-1].mCh + 1 == remainingMappings[m].mCh)
			++m;

		// slice off the range to encode into a segment
		vdspan<TTFCharMapping> segmentMappings = remainingMappings.first(m);
		remainingMappings = remainingMappings.subspan(m);

		while(!segmentMappings.empty()) {
			// determine the length of run at the start
			uint32 firstCh = segmentMappings.front().mCh;
			startCode.push_back((uint16)firstCh);

			size_t segmentLen = segmentMappings.size();
			uint16 firstCodeOffset = segmentMappings.front().GetOffset();
			uint32 runLength = 1;

			while(runLength < segmentLen && segmentMappings[runLength].GetOffset() == firstCodeOffset)
				++runLength;

			// if the initial run is at least 4 or covers the whole remainder,
			// encode it as a run
			if (runLength >= 4 || runLength >= segmentLen) {
				endCode.push_back(segmentMappings[runLength - 1].mCh);
				idDelta.push_back(firstCodeOffset);
				idRangeOffset.push_back(0);

				segmentMappings = segmentMappings.subspan(runLength);
				continue;
			}

			// determine how many elements to encode explicitly -- stop if we
			// hit a run of 8, or 4 at the end
			uint32 encodeLen = runLength;

			runLength = 0;

			while(encodeLen < segmentLen) {
				++encodeLen;

				if (segmentMappings[encodeLen].GetOffset() == segmentMappings[encodeLen-1].GetOffset()) {
					++runLength;

					if (runLength >= 8) {
						encodeLen -= runLength;
						runLength = 0;
						break;
					}
				} else
					runLength = 1;
			}

			if (runLength >= 4)
				encodeLen -= runLength;

			endCode.push_back((uint16)(firstCh + encodeLen - 1));
			idDelta.push_back(0);
			idRangeOffset.push_back((uint16)(glyphIdArray.size() + 1));

			for(uint32 i=0; i<encodeLen; ++i)
				glyphIdArray.push_back((uint16)segmentMappings[i].mGlyphIndex);

			segmentMappings = segmentMappings.subspan(encodeLen);
		}
	}

	// convert idRangeOffset[] from absolute to relative offsets
	uint32 relOffset = 1;
	for(auto it = idRangeOffset.begin(), itEnd = idRangeOffset.end();
		it != itEnd;)
	{
		--itEnd;

		if (*itEnd)
			*itEnd = *itEnd + relOffset;

		++relOffset;
	}

	// add sentinel
	startCode.push_back(0xFFFF);
	endCode.push_back(0xFFFF);
	idDelta.push_back(1);
	idRangeOffset.push_back(0);

	// encode and write header
	struct CmapHeader {
		vdbe<uint16> mVersion = 0;
		vdbe<uint16> mNumTables = 1;

		// encoding table [0]
		vdbe<uint16> mPlatformID = 3;	// Windows
		vdbe<uint16> mEncodingID = 1;	// Unicode
		vdbe<uint32> mSubtableOffset = 12;

		// subtable for encoding (3,1)
		vdbe<uint16> mFormat = 4;
		vdbe<uint16> mLength = 0;
		vdbe<uint16> mLanguage = 0;
		vdbe<uint16> mSegCountX2 = 0;
		vdbe<uint16> mSearchRange = 0;
		vdbe<uint16> mEntrySelector = 0;
		vdbe<uint16> mRangeShift = 0;
	} header;

	size_t numSegments = startCode.size();
	size_t numSegmentBits = 0;

	while(((size_t)2 << numSegmentBits) <= numSegments)
		++numSegmentBits;

	header.mSegCountX2 = (uint16)(numSegments * 2);
	header.mSearchRange = (uint16)(2 << numSegmentBits);
	header.mEntrySelector = (uint16)numSegmentBits;
	header.mRangeShift = (uint16)(numSegments * 2 - (2 << numSegmentBits));
	header.mLength = (uint16)(16 + numSegments*8 + 2*glyphIdArray.size());

	BeginTable("cmap");
	WriteRaw(header);
	WriteRaw(endCode.data(), endCode.size() * sizeof(endCode[0]));

	uint16 reservedPad = 0;
	WriteRaw(reservedPad);

	WriteRaw(startCode.data(), startCode.size() * sizeof(startCode[0]));
	WriteRaw(idDelta.data(), idDelta.size() * sizeof(idDelta[0]));
	WriteRaw(idRangeOffset.data(), idRangeOffset.size() * sizeof(idRangeOffset[0]));
	WriteRaw(glyphIdArray.data(), glyphIdArray.size() * sizeof(glyphIdArray[0]));
	EndTable();
}

void ATTrueTypeEncoder::WriteTableGlyfLoca() {
	const size_t numGlyphs = mGlyphs.size();
	vdblock<vdbe<uint16>> glyphOffsets(numGlyphs + 1);

	const auto basePosition = mTableBuffer.size();

	BeginTable("glyf");

	for(size_t i = 0; i < numGlyphs; ++i) {
		const TTFGlyphInfo& glyph = mGlyphs[i];
		
		glyphOffsets[i] = (uint16)((mTableBuffer.size() - basePosition) >> 1);

		// if there are no contours, write an empty glyph block
		if (!glyph.mNumContours)
			continue;

		// write header
		struct GlyphHeader {
			vdbe<sint16> mNumberOfContours;
			vdbe<sint16> mXMin;
			vdbe<sint16> mYMin;
			vdbe<sint16> mXMax;
			vdbe<sint16> mYMax;
		} header;

		static_assert(sizeof(header) == 10);

		header.mNumberOfContours = (sint16)glyph.mNumContours;

		// if the glyph is empty, encode a zero bounding box instead of a backwards
		// one
		if (glyph.mBBoxX1 > glyph.mBBoxX2) {
			header.mXMin = 0;
			header.mYMin = 0;
			header.mXMax = 0;
			header.mYMax = 0;
		} else {
			header.mXMin = (sint16)glyph.mBBoxX1;
			header.mYMin = (sint16)glyph.mBBoxY1;
			header.mXMax = (sint16)glyph.mBBoxX2;
			header.mYMax = (sint16)glyph.mBBoxY2;
		}

		WriteRaw(header);
		
		// check if non-empty simple or composite glyph
		if (glyph.mNumContours > 0) {
			// simple glyph
			const TTFPoint *points = &mGlyphPoints[glyph.mStartIndex];

			// write end contour table
			for(size_t j = 0; j < glyph.mCount; ++j) {
				if (points[j].mbEndContour) {
					vdbe<uint16> endPos = (uint16)j;

					WriteRaw(endPos);
				}
			}

			vdbe<uint16> numInstructions = 0;
			WriteRaw(numInstructions);

			// write flags
			static constexpr uint8 kFlagReserved = 0x80;
			static constexpr uint8 kFlagYSameOrPosShort = 0x20;
			static constexpr uint8 kFlagXSameOrPosShort = 0x10;
			static constexpr uint8 kFlagRepeat = 0x08;
			static constexpr uint8 kFlagYShortVector = 0x04;
			static constexpr uint8 kFlagXShortVector = 0x02;
			static constexpr uint8 kFlagOnCurvePoint = 0x01;

			uint8 lastFlags = kFlagReserved;
			uint8 lastRepeatCount = 0;
			sint32 lastX = 0;
			sint32 lastY = 0;

			for(size_t j = 0; j < glyph.mCount; ++j) {
				const TTFPoint& pt = points[j];
				sint32 dx = pt.mX - lastX;
				sint32 dy = pt.mY - lastY;
				uint8 flags = 0;

				if (pt.mbOnCurve)
					flags |= kFlagOnCurvePoint;

				if (dx == 0)
					flags |= kFlagXSameOrPosShort;
				else if (dx >= -255 && dx <= 255) {
					flags |= kFlagXShortVector;

					if (dx >= 0)
						flags |= kFlagXSameOrPosShort;
				}

				if (dy == 0)
					flags |= kFlagYSameOrPosShort;
				else if (dy >= -255 && dy <= 255) {
					flags |= kFlagYShortVector;

					if (dy >= 0)
						flags |= kFlagYSameOrPosShort;
				}

				if (lastFlags == flags && lastRepeatCount < 255) {
					++lastRepeatCount;

					if (lastRepeatCount == 1)
						mTableBuffer.push_back(flags);
					else {
						if (lastRepeatCount == 2)
							*(mTableBuffer.end() - 2) = flags | kFlagRepeat;

						mTableBuffer.back() = lastRepeatCount;
					}
				} else {
					mTableBuffer.push_back(flags);
					lastFlags = flags;
					lastRepeatCount = 0;
				}

				lastX = pt.mX;
				lastY = pt.mY;
			}

			// write X coordinates
			lastX = 0;
			for(size_t j = 0; j < glyph.mCount; ++j) {
				const TTFPoint& pt = points[j];
				sint32 dx = pt.mX - lastX;

				if (dx) {
					sint32 adx = abs(dx);

					if (adx < 256) {
						mTableBuffer.push_back((uint8)adx);
					} else {
						mTableBuffer.push_back((uint8)(dx >> 8));
						mTableBuffer.push_back((uint8)dx);
					}
				}

				lastX = pt.mX;
			}

			// write Y coordinates
			lastY = 0;
			for(size_t j = 0; j < glyph.mCount; ++j) {
				const TTFPoint& pt = points[j];
				sint32 dy = pt.mY - lastY;

				if (dy) {
					sint32 ady = abs(dy);

					if (ady < 256) {
						mTableBuffer.push_back((uint8)ady);
					} else {
						mTableBuffer.push_back((uint8)(dy >> 8));
						mTableBuffer.push_back((uint8)dy);
					}
				}

				lastY = pt.mY;
			}
		} else if (glyph.mNumContours < 0) {
			// composite glyph
			static constexpr uint16 kFlagArg12Words = 0x0001;
			static constexpr uint16 kFlagArgsXYValues = 0x0002;
			static constexpr uint16 kFlagMoreComponents = 0x0020;
			static constexpr uint16 kFlagUnscaledComponentOffset = 0x1000;

			struct ShortRef {
				vdbe<uint16> mFlags;
				vdbe<uint16> mGlyphIndex;
				vdbe<sint8> mXOffset;
				vdbe<sint8> mYOffset;
			};

			struct LongRef {
				vdbe<uint16> mFlags;
				vdbe<uint16> mGlyphIndex;
				vdbe<sint16> mXOffset;
				vdbe<sint16> mYOffset;
			};

			const TTFGlyphReference *glyphRefs = &mGlyphReferences[glyph.mStartIndex];

			for(size_t j = 0; j < glyph.mCount; ++j) {
				const TTFGlyphReference& glyphRef = glyphRefs[j];
				uint16 baseFlags = kFlagArgsXYValues | kFlagUnscaledComponentOffset;

				if (j < glyph.mCount - 1)
					baseFlags |= kFlagMoreComponents;

				if (glyphRef.mOffsetX == (sint8)glyphRef.mOffsetX &&
					glyphRef.mOffsetY == (sint8)glyphRef.mOffsetY)
				{
					ShortRef shortRef;
					shortRef.mFlags = baseFlags;
					shortRef.mGlyphIndex = (uint16)glyphRef.mSrcGlyph;
					shortRef.mXOffset = (sint8)glyphRef.mOffsetX;
					shortRef.mYOffset = (sint8)glyphRef.mOffsetY;

					WriteRaw(shortRef);
				} else {
					LongRef longRef;
					longRef.mFlags = baseFlags | kFlagArg12Words;
					longRef.mGlyphIndex = (uint16)glyphRef.mSrcGlyph;
					longRef.mXOffset = (sint16)glyphRef.mOffsetX;
					longRef.mYOffset = (sint16)glyphRef.mOffsetY;

					WriteRaw(longRef);
				}
			}
		}

		// dword alignment
		if (size_t tail = (4 - mTableBuffer.size()) & 3) {
			while(tail--)
				mTableBuffer.push_back(0);
		}
	}
	
	glyphOffsets.back() = (uint16)((mTableBuffer.size() - basePosition) >> 1);

	EndTable();

	BeginTable("loca");
	WriteRaw(glyphOffsets.data(), glyphOffsets.size() * sizeof(glyphOffsets[0]));
	EndTable();
}

void ATTrueTypeEncoder::WriteTableName() {
	struct NameHeader {
		vdbe<uint16> mVersion = 0;
		vdbe<uint16> mCount = 0;
		vdbe<uint16> mStorageOffset = 0;
	} header;

	header.mCount = (uint16)(mNames.size() * 2);
	header.mStorageOffset = (uint16)(mNames.size() * 24 + 6);

	BeginTable("name");

	WriteRaw(header);

	struct NameEntry {
		vdbe<uint16> mPlatformID = 0;
		vdbe<uint16> mEncodingID = 0;
		vdbe<uint16> mLanguageID = 0x0409;	// winansi
		vdbe<uint16> mNameID = 0;
		vdbe<uint16> mLength = 0;
		vdbe<uint16> mStringOffset = 0;
	} entry;

	std::sort(mNames.begin(), mNames.end(),
		[](const TTFNameEntry& a, const TTFNameEntry& b) {
			return a.mNameId < b.mNameId;
		}
	);

	// write generic entries
	for(int pass=0; pass<2; ++pass) {
		for(const TTFNameEntry& ne : mNames) {
			entry.mNameID = (uint16)ne.mNameId;
			entry.mLength = (uint16)(ne.mLength*2);
			entry.mStringOffset = (uint16)(ne.mOffset*2);

			WriteRaw(entry);
		}

		entry.mPlatformID = 3;
		entry.mEncodingID = 1;
	}

	WriteRaw(mNameChars.data(), mNameChars.size() * sizeof(mNameChars[0]));
	EndTable();
}

void ATTrueTypeEncoder::BeginTable(TTFTypeId id) {
	auto& te = mTableDirectory.emplace_back();

	te.mId = id.mId;
	te.mPos = (uint32)mTableBuffer.size();
}

void ATTrueTypeEncoder::EndTable() {
	auto& te = mTableDirectory.back();
	size_t n = mTableBuffer.size();

	te.mLength = (uint32)(n - te.mPos);

	if (n & 3)
		mTableBuffer.resize((n + 3) & ~3, 0);

	size_t cnt4 = ((size_t)te.mLength + 3) >> 2;
	const uint8 *p = mTableBuffer.data() + te.mPos;
	uint32 chk = 0;

	for(size_t i=0; i<cnt4; ++i) {
		chk += VDReadUnalignedBEU32(p);
		p += 4;
	}

	te.mChecksum = chk;
}

void ATTrueTypeEncoder::WriteRaw(const void *p, size_t n) {
	if (n) {
		mTableBuffer.resize(mTableBuffer.size() + n);

		memcpy(&*mTableBuffer.end() - n, p, n);
	}
}
