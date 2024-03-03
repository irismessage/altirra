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
