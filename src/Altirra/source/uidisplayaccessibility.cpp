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
#include <vd2/system/constexpr.h>
#include "options.h"
#include "uidisplayaccessibility.h"

uint32 ATUIDisplayAccessibilityScreen::TextPointToOffset(const ATUIDisplayAccessibilityTextPoint& txpt) const {
	const uint32 n = (uint32)mText.size();

	if (txpt.y >= (int)mLines.size())
		return n;

	return std::min<uint32>(n, mLines[txpt.y].mTextOffset + txpt.x);
}

ATUIDisplayAccessibilityTextPoint ATUIDisplayAccessibilityScreen::TextOffsetToPoint(uint32 txoffset) const {
	const uint32 n = (uint32)mText.size();

	if (txoffset > n)
		txoffset = n;

	uint32 lineStart = 0;
	sint32 y = 0;
	for(uint32 i = 0; i < txoffset; ++i) {
		if (mText[i] == '\n') {
			++y;
			lineStart = i + 1;
		}
	}

	return ATUIDisplayAccessibilityTextPoint { .x = (sint32)(txoffset - lineStart), .y = y };
}

uint32 ATUIDisplayAccessibilityScreen::TextOffsetToFormatIndex(uint32 txoffset) const {
	const auto start = mFormatSpans.begin();
	const auto end = mFormatSpans.end() - 1;

	auto it = std::upper_bound(start, end, txoffset + 1, [](uint32 offset, const ATUIDisplayAccFormatSpan& span) { return offset < span.mOffset; });

	if (it != start)
		--it;

	return (uint32)(it - start);
}

ATUIDisplayAccessibilityTextPoint ATUIDisplayAccessibilityScreen::GetDocumentEnd() const {
	ATUIDisplayAccessibilityTextPoint end;

	end.x = mLines.back().mTextLength;
	end.y = (int)mLines.size() - 1;

	return end;
}

ATUIDisplayAccessibilityScreen::ATUIDisplayAccessibilityScreen() {
	Clear();
}

bool ATUIDisplayAccessibilityScreen::IsAtWordBoundary(const ATUIDisplayAccessibilityTextPoint& pt) const {
	// The Win32 text edit / Notepad convention, a word consists of zero or more
	// non-whitespace characters and the following whitespace characters up to
	// the end of a line. If a line starts with whitespace characters, that
	// whitespace is grouped with end of the previous line.
	//
	// In our case, words are split:
	// - Between any two non-whitespace classes
	// - Between a whitespace class followed by a non-whitespace class
	// - Never at the end of a line, unless it's the end of the document
	// - At the beginning of a line, if the first character is not whitespace
	//   character or the line is empty
	// - NOT between a non-whitespace class followed by a whitespace class

	if (pt.y < 0 || pt.y >= (int)mLines.size())
		return true;

	const auto& line = mLines[pt.y];
	if (pt.x >= (int)line.mTextLength)
		return pt.y == (int)mLines.size() - 1;

	if (pt.x <= 0)
		return line.mTextLength == 0 || GetCharWordClass(mText[line.mTextOffset]) != 0;

	// we are now guaranteed to have a char before and after
	const auto charClassBefore = GetCharWordClass(mText[line.mTextOffset + pt.x - 1]);
	const auto charClassAfter = GetCharWordClass(mText[line.mTextOffset + pt.x]);

	// same class is not a break
	if (charClassBefore == charClassAfter)
		return false;

	// whitespace -> non-whitespace is a break
	if (!charClassBefore)
		return true;

	// non-whitespace -> whitespace is not a break, as whitespace after is included
	if (!charClassAfter)
		return false;

	// non-whitespace -> different non-whitespace is a break
	return true;
}

ATUIDisplayAccessibilityTextPoint ATUIDisplayAccessibilityScreen::MoveToPrevWordBoundary(const ATUIDisplayAccessibilityTextPoint& pt) const {
	ATUIDisplayAccessibilityTextPoint pt2(pt);

	if (pt2.y <= 0)
		return ATUIDisplayAccessibilityTextPoint{};

	if (pt2.y >= (int)mLines.size())
		return GetDocumentEnd();

	do {
		if (pt2.x > 0) {
			--pt2.x;
		} else {
			if (pt2.y == 0)
				return ATUIDisplayAccessibilityTextPoint{};

			--pt2.y;
			pt2.x = (int)mLines[pt2.y].mTextLength;
		}
	} while(!IsAtWordBoundary(pt2));

	return pt2;
}

ATUIDisplayAccessibilityTextPoint ATUIDisplayAccessibilityScreen::MoveToNextWordBoundary(const ATUIDisplayAccessibilityTextPoint& pt) const {
	ATUIDisplayAccessibilityTextPoint pt2(pt);

	if (pt2.y < 0)
		return ATUIDisplayAccessibilityTextPoint{};

	if (pt2.y >= (int)mLines.size())
		return GetDocumentEnd();

	do {
		if (pt2.x < (int)mLines[pt2.y].mTextLength) {
			++pt2.x;
		} else {
			pt2.x = 0;
			++pt2.y;

			if (pt2.y >= (int)mLines.size())
				return GetDocumentEnd();
		}
	} while(!IsAtWordBoundary(pt2));

	return pt2;
}

void ATUIDisplayAccessibilityScreen::Clear() {
	mText.clear();
	mLines.clear();
	mFormatSpans.clear();

	auto& line = mLines.emplace_back();
	line.mStartBeamX = 48;
	line.mStartBeamY = 32;

	auto& format = mFormatSpans.emplace_back();
	format.mOffset = 0;
	format.mFgColor = 0xFFFFFF;
	format.mBgColor = 0;
}

uint8 ATUIDisplayAccessibilityScreen::GetCharWordClass(uint8 c)
{
	static constexpr auto kWordClassTable = VDCxArray<uint8, 128>::from_index(
		[](uint8 ch) -> uint8 {
			// class 0: whitespace
			if (ch == 0x20)
				return 0;

			// class 1: letter
			if ((ch >= 0x41 && ch <= 0x5A) || (ch >= 0x61 && ch <= 0x7A))
				return 1;

			// class 2: number
			if (ch >= 0x30 && ch <= 0x39)
				return 2;

			// class 3: anything else
			return 3;
		}
	);

	return kWordClassTable[c & 0x7F];
}
