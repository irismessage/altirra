//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_DEVICEPRINTER_H
#define f_AT_ATCORE_DEVICEPRINTER_H

#include <vd2/system/refcount.h>
#include <vd2/system/unknown.h>

class vdfloat2;

// Interface for output sinks connecting to printer ports and receiving text
// output. This can either be the default output or a child device with a
// parallel port interface.
//
class IATPrinterOutput : public IVDRefUnknown {
public:
	static constexpr uint32 kTypeID = "IATPrinterOutput"_vdtypeid;

	// True if the printer output can accept and wants Unicode output, false
	// if it wants the raw output. Printers may still produce raw output
	// even if the output wants Unicode, but printers must not produce
	// Unicode output if the output doesn't want it.
	virtual bool WantUnicode() const = 0;
	virtual void WriteRaw(const uint8 *buf, size_t len) {}
	virtual void WriteUnicode(const wchar_t *buf, size_t len) {}
};

struct ATPrinterGraphicsSpec {
	// Page width edge-to-edge, in millimeters.
	float mPageWidthMM;

	// Default vertical border between top of the first page and the top of
	// the print head. This is typically the same as the left/right borders
	// and is used to nicely position the paper at start.
	float mPageVBorderMM;

	// Radius of a printed dot, in millimeters.
	float mDotRadiusMM;

	// Distance from center to center of adjacent dots printed together in
	// the same column.
	float mVerticalDotPitchMM;

	// True if bit 0 in the pin pattern is the topmost bit in the print head;
	// false if it is the bottom-most.
	bool mbBit0Top;

	// Number of pins in the print head.
	uint32 mNumPins;
};

// Interface for output sinks connecting to printer ports and receiving
// graphical output. This is currently only the default output.
//
class IATPrinterGraphicalOutput : public IVDRefUnknown {
public:
	static constexpr uint32 kTypeID = "IATPrinterGraphicalOutput"_vdtypeid;

	// Set callback after paper is cleared.
	virtual void SetOnClear(vdfunction<void()> fn) = 0;

	// Advance the paper upward by the specified number of millimeters. May be
	// negative for reverse feed.
	virtual void FeedPaper(float distanceMM) = 0;

	// Print dots in a column at the specified X position in millimeters from
	// the left edge of the page.
	virtual void Print(float x, uint32 dots) = 0;

	// Add a vector (line segment) from pt1 to pt2 with the specified dot size.
	// Color index sets the pen index.
	virtual void AddVector(const vdfloat2& pt1, const vdfloat2& pt2, uint32 colorIndex) = 0;
};

// Service that provides for creation of default printer outputs.
//
// NOTE: Most devices that support a printer output can use either the default
// printer output here or connect to a parallel port device. Devices should not
// create a default printer port until the parallel port is used, to prevent
// creating spurious printer outputs while the port is being set up.
//
class IATPrinterOutputManager : public IVDRefCount {
public:
	static constexpr auto kTypeID = "IATPrinterOutputManager"_vdtypeid;

	// Create a printer output for text output. The output is automatically
	// closed when the last reference is deleted.
	virtual vdrefptr<IATPrinterOutput> CreatePrinterOutput() = 0;

	// Create a printer output for graphical output. The output is automatically
	// closed when the last reference is deleted.
	virtual vdrefptr<IATPrinterGraphicalOutput> CreatePrinterGraphicalOutput(const ATPrinterGraphicsSpec& spec) = 0;

protected:
	~IATPrinterOutputManager() = default;
};

#endif
