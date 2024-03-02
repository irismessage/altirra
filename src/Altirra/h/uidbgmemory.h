//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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

#ifndef f_AT_UIDBGMEMORY_H
#define f_AT_UIDBGMEMORY_H

#include <at/atnativeui/dialog.h>
#include "debugger.h"
#include "uidbgpane.h"

class ATMemoryWindowPanel final : public VDDialogFrameW32 {
public:
	ATMemoryWindowPanel();
	~ATMemoryWindowPanel();

	void SetOnRelayout(vdfunction<void()> fn);
	void SetOnAddressSet(vdfunction<void(const wchar_t *)> fn);
	void SetOnColumnsSet(vdfunction<bool(uint32&)> fn);

	void SetColumns(uint32 cols);
	void SetAddressText(const wchar_t *s);

	void ToggleExpand();
	sint32 GetDesiredHeight() const;

public:
	bool OnLoaded() override;

private:
	bool mbExpanded = false;

	VDUIProxyButtonControl mExpandView;
	VDUIProxyComboBoxExControl mAddressView;
	VDUIProxyComboBoxExControl mColumnsView;

	vdfunction<void()> mpOnRelayout;
	vdfunction<void(const wchar_t *)> mpOnAddressSet;
	vdfunction<bool(uint32&)> mpOnColumnsSet;

	static constexpr uint32 kStdColumnCounts[] = {
		1, 2, 4, 8, 16, 24, 32, 40, 48, 64, 80, 128, 256
	};
};

class ATMemoryWindow final : public ATUIDebuggerPaneWindow,
							public IATDebuggerClient
{
public:
	enum class ValueMode : uint8 {
		HexBytes,
		HexWords,
		DecBytes,
		DecWords
	};

	enum class InterpretMode : uint8 {
		None,
		Atascii,
		Internal,
		Font1Bpp,
		Font2Bpp,
		Graphics1Bpp,
		Graphics2Bpp,
		Graphics4Bpp,
		Graphics8Bpp,
	};

	ATMemoryWindow(uint32 id = kATUIPaneId_Memory);
	~ATMemoryWindow();

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId);

	void SetPosition(uint32 addr);
	void SetColumns(uint32 cols, bool updateUI);

	void SetValueMode(ValueMode mode);
	void SetInterpretMode(InterpretMode mode);

protected:
	VDGUIHandle Create(uint32 exStyle, uint32 style, int x, int y, int cx, int cy, VDGUIHandle parent, int id);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnFontsUpdated();
	void RecreateContentFont();
	void UpdateContentFontMetrics();
	void UpdateLineHeight();
	void UpdateScrollRange();
	void UpdateHScrollRange();
	void AdjustColumnCount();
	void OnThemeUpdated();
	void OnMouseWheel(uint32 keyMask, float clicks);
	void OnPaint();
	void OnViewScroll(sint32 pos, bool tracking);
	void OnViewHScroll(sint32 pos, bool tracking);
	void RemakeView(uint32 focusAddr, bool fromScroll);
	int GetAddressLength() const;
	bool GetAddressFromPoint(int x, int y, uint32& addr, bool& isData) const;
	void InvalidateAddress(uint32 addr);
	void UpdateHighlightedAddress(int x, int y, bool select);
	void SetHighlightedAddress(std::optional<uint32> highlightAddr, bool highlightData, bool select);
	void BeginEdit();
	void CancelEdit();
	void CommitEdit();

	HMENU	mMenu = nullptr;
	HFONT	mhfontContents = nullptr;

	RECT	mTextArea {};
	uint32	mViewStart = 0;
	bool	mbViewValid = false;
	uint32	mCharWidth = 16;
	uint32	mCharHeight = 16;
	uint32	mLineHeight = 16;
	uintptr	mLastTarget = 0;
	sint32	mHScrollPos = 0;
	sint32	mCompletelyVisibleRows = 0;
	sint32	mPartiallyVisibleRows = 1;
	float	mWheelAccum = 0;
	float	mZoomFactor = 1;
	uint32	mColumns = 16;
	std::optional<uint32> mHighlightedAddress;
	bool	mbHighlightedData = false;
	bool	mbSelectionEnabled = false;

	sint32	mEditValue = -1;
	int		mEditPhase = 0;
	bool	mbEditWord = false;

	ValueMode mValueMode = ValueMode::HexBytes;
	InterpretMode mInterpretMode = InterpretMode::Atascii;

	VDStringW	mName;
	VDStringA	mTempLine;
	VDStringA	mTempLine2;

	// the currently active view data
	uint32 mViewDataStart = 0;
	vdfastvector<uint8> mViewData;

	// snapshot from last remake with current view (same view start)
	vdfastvector<uint8> mRefViewData;
	vdfastvector<uint8> mRefViewData2;

	// snapshot from previous cycle and view
	uint32 mOldViewDataStart = 0;
	vdfastvector<uint8> mOldViewData;

	uint32 mViewDataCycle = 0;
	uint32 mCurrentCycle = 0;

	uint32 mChangedBitsStartAddress = 0;
	vdfastvector<uint32> mChangedBits;
	vdfastvector<uint32> mNewChangedBits;

	ATMemoryWindowPanel mControlPanel;
	VDUIProxyScrollBarControl mScrollBar;
	VDUIProxyScrollBarControl mHScrollBar;
	VDUIProxyMessageDispatcherW32 mDispatcher;

	struct {
		BITMAPINFO bi;
		RGBQUAD palext[255];
	} mImageHeader {};

	uint8 mRenderLineData[256] {};

	// Most of the time the image data is the same size, except when we have
	// to render 2bpp data as 8bpp.
	uint8 mRenderImageData[1024] {};
};

#endif
