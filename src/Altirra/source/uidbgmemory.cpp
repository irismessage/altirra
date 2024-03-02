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

#include "stdafx.h"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <vd2/system/binary.h>
#include <vd2/system/w32assist.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/theme_win32.h>
#include <at/atcore/address.h>
#include "console.h"
#include "resource.h"
#include "uicommondialogs.h"
#include "uidbgmemory.h"

ATMemoryWindowPanel::ATMemoryWindowPanel()
	: VDDialogFrameW32(IDD_DEBUG_MEMORYCTL)
{
	mAddressView.SetOnEndEdit(
		[this](const wchar_t *s) -> bool {
			if (mpOnAddressSet)
				mpOnAddressSet(s);

			return true;
		}
	);

	mColumnsView.SetOnSelectionChanged(
		[this](int idx) {
			if ((unsigned)idx < vdcountof(kStdColumnCounts)) {
				uint32 cols = kStdColumnCounts[idx];
				mpOnColumnsSet(cols);
			}
		}
	);

	mColumnsView.SetOnEndEdit(
		[this](const wchar_t *s) -> bool {
			uint32 columns = wcstoul(s, nullptr, 10);
			if (mpOnColumnsSet) {
				uint32 newCols = columns;
				if (!mpOnColumnsSet(newCols))
					MessageBeep(MB_ICONEXCLAMATION);

				if (newCols != columns) {
					VDStringW text;
					text.sprintf(L"%u", (unsigned)newCols);
					mColumnsView.SetCaption(text.c_str());
				}
			}

			return true;
		}
	);

	mExpandView.SetOnClicked(
		[this] {
			ToggleExpand();
		}
	);
}

ATMemoryWindowPanel::~ATMemoryWindowPanel()
{
}

void ATMemoryWindowPanel::SetOnRelayout(vdfunction<void()> fn) {
	mpOnRelayout = std::move(fn);
}

void ATMemoryWindowPanel::SetOnAddressSet(vdfunction<void(const wchar_t *)> fn) {
	mpOnAddressSet = std::move(fn);
}

void ATMemoryWindowPanel::SetOnColumnsSet(vdfunction<bool(uint32&)> fn) {
	mpOnColumnsSet = std::move(fn);
}

void ATMemoryWindowPanel::SetColumns(uint32 cols) {
	VDStringW s;
	s.sprintf(L"%u", cols);
	mColumnsView.SetCaption(s.c_str());
}

void ATMemoryWindowPanel::SetAddressText(const wchar_t *s) {
	mAddressView.SetCaption(s);
}

bool ATMemoryWindowPanel::OnLoaded() {
	AddProxy(&mExpandView, IDC_EXPAND);
	AddProxy(&mAddressView, IDC_ADDRESS);
	AddProxy(&mColumnsView, IDC_COLUMNS);

	VDStringW s;
	for(const uint32 n : kStdColumnCounts) {
		s.sprintf(L"%u", n);
		mColumnsView.AddItem(s.c_str());
	}

	return VDDialogFrameW32::OnLoaded();
}

void ATMemoryWindowPanel::ToggleExpand() {
	mbExpanded = !mbExpanded;

	if (mpOnRelayout)
		mpOnRelayout();
}

sint32 ATMemoryWindowPanel::GetDesiredHeight() const {
	if (!mbExpanded) {
		ATUINativeWindowProxy divider(GetControl(IDC_STATIC_DIVIDER));

		if (divider.IsValid())
			return divider.GetArea().top;
	}

	return ComputeTemplatePixelSize().h;
}

///////////////////////////////////////////////////////////////////////////

ATMemoryWindow::ATMemoryWindow(uint32 id)
	: ATUIDebuggerPaneWindow(id, L"Memory")
	, mMenu(LoadMenu(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDR_MEMORY_CONTEXT_MENU)))
{
	mPreferredDockCode = kATContainerDockRight;

	if (id >= kATUIPaneId_MemoryN) {
		mName.sprintf(L"Memory %u", (id & kATUIPaneId_IndexMask) + 1);
		SetName(mName.c_str());
	}

	mScrollBar.SetOnValueChanged([this](sint32 pos, bool tracking) { OnViewScroll(pos, tracking); });
	mHScrollBar.SetOnValueChanged([this](sint32 pos, bool tracking) { OnViewHScroll(pos, tracking); });

	mControlPanel.SetOnAddressSet(
		[this](const wchar_t *s) {
			sint32 addr = ATGetDebugger()->ResolveSymbol(VDTextWToU8(VDStringSpanW(s)).c_str(), true);

			if (addr < 0)
				MessageBeep(MB_ICONERROR);
			else
				SetPosition(addr);
		}
	);

	mControlPanel.SetOnColumnsSet(
		[this](uint32& cols) -> bool {
			SetColumns(cols, false);

			return mColumns == cols;
		}
	);

	mControlPanel.SetOnRelayout(
		[this] { OnSize(); }
	);
}

ATMemoryWindow::~ATMemoryWindow() {
	if (mMenu)
		DestroyMenu(mMenu);
}

LRESULT ATMemoryWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	LRESULT r;
	if (mDispatcher.TryDispatch(msg, wParam, lParam, r))
		return r;

	switch(msg) {
		case WM_SIZE:
			OnSize();
			break;

		case WM_ERASEBKGND:
			return FALSE;

		case WM_PAINT:
			OnPaint();
			return 0;

		case WM_CHAR:
			if (mHighlightedAddress.has_value() && mbSelectionEnabled && wParam >= 0x20 && wParam < 0x7F) {
				const uint32 hiAddr = mHighlightedAddress.value();

				if (mbHighlightedData) {
					switch(mInterpretMode) {
						case InterpretMode::Atascii:
							mEditValue = (uint8)wParam;
							CommitEdit();
							SetHighlightedAddress(hiAddr + 1, true, true);
							break;

						case InterpretMode::Internal:
							switch(wParam & 0x60) {
								case 0x20:	mEditValue = (uint8)(wParam ^ 0x20); break;
								case 0x40:	mEditValue = (uint8)(wParam ^ 0x60); break;
								case 0x60:	mEditValue = (uint8)wParam; break;
								default:	break;
							}
							
							CommitEdit();
							SetHighlightedAddress(hiAddr + 1, true, true);
							break;
					}
				} else {
					switch(mValueMode) {
						case ValueMode::DecBytes:
						case ValueMode::DecWords:
							if (wParam >= '0' && wParam <= '9') {
								const int digit = (int)(wParam - '0');
								sint32 newVal = (mEditValue < 0 ? 0 : mEditValue * 10) + digit;

								if (newVal <= (mbEditWord ? 0xFFFF : 0xFF)) {
									mEditValue = newVal;
									InvalidateAddress(mHighlightedAddress.value());
								}
							}
							break;

						case ValueMode::HexBytes:
						case ValueMode::HexWords: {
							int digit;
							
							if (wParam >= '0' && wParam <= '9')
								digit = (int)(wParam - '0');
							else if (wParam >= 'a' && wParam <= 'f')
								digit = (int)(wParam - 'a') + 10;
							else if (wParam >= 'A' && wParam <= 'F')
								digit = (int)(wParam - 'A') + 10;
							else
								break;

							if (mEditValue < 0)
								BeginEdit();

							bool advance = false;
							if (mbEditWord) {
								int shift = 4*(3 - mEditPhase);

								mEditValue ^= (mEditValue ^ (digit << shift)) & (0xF << shift);

								advance = (++mEditPhase >= 4);
							} else {
								int shift = 4*(1 - mEditPhase);

								mEditValue ^= (mEditValue ^ (digit << shift)) & (0xF << shift);

								advance = (++mEditPhase >= 2);
							}

							InvalidateAddress(mHighlightedAddress.value());

							if (advance) {
								CommitEdit();
								SetHighlightedAddress(mHighlightedAddress.value() + (mbEditWord ? 2 : 1), false, true);
							}
							break;
						}
					}
				}
			}
			return 0;

		case WM_KEYDOWN:
			if (mHighlightedAddress.has_value() && mbSelectionEnabled) {
				if (wParam == VK_ESCAPE) {
					CancelEdit();
				} else if (wParam == VK_RETURN) {
					CommitEdit();
					SetHighlightedAddress(mHighlightedAddress.value() + (mbEditWord ? 2 : 1), mbHighlightedData, true);
				} else if (wParam == VK_LEFT) {
					CommitEdit();
					SetHighlightedAddress(mHighlightedAddress.value() - (mbEditWord ? 2 : 1), mbHighlightedData, true);
				} else if (wParam == VK_RIGHT) {
					CommitEdit();
					SetHighlightedAddress(mHighlightedAddress.value() + (mbEditWord ? 2 : 1), mbHighlightedData, true);
				} else if (wParam == VK_UP) {
					CommitEdit();
					SetHighlightedAddress(mHighlightedAddress.value() - mColumns, mbHighlightedData, true);
				} else if (wParam == VK_DOWN) {
					CommitEdit();
					SetHighlightedAddress(mHighlightedAddress.value() + mColumns, mbHighlightedData, true);
				}
			}
			return 0;

		case WM_MOUSEMOVE:
			{
				TRACKMOUSEEVENT tme { sizeof(TRACKMOUSEEVENT) };
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = mhwnd;
				TrackMouseEvent(&tme);

				if (!mbSelectionEnabled) {
					int x = GET_X_LPARAM(lParam);
					int y = GET_Y_LPARAM(lParam);

					UpdateHighlightedAddress(x, y, false);
				}
			}
			return 0;

		case WM_LBUTTONDOWN:
			{
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);

				SetFocus(mhwnd);
				UpdateHighlightedAddress(x, y, true);
			}
			return 0;

		case WM_MOUSELEAVE:
			if (mHighlightedAddress.has_value() && !mbSelectionEnabled) {
				InvalidateAddress(mHighlightedAddress.value());
				mHighlightedAddress.reset();
			}
			return 0;

		case WM_MOUSEWHEEL:
			OnMouseWheel(LOWORD(wParam), (float)(SHORT)HIWORD(wParam) / (float)WHEEL_DELTA);
			return 0;

		case WM_CONTEXTMENU:
			{
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);

				POINT pt = {x, y};
				if (x == -1 && y == -1) {
					// keyboard activation - just use top-left of text area
					pt.x = mTextArea.left;
					pt.y = mTextArea.top;

					auto [cx, cy] = TransformClientToScreen(vdpoint32(pt.x, pt.y));
					x = cx;
					y = cy;
				} else {
					// mouse activation
					ScreenToClient(mhwnd, &pt);
				}

				uint32 addr;
				bool isData;
				const bool addrValid = GetAddressFromPoint(pt.x, pt.y, addr, isData);

				HMENU hSubMenu = GetSubMenu(mMenu, 0);

				VDEnableMenuItemByCommandW32(hSubMenu, ID_CONTEXT_TOGGLEREADBREAKPOINT, addrValid);
				VDEnableMenuItemByCommandW32(hSubMenu, ID_CONTEXT_TOGGLEWRITEBREAKPOINT, addrValid);
				VDEnableMenuItemByCommandW32(hSubMenu, ID_CONTEXT_ADDTOWATCHWINDOW, addrValid);
				VDEnableMenuItemByCommandW32(hSubMenu, ID_CONTEXT_ADDTOSCREENWATCH, addrValid);

				VDCheckRadioMenuItemByCommandW32(hSubMenu, ID_SHOWVALUESAS_BYTES, mValueMode == ValueMode::HexBytes);
				VDCheckRadioMenuItemByCommandW32(hSubMenu, ID_SHOWVALUESAS_WORDS, mValueMode == ValueMode::HexWords);
				VDCheckRadioMenuItemByCommandW32(hSubMenu, ID_SHOWVALUESAS_DECBYTES, mValueMode == ValueMode::DecBytes);
				VDCheckRadioMenuItemByCommandW32(hSubMenu, ID_SHOWVALUESAS_DECWORDS, mValueMode == ValueMode::DecWords);

				VDCheckRadioMenuItemByCommandW32(hSubMenu, ID_CONTEXT_INTERPRET_NONE, mInterpretMode == InterpretMode::None);
				VDCheckRadioMenuItemByCommandW32(hSubMenu, ID_CONTEXT_INTERPRET_ATASCII, mInterpretMode == InterpretMode::Atascii);
				VDCheckRadioMenuItemByCommandW32(hSubMenu, ID_CONTEXT_INTERPRET_INTERNAL, mInterpretMode == InterpretMode::Internal);
				VDCheckRadioMenuItemByCommandW32(hSubMenu, ID_CONTEXT_INTERPRET_1BPPFONT, mInterpretMode == InterpretMode::Font1Bpp);
				VDCheckRadioMenuItemByCommandW32(hSubMenu, ID_CONTEXT_INTERPRET_2BPPFONT, mInterpretMode == InterpretMode::Font2Bpp);
				VDCheckRadioMenuItemByCommandW32(hSubMenu, ID_CONTEXT_INTERPRET_1BPPGFX, mInterpretMode == InterpretMode::Graphics1Bpp);
				VDCheckRadioMenuItemByCommandW32(hSubMenu, ID_CONTEXT_INTERPRET_2BPPGFX, mInterpretMode == InterpretMode::Graphics2Bpp);
				VDCheckRadioMenuItemByCommandW32(hSubMenu, ID_CONTEXT_INTERPRET_4BPPGFX, mInterpretMode == InterpretMode::Graphics4Bpp);
				VDCheckRadioMenuItemByCommandW32(hSubMenu, ID_CONTEXT_INTERPRET_8BPPGFX, mInterpretMode == InterpretMode::Graphics8Bpp);

				UINT id = TrackPopupMenu(hSubMenu, TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD, x, y, 0, mhwnd, NULL);

				IATDebugger& d = *ATGetDebugger();

				switch(id) {
					case ID_SHOWVALUESAS_BYTES:
						SetValueMode(ValueMode::HexBytes);
						break;

					case ID_SHOWVALUESAS_WORDS:
						SetValueMode(ValueMode::HexWords);
						break;

					case ID_SHOWVALUESAS_DECBYTES:
						SetValueMode(ValueMode::DecBytes);
						break;

					case ID_SHOWVALUESAS_DECWORDS:
						SetValueMode(ValueMode::DecWords);
						break;

					case ID_CONTEXT_RESETZOOM:
						if (mZoomFactor != 1.0f) {
							mZoomFactor = 1.0f;

							RecreateContentFont();
						}
						break;

					case ID_CONTEXT_INTERPRET_NONE:
						SetInterpretMode(InterpretMode::None);
						break;

					case ID_CONTEXT_INTERPRET_ATASCII:
						SetInterpretMode(InterpretMode::Atascii);
						break;

					case ID_CONTEXT_INTERPRET_INTERNAL:
						SetInterpretMode(InterpretMode::Internal);
						break;

					case ID_CONTEXT_INTERPRET_1BPPFONT:
						SetInterpretMode(InterpretMode::Font1Bpp);
						break;

					case ID_CONTEXT_INTERPRET_2BPPFONT:
						SetInterpretMode(InterpretMode::Font2Bpp);
						break;

					case ID_CONTEXT_INTERPRET_1BPPGFX:
						SetInterpretMode(InterpretMode::Graphics1Bpp);
						break;

					case ID_CONTEXT_INTERPRET_2BPPGFX:
						SetInterpretMode(InterpretMode::Graphics2Bpp);
						break;

					case ID_CONTEXT_INTERPRET_4BPPGFX:
						SetInterpretMode(InterpretMode::Graphics4Bpp);
						break;

					case ID_CONTEXT_INTERPRET_8BPPGFX:
						SetInterpretMode(InterpretMode::Graphics8Bpp);
						break;

					case ID_CONTEXT_TOGGLEREADBREAKPOINT:
						if (addrValid)
							d.ToggleAccessBreakpoint(addr, false);
						break;

					case ID_CONTEXT_TOGGLEWRITEBREAKPOINT:
						if (addrValid)
							d.ToggleAccessBreakpoint(addr, true);
						break;

					case ID_CONTEXT_ADDTOWATCHWINDOW:
						ATActivateUIPane(kATUIPaneId_WatchN + 0, true);

						if (auto *watchPane = ATGetUIPaneAs<IATUIDebuggerWatchPane>(kATUIPaneId_WatchN + 0)) {
							VDStringA s;

							if (mValueMode == ValueMode::HexWords)
								s.sprintf("dw $%04X", addr);
							else
								s.sprintf("db $%04X", addr);

							watchPane->AddWatch(s.c_str());
						}
						
						break;

					case ID_CONTEXT_ADDTOSCREENWATCH:
						if (d.AddWatch(addr, mValueMode == ValueMode::HexWords ? 2 : 1) < 0)
							ATUIShowError2((VDGUIHandle)GetHandleW32(), L"The limit of on-screen watches has already been reached.", L"Too many watches");
						break;
				}
			}

			return 0;
	}

	return ATUIDebuggerPaneWindow::WndProc(msg, wParam, lParam);
}

bool ATMemoryWindow::OnCreate() {
	if (!ATUIDebuggerPaneWindow::OnCreate())
		return false;

	if (!mControlPanel.Create((VDGUIHandle)mhwnd))
		return false;

	mControlPanel.SetAddressText(VDTextU8ToW(ATGetDebugger()->GetAddressText(mViewStart, true)).c_str());

	HWND hwndScrollBar = CreateWindowEx(0, WC_SCROLLBAR, _T(""), WS_VISIBLE|WS_CHILD|SBS_VERT, 0, 0, 0, 0, mhwnd, (HMENU)102, VDGetLocalModuleHandleW32(), nullptr);
	if (!hwndScrollBar)
		return false;

	mScrollBar.Attach(hwndScrollBar);
	mDispatcher.AddControl(&mScrollBar);

	HWND hwndScrollBar2 = CreateWindowEx(0, WC_SCROLLBAR, _T(""), WS_VISIBLE|WS_CHILD|SBS_HORZ, 0, 0, 0, 0, mhwnd, (HMENU)103, VDGetLocalModuleHandleW32(), nullptr);
	if (!hwndScrollBar2)
		return false;
	mHScrollBar.Attach(hwndScrollBar2);
	mDispatcher.AddControl(&mHScrollBar);

	UpdateScrollRange();

	OnFontsUpdated();

	OnSize();
	ATGetDebugger()->AddClient(this, true);
	return true;
}

void ATMemoryWindow::OnDestroy() {
	ATGetDebugger()->RemoveClient(this);

	if (mhfontContents) {
		DeleteObject(mhfontContents);
		mhfontContents = nullptr;
	}

	mControlPanel.Destroy();

	ATUIDebuggerPaneWindow::OnDestroy();
}

void ATMemoryWindow::OnSize() {
	RECT r;
	if (!GetClientRect(mhwnd, &r))
		return;

	RECT rAddr = {0};

	sint32 panelHeight = mControlPanel.GetDesiredHeight();

	mControlPanel.SetArea(vdrect32(0, 0, r.right, panelHeight));
	mControlPanel.SetColumns(mColumns);

	uint32 dpi = ATUIGetWindowDpiW32(mhwnd);
	uint32 globalDpi = ATUIGetGlobalDpiW32();
	int rawVSWidth = GetSystemMetrics(SM_CXVSCROLL);
	int rawHSHeight = GetSystemMetrics(SM_CYHSCROLL);
	int vsWidth = dpi && globalDpi ? VDRoundToInt((float)rawVSWidth * (float)dpi / (float)globalDpi) : rawVSWidth;
	int hsHeight = dpi && globalDpi ? VDRoundToInt((float)rawHSHeight * (float)dpi / (float)globalDpi) : rawHSHeight;
	
	mTextArea.left = 0;
	mTextArea.top = panelHeight;
	mTextArea.right = r.right - vsWidth;
	mTextArea.bottom = r.bottom - hsHeight;

	if (mTextArea.bottom < mTextArea.top)
		mTextArea.bottom = mTextArea.top;

	if (mTextArea.right < mTextArea.left)
		mTextArea.right = mTextArea.left;

	mScrollBar.SetArea(vdrect32(mTextArea.right, mTextArea.top, mTextArea.right + vsWidth, mTextArea.bottom));
	mHScrollBar.SetArea(vdrect32(0, mTextArea.bottom, mTextArea.right, mTextArea.bottom + hsHeight));

	mCompletelyVisibleRows = (mTextArea.bottom - mTextArea.top) / mLineHeight;
	mPartiallyVisibleRows = (mTextArea.bottom - mTextArea.top + mLineHeight - 1) / mLineHeight;

	mScrollBar.SetPageSize(std::max<sint32>(mCompletelyVisibleRows, 1));
	mHScrollBar.SetPageSize(mTextArea.right - mTextArea.left);

	mbViewValid = false;
	RemakeView(mViewStart, false);
}

void ATMemoryWindow::OnFontsUpdated() {
	RecreateContentFont();
}

void ATMemoryWindow::RecreateContentFont() {
	if (mhfontContents) {
		DeleteObject(mhfontContents);
		mhfontContents = nullptr;
	}

	if (mZoomFactor != 1.0f) {
		LOGFONT lf {};
		int ptsize = 0;

		ATConsoleGetFont(lf, ptsize);

		lf.lfHeight = std::min<int>(-1, VDRoundToInt((float)lf.lfHeight * mZoomFactor));

		mhfontContents = CreateFontIndirect(&lf);
	}

	UpdateContentFontMetrics();
}

void ATMemoryWindow::UpdateContentFontMetrics() {
	bool doUpdates = false;

	HDC hdc = GetDC(mhwnd);
	if (hdc) {
		HGDIOBJ hOldFont = SelectObject(hdc, mhfontContents ? mhfontContents : ATGetConsoleFontW32());
		if (hOldFont) {
			TEXTMETRIC tm = {0};
			if (GetTextMetrics(hdc, &tm)) {
				mCharWidth = tm.tmAveCharWidth;
				mCharHeight = tm.tmHeight;

				doUpdates = true;
			}

			SelectObject(hdc, hOldFont);
		}

		ReleaseDC(mhwnd, hdc);
	}

	if (doUpdates) {
		UpdateLineHeight();
		UpdateHScrollRange();
		Invalidate();
	}
}

void ATMemoryWindow::UpdateLineHeight() {
	uint32 lineHeight = mCharHeight;

	switch(mInterpretMode) {
		case InterpretMode::Font1Bpp:
		case InterpretMode::Font2Bpp:
			{
				// compute min size as 24x24 characters at 96 dpi, scaling up for high DPI (this may be 0 if we couldn't
				// get window DPI -- that's fine)
				uint32 minHeight = VDRoundToInt((float)ATUIGetWindowDpiW32(mhwnd) * 24.0f / 96.0f * mZoomFactor);

				// use the larger of char height and graphic height, rounded up to a multiple of 8 for clean scaling
				lineHeight = (std::max<uint32>(minHeight, lineHeight) + 7) & ~7;
			}
			break;
	}

	if (mLineHeight != lineHeight) {
		mLineHeight = lineHeight;

		OnSize();
		Invalidate();
	}
}

void ATMemoryWindow::UpdateScrollRange() {
	mScrollBar.SetRange(0, 0x10000 / mColumns);
}

void ATMemoryWindow::UpdateHScrollRange() {
	// "ADDR:"
	sint32 numChars = GetAddressLength() + 1;

	// add in value data
	switch (mValueMode) {
		case ValueMode::HexBytes:
			numChars += 3 * mColumns;
			break;

		case ValueMode::HexWords:
			numChars += 5 * (mColumns >> 1);
			break;

		case ValueMode::DecBytes:
			numChars += 4 * mColumns;
			break;

		case ValueMode::DecWords:
			numChars += 6 * (mColumns >> 1);
			break;
	}

	// add in interpret data
	sint32 numPixels = 0;
	switch (mInterpretMode) {
		case InterpretMode::None:
			break;

		case InterpretMode::Atascii:
		case InterpretMode::Internal:
			numChars += 3 + mColumns;
			break;

		case InterpretMode::Font1Bpp:
		case InterpretMode::Font2Bpp:
			++numChars;
			numPixels += mLineHeight * (mColumns >> 3);
			break;

		case InterpretMode::Graphics1Bpp:
		case InterpretMode::Graphics2Bpp:
		case InterpretMode::Graphics4Bpp:
			++numChars;
			numPixels += mColumns * 8 * std::max<uint32>(mLineHeight / 2, 1);
			break;

		case InterpretMode::Graphics8Bpp:
			++numChars;
			numPixels += mColumns * 8 * std::max<uint32>(mLineHeight / 2, 1);
			break;
	}

	const sint32 hscrRange = numPixels + numChars * mCharWidth;

	mHScrollBar.SetRange(0, hscrRange);

	// We need to reapply this because the scrollbar will clamp the page size
	// if it exceeds the range, which means it can now be incorrect.
	const sint32 hscrPage = mTextArea.right - mTextArea.left;
	mHScrollBar.SetPageSize(hscrPage);

	// if we're scrolled past the new range, force a scroll
	const sint32 hscrMax = std::max(0, hscrRange - hscrPage);
	if (mHScrollPos > hscrMax)
		OnViewHScroll(hscrMax, false);
}

void ATMemoryWindow::AdjustColumnCount() {
	SetColumns(mColumns, true);
}

void ATMemoryWindow::OnThemeUpdated() {
	Invalidate();
}

void ATMemoryWindow::OnMouseWheel(uint32 keyMask, float clicks) {
	mWheelAccum += clicks;

	sint32 actions = (sint32)(mWheelAccum + (mWheelAccum < 0 ? -0.01f : 0.01f));
	mWheelAccum -= (float)actions;

	if (!actions)
		return;

	if (keyMask & MK_CONTROL) {
		float newZoomScale = std::clamp(mZoomFactor * powf(2.0f, 0.1f * actions), 0.1f, 10.0f);

		if (fabsf(newZoomScale - 1.0f) < 0.05f)
			newZoomScale = 1.0f;

		if (mZoomFactor != newZoomScale) {
			mZoomFactor = newZoomScale;

			RecreateContentFont();
		}
	} else {
		UINT linesPerAction = 3;
		if (SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &linesPerAction, FALSE)) {
			if (linesPerAction == WHEEL_PAGESCROLL)
				linesPerAction = mCompletelyVisibleRows;
		}

		sint32 deltaLines = actions * linesPerAction;

		if (deltaLines) {
			sint32 v = mScrollBar.GetValue();
		
			v -= deltaLines;
			mScrollBar.SetValue(v);

			OnViewScroll(mScrollBar.GetValue(), false);
		}
	}
}

void ATMemoryWindow::OnPaint() {
	PAINTSTRUCT ps;
	HDC hdc = ::BeginPaint(mhwnd, &ps);
	if (!hdc)
		return;

	int saveHandle = ::SaveDC(hdc);
	if (saveHandle) {
		auto& imghdr = mImageHeader;

		imghdr.bi.bmiHeader.biSize				= sizeof(BITMAPINFOHEADER);
		imghdr.bi.bmiHeader.biWidth				= mColumns;
		imghdr.bi.bmiHeader.biHeight			= 8;
		imghdr.bi.bmiHeader.biPlanes			= 1;
		imghdr.bi.bmiHeader.biBitCount			= 1;
		imghdr.bi.bmiHeader.biCompression		= BI_RGB;
		imghdr.bi.bmiHeader.biSizeImage			= 0;
		imghdr.bi.bmiHeader.biXPelsPerMeter		= 0;
		imghdr.bi.bmiHeader.biYPelsPerMeter		= 0;
		imghdr.bi.bmiHeader.biClrUsed			= 2;
		imghdr.bi.bmiHeader.biClrImportant		= 2;
		imghdr.bi.bmiColors[0] = RGBQUAD { 16, 16, 16, 0 };

		switch(mInterpretMode) {
			case InterpretMode::Font1Bpp:
				imghdr.palext[0] = RGBQUAD { 240, 240, 240, 0 };
				break;

			case InterpretMode::Font2Bpp:
				imghdr.bi.bmiHeader.biWidth				= mColumns >> 1;
				imghdr.bi.bmiHeader.biBitCount			= 8;
				imghdr.bi.bmiHeader.biClrUsed			= 4;
				imghdr.bi.bmiHeader.biClrImportant		= 4;
				imghdr.palext[0] = RGBQUAD {  91,  91,  91, 0 };
				imghdr.palext[1] = RGBQUAD { 165, 165, 165, 0 };
				imghdr.palext[2] = RGBQUAD { 240, 240, 240, 0 };
				break;

			case InterpretMode::Graphics1Bpp:
				imghdr.bi.bmiHeader.biWidth = mColumns << 3;
				imghdr.bi.bmiHeader.biHeight = 1;
				imghdr.palext[0] = RGBQUAD { 240, 240, 240, 0 };
				break;

			case InterpretMode::Graphics2Bpp:
				imghdr.bi.bmiHeader.biWidth = mColumns << 2;
				imghdr.bi.bmiHeader.biHeight = 1;
				imghdr.bi.bmiHeader.biBitCount = 8;
				imghdr.bi.bmiHeader.biClrUsed = 4;
				imghdr.bi.bmiHeader.biClrImportant = 4;
				imghdr.palext[0] = RGBQUAD {  91,  91,  91, 0 };
				imghdr.palext[1] = RGBQUAD { 165, 165, 165, 0 };
				imghdr.palext[2] = RGBQUAD { 240, 240, 240, 0 };
				break;

			case InterpretMode::Graphics4Bpp:
				imghdr.bi.bmiHeader.biWidth = mColumns << 1;
				imghdr.bi.bmiHeader.biHeight = 1;
				imghdr.bi.bmiHeader.biBitCount = 4;
				imghdr.bi.bmiHeader.biClrUsed = 16;
				imghdr.bi.bmiHeader.biClrImportant = 16;

				for(int i=0; i<15; ++i) {
					BYTE c = (BYTE)(16 + (240 * i + 7) / 15);

					imghdr.palext[i] = RGBQUAD { c, c, c, 0 };
				}
				break;

			case InterpretMode::Graphics8Bpp:
				imghdr.bi.bmiHeader.biWidth = mColumns;
				imghdr.bi.bmiHeader.biHeight = 1;
				imghdr.bi.bmiHeader.biBitCount = 8;
				imghdr.bi.bmiHeader.biClrUsed = 256;
				imghdr.bi.bmiHeader.biClrImportant = 256;
		
				imghdr.bi.bmiColors[0] = RGBQUAD { 0, 0, 0, 0 };

				for(int i=0; i<255; ++i) {
					BYTE c = (BYTE)(i + 1);

					imghdr.palext[i] = RGBQUAD { c, c, c, 0 };
				}
				break;
		}

		const uint32 imgw = imghdr.bi.bmiHeader.biWidth;

		const ATUIThemeColors& tc = ATUIGetThemeColors();

		COLORREF normalTextColor = VDSwizzleU32(tc.mContentFg) >> 8;
		COLORREF changedTextColor = ((normalTextColor & 0xFEFEFE) >> 1) | 0x0000FF;
		COLORREF normalBkColor = VDSwizzleU32(tc.mContentBg) >> 8;
		COLORREF highlightedBkColor = VDSwizzleU32(mbSelectionEnabled ? tc.mHighlightedBg : tc.mInactiveHiBg) >> 8;
		COLORREF highlightedTextColor = VDSwizzleU32(mbSelectionEnabled ? tc.mHighlightedFg : tc.mInactiveHiFg) >> 8;

		::IntersectClipRect(hdc, mTextArea.left, mTextArea.top, mTextArea.right, mTextArea.bottom);

		::SelectObject(hdc, mhfontContents ? mhfontContents : ATGetConsoleFontW32());
		::SetTextAlign(hdc, TA_TOP | TA_LEFT);
		::SetBkMode(hdc, TRANSPARENT);
		::SetBkColor(hdc, normalBkColor);
		::SetStretchBltMode(hdc, STRETCH_DELETESCANS);

		int rowStart	= (ps.rcPaint.top - mTextArea.top) / mLineHeight;
		int rowEnd		= (ps.rcPaint.bottom - mTextArea.top + mLineHeight - 1) / mLineHeight;

		int textOffsetY = ((int)mLineHeight - (int)mCharHeight) >> 1;

		uint32 incMask = ATAddressGetSpaceSize(mViewStart) - 1;

		IATDebugger *debugger = ATGetDebugger();

		const uint32 viewDataSize = (uint32)mViewData.size();
		uint32 addrBase = mViewStart & ~incMask;
		for(int rowIndex = rowStart; rowIndex < rowEnd; ++rowIndex) {
			uint32 addr = addrBase + ((mViewStart + rowIndex * mColumns) & incMask);

			mTempLine.sprintf("%s:", debugger->GetAddressText(addr, false).c_str());
			uint32 startCol = (uint32)mTempLine.size();

			mTempLine2.clear();
			mTempLine2.resize(startCol, ' ');

			uint32 changeOffset = (uint32)(addr - mChangedBitsStartAddress);
			uint32 changeWordOffset = changeOffset >> 5;
			uint32 changeBitOffset = changeOffset & 31;

			// The change detection routine pads the change mask to ensure that any range that might include
			// set bits is mappable as a full row within the change mask; if we are even partially outside
			// of the change mask, it means that all bits within it are guaranteed 0 and we can use this
			// null mask instead.
			uint32 changeMask[9] {};

			if (changeWordOffset < mChangedBits.size() && changeWordOffset + 9 < mChangedBits.size()) {
				const uint32 *changeSrc = &mChangedBits[changeWordOffset];

				// If we have a bit offset, preshift the mask so we don't have to worry about spanning
				// bitmasks when testing bit pairs for words.
				if (changeBitOffset) {
					for(int i=0; i<9; ++i)
						changeMask[i] = (changeSrc[i] >> changeBitOffset) + (changeSrc[i + 1] << (32 - changeBitOffset));
					
				} else {
					for(int i=0; i<9; ++i)
						changeMask[i] = changeSrc[i];
				}
			}

			uint32 viewOffset = addr - mViewDataStart;
			if (viewOffset < viewDataSize && viewDataSize - viewOffset < mColumns) {
				memcpy(mRenderLineData, &mViewData[viewOffset], mColumns);
			} else {
				for(uint32 i=0; i<mColumns; ++i) {
					if (viewOffset < viewDataSize)
						mRenderLineData[i] = mViewData[viewOffset];
					else
						mRenderLineData[i] = 0;

					++viewOffset;
				}
			}

			if (mHighlightedAddress.has_value() && mbSelectionEnabled && mEditValue >= 0) {
				uint32 hiAddr = mHighlightedAddress.value();

				if (hiAddr >= addr) {
					uint32 editOffset = hiAddr - addr;
					if (editOffset < mColumns) {
						mRenderLineData[editOffset] = (uint8)(mEditValue & 0xFF);

						if (!mbHighlightedData && (mValueMode == ValueMode::HexWords || mValueMode == ValueMode::DecWords) && editOffset + 1 < mColumns)
							mRenderLineData[editOffset + 1] = (uint8)((mEditValue >> 8) & 0xFF);
					}
				}
			}

			const uint32 *changeSrc = changeMask;

			switch(mValueMode) {
				case ValueMode::HexBytes:
					for(uint32 i=0, changeBit = 1; i<mColumns; ++i) {
						if (*changeSrc & changeBit) {
							mTempLine += "   ";
							mTempLine2.append_sprintf(" %02X", mRenderLineData[i]);
						} else {
							mTempLine.append_sprintf(" %02X", mRenderLineData[i]);
							mTempLine2 += "   ";
						}

						changeBit += changeBit;
						if (!changeBit) {
							changeBit = 1;
							++changeSrc;
						}
					}
					break;

				case ValueMode::DecBytes:
					for(uint32 i=0, changeBit = 1; i<mColumns; ++i) {
						if (*changeSrc & changeBit) {
							mTempLine += "    ";
							mTempLine2.append_sprintf(" %3d", mRenderLineData[i]);
						} else {
							mTempLine.append_sprintf(" %3d", mRenderLineData[i]);
							mTempLine2 += "    ";
						}

						changeBit += changeBit;
						if (!changeBit) {
							changeBit = 1;
							++changeSrc;
						}
					}
					break;

				case ValueMode::HexWords:
					for(uint32 i=0, wcols = mColumns >> 1, changeBits = 3; i<wcols; ++i) {
						if (*changeSrc & changeBits) {
							mTempLine += "     ";
							mTempLine2.append_sprintf(" %04X", VDReadUnalignedLEU16(&mRenderLineData[i*2]));
						} else {
							mTempLine.append_sprintf(" %04X", VDReadUnalignedLEU16(&mRenderLineData[i*2]));
							mTempLine2 += "     ";
						}

						changeBits <<= 2;
						if (!changeBits) {
							changeBits = 3;
							++changeSrc;
						}
					}
					break;

				case ValueMode::DecWords:
					for(uint32 i=0, wcols = mColumns >> 1, changeBits = 3; i<wcols; ++i) {
						if (*changeSrc & changeBits) {
							mTempLine += "      ";
							mTempLine2.append_sprintf(" %5d", VDReadUnalignedLEU16(&mRenderLineData[i*2]));
						} else {
							mTempLine.append_sprintf(" %5d", VDReadUnalignedLEU16(&mRenderLineData[i*2]));
							mTempLine2 += "      ";
						}

						changeBits <<= 2;
						if (!changeBits) {
							changeBits = 3;
							++changeSrc;
						}
					}
					break;
			}

			uint32 interpretCol = (uint32)mTempLine.size();
			if (mInterpretMode == InterpretMode::Atascii || mInterpretMode == InterpretMode::Internal) {
				mTempLine += " |";
				for(uint32 i=0; i<mColumns; ++i) {
					uint8 c = mRenderLineData[i];

					if (mInterpretMode == InterpretMode::Internal) {
						static constexpr uint8 kXorTab[] = { 0x20, 0x60, 0x40, 0x00 };

						c ^= kXorTab[(c >> 5) & 3];
					}

					if ((uint8)(c - 0x20) >= 0x5f)
						c = '.';

					mTempLine += c;
				}

				mTempLine += '|';
			}

			RECT rLine;
			rLine.left = ps.rcPaint.left;
			rLine.top = mTextArea.top + mLineHeight * rowIndex;
			rLine.right = ps.rcPaint.right;
			rLine.bottom = rLine.top + mLineHeight;

			::SetTextColor(hdc, normalTextColor);
			::ExtTextOutA(hdc, mTextArea.left - mHScrollPos, rLine.top + textOffsetY, ETO_OPAQUE, &rLine, mTempLine.data(), mTempLine.size(), NULL);

			if (changeMask) {
				::SetTextColor(hdc, changedTextColor);
				::ExtTextOutA(hdc, mTextArea.left - mHScrollPos, rLine.top + textOffsetY, 0, NULL, mTempLine2.data(), mTempLine2.size(), NULL);
			}

			if (mInterpretMode >= InterpretMode::Font1Bpp) {
				uint32 ix = mTextArea.left + (interpretCol + 1) * mCharWidth - mHScrollPos;
				uint32 iy = rLine.top;

				if (mInterpretMode == InterpretMode::Font1Bpp) {
					// compute dword-aligned pitch
					uint32 pitch = ((mColumns >> 3) + 3) & ~3;

					for(int i=0; i<8; ++i) {
						uint8 *dst = &mRenderImageData[i * pitch];
						const uint8 *src = &mRenderLineData[7-i];

						for(uint32 j = 0; j < mColumns; j += 8) 
							*dst++ = src[j];
					}

					::StretchDIBits(hdc, ix, iy, mLineHeight*(mColumns >> 3), mLineHeight, 0, 0, imgw, 8, mRenderImageData, &imghdr.bi, DIB_RGB_COLORS, SRCCOPY);
				} else if (mInterpretMode == InterpretMode::Font2Bpp) {
					uint8 *dst = mRenderImageData;
					for(int i=0; i<8; ++i) {
						const uint8 *src = &mRenderLineData[7-i];

						for(uint32 j = 0; j < mColumns; j += 8) {
							uint8 c = src[j];

							for(int k=0; k<4; ++k)
								*dst++ = (c >> (6 - 2*k)) & 3;
						}
					}

					::StretchDIBits(hdc, ix, iy, mLineHeight*(mColumns >> 3), mLineHeight, 0, 0, imgw, 8, mRenderImageData, &imghdr.bi, DIB_RGB_COLORS, SRCCOPY);
				} else if (mInterpretMode == InterpretMode::Graphics1Bpp) {
					uint32 pixelScale = std::max<uint32>(mLineHeight / 2, 1);
					::StretchDIBits(hdc, ix, iy + (mLineHeight - pixelScale) / 2, pixelScale*imgw, pixelScale, 0, 0, imgw, 1, mRenderLineData, &imghdr.bi, DIB_RGB_COLORS, SRCCOPY);
				} else if (mInterpretMode == InterpretMode::Graphics2Bpp) {
					uint8 *dst = mRenderImageData;
					for(uint32 i=0; i<mColumns; ++i) {
						uint8 c = mRenderLineData[i];

						dst[0] = (c >> 6) & 3;
						dst[1] = (c >> 4) & 3;
						dst[2] = (c >> 2) & 3;
						dst[3] = (c >> 0) & 3;
						dst += 4;
					}

					uint32 pixelScale = std::max<uint32>(mLineHeight / 2, 1);
					::StretchDIBits(hdc, ix, iy + (mLineHeight - pixelScale) / 2, pixelScale*imgw*2, pixelScale, 0, 0, imgw, 1, mRenderImageData, &imghdr.bi, DIB_RGB_COLORS, SRCCOPY);
				} else if (mInterpretMode == InterpretMode::Graphics4Bpp) {
					uint32 pixelScale = std::max<uint32>(mLineHeight / 2, 1);
					::StretchDIBits(hdc, ix, iy + (mLineHeight - pixelScale) / 2, pixelScale*imgw*4, pixelScale, 0, 0, imgw, 1, mRenderLineData, &imghdr.bi, DIB_RGB_COLORS, SRCCOPY);
				} else if (mInterpretMode == InterpretMode::Graphics8Bpp) {
					uint32 pixelScale = std::max<uint32>(mLineHeight / 2, 1);
					::StretchDIBits(hdc, ix, iy + (mLineHeight - pixelScale) / 2, pixelScale*imgw, pixelScale, 0, 0, imgw, 1, mRenderLineData, &imghdr.bi, DIB_RGB_COLORS, SRCCOPY);
				}
			}

			if (mHighlightedAddress.has_value()) {
				const uint32 hiAddr = mHighlightedAddress.value();

				if ((uint32)(hiAddr - addr) < mColumns) {
					uint32 hiOffset = hiAddr - addr;

					::SetBkColor(hdc, highlightedBkColor);
					::SetTextColor(hdc, highlightedTextColor);

					RECT rHiRect = rLine;

					if (mbHighlightedData) {
						if (mInterpretMode == InterpretMode::Atascii || mInterpretMode == InterpretMode::Internal) {
							rHiRect.left = mTextArea.left + (interpretCol + 2 + hiOffset) * mCharWidth - mHScrollPos;
							rHiRect.right = rHiRect.left + mCharWidth;
							::ExtTextOutA(hdc, rHiRect.left, rLine.top + textOffsetY, ETO_OPAQUE, &rHiRect, ((changeMask[hiOffset >> 4] & (3 << (hiOffset & 31))) ? mTempLine2.c_str() : mTempLine.c_str()) + interpretCol + 2 + hiOffset, 1, nullptr);
						}
					} else {
						int charsPerEntry = 0;
						int elementOffset = hiOffset;

						if (mValueMode == ValueMode::HexWords) {
							hiOffset &= ~(int)1;
							elementOffset >>= 1;
							charsPerEntry = 4;
						} else if (mValueMode == ValueMode::DecWords) {
							hiOffset &= ~(int)1;
							elementOffset >>= 1;
							charsPerEntry = 5;
						} else if (mValueMode == ValueMode::DecBytes) {
							charsPerEntry = 3;
						} else {
							charsPerEntry = 2;
						}

						if (charsPerEntry) {
							rHiRect.left = mTextArea.left + (startCol + 1 + (charsPerEntry + 1) * elementOffset) * mCharWidth - mHScrollPos;
							rHiRect.right = rHiRect.left + charsPerEntry * mCharWidth;

							::ExtTextOutA(hdc,
								rHiRect.left,
								rLine.top + textOffsetY,
								ETO_OPAQUE,
								&rHiRect,
								((changeMask[elementOffset >> 5] & (1 << (elementOffset & 31))) ? mTempLine2.c_str() : mTempLine.c_str()) + startCol + 1 + (charsPerEntry + 1) * elementOffset,
								charsPerEntry,
								nullptr);
						}
					}

					::SetBkColor(hdc, normalBkColor);
				}
			}
		}

		::RestoreDC(hdc, saveHandle);

		::ExcludeClipRect(hdc, mTextArea.left, mTextArea.top, mTextArea.right, mTextArea.bottom);
		::FillRect(hdc, &ps.rcPaint, ATUIGetThemeColorsW32().mStaticBgBrush);
	}

	::EndPaint(mhwnd, &ps);
}

void ATMemoryWindow::OnViewScroll(sint32 pos, [[maybe_unused]] bool tracking) {
	RemakeView((mViewStart & 0xFFFF0000) + (mViewStart & 0xFFFF) % mColumns + pos * mColumns, true);
}

void ATMemoryWindow::OnViewHScroll(sint32 pos, [[maybe_unused]] bool tracking) {
	sint32 delta = pos - mHScrollPos;
	if (!delta)
		return;

	mHScrollPos = pos;

	::ScrollWindow(mhwnd, -delta, 0, &mTextArea, &mTextArea);
}

struct ATWrapRange32 {
	uint32 mStart;
	uint32 mLen;

	bool operator==(const ATWrapRange32&) const = default;

	ATWrapRange32 operator&(const ATWrapRange32& other) const {
		ATWrapRange32 result { 0, 0 };

		const uint32 offset = other.mStart - mStart;

		if (offset < mLen) {
			result = { other.mStart, std::min<uint32>(mLen - offset, other.mLen) };
		} else if ((uint32)(0U - offset) < other.mLen) {
			result = { mStart, std::min<uint32>(other.mLen + offset, mLen) };
		}

		if (!result.mLen)
			result.mStart = 0;

		return result;
	}
};

void ATMemoryWindow::RemakeView(uint32 focusAddr, bool fromScroll) {
	bool changed = false;

	if (mViewStart != focusAddr || !mbViewValid) {
		mViewStart = focusAddr;
		mbViewValid = true;
		changed = true;
	}

	const uint32 rows = mPartiallyVisibleRows;
	const uint32 len = rows * mColumns;

	// check if the tick has changed, and if so, snapshot the last data as old data
	bool copyOldToRef = false;

	if (mViewDataCycle != mCurrentCycle) {
		mViewDataCycle = mCurrentCycle;

		mOldViewData.swap(mViewData);
		mOldViewDataStart = mViewDataStart;
		copyOldToRef = true;
	}

	// read current view data
	mViewData.clear();
	mViewData.resize(len, 0);
	
	ATGetDebugger()->GetTarget()->DebugReadMemory(mViewStart, mViewData.data(), len);

	// scroll the reference view window to the new view
	ATWrapRange32 newViewRange { mViewStart, len };
	ATWrapRange32 refViewRange { mViewDataStart, (uint32)mRefViewData.size() };
	ATWrapRange32 oldViewRange { mOldViewDataStart, (uint32)mOldViewData.size() };

	if (refViewRange != newViewRange) {
		// start with new data
		mRefViewData2 = mViewData;

		// copy over previous reference data, if there is overlap
		ATWrapRange32 refCommonRange = newViewRange & refViewRange;
		if (refCommonRange.mLen) {
			memcpy(
				mRefViewData2.data() + (refCommonRange.mStart - newViewRange.mStart),
				mRefViewData .data() + (refCommonRange.mStart - refViewRange.mStart),
				refCommonRange.mLen
			);
		}

		mRefViewData.swap(mRefViewData2);
		copyOldToRef = true;
	}

	// copy over old data to reference
	if (copyOldToRef) {
		ATWrapRange32 oldCommonRange = oldViewRange & newViewRange;

		if (oldCommonRange.mLen) {
			memcpy(
				mRefViewData.data() + (oldCommonRange.mStart - newViewRange.mStart),
				mOldViewData.data() + (oldCommonRange.mStart - oldViewRange.mStart),
				oldCommonRange.mLen
			);
		}
	}

	const uint32 newViewStart = mViewStart;
	const uint32 newViewEnd = mViewStart + len;
	const uint32 oldViewLen = (uint32)mOldViewData.size();
	const uint32 oldViewStart = mOldViewDataStart;
	const uint32 oldViewEnd = mOldViewDataStart + oldViewLen;

	// if the new view range is not contained within the old view range, there
	// is a change
	if (newViewStart < oldViewStart || newViewEnd > oldViewEnd)
		changed = true;

	// overallocate the array to ensure that a full 256 bits + alignment bits is
	// always available on either side regardless of the start offset -- this ensures
	// that the row handler can avoid needing to range check in the
	// middle of a row
	const uint32 requiredMaskWords = ((len + 31) >> 5) + 18;

	// compute new change mask
	mNewChangedBits.clear();
	mNewChangedBits.resize(requiredMaskWords, 0);
	mChangedBitsStartAddress = mViewStart - 9*32;

	{
		const uint8 *VDRESTRICT ref = mRefViewData.data();
		const uint8 *VDRESTRICT src = mViewData.data();
		uint32 *VDRESTRICT p = mNewChangedBits.data() + 9;

		uint32 bit = 1;
		for(uint32 i=0; i<len; ++i) {
			if (ref[i] != src[i]) {
				*p += bit;
				changed = true;
			}

			bit += bit;
			if (!bit) {
				bit = 1;
				++p;
			}
		}
	}

	if (!changed && mChangedBits != mNewChangedBits) {
		changed = true;
	}

	mChangedBits.swap(mNewChangedBits);

	mViewDataStart = mViewStart;

	if (changed) {
		if (!fromScroll)
			mScrollBar.SetValue((sint32)((mViewStart & 0xFFFF) / mColumns));

		if (!mbSelectionEnabled)
			mHighlightedAddress.reset();

		::InvalidateRect(mhwnd, NULL, TRUE);
	}
}

int ATMemoryWindow::GetAddressLength() const {
	int addrLen = 4;

	switch(mViewStart & kATAddressSpaceMask) {
		case kATAddressSpace_CPU:
			if (mViewStart >= 0x010000)
				addrLen = 6;
			break;

		case kATAddressSpace_ANTIC:
		case kATAddressSpace_RAM:
		case kATAddressSpace_ROM:
			addrLen = 6;
			break;

		case kATAddressSpace_VBXE:
			addrLen = 7;
			break;

		case kATAddressSpace_EXTRAM:
			addrLen = 7;
			break;
	}

	return addrLen;
}

bool ATMemoryWindow::GetAddressFromPoint(int x, int y, uint32& addr, bool& isData) const {
	isData = false;

	if (!mLineHeight || !mCharWidth)
		return false;
	
	if ((uint32)(x - mTextArea.left) >= (uint32)(mTextArea.right - mTextArea.left))
		return false;

	if ((uint32)(y - mTextArea.top) >= (uint32)(mTextArea.bottom - mTextArea.top))
		return false;

	const int addrLen = GetAddressLength();

	int xc = (x - mTextArea.left + mHScrollPos) / mCharWidth - addrLen - 2;
	int yc = (y - mTextArea.top) / mLineHeight;

	// 0000: 00 00 00 00 00 00 00 00-00 00 00 00 00 00 00 00 |................|
	if (mValueMode == ValueMode::HexBytes) {
		if (xc >= 0 && xc < (int)mColumns * 3) {
			addr = mViewStart + (uint16)(yc * mColumns + xc/3);
			return true;
		}

		xc -= mColumns * 3;
	} else if (mValueMode == ValueMode::HexWords) {
		if (xc >= 0 && xc < (int)((mColumns * 5) / 2)) {
			addr = mViewStart + (uint16)(yc * mColumns + (xc/5) * 2);
			return true;
		}

		xc -= (mColumns * 5)/2;
	} else if (mValueMode == ValueMode::DecBytes) {
		if (xc >= 0 && xc < (int)mColumns * 4) {
			addr = mViewStart + (uint16)(yc * mColumns + xc/4);
			return true;
		}

		xc -= mColumns * 4;
	} else if (mValueMode == ValueMode::DecWords) {
		if (xc >= 0 && xc < (int)mColumns * 3) {
			addr = mViewStart + (uint16)(yc * mColumns + (xc/6) * 2);
			return true;
		}

		xc -= mColumns * 3;
	}

	--xc;
	if (xc >= 0 && xc < (int)mColumns && mInterpretMode != InterpretMode::None) {
		isData = true;
		addr = mViewStart + (uint16)(yc * mColumns + xc);
		return true;
	}

	return false;
}

void ATMemoryWindow::InvalidateAddress(uint32 addr) {
	uint32 offset = addr - mViewStart;
	uint32 row = offset / mColumns;

	uint32 visibleRows = ((uint32)(mTextArea.bottom - mTextArea.top) + mLineHeight - 1) / mLineHeight;

	if (row < visibleRows) {
		RECT r = mTextArea;
		r.top += mLineHeight * row;
		if (r.top < r.bottom) {
			r.bottom = r.top + mLineHeight;

			InvalidateRect(mhwnd, &r, TRUE);
		}
	}
}

void ATMemoryWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	if (mLastTarget != (uintptr)state.mpDebugTarget) {
		mLastTarget = (uintptr)state.mpDebugTarget;

		mbViewValid = false;
	}

	mCurrentCycle = state.mCycle;

	RemakeView(mViewStart, false);
}

void ATMemoryWindow::OnDebuggerEvent(ATDebugEvent eventId) {
	if (eventId == kATDebugEvent_MemoryChanged) {
		RemakeView(mViewStart, false);
	}
}

void ATMemoryWindow::SetPosition(uint32 addr) {
	mControlPanel.SetAddressText(VDTextU8ToW(ATGetDebugger()->GetAddressText(addr, true)).c_str());

	RemakeView(addr, false);
}

void ATMemoryWindow::SetColumns(uint32 cols, bool updateUI) {
	cols = std::clamp<uint32>(cols, 1, 256);

	if (mInterpretMode == InterpretMode::Font1Bpp || mInterpretMode == InterpretMode::Font2Bpp) {
		cols = (cols + 7) & ~7;
	}

	if (mValueMode == ValueMode::DecWords || mValueMode == ValueMode::HexWords) {
		cols = (cols + 1) & ~1;
	}

	if (mColumns != cols) {
		mColumns = cols;

		UpdateScrollRange();
		UpdateHScrollRange();

		mbViewValid = false;
		RemakeView(mViewStart, false);

		if (updateUI)
			mControlPanel.SetColumns(mColumns);
	}
}

void ATMemoryWindow::SetValueMode(ValueMode mode) {
	if (mValueMode == mode)
		return;

	mValueMode = mode;

	AdjustColumnCount();
	Invalidate();
}

void ATMemoryWindow::SetInterpretMode(InterpretMode mode) {
	if (mInterpretMode == mode)
		return;

	mInterpretMode = mode;

	AdjustColumnCount();
	UpdateLineHeight();
	UpdateHScrollRange();
	Invalidate();
}

void ATMemoryWindow::UpdateHighlightedAddress(int x, int y, bool select) {
	std::optional<uint32> highlightAddr;
	bool highlightData = false;
				
	uint32 addr;
	bool isData;
	if (GetAddressFromPoint(x, y, addr, isData)) {
		highlightAddr = addr;
		highlightData = isData;
	}

	SetHighlightedAddress(highlightAddr, highlightData, select);
}

void ATMemoryWindow::SetHighlightedAddress(std::optional<uint32> highlightAddr, bool highlightData, bool select) {
	if (mHighlightedAddress != highlightAddr || mbHighlightedData != highlightData || mbSelectionEnabled != select) {
		if (mHighlightedAddress.has_value())
			InvalidateAddress(mHighlightedAddress.value());

		mHighlightedAddress = highlightAddr;
		mbHighlightedData = highlightData;
		mbSelectionEnabled = select;
		mEditValue = -1;
		mEditPhase = 0;
		mbEditWord = false;

		if (!mbHighlightedData && (mValueMode == ValueMode::DecWords || mValueMode == ValueMode::HexWords))
			mbEditWord = true;

		if (highlightAddr.has_value())
			InvalidateAddress(highlightAddr.value());
	}
}

void ATMemoryWindow::BeginEdit() {
	if (mHighlightedAddress.has_value() && mbSelectionEnabled && mEditValue < 0) {
		uint8 buf[2] {};
		ATGetDebugger()->GetTarget()->ReadMemory(mHighlightedAddress.value(), buf, mbEditWord ? 2 : 1);

		mEditValue = VDReadUnalignedLEU16(buf);
		InvalidateAddress(mHighlightedAddress.value());
	}
}

void ATMemoryWindow::CancelEdit() {
	if (mHighlightedAddress.has_value() && mbSelectionEnabled && mEditValue >= 0) {
		mEditValue = -1;
		InvalidateAddress(mHighlightedAddress.value());
	}
}

void ATMemoryWindow::CommitEdit() {
	if (mHighlightedAddress.has_value() && mbSelectionEnabled && mEditValue >= 0) {
		uint8 buf[2] {};
		VDWriteUnalignedLEU16(buf, (uint16)mEditValue);

		const uint32 hiAddr = mHighlightedAddress.value();

		if (hiAddr >= mViewStart) {
			const uint32 hiOffset = hiAddr - mViewStart;

			if (hiOffset < mViewData.size()) {
				mViewData[hiOffset] = (uint8)(mEditValue & 0xFF);

				if (mbEditWord && hiOffset + 1 < mViewData.size())
					mViewData[hiOffset + 1] = (uint8)((mEditValue >> 8) & 0xFF);
			}
		}

		// the changed bits start address may be <0, so we actually need this to wrap
		const uint32 chOffset = hiAddr - mChangedBitsStartAddress;
		uint32 chIndex = chOffset >> 5;

		if (chIndex < mChangedBits.size())
			mChangedBits[chIndex] |= (1 << (chOffset & 31));

		ATGetDebugger()->GetTarget()->WriteMemory(hiAddr, buf, mbEditWord ? 2 : 1);

		InvalidateAddress(hiAddr);
	}
}

void ATUIDebuggerRegisterMemoryPane() {
	ATRegisterUIPaneClass(kATUIPaneId_MemoryN, ATUIPaneClassFactory<ATMemoryWindow, ATUIPane>);
}
