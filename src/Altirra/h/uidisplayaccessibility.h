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

#ifndef f_AT_UIDISPLAYACCESSIBILITY_H
#define f_AT_UIDISPLAYACCESSIBILITY_H

#include <vd2/system/vdstl.h>
#include <vd2/system/vectors.h>
#include <vd2/system/refcount.h>

struct ATUIDisplayAccessibilityTextPoint {
	sint32 x = 0;
	sint32 y = 0;

	bool operator==(const ATUIDisplayAccessibilityTextPoint&) const = default;
	bool operator!=(const ATUIDisplayAccessibilityTextPoint&) const = default;

	std::strong_ordering operator<=>(const ATUIDisplayAccessibilityTextPoint& other) const {
		if (y != other.y)
			return y <=> other.y;

		return x <=> other.x;
	}
};

struct ATUIDisplayAccessibilityLineInfo {
	uint32 mStartBeamX = 0;
	uint32 mStartBeamY = 0;
	uint32 mBeamToCellShift = 0;
	uint32 mHeight = 0;
	
	// Starting character offset of line text in text buffer.
	uint32 mTextOffset = 0;

	// Number of characters in line, not counting EOL at end if present.
	uint32 mTextLength = 0;

	bool operator==(const ATUIDisplayAccessibilityLineInfo&) const = default;
	bool operator!=(const ATUIDisplayAccessibilityLineInfo&) const = default;
};

struct ATUIDisplayAccFormatSpan {
	// Starting character offset in text buffer.
	uint32 mOffset;

	uint32 mFgColor;
	uint32 mBgColor;

	bool operator==(const ATUIDisplayAccFormatSpan&) const = default;
	bool operator!=(const ATUIDisplayAccFormatSpan&) const = default;
};

struct ATUIDisplayAccessibilityScreen : public vdrefcount {
	// Text buffer, containing all lines in order. Lines are separated by an LF
	// that is not included in either adjacent line. A screen with N line infos
	// always has N-1 LFs.
	VDStringW mText;

	// All lines in order. There is always at least one line.
	vdfastvector<ATUIDisplayAccessibilityLineInfo> mLines;
	vdfastvector<ATUIDisplayAccFormatSpan> mFormatSpans;

	ATUIDisplayAccessibilityScreen();

	bool operator==(const ATUIDisplayAccessibilityScreen&) const = default;
	bool operator!=(const ATUIDisplayAccessibilityScreen&) const = default;

	uint32 TextPointToOffset(const ATUIDisplayAccessibilityTextPoint& txpt) const;
	ATUIDisplayAccessibilityTextPoint TextOffsetToPoint(uint32 txoffset) const;

	uint32 TextOffsetToFormatIndex(uint32 txoffset) const;

	ATUIDisplayAccessibilityTextPoint GetDocumentEnd() const;

	bool IsAtWordBoundary(const ATUIDisplayAccessibilityTextPoint& pt) const;
	ATUIDisplayAccessibilityTextPoint MoveToPrevWordBoundary(const ATUIDisplayAccessibilityTextPoint& pt) const;
	ATUIDisplayAccessibilityTextPoint MoveToNextWordBoundary(const ATUIDisplayAccessibilityTextPoint& pt) const;

	void Clear();

private:
	static uint8 GetCharWordClass(uint8 c);
};

#endif
