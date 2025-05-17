//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009-2024 Avery Lee
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
#include <at/atcore/propertyset.h>
#include "printer1020.h"
#include "printer1020font.h"

void ATCreateDevicePrinter1020(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevicePrinter1020> p(new ATDevicePrinter1020);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefPrinter1020 = { "1020", nullptr, L"1020 Color Printer", ATCreateDevicePrinter1020 };

ATDevicePrinter1020::ATDevicePrinter1020()
	: ATDevicePrinterBase(true, false, false, true)
{
	SetSaveStateAgnostic();
}

ATDevicePrinter1020::~ATDevicePrinter1020() {
}

void ATDevicePrinter1020::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPrinter1020;
}

void ATDevicePrinter1020::ColdReset() {
	ATDevicePrinterBase::ColdReset();

	ResetState();
}

void ATDevicePrinter1020::OnCreatedGraphicalOutput() {
	mpPrinterGraphicalOutput->SetOnClear(
		[this] {
			mBaseX = 0;
			mBaseY = 0;
			mX = 0;
			mY = 0;
		}
	);
}

ATPrinterGraphicsSpec ATDevicePrinter1020::GetGraphicsSpec() const {
	ATPrinterGraphicsSpec spec {};
	spec.mPageWidthMM = 114.5f;				// 4.5" wide paper (based on CGP-115 service manual)
	spec.mPageVBorderMM = 8.0f;				// vertical border
	spec.mDotRadiusMM = 0.090f;				// guess for dot radius
	spec.mVerticalDotPitchMM = 0.403175f;	// 0.0159" vertical pitch (guess)
	spec.mbBit0Top = true;
	spec.mNumPins = 9;

	return spec;
}

bool ATDevicePrinter1020::IsSupportedDeviceId(uint8 id) const {
	// The source for P4: being supported is the Atari 8-bit FAQ.
	return id == 0x40 || id == 0x43;
}

bool ATDevicePrinter1020::IsSupportedOrientation(uint8 aux1) const {
	return true;
}

uint8 ATDevicePrinter1020::GetWidthForOrientation(uint8 aux1) const {
	return 40;
}

void ATDevicePrinter1020::GetStatusFrameInternal(uint8 frame[4]) {
	ResetState();
}

void ATDevicePrinter1020::HandleFrameInternal(IATPrinterOutput& output, uint8 orientation, uint8 *buf, uint32 len, bool graphics) {
	// Commands:
	//	ESC CTRL G		Switch to graphics mode
	//	ESC CTRL P		20 character text mode
	//	ESC CTRL S		80 character text mode
	//	ESC CTRL N		40 character text mode
	//	ESC CTRL W		Enable international characters
	//	ESC CTRL X		Disable international characters
	//
	// Graphics commands:
	//	S<0-63>			Set text scale
	//	H				Return to home position
	//	C<0-3>			Set color
	//	L<0-15>			Set line style
	//	Dx,y[;x,y...]	Draw line to absolute points
	//	I				Initialize (reset home origin)
	//	Jx,y			Draw line to relative point
	//	Mx,y			Move to absolute point
	//	Rx,y			Move to relative point
	//	Xa,d,n			Draw axis a (0=Y, 2=X), d distance between marks, n marks
	//	Q<0-3>			Set text orientation
	//	P<text>			Print text from graphics mode
	//	A				Return to text mode

	while(len--) {
		uint8 ch = *buf++;

		switch(mState) {
			case State::TextMode:
				if (ch == 0x1B) {
					mState = State::TextEscape;
					break;
				}

				PrintChar(ch, false);
				break;

			case State::TextEscape:
				mState = State::TextMode;

				switch(ch) {
					case 0x07:	// ESC CTRL G: graphics mode
						mState = State::GraphicsMode;
						break;
					case 0x10:	// ESC CTRL P: 20 char mode
						mCharSize = 4;
						break;
					case 0x13:	// ESC CTRL S: 80 char mode
						mCharSize = 1;
						break;
					case 0x0E:	// ESC CTRL N: 40 char mode
						mCharSize = 2;
						break;
					case 0x17:	// ESC CTRL W: enable international
						mbIntCharsEnabled = true;
						break;
					case 0x18:	// ESC CTRL X: disable international
						mbIntCharsEnabled = false;
						break;

					case 0x1B:	// ESC ESC: Stay in ESC mode (needed by BIOPLOT)
						mState = State::TextEscape;
						break;

					default:
						break;
				}
				break;

			case State::GraphicsMode:
graphics_command:
				mGraphicsCommand = ch;

				switch(ch) {
					case 0x1B:
						// return to text mode and process escape (undocumented; relied upon
						// by 1020 diagnostic)
						mState = State::TextEscape;
						mX = 0;
						mBaseX = 0;
						mBaseY = mY;
						mLineStyle = 0;
						break;

					case 'A':
						// return to text mode
						mState = State::TextMode;
						mX = 0;
						mBaseX = 0;
						mLineStyle = 0;
						break;

					case 'H':
						// return to home position
						mX = mBaseX;
						mY = mBaseY;
						break;

					case 'I':	// Initialize (set home origin)
						mBaseX = mX;
						mBaseY = mY;
						break;

					case 'S':	// S<0-63>	Set text scale
					case 'L':	// L<0-15>	Set line style
					case 'C':	// C0-3		Set color
					case 'Q':	// Q<0-3>	Set text orientation
						mArgsLeft = 1;
						mArgCount = 1;
						mState = State::ArgStart;
						break;

					case 'D':	// Dx,y[;x,y...]	Draw line to absolute points
					case 'J':	// Jx,y		Draw line to relative points
					case 'R':	// Rx,y		Move to relative point
					case 'M':	// Mx,y		Move to absolute point
						mArgsLeft = 2;
						mArgCount = 2;
						mState = State::ArgStart;
						break;

					case 'X':	// Xa,d,n	Draw axis a (0=Y, 2=X), d distance between marks, n marks
						mArgsLeft = 3;
						mArgCount = 3;
						mState = State::ArgStart;
						break;

					case 'P':	// P<text>	Print text while in graphics mode
						mState = State::GraphicsTextCommand;
						break;
				}
				break;

			case State::GraphicsTextCommand:
				if (ch == 0x9B)
					mState = State::GraphicsMode;
				else
					PrintChar(ch, true);

				break;

			case State::ArgStart:
				mArg3 = mArg2;
				mArg2 = mArg1;
				mArg1 = 0;
				mArgSign = +1;

				mState = State::ArgStart2;
				[[fallthrough]];
			case State::ArgStart2:
				// The precise Atari 1020 logic is not known, but interestingly the Commodore VIC-1520
				// apparently has the original logic rewritten into 6502 assembly. The logic is as follows:
				//
				// At the start of a number:
				// - A minus sign sets the negative flag.
				// - A dot skips the digit loop.
				// - Any other non-digits are skipped. This notably includes space.
				//
				// Then, while digits are found:
				// - Accumulate decimal digits. There does not seem to be any overflow protection.
				//
				// Afterward:
				// - 'E' resets the number to 0. This handles small numbers in scientific notation.
				//   There is no attempt to parse the exponent, which is skipped like any other unrecognized
				//   characters in this phase.
				// - Space or comma ends the number. (; and * appear to be Atari 1020 specific as they
				//   are not implemented in the VIC-1520, nor mentioned in the CGP-115 docs.)
				//
				// The reason for most of this is that the 1020 only uses integers, but needs to support BASIC
				// printing out floating point numbers, as many of the examples in the user manual do. The
				// tricky part is that this includes small numbers that go to scientific notation, e.g.
				// -4.07293E-05. These need to be flushed to zero.
				//
				// The 1020 manual does not specify what happens if arguments are omitted or empty, but
				// the CGP-115 manual mentions in a few places that arguments can be omitted, i.e. C instead
				// of C0 to go to black.

				if (ch == '-') {
					mArgSign = -1;
					break;
				}

				if (ch == '.') {
					mState = State::ArgEnd;
					break;
				}
				
				mState = State::ArgDigit;
				[[fallthrough]];
			case State::ArgDigit:
				if (ch >= 0x30 && ch < 0x3A) {
					// The 1020 diagnostic test does something goofy: it uses 1000 to mean 0. We need
					// to emulate this or the right half of the dash chart goes wild.
					mArg1 = (mArg1 * 10 + (int)(ch - 0x30) * mArgSign) % 1000;
					break;
				}

				mState = State::ArgEnd;
				[[fallthrough]];
			case State::ArgEnd:
				if (ch == 0x9B || ch == '*' || ch == ';') {
					// EOL, asterisk, or semicolon ends all arguments for the current command
					// iteration.
					while(--mArgsLeft) {
						mArg3 = mArg2;
						mArg2 = mArg1;
						mArg1 = 0;
					}
				} else if (ch == ',' || ch == ' ') {
					// Comma or space ends the current argument.
					if (--mArgsLeft) {
						mState = State::ArgStart;
						break;
					}
				} else if (ch == 'E') {
					// Scientific notation -- assume that the number is smaller than +/-1 and flush
					// to zero, then continue discarding the rest of the number. Note that only an
					// uppercase E is accepted.
					mArg1 = 0;
					break;
				} else {
					// Any other character -- skip.
					//
					// This is crucial for BASIC being able to dump fp numbers to the plotter
					// and have it work sensibly, as many test programs do. Decimals are a no
					// brainer -- but more subtly, scientific notation needs to be handled
					// too.
					break;
				}

				switch(mGraphicsCommand) {
					case 'S':	// S<0-63>	Set text scale
						mCharSize = (mArg1 & 63) + 1;
						break;

					case 'L':	// L<0-15>	Set line style
						mLineStyle = mArg1 & 15;
						break;

					case 'C':	// C0-3		Set color
						mPenIndex = mArg1 & 3;
						break;

					case 'Q':	// Q<0-3>	Set text orientation
						mCharRotation = mArg1 & 3;
						break;

					case 'D':	// Dx,y[;x,y...]	Draw line to absolute points
						DrawToAbsolute(mBaseX + mArg2, mBaseY + mArg1);
						break;

					case 'J':	// Jx,y		Draw line to relative points
						DrawToAbsolute(mX + mArg2, mY + mArg1);
						break;

					case 'R':	// Rx,y		Move to relative point
						mX = std::clamp<sint32>(mX + mArg2, 0, 480);
						mY += mArg1;
						break;

					case 'M':	// Mx,y		Move to absolute point
						mX = std::clamp<sint32>(mBaseX + mArg2, 0, 480);
						mY = mBaseY + mArg1;
						break;

					case 'X':	// Xa,b,c	Draw axis with orientation a, tick spacing b, tick count c
						// The 1020 diagnostic test rather abuses the heck out of this command:
						//
						// X0,20,256	(expected to draw one vertical tick at +20 step)
						// X0,1000,-1	(expected to be ignored)
						// X1,-1000,0	(expected to be ignored)
						// X1,-20,12	(expected to draw 12 horizontal ticks at -20 step)
						// X2,20,-256	(expected to draw 1 horizontal tick at +20 step)
						//
						// The way we make sense of this mess:
						// - Digit parsing only takes last three digits (done above)
						// - Distance of 0 causes command to be ignored (CGP-115 says 1-999 valid)
						// - Only 8 bits of count are used (CGP-115 says 1-255 valid)
						// - Count of 0 means 1

						if (mArg2 != 0) {
							// Compute normal axis direction.
							//
							// The 1020 diagnostic uses both 1 and 2 for horizontal axis, so we assume
							// here that the condition should be non-zero.
							const sint32 dx = mArg3 ? 1 : 0;
							const sint32 dy = 1 - dx;

							// scale by distance
							const sint32 stepx = dx * mArg2;
							const sint32 stepy = dy * mArg2;

							// draw ticks
							//
							sint32 n = abs(mArg1) & 255;
							if (!n)
								++n;

							sint32 x = mX;
							sint32 y = mY;

							const sint32 tickx =  2 * dy;
							const sint32 ticky = -2 * dx;
							for(sint32 i = 0; i < n; ++i) {
								// draw a line segment
								if (!DrawToAbsolute(x + stepx, y + stepy))
									break;

								x += stepx;
								y += stepy;

								// draw a tick
								if (!MoveToAbsolute(x + tickx, y + ticky))
									break;

								if (!DrawToAbsolute(x - tickx, y - ticky))
									break;

								if (!MoveToAbsolute(x, y))
									break;
							}
						}
						break;

					default:
						break;
				}

				if (ch == ';') {
					mArgsLeft = mArgCount;
					mState = State::ArgStart;
					break;
				}

				mState = State::GraphicsMode;
				goto graphics_command;
		}

		if (ch == 0x9B)
			break;
	}
}

void ATDevicePrinter1020::ResetState() {
	mState = State::TextMode;
	mX = 0;
	mBaseX = 0;
	mBaseY = mY;
	mPenIndex = 0;
	mLineStyle = 0;
	mCharSize = 2;	// 40 col
	mbIntCharsEnabled = false;
}

void ATDevicePrinter1020::PrintChar(uint8 ch, bool graphical) {
	// if international chars are disabled, discard 00-1F and blank 60/7B/7D-7F.
	if (!mbIntCharsEnabled && ch != 0x9B) {
		uint8 ch2 = ch & 0x7F;
		if (ch2 < 0x20)
			return;

		if (ch2 == 0x60 || ch2 == 0x7B || ch2 >= 0x7D)
			ch = 0x20;
	}

	// compute the advance width
	const sint32 size = mCharSize;
	const sint32 advanceWidth = 6;

	// compute transformation matrix
	const sint8 kRotTab[5] { 1, 0, -1, 0, 1 };
	sint32 hx = size*kRotTab[mCharRotation];
	sint32 hy = size*kRotTab[mCharRotation + 1];
	sint32 vx = -hy;
	sint32 vy = hx;

	// Check for EOL, or if there isn't enough width, push EOL. We only do this
	// in text mode because the 1020 diagnostic test draws a gigantic size 64
	// character mid-line such that the right side spacing would extend beyond
	// the right edge, so the assumption is that the P command in graphics mode
	// does not line wrap.
	if (!graphical && (ch == 0x9B || mX + advanceWidth * size > 480)) {
		MoveToAbsolute(mBaseX - vx * 12, mBaseY - vy * 12);
		mBaseX = mX;
		mBaseY = mY;

		if (ch == 0x9B)
			return;
	}

	// save off baseline
	sint32 bx = mX;
	sint32 by = mY;

	// compute glyph origin at baseline
	sint32 x = bx;
	sint32 y = by;

	// look up character data
	const uint8 *dat = &g_ATPrinterFont1020.mpFontData[g_ATPrinterFont1020.mCharOffsets[ch & 0x7F]];

	if (!(dat[0] & ATPrinterFont1020::kEndBit)) {
		for(;;) {
			const uint8 code = *dat++;
			
			// unpack glyph space point
			sint32 h = (code >> 4) & 7;
			sint32 v = code & 7;

			// transform to absolute space
			sint32 x2 = x + h * hx + v * vx;
			sint32 y2 = y + h * hy + v * vy;

			if (code & ATPrinterFont1020::kMoveBit) {
				if (!MoveToAbsolute(x2, y2))
					break;
			} else {
				if (!DrawToAbsolute(x2, y2))
					break;
			}

			if (code & ATPrinterFont1020::kEndBit)
				break;
		}
	}

	// move to next position
	MoveToAbsolute(bx + advanceWidth * hx, by + advanceWidth * hy);
}

bool ATDevicePrinter1020::ClipTo(sint32& x, sint32& y) {
	// Most devices don't document their out of bounds behavior, but the VIC-1520
	// does: it stops when it would step out of bounds.
	if (x < 0) {
		if (mX < 0) {
			VDFAIL("Left clipping failed, already out of bounds");
			mX = 0;
		} else {
			y = mY + VDRoundToInt32((float)(y - mY) * (float)mX / (float)(mX - x));
		}

		x = 0;
		return false;
	} else if (x > 480) {
		if (mX > 480) {
			VDFAIL("Right clipping failed, already out of bounds");
			mX = 480;
		} else {
			y = mY + VDRoundToInt32((float)(y - mY) * (float)(mX - 480) / (float)(mX - x));
		}

		x = 480;
		return false;
	}

	return true;
}

bool ATDevicePrinter1020::MoveToAbsolute(sint32 x, sint32 y) {
	bool ok = ClipTo(x, y);
	mX = x;
	mY = y;

	return ok;
}

bool ATDevicePrinter1020::DrawToAbsolute(sint32 x, sint32 y) {
	bool ok = ClipTo(x, y);

	if (mpPrinterGraphicalOutput) {
		// The VIC-1520's dashed line algorithm, which we assume is the same as the Atari 1020's,
		// is as follows:
		//
		// - The line dashing is counted in steps. Since the plotter uses a Bresenham, this
		//   means that the line dashes are counted along the major axis and not along the line
		//   length.
		//
		// - If dashing is off (line style 0), then the pen is always down.
		//
		// - If dashing is on, then every L steps the pen is toggled whenever the counter is 0,
		//   and the counter is reset. Then, the counter is decremented.
		//
		// - Pen up/down occurs before a step.
		//
		// The various plotters seem to differ in line offsets across lines. The VIC-1520 manual
		// shows line patterns consistently starting on, while the 1020 shows them consistently
		// starting off. The CGP-115 appears to carry over the remainder to the next line.

		vdfloat2 pt1 = ConvertPointToMM(mX, mY);
		vdfloat2 pt2 = ConvertPointToMM(x, y);

		if (mLineStyle) {
			sint32 stepsLeft = std::max<sint32>(abs(x - mX), abs(y - mY)) + 1;

			if (stepsLeft) {
				const vdfloat2 step = (pt2 - pt1) / (float)stepsLeft;
				const int dashLen = mLineStyle;

				while(stepsLeft > 0) {
					stepsLeft -= dashLen;

					if (stepsLeft <= 0)
						break;

					int drawSteps = std::min<sint32>(stepsLeft, dashLen);

					pt1 += step * dashLen;
					pt2 = pt1 + step * drawSteps;

					mpPrinterGraphicalOutput->AddVector(pt1, pt2, mPenIndex);

					pt1 = pt2;
					stepsLeft -= dashLen;
				}
			}
		} else {
			mpPrinterGraphicalOutput->AddVector(pt1, pt2, mPenIndex);
		}
	}

	mX = x;
	mY = y;

	return ok;
}

vdfloat2 ATDevicePrinter1020::ConvertPointToMM(sint32 x, sint32 y) const {
	return vdfloat2 { (float)x * kUnitsToMM + 10.0f, (float)y * -kUnitsToMM + 10.0f };
}

vdfloat2 ATDevicePrinter1020::ConvertVectorToMM(sint32 x, sint32 y) const {
	return vdfloat2 { (float)x * kUnitsToMM, (float)y * -kUnitsToMM };
}
