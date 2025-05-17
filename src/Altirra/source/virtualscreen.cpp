//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/cio.h>
#include "cpu.h"
#include "cpumemory.h"
#include "kerneldb.h"
#include "virtualscreen.h"

class ATVirtualScreenHandler final : public IATVirtualScreenHandler {
public:
	ATVirtualScreenHandler(ATCPUEmulatorMemory& mem);
	~ATVirtualScreenHandler();

	void GetScreen(uint32& width, uint32& height, const uint8 *&screen) const;
	bool GetCursorInfo(uint32& x, uint32& y) const;

	void SetReadyCallback(const vdfunction<void()>& fn) override;
	bool IsReadyForInput() const override;
	
	void SetResizeCallback(const vdfunction<void()>& fn) override;
	void SetPassThroughChangedCallback(const vdfunction<void()>& fn) override;

	void ToggleSuspend() override;

	vdsize32 GetTerminalSize() const override;
	void Resize(uint32 w, uint32 h) override;
	void ReadScreenAsInput() override;
	void PushLine(const char *line) override;

	bool IsPassThroughEnabled() const { return mbPassThrough; }
	bool IsRawInputActive() const { return !mbWaitingForInput || !mbLineInputEnabled; }
	void SetShiftControlLockState(bool shift, bool ctrl);
	bool GetShiftLockState() const;
	bool GetControlLockState() const;

	bool CheckForBell() {
		bool pending = mbBellPending;
		mbBellPending = false;
		return pending;
	}
	
	int ReadRawText(uint8 *dst, int x, int y, int n) const override;
	bool CheckForCursorChange() override;
	void CheckForDisplayListReplaced() override;

	void ColdReset() override;
	void WarmReset() override;
	void Enable() override;
	void Disable() override;

	void SetGetCharAddresses(uint16 editor, uint16 screen) override;
	uint8 OnEditorCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset) override;
	uint8 OnScreenCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset) override;

protected:
	void SetWaitingForInput(bool waiting);
	uint8 HandleInput();
	void PutChar(uint8 c, bool updateCursor);
	void PutRawChar(uint8 c);
	void ClearScreen();
	void Scroll();
	void ClearLines(uint32 y, uint32 n);
	void MoveLines(uint32 dstY, uint32 srcY, uint32 height);
	void ReadParams(ATCPUEmulatorMemory& mem);
	void WriteParams(ATCPUEmulatorMemory& mem);
	uint32 ComputeLogicalColumn() const;
	bool IsLogicalLineStart(uint8 y) const;
	void ClearLogicalLineStart(uint8 y);
	void SetLogicalLineStart(uint8 y);
	void SetLogicalLineStart(uint8 y, bool v);

	uint8 GetLogicalLineBegin(uint8 y) const;
	uint8 GetLogicalLineRemainingHeight(uint8 y) const;
	bool IsPhysicalLineEmpty(uint8 y) const;

	void CheckGetLineChanged();
	void AdjustMargin();

	void MoveCursor(uint32 x, uint32 y);
	void ShowCursor(bool visible);
	void NotifyCursorChanged();

	void SetScreenModeValid(bool valid);
	void SetDisplayValid(bool valid);
	void UpdatePassThrough();

	uint16	mEditorGetCharAddress = 0;
	uint16	mScreenGetCharAddress = 0;
	bool	mbWaitingForInput = false;

	uint8	mShiftCtrlLockState = 0x40;
	bool	mbWriteShiftCtrlLockState = false;
	bool	mbBellPending = false;
	bool	mbResetMarginsPending = false;
	bool	mbForcedInputMode = false;
	bool	mbToggleSuspend = false;
	bool	mbLineInputEnabled = false;
	bool	mbCursorVisible = false;
	bool	mbCursorChanged = false;
	bool	mbMarginAdjusted = false;
	bool	mbIgnoreEditorGetForMarginAdjust = false;

	bool	mbDisplayValid = false;
	bool	mbScreenModeValid = false;
	bool	mbPassThrough = false;

	uint32	mGetLineX = 0;
	uint32	mGetLineY = 0;
	uint32	mWidth = 40;
	uint32	mHeight = 24;
	uint32	mX = 0;
	uint32	mY = 0;
	uint32	mCursorX = 0;
	uint32	mCursorY = 0;
	uint32	mLeftMargin = 2;
	uint32	mRightMargin = 39;
	uint32	mInputIndex = 0;

	vdfastvector<uint8> mScreen;
	vdfastvector<uint8> mActiveInputLine;

	vdfunction<void()> mpReadyHandler;
	vdfunction<void()> mpResizeHandler;
	vdfunction<void()> mpPassThroughChangedHandler;

	ATCPUEmulatorMemory& mCPUMemory;

	uint8 mTabMap[32] {};
	uint8 mLogicalLineMap[32] {};
};

ATVirtualScreenHandler::ATVirtualScreenHandler(ATCPUEmulatorMemory& mem)
	: mCPUMemory(mem)
{
	mScreen.resize(mWidth * mHeight, 0x20);
	std::fill(std::begin(mLogicalLineMap), std::end(mLogicalLineMap), 0xFF);
}

ATVirtualScreenHandler::~ATVirtualScreenHandler() {
}

void ATVirtualScreenHandler::GetScreen(uint32& width, uint32& height, const uint8 *&screen) const {
	width = mWidth;
	height = mHeight;
	screen = mScreen.data();
}

bool ATVirtualScreenHandler::GetCursorInfo(uint32& x, uint32& y) const {
	x = mCursorX;
	y = mCursorY;
	return mbCursorVisible;
}

void ATVirtualScreenHandler::SetReadyCallback(const vdfunction<void()>& fn) {
	mpReadyHandler = fn;
}

bool ATVirtualScreenHandler::IsReadyForInput() const {
	return mActiveInputLine.empty();
}

void ATVirtualScreenHandler::SetResizeCallback(const vdfunction<void()>& fn) {
	mpResizeHandler = fn;
}

void ATVirtualScreenHandler::SetPassThroughChangedCallback(const vdfunction<void()>& fn) {
	mpPassThroughChangedHandler = fn;
}

void ATVirtualScreenHandler::ToggleSuspend() {
	mbToggleSuspend = !mbToggleSuspend;
}

vdsize32 ATVirtualScreenHandler::GetTerminalSize() const {
	return vdsize32(mWidth, mHeight);
}

void ATVirtualScreenHandler::Resize(uint32 w, uint32 h) {
	// width/size are clamped to 255 to make interop easier, and we don't allow
	// resizing below the OS size of 40x24.
	w = std::clamp<uint32>(w, 40, 255);
	h = std::clamp<uint32>(h, 24, 255);

	if (mWidth == w && mHeight == h)
		return;

	vdfastvector<uint8> newScreen(w * h, 0x20);

	uint32 commonW = std::min<uint32>(w, mWidth);
	uint32 commonH = std::min<uint32>(h, mHeight);

	for(uint32 y=0; y<commonH; ++y)
		memcpy(&newScreen[y * w], &mScreen[y * mWidth], commonW);
	
	mScreen.swap(newScreen);
	mWidth = w;
	mHeight = h;

	if (mX >= w)
		mX = w - 1;

	if (mY >= h)
		mY = h - 1;

	mbResetMarginsPending = true;

	if (mpResizeHandler)
		mpResizeHandler();
}

void ATVirtualScreenHandler::ReadScreenAsInput() {
	// if for some reason the get line position is no longer on screen,
	// fix it
	if (mGetLineY >= mHeight)
		CheckGetLineChanged();
	else
		mGetLineX = std::clamp<uint32>(mGetLineX, mLeftMargin, mRightMargin);

	// scan the logical line into the input buffer
	mActiveInputLine.clear();

	uint32 x = mGetLineX;
	uint32 y = GetLogicalLineBegin(mY);
	uint32 spaces = 0;

	for(;;) {
		const uint8 *line = &mScreen[y * mWidth];

		for(uint32 x2 = x; x2 <= mRightMargin; ++x2) {
			uint8 c = line[x2];

			if (c == 0x20) {
				++spaces;
			} else {
				while(spaces) {
					--spaces;
					mActiveInputLine.push_back(0x20);
				}

				mActiveInputLine.push_back(c);
			}
		}

		x = mLeftMargin;
		++y;
		if (y >= mHeight) {
			Scroll();
			break;
		}

		if (IsLogicalLineStart(y)) {
			mY = y;
			break;
		}
	}

	mX = mLeftMargin;
	MoveCursor(mX, mY);

	mActiveInputLine.push_back(0x9B);
}

void ATVirtualScreenHandler::PushLine(const char *line) {
	size_t len = strlen(line);
	mActiveInputLine.resize(len + 1);

	for(size_t i=0; i<len; ++i)
		mActiveInputLine[i] = (uint8)line[i];

	mActiveInputLine.back() = 0x9B;

	ReadParams(mCPUMemory);

	for(size_t i=0; i<len; ++i)
		PutRawChar(mActiveInputLine[i]);

	PutChar(0x9B, true);

	WriteParams(mCPUMemory);
}

void ATVirtualScreenHandler::SetShiftControlLockState(bool shift, bool ctrl) {
	mShiftCtrlLockState = (ctrl ? 0x80 : 0x00) + (shift ? 0x40 : 0x00);
	mbWriteShiftCtrlLockState = true;
}

bool ATVirtualScreenHandler::GetShiftLockState() const {
	return (mShiftCtrlLockState & 0x40) != 0;
}

bool ATVirtualScreenHandler::GetControlLockState() const {
	return (mShiftCtrlLockState & 0x80) != 0;
}

int ATVirtualScreenHandler::ReadRawText(uint8 *dst, int x, int y, int n) const {
	if ((x|y) < 0)
		return 0;

	if ((uint32)y >= mHeight)
		return 0;

	if ((uint32)x >= mWidth)
		return 0;

	if (n <= 0)
		return 0;

	uint32 n2 = std::min<uint32>(n, mWidth - x);
	const uint8 *src = &mScreen[y * mWidth];

	memcpy(dst, src, n2);
	return n2;
}

bool ATVirtualScreenHandler::CheckForCursorChange() {
	if (!mbCursorChanged)
		return false;

	mbCursorChanged = false;
	return true;
}

void ATVirtualScreenHandler::CheckForDisplayListReplaced() {
	const uint16 dladdr = (uint16)(
		mCPUMemory.DebugReadByte(ATKernelSymbols::SDLSTL) +
		((uint32)mCPUMemory.DebugReadByte(ATKernelSymbols::SDLSTH) << 8)
	);

	const uint8 fbAddrLo = mCPUMemory.DebugReadByte(ATKernelSymbols::SAVMSC);
	const uint8 fbAddrHi = mCPUMemory.DebugReadByte(ATKernelSymbols::SAVMSC + 1);

	const uint8 lms0 = mCPUMemory.DebugReadByte(dladdr + 3);
	const uint8 lms1 = mCPUMemory.DebugReadByte(dladdr + 4);
	const uint8 lms2 = mCPUMemory.DebugReadByte(dladdr + 5);

	const bool dlValid = (lms0 == 0x42 && lms1 == fbAddrLo && lms2 == fbAddrHi);

	SetDisplayValid(dlValid);	
}

void ATVirtualScreenHandler::WarmReset() {
	mbMarginAdjusted = false;
	mbIgnoreEditorGetForMarginAdjust = false;

	ClearScreen();
	SetScreenModeValid(false);
	SetDisplayValid(false);
}

void ATVirtualScreenHandler::ColdReset() {
	WarmReset();
}

void ATVirtualScreenHandler::Enable() {
	// Check if it's safe to apply parameters, as the OS needs to get through
	// partial system init before RMARGN is set. Currently we do this by
	// checking if playfield DMA is enabled.
	ATKernelDatabase kdb(&mCPUMemory);
	if (kdb.DMACTL & 3) {
		AdjustMargin();

		// check if main screen is GR.0
		const uint8 mainMode = kdb.SWPFLG ? kdb.TINDEX : kdb.DINDEX;
		SetScreenModeValid(mainMode == 0);
	}
}

void ATVirtualScreenHandler::Disable() {
	ATKernelDatabase kdb(&mCPUMemory);

	if (mbMarginAdjusted) {
		mbMarginAdjusted = false;

		if (kdb.RMARGN > 39)
			kdb.RMARGN = 39;

		kdb.ROWCRS = 0;
		kdb.COLCRS = 0;
	}
}

void ATVirtualScreenHandler::SetGetCharAddresses(uint16 editor, uint16 screen) {
	mEditorGetCharAddress = editor;
	mScreenGetCharAddress = screen;
}

uint8 ATVirtualScreenHandler::OnEditorCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset) {
	const uint8 iocb = cpu->GetX();
	ATKernelDatabase kdb(mem);

	switch(offset) {
		case 0:		// open
			mbForcedInputMode = (mem->ReadByte(ATKernelSymbols::ICAX1 + iocb) & 1) != 0;
			SetWaitingForInput(false);

			ReadParams(*mem);
			ClearScreen();
			mX = mLeftMargin;
			mY = 0;
			MoveCursor(mX, mY);
			ShowCursor(true);
			SetScreenModeValid(true);
			mbMarginAdjusted = false;

			if (!mbIgnoreEditorGetForMarginAdjust) {
				mbIgnoreEditorGetForMarginAdjust = true;
				cpu->PushWord(mEditorGetCharAddress - 1);
			}

			// allow default E:/S: open to execute
			return 0;

		case 2:		// close
			// don't intercept close
			return 0;

		case 4:		// get byte
			if (!mbScreenModeValid) {
				mbIgnoreEditorGetForMarginAdjust = false;
				return 0;
			}

			AdjustMargin();

			if (mbIgnoreEditorGetForMarginAdjust) {
				mbIgnoreEditorGetForMarginAdjust = false;
				return 0x60;
			}

			if (mbToggleSuspend) {
				mbToggleSuspend = false;

				mem->CPUWriteByte(ATKernelSymbols::SSFLAG, mem->CPUReadByte(ATKernelSymbols::SSFLAG) ^ 0xFF);
			}

			if (mbWriteShiftCtrlLockState) {
				mbWriteShiftCtrlLockState = false;
				kdb.SHFLOK = mShiftCtrlLockState;
			} else
				mShiftCtrlLockState = kdb.SHFLOK;

			if (kdb.BRKKEY == 0) {
				cpu->Ldy(kATCIOStat_Break);
				if (mbWaitingForInput) {
					SetWaitingForInput(false);
					WriteParams(*mem);
				}

				return 0x60;
			}

			while(mInputIndex >= mActiveInputLine.size()) {
				mInputIndex = 0;

				mActiveInputLine.clear();

				if (!mbWaitingForInput) {
					SetWaitingForInput(true);

					ReadParams(*mem);

					mGetLineX = mX;
					mGetLineY = GetLogicalLineBegin(mY);
				}

				if (mpReadyHandler) {
					mpReadyHandler();

					if (!mActiveInputLine.empty())
						break;
				}

				if (mbWaitingForInput) {
					uint8 status = HandleInput();
					if (status == kATCIOStat_Success)
						continue;

					if (status != 0) {
						cpu->Ldy(status);
						WriteParams(*mem);
						SetWaitingForInput(false);
						return 0x60;
					}

					cpu->PushWord(mEditorGetCharAddress - 1);
					cpu->PushWord(0xE4C0 - 1);
					cpu->PushWord(0xE4C0 - 1);
					return 0x60;
				}
			}

			SetWaitingForInput(false);
			WriteParams(*mem);
			cpu->SetA(mActiveInputLine[mInputIndex++]);
			cpu->Ldy(kATCIOStat_Success);
			break;

		case 6:		// put byte
			if (!mbScreenModeValid)
				return 0;

			AdjustMargin();

			if (mbToggleSuspend) {
				mbToggleSuspend = false;

				mem->CPUWriteByte(ATKernelSymbols::SSFLAG, mem->CPUReadByte(ATKernelSymbols::SSFLAG) ^ 0xFF);
			}

			if (mem->ReadByte(ATKernelSymbols::SSFLAG)) {
				cpu->PushWord(cpu->GetInsnPC() - 1);
				cpu->PushWord(0xE4C0 - 1);
				cpu->PushWord(0xE4C0 - 1);
				return 0x60;
			} else if (!mem->ReadByte(ATKernelSymbols::BRKKEY)) {
				mem->WriteByte(ATKernelSymbols::BRKKEY, 0x80);
				cpu->Ldy(kATCIOStat_Break);
				SetWaitingForInput(false);
				break;
			} else {
				ReadParams(*mem);

				uint8 c = cpu->GetA();

				PutChar(c, true);
				
				WriteParams(*mem);

				cpu->Ldy(kATCIOStat_Success);
				SetWaitingForInput(false);
			}
			break;

		case 8:		// get status
			if (mbScreenModeValid)
				return 0;

			cpu->Ldy(kATCIOStat_Success);
			break;

		case 10:	// special
			if (mbScreenModeValid)
				return 0;

			cpu->Ldy(kATCIOStat_Success);
			break;
	}

	// force RTS
	return 0x60;
}

uint8 ATVirtualScreenHandler::OnScreenCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset) {
	ATKernelDatabase kdb(mem);

	switch(offset) {
		case 0:		// open
			ReadParams(*mem);
			ClearScreen();
			mX = mLeftMargin;
			mY = 0;
			MoveCursor(mX, mY);
			ShowCursor(true);

			if ((mem->ReadByte(ATKernelSymbols::ICAX2 + cpu->GetX()) & 15) == 0) {
				SetScreenModeValid(true);
				mbMarginAdjusted = false;

				if (!mbIgnoreEditorGetForMarginAdjust) {
					mbIgnoreEditorGetForMarginAdjust = true;
					cpu->PushWord(mEditorGetCharAddress - 1);
				}
			} else {
				SetScreenModeValid(false);
			}

			// allow default E:/S: open to execute
			return 0;

		case 2:		// close
			// don't intercept close
			return 0;

		case 4:		// get byte
			if (!mbScreenModeValid)
				return 0;

			AdjustMargin();

			if (kdb.BRKKEY == 0) {
				cpu->Ldy(kATCIOStat_Break);
				return 0x60;
			}

			ReadParams(*mem);

			if (mX >= mWidth || mY >= mHeight) {
				cpu->Ldy(kATCIOStat_CursorRange);
				return 0x60;
			}

			WriteParams(*mem);

			cpu->SetA(mScreen[mY * mWidth + mX]);
			if (mX >= mRightMargin) {
				mX = mLeftMargin;
				++mY;
			}

			cpu->Ldy(kATCIOStat_Success);
			break;

		case 6:		// put byte
			if (!mbScreenModeValid)
				return 0;

			AdjustMargin();

			if (mbToggleSuspend) {
				mbToggleSuspend = false;

				mem->CPUWriteByte(ATKernelSymbols::SSFLAG, mem->CPUReadByte(ATKernelSymbols::SSFLAG) ^ 0xFF);
			}

			if (mem->ReadByte(ATKernelSymbols::SSFLAG)) {
				cpu->PushWord(cpu->GetInsnPC() - 1);
				cpu->PushWord(0xE4C0 - 1);
				cpu->PushWord(0xE4C0 - 1);
				return 0x60;
			} else if (!mem->ReadByte(ATKernelSymbols::BRKKEY)) {
				mem->WriteByte(ATKernelSymbols::BRKKEY, 0x80);
				cpu->Ldy(kATCIOStat_Break);
				SetWaitingForInput(false);
				break;
			} else {
				ReadParams(*mem);

				uint8 c = cpu->GetA();

				// S: only handles clear ($7D) and EOL ($9B). The rest are E:
				// only special characters.
				if (c == 0x7D || c == 0x9B)
					PutChar(c, false);
				else
					PutRawChar(c);
				
				WriteParams(*mem);

				cpu->Ldy(kATCIOStat_Success);
				SetWaitingForInput(false);
			}
			break;

		case 8:		// get status
			if (mbScreenModeValid)
				return 0;

			cpu->Ldy(kATCIOStat_Success);
			break;

		case 10:	// special
			if (mbScreenModeValid)
				return 0;

			cpu->Ldy(kATCIOStat_Success);
			break;
	}

	// force RTS
	return 0x60;
}

void ATVirtualScreenHandler::SetWaitingForInput(bool waiting) {
	mbWaitingForInput = waiting;

	if (mbLineInputEnabled)
		ShowCursor(waiting);
}

uint8 ATVirtualScreenHandler::HandleInput() {
	if (mbLineInputEnabled)
		return 0;

	// check if we have a character
	ATKernelDatabase kdb(&mCPUMemory);

	const uint8 ch = kdb.CH;
	if (ch == 0xFF)
		return 0;

	// we do -- consume it
	kdb.CH = 0xFF;

	// discard Ctrl+Shift keystrokes
	if (ch >= 0xC0)
		return 0;

	// translate through key table
	// This is a copy of the keyboard table from AltirraOS, since we are doing
	// the same thing here; this is easier than trying to call into the K:
	// handler.
	static constexpr uint8 kScanCodeTable[] {
		// lowercase
		0x6C, 0x6A, 0x3B, 0x80, 0x80, 0x6B, 0x2B, 0x2A,	// L   J   ;:  F1  F2  K   +\  *^
		0x6F, 0x80, 0x70, 0x75, 0x9B, 0x69, 0x2D, 0x3D,	// O       P   U   Ret I   -_  =|
		0x76, 0x80, 0x63, 0x80, 0x80, 0x62, 0x78, 0x7A,	// V   Hlp C   F3  F4  B   X   Z
		0x34, 0x80, 0x33, 0x36, 0x1B, 0x35, 0x32, 0x31,	// 4$      3#  6&  Esc 5%  2"  1!
		0x2C, 0x20, 0x2E, 0x6E, 0x80, 0x6D, 0x2F, 0x81,	// ,[  Spc .]  N       M   /?  Inv
		0x72, 0x80, 0x65, 0x79, 0x7F, 0x74, 0x77, 0x71,	// R       E   Y   Tab T   W   Q
		0x39, 0x80, 0x30, 0x37, 0x7E, 0x38, 0x3C, 0x3E,	// 9(      0)  7'  Bks 8@  <   >
		0x66, 0x68, 0x64, 0x80, 0x82, 0x67, 0x73, 0x61,	// F   H   D       Cps G   S   A
	
		// SHIFT
		0x4C, 0x4A, 0x3A, 0x80, 0x80, 0x4B, 0x5C, 0x5E,	// L   J   ;:  F1  F2  K   +\  *^
		0x4F, 0x80, 0x50, 0x55, 0x9B, 0x49, 0x5F, 0x7C,	// O       P   U   Ret I   -_  =|
		0x56, 0x80, 0x43, 0x80, 0x80, 0x42, 0x58, 0x5A,	// V   Hlp C   F3  F4  B   X   Z
		0x24, 0x80, 0x23, 0x26, 0x1B, 0x25, 0x22, 0x21,	// 4$      3#  6&  Esc 5%  2"  1!
		0x5B, 0x20, 0x5D, 0x4E, 0x80, 0x4D, 0x3F, 0x80,	// ,[  Spc .]  N       M   /?  Inv
		0x52, 0x80, 0x45, 0x59, 0x9F, 0x54, 0x57, 0x51,	// R       E   Y   Tab T   W   Q
		0x28, 0x80, 0x29, 0x27, 0x9C, 0x40, 0x7D, 0x9D,	// 9(      0)  7'  Bks 8@  <   >
		0x46, 0x48, 0x44, 0x80, 0x83, 0x47, 0x53, 0x41,	// F   H   D       Cps G   S   A
	
		// CTRL
		0x0C, 0x0A, 0x7B, 0x80, 0x80, 0x0B, 0x1E, 0x1F,	// L   J   ;:  F1  F2  K   +\  *^
		0x0F, 0x80, 0x10, 0x15, 0x9B, 0x09, 0x1C, 0x1D,	// O       P   U   Ret I   -_  =|
		0x16, 0x80, 0x03, 0x80, 0x80, 0x02, 0x18, 0x1A,	// V   Hlp C   F3  F4  B   X   Z
		0x80, 0x80, 0x85, 0x80, 0x1B, 0x80, 0xFD, 0x80,	// 4$      3#  6&  Esc 5%  2"  1!
		0x00, 0x20, 0x60, 0x0E, 0x80, 0x0D, 0x80, 0x80,	// ,[  Spc .]  N       M   /?  Inv
		0x12, 0x80, 0x05, 0x19, 0x9E, 0x14, 0x17, 0x11,	// R       E   Y   Tab T   W   Q
		0x80, 0x80, 0x80, 0x80, 0xFE, 0x80, 0x7D, 0xFF,	// 9(      0)  7'  Bks 8@  <   >
		0x06, 0x08, 0x04, 0x80, 0x84, 0x07, 0x13, 0x01,	// F   H   D       Cps G   S   A
	};
	static_assert(vdcountof(kScanCodeTable) == 192);

	uint8 code = kScanCodeTable[ch];

	// handle specials
	switch(code) {
		case 0x80:	// invalid
			return 0;

		case 0x81:	// inverse
			kdb.INVFLG = kdb.INVFLG ^ 0x80;
			return 0;

		case 0x82:	// caps
			// toggle between shifted and non-shifted; ctrl+lock goes to no lock
			// (XL/XE OS behavior)
			kdb.SHFLOK = kdb.SHFLOK ? 0x00 : 0x40;
			return 0;

		case 0x83:	// shift+caps
			kdb.SHFLOK = 0x40;
			return 0;

		case 0x84:	// ctrl+caps
			kdb.SHFLOK = 0x80;
			return 0;

		case 0x85:	// EOF
			return kATCIOStat_EndOfFile;
	}

	// handle EOL -- need to initiate line scan, and can't use regular EOL
	// logic as we need to go to the end of the logical line
	ReadParams(mCPUMemory);

	if (code == 0x9B) {
		ReadScreenAsInput();
	} else {
		// apply shift/control lock to alphabetics
		if (code >= 0x61 && code <= 0x7A) {
			switch(kdb.SHFLOK >> 6) {
				case 0:		// none
					break;

				case 1:		// shift lock
				case 3:
					code &= 0xDF;
					break;

				case 2:		// control lock
					code &= 0x1F;
					break;
			}
		}

		// apply inverse video
		code ^= kdb.INVFLG;

		// echo character
		PutChar(code, true);
	}

	WriteParams(mCPUMemory);

	return kATCIOStat_Success;
}

void ATVirtualScreenHandler::PutChar(uint8 c, bool updateCursor) {
	ATKernelDatabase kdb(&mCPUMemory);

	if (!mbLineInputEnabled)
		ShowCursor(kdb.CRSINH == 0);

	// forcibly clamp coordinates in case they're out of bounds
	if (mX >= mWidth || mY >= mHeight) {
		VDFAIL("Coordinates out of bounds.");

		mX = std::min<uint8>(mX, mWidth - 1);
		mY = std::min<uint8>(mY, mHeight - 1);
	}

	// EOL cannot be escaped, and does not reset the escape flag.
	if (c != 0x9B) {
		bool forceRawChar = false;

		if (kdb.ESCFLG) {
			kdb.ESCFLG = 0;

			forceRawChar = true;
		} else if (kdb.DSPFLG) {
			forceRawChar = true;
		}
		
		if (forceRawChar) {
			PutRawChar(c);

			if (updateCursor)
				MoveCursor(mX, mY);

			return;
		}
	}

	switch(c) {
		case 0x1B:		// Escape
			// needs to be $80 per OS Manual, Appendix L, B26
			kdb.ESCFLG = 0x80;
			break;

		case 0x1C:		// Cursor up
			if (mY)
				--mY;
			else
				mY = mHeight - 1;

			CheckGetLineChanged();
			break;

		case 0x1D:		// Cursor down
			if (++mY >= mHeight)
				mY = 0;

			CheckGetLineChanged();
			break;

		case 0x1E:		// Cursor left
			if (mX > mLeftMargin)
				--mX;
			else
				mX = mRightMargin;
			break;

		case 0x1F:		// Cursor right
			if (mX < mRightMargin)
				++mX;
			else
				mX = mLeftMargin;
			break;

		case 0x7D:		// Clear
			ClearScreen();
			break;

		case 0x7E:		// Backspace
			if (mX > mLeftMargin) {
				--mX;
				mScreen[mX + mY*mWidth] = ' ';
			} else if (mY && !IsLogicalLineStart(mY)) {
				mX = mRightMargin;

				// Check if we're at the end of the logical line and are backing
				// out of a physical line that is now empty. If so, trim it from
				// the logical line. This is slightly different than stock E:,
				// which will delete the rest of the logical line even if there
				// is another non-blank following physical line in the logical
				// line; we deliberately don't follow that here.
				if (mY >= mHeight - 1 || IsLogicalLineStart(mY + 1)) {
					if (IsPhysicalLineEmpty(mY)) {
						MoveLines(mY + 1, mY, mHeight - (mY + 1));
						ClearLines(mHeight - 1, 1);
					}
				}

				--mY;
				mScreen[mX + mY*mWidth] = ' ';
			}
			break;

		case 0x7F:		// Tab
			// Advance to next tab stop or the start of the next logical line,
			// whichever comes first. May scroll.
			{
				uint32 logbase = ComputeLogicalColumn() - mX;

				for(;;) {
					if (mX >= mRightMargin) {
						mX = mLeftMargin;

						if (mY >= mHeight - 1) {
							Scroll();
							break;
						}

						++mY;
						
						if (IsLogicalLineStart(mY))
							break;

						logbase += mWidth;
					} else
						++mX;

					uint32 logcol = logbase + mX;

					if (logcol < 256 && (mTabMap[logcol >> 3] & (0x80 >> (logcol & 7))))
						break;
				}
			}
			break;

		case 0x9B:		// EOL
			mX = mLeftMargin;
			if (++mY >= mHeight)
				Scroll();

			SetLogicalLineStart(mY);
			break;

		case 0x9C:		// Delete line
			{
				// determine the physical lines containing the current logical
				// line
				mY = GetLogicalLineBegin(mY);

				uint32 n = GetLogicalLineRemainingHeight(mY);

				// delete the entire logical line
				MoveLines(mY, mY + n, mHeight - (mY + n));
				ClearLines(mHeight - n, n);

				// place cursor at start of next logical line
				mX = mLeftMargin;
			}
			break;

		case 0x9D:		// Insert line
			// move physical lines down one, including the current line
			MoveLines(mY + 1, mY, mHeight - mY);

			// clear the current physical line, but do NOT mark it as a new
			// logical line; we must stay connected to the previous physical
			// line
			memset(&mScreen[mWidth * mY], ' ', mWidth);

			// split the logical line starting at the next line (NOT the current
			// line)
			if (mY + 1 < mHeight)
				SetLogicalLineStart(mY + 1);

			// move cursor to left margin
			mX = mLeftMargin;
			break;

		case 0x9E:		// Clear Tab
			{
				uint32 col = ComputeLogicalColumn();

				if (col < 256)
					mTabMap[col >> 3] &= ~(0x80 >> (col & 7));
			}
			break;

		case 0x9F:		// Set Tab
			{
				uint32 col = ComputeLogicalColumn();

				if (col < 256)
					mTabMap[col >> 3] |= (0x80 >> (col & 7));
			}
			break;

		case 0xFD:		// Bell
			mbBellPending = true;
			break;

		case 0xFE:		// Delete character
			{
				uint8 n = GetLogicalLineRemainingHeight(mY);
				uint8 incomingChar = ' ';

				// Delete character from logical line, wrapping from margin to
				// margin when shifting characters between physical lines.
				for(uint8 i = n; i; --i) {
					uint8 *line = &mScreen[(mY + i - 1) * mWidth];
					uint8 outgoingChar = line[mLeftMargin];

					uint8 deleteX = (i == 1) ? mX : mLeftMargin;
					if (mRightMargin > deleteX)
						memmove(line + deleteX, line + deleteX + 1, mRightMargin - deleteX);

					incomingChar = outgoingChar;
				}

				// If the last physical line is empty and isn't the start of
				// the physical line, delete it. This happens even if the cursor
				// isn't at the left margin. Note that this may put the cursor
				// at the next physical line.
				if (!IsLogicalLineStart(mY)) {
					const uint8 *line = &mScreen[mY * mWidth];
					bool allBlank = true;

					for(uint32 x = mLeftMargin; x <= mRightMargin; ++x) {
						if (line[x] != ' ') {
							allBlank = false;
							break;
						}
					}

					if (allBlank) {
						MoveLines(mY, mY + 1, mHeight - (mY + 1));
						ClearLines(mHeight - 1, 1);
					}
				}
			}
			break;

		case 0xFF:		// Insert character
			{
				// Shift characters starting at the current line and proceeding
				// until the end of the logical line.
				uint8 shiftIn = ' ';
				uint32 y = mY;
				uint32 x = mX;

				for(;;) {
					uint8 *line = &mScreen[y * mWidth];

					if (x <= mRightMargin) {
						uint8 shiftOut = line[mRightMargin];

						if (x < mRightMargin)
							memmove(line + x + 1, line + x, mRightMargin - x);

						line[x] = shiftIn;
						shiftIn = shiftOut;
					}

					x = mLeftMargin;

					++y;

					if (y >= mHeight)
						break;
					
					if (IsLogicalLineStart(y)) {
						// We hit the end of the logical line. Check if we
						// shifted out a non-space character; if so, scroll
						// down the rest of the screen and extend the logical
						// line.
						if (shiftIn != ' ') {
							MoveLines(y + 1, y, mHeight - y);
							ClearLines(y, 1);
							ClearLogicalLineStart(y);
						}
						break;
					}
				}
			}
			break;

		default:
			PutRawChar(c);
			break;
	}

	if (updateCursor)
		MoveCursor(mX, mY);
}

void ATVirtualScreenHandler::PutRawChar(uint8 c) {
	// forcibly clamp coordinates in case they're out of bounds
	if (mX >= mWidth || mY >= mHeight) {
		VDFAIL("Coordinates out of bounds.");

		mX = std::min<uint8>(mX, mWidth - 1);
		mY = std::min<uint8>(mY, mHeight - 1);
	}

	mScreen[mX + mY*mWidth] = c;

	if (++mX > mRightMargin) {
		mX = mLeftMargin;

		if (++mY >= mHeight)
			Scroll();
		else if (IsLogicalLineStart(mY)) {
			MoveLines(mY + 1, mY, mHeight - (mY + 1));
			ClearLines(mY, 1);
		}

		ClearLogicalLineStart(mY);
	}
}

void ATVirtualScreenHandler::ClearScreen() {
	// This must not write kernel variables, as we can call it from reset.
	memset(mScreen.data(), ' ', mScreen.size() * sizeof(mScreen[0]));
	mX = mLeftMargin;
	mY = 0;

	std::fill(std::begin(mLogicalLineMap), std::end(mLogicalLineMap), (uint8)0xFF);
	std::fill(std::begin(mTabMap), std::end(mTabMap), (uint8)0x01);
}

void ATVirtualScreenHandler::Scroll() {
	uint32 linesToScroll = GetLogicalLineRemainingHeight(0);

	MoveLines(0, linesToScroll, mHeight - linesToScroll);

	mY = mHeight - linesToScroll;

	ClearLines(mY, linesToScroll);
}

void ATVirtualScreenHandler::ClearLines(uint32 y, uint32 n) {
	if (y >= mHeight)
		return;

	n = std::min<uint32>(n, mHeight - y);
	if (!n)
		return;

	memset(&mScreen[mWidth * y], ' ', mWidth * n);

	for(uint32 i = 0; i < n; ++i)
		SetLogicalLineStart(y + i);
}

void ATVirtualScreenHandler::MoveLines(uint32 dstY, uint32 srcY, uint32 n) {
	if (dstY >= mHeight || srcY >= mHeight || srcY == dstY)
		return;

	n = std::min<uint32>(n, mHeight - std::max<uint32>(srcY, dstY));
	if (!n)
		return;

	memmove(
		&mScreen[dstY * mWidth],
		&mScreen[srcY * mWidth],
		mWidth * n
	);

	if (srcY < dstY) {
		for(uint32 i = n; i; --i)
			SetLogicalLineStart(dstY + i - 1, IsLogicalLineStart(srcY + i - 1));
	} else {
		for(uint32 i = 0; i < n; ++i)
			SetLogicalLineStart(dstY + i, IsLogicalLineStart(srcY + i));
	}
}

void ATVirtualScreenHandler::ReadParams(ATCPUEmulatorMemory& mem) {
	mX = mem.CPUReadByte(ATKernelSymbols::COLCRS);
	mY = mem.CPUReadByte(ATKernelSymbols::ROWCRS);
	mLeftMargin = mem.CPUReadByte(ATKernelSymbols::LMARGN);
	mRightMargin = mem.CPUReadByte(ATKernelSymbols::RMARGN);

	if (mbResetMarginsPending) {
		if (mLeftMargin >= mWidth)
			mLeftMargin = 2;

		mRightMargin = mWidth - 1;
	}

	if (mLeftMargin > mWidth - 1)
		mLeftMargin = mWidth - 1;

	if (mRightMargin > mWidth - 1)
		mRightMargin = mWidth - 1;

	if (mRightMargin < mLeftMargin)
		mRightMargin = mLeftMargin;

	// we must not clip the cursor to the margins; this breaks NAZZ, which
	// does POSITION 0,Y with LMARGN=2.
	if (mX >= mWidth)
		mX = mWidth - 1;

	if (mY >= mHeight)
		mY = mHeight - 1;

	mTabMap[0] = mem.CPUReadByte(ATKernelSymbols::TABMAP + 0);
	mTabMap[1] = mem.CPUReadByte(ATKernelSymbols::TABMAP + 1);
	mTabMap[2] = mem.CPUReadByte(ATKernelSymbols::TABMAP + 2);
}

void ATVirtualScreenHandler::WriteParams(ATCPUEmulatorMemory& mem) {
	ATKernelDatabase kdb(&mem);

	kdb.COLCRS = mX;
	kdb.ROWCRS = mY;

	if (mbResetMarginsPending) {
		mbResetMarginsPending = false;

		if (kdb.LMARGN >= mWidth)
			kdb.LMARGN = 2;

		kdb.RMARGN = mWidth - 1;
	}
}

uint32 ATVirtualScreenHandler::ComputeLogicalColumn() const {
	uint32 col = mX;
	for(uint32 y = mY; y; --y) {
		if (IsLogicalLineStart(y))
			break;

		col += mWidth;
	}

	return col;
}

bool ATVirtualScreenHandler::IsLogicalLineStart(uint8 y) const {
	return (mLogicalLineMap[y >> 3] & (0x80 >> (y & 7))) != 0;
}

void ATVirtualScreenHandler::ClearLogicalLineStart(uint8 y) {
	mLogicalLineMap[y >> 3] &= ~((0x80 >> (y & 7)));
}

void ATVirtualScreenHandler::SetLogicalLineStart(uint8 y) {
	mLogicalLineMap[y >> 3] |= (0x80 >> (y & 7));
}

void ATVirtualScreenHandler::SetLogicalLineStart(uint8 y, bool v) {
	if (v)
		SetLogicalLineStart(y);
	else
		ClearLogicalLineStart(y);
}

uint8 ATVirtualScreenHandler::GetLogicalLineBegin(uint8 y) const {
	while(y && !IsLogicalLineStart(y))
		--y;

	return y;
}

uint8 ATVirtualScreenHandler::GetLogicalLineRemainingHeight(uint8 y) const {
	uint8 height = 1;

	while(y < mHeight - 1 && !IsLogicalLineStart(y + 1)) {
		++y;
		++height;
	}

	return height;
}

bool ATVirtualScreenHandler::IsPhysicalLineEmpty(uint8 y) const {
	if (y >= mHeight)
		return true;

	const uint8 *line = &mScreen[y * mWidth];

	for(uint32 x = mLeftMargin; x <= mRightMargin; ++x) {
		if (line[x] != ' ')
			return false;
	}

	return true;
}

void ATVirtualScreenHandler::CheckGetLineChanged() {
	if (mY == mGetLineY)
		return;
	
	auto newLineY = GetLogicalLineBegin(mY);
	if (mGetLineY != newLineY) {
		// cursor moved to another logical line -- reset get line position
		// to start of new logical line
		mGetLineX = mLeftMargin;
		mGetLineY = newLineY;
	}
}

void ATVirtualScreenHandler::AdjustMargin() {
	if (!mbMarginAdjusted) {
		mbMarginAdjusted = true;

		ATKernelDatabase kdb(&mCPUMemory);

		if (kdb.RMARGN == 39)
			kdb.RMARGN = mWidth - 1;
	}
}

void ATVirtualScreenHandler::MoveCursor(uint32 x, uint32 y) {
	if (mCursorX != x || mCursorY != y) {
		mCursorX = x;
		mCursorY = y;

		if (mbCursorVisible)
			NotifyCursorChanged();
	}
}

void ATVirtualScreenHandler::ShowCursor(bool visible) {
	if (mbCursorVisible != visible) {
		mbCursorVisible = visible;
		NotifyCursorChanged();
	}
}

void ATVirtualScreenHandler::NotifyCursorChanged() {
	mbCursorChanged = true;
}

void ATVirtualScreenHandler::SetScreenModeValid(bool valid) {
	if (mbScreenModeValid != valid) {
		mbScreenModeValid = valid;

		UpdatePassThrough();
	}
}

void ATVirtualScreenHandler::SetDisplayValid(bool valid) {
	if (mbDisplayValid != valid) {
		mbDisplayValid = valid;

		UpdatePassThrough();
	}
}

void ATVirtualScreenHandler::UpdatePassThrough() {
	bool enabled = !mbDisplayValid || !mbScreenModeValid;

	if (mbPassThrough != enabled) {
		mbPassThrough = enabled;

		if (mpPassThroughChangedHandler)
			mpPassThroughChangedHandler();
	}
}

///////////////////////////////////////////////////////////////////////////

IATVirtualScreenHandler *ATCreateVirtualScreenHandler(ATCPUEmulatorMemory& mem) {
	vdautoptr<ATVirtualScreenHandler> hook(new ATVirtualScreenHandler(mem));

	return hook.release();
}
