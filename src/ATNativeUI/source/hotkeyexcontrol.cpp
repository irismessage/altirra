//	Altirra - Atari 800/800XL/5200 emulator
//	UI library
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
#include <tchar.h>
#include <vd2/Dita/accel.h>
#include <vd2/system/color.h>
#include <vd2/system/refcount.h>
#include <vd2/system/w32assist.h>
#include <vd2/system/VDString.h>
#include <at/atnativeui/hotkeyexcontrol.h>

class VDUIHotKeyExControlW32 final : public vdrefcounted<IVDUIHotKeyExControl> {
	VDUIHotKeyExControlW32(const VDUIHotKeyExControlW32&) = delete;
	VDUIHotKeyExControlW32& operator=(const VDUIHotKeyExControlW32&) = delete;
public:
	VDUIHotKeyExControlW32(HWND hwnd);
	~VDUIHotKeyExControlW32();

	void *AsInterface(uint32 iid);

	void CaptureAnyNextKey();

	void SetCookedMode(bool enabled);

	void GetAccelerator(VDUIAccelerator& accel);
	void SetAccelerator(const VDUIAccelerator& accel);

	VDEvent<IVDUIHotKeyExControl, VDUIAccelerator>& OnChange() {
		return mEventOnChange;
	}

	static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	void OnPaint();
	void SetFont(HFONT font);
	void UpdateCaretPosition();
	void UpdateText();

	void ClearAnyKeyCapture(bool eatNextChar, bool eatNextKeyUp);

	void EnableMessageHook();
	void DisableMessageHook();
	static LRESULT CALLBACK SuppressSysCharHook(int code, WPARAM wParam, LPARAM lParam);

	const HWND mhwnd;
	HFONT mhfont {};
	int mFontHeight {};
	HHOOK mhHook = nullptr;

	bool mbEatNextInjectedCaps = false;
	bool mbTurnOffCaps = false;
	bool mbCookedMode = false;
	bool mbAllKeysEnabled = false;
	bool mbAllKeysEatNextKeyUp = false;
	bool mbAllKeysEatNextChar = false;

	VDUIAccelerator	mAccel;
	uint32		mCurrentMods;

	VDStringW	mBuffer;

	VDEvent<IVDUIHotKeyExControl, VDUIAccelerator> mEventOnChange;
};

bool VDUIRegisterHotKeyExControl() {
	WNDCLASS wc;

	wc.style		= CS_VREDRAW | CS_HREDRAW;
	wc.lpfnWndProc	= VDUIHotKeyExControlW32::StaticWndProc;
	wc.cbClsExtra	= 0;
	wc.cbWndExtra	= sizeof(IVDUnknown *);
	wc.hInstance	= VDGetLocalModuleHandleW32();
	wc.hIcon		= NULL;
	wc.hCursor		= LoadCursor(NULL, IDC_IBEAM);
	wc.hbrBackground= (HBRUSH)(COLOR_3DFACE + 1);
	wc.lpszMenuName	= NULL;
	wc.lpszClassName= VDUIHOTKEYEXCLASS;

	return RegisterClass(&wc) != 0;
}

IVDUIHotKeyExControl *VDGetUIHotKeyExControl(VDGUIHandle h) {
	return vdpoly_cast<IVDUIHotKeyExControl *>((IVDUnknown *)GetWindowLongPtr((HWND)h, 0));
}

VDUIHotKeyExControlW32::VDUIHotKeyExControlW32(HWND hwnd)
	: mhwnd(hwnd)
{
	mAccel.mVirtKey = 0;
	mAccel.mModifiers = 0;
	mCurrentMods = 0;
}

VDUIHotKeyExControlW32::~VDUIHotKeyExControlW32() {
}

void *VDUIHotKeyExControlW32::AsInterface(uint32 iid) {
	if (iid == IVDUIHotKeyExControl::kTypeID)
		return static_cast<IVDUIHotKeyExControl *>(this);
	return NULL;
}

void VDUIHotKeyExControlW32::CaptureAnyNextKey() {
	if (!mbAllKeysEnabled) {
		mbAllKeysEnabled = true;
		mbAllKeysEatNextKeyUp = false;
		mbAllKeysEatNextChar = false;

		InvalidateRect(mhwnd, nullptr, TRUE);

		SetFocus(mhwnd);

		if (GetFocus() == mhwnd)
			EnableMessageHook();
	}
}

void VDUIHotKeyExControlW32::SetCookedMode(bool enable) {
	mbCookedMode = enable;
}

void VDUIHotKeyExControlW32::GetAccelerator(VDUIAccelerator& accel) {
	accel = mAccel;
}

void VDUIHotKeyExControlW32::SetAccelerator(const VDUIAccelerator& accel) {
	if (mAccel == accel)
		return;

	mbCookedMode = (accel.mModifiers & VDUIAccelerator::kModCooked) != 0;
	mAccel = accel;
	mCurrentMods = (mCurrentMods & ~VDUIAccelerator::kModUp) | (accel.mModifiers & VDUIAccelerator::kModUp);

	UpdateText();
	UpdateCaretPosition();
}

LRESULT CALLBACK VDUIHotKeyExControlW32::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	VDUIHotKeyExControlW32 *pThis = (VDUIHotKeyExControlW32 *)GetWindowLongPtr(hwnd, 0);

	if (msg == WM_NCCREATE) {
		pThis = new VDUIHotKeyExControlW32(hwnd);

		if (!pThis)
			return FALSE;

		pThis->AddRef();
		SetWindowLongPtr(hwnd, 0, (LONG_PTR)pThis);
	} else if (msg == WM_NCDESTROY) {
		pThis->DisableMessageHook();
		pThis->Release();
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	return pThis->WndProc(msg, wParam, lParam);
}

LRESULT VDUIHotKeyExControlW32::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_NCCREATE:
			SetFont(NULL);
			break;

		case WM_GETDLGCODE:
			if (lParam) {
				const MSG *msg = (const MSG *)lParam;

				if (!mbAllKeysEnabled) {
					// don't eat Tab and Shift+Tab if we aren't capturing all keys
					switch(msg->message) {
						case WM_CHAR:
							if (!mbAllKeysEatNextChar && msg->wParam == 0x09)
								return 0;
							break;

						case WM_KEYDOWN:
							if (msg->wParam == VK_TAB)
								return 0;

							break;

						default:
							break;
					}
				}

				return DLGC_WANTMESSAGE;
			}

			return DLGC_WANTALLKEYS;

		case WM_ERASEBKGND:
			return FALSE;

		case WM_PAINT:
			OnPaint();
			return 0;

		case WM_GETFONT:
			return (LRESULT)wParam;

		case WM_SETFONT:
			SetFont((HFONT)wParam);
			if (LOWORD(lParam))
				InvalidateRect(mhwnd, NULL, TRUE);
			return 0;

		case WM_LBUTTONDOWN:
			SetFocus(mhwnd);
			return 0;

		case WM_SETFOCUS:
			CreateCaret(mhwnd, NULL, 0, mFontHeight);
			UpdateCaretPosition();
			ShowCaret(mhwnd);

			if (mbAllKeysEnabled)
				EnableMessageHook();
			break;

		case WM_KILLFOCUS:
			mCurrentMods &= ~(VDUIAccelerator::kModCtrl | VDUIAccelerator::kModShift | VDUIAccelerator::kModAlt);
			ClearAnyKeyCapture(false, false);

			HideCaret(mhwnd);
			DestroyCaret();
			DisableMessageHook();
			break;

		case WM_SYSCHAR:
			if (!mbAllKeysEnabled && !mbAllKeysEatNextChar)
				break;
			[[fallthrough]];
		case WM_CHAR:
			if (mbAllKeysEatNextChar) {
				mbAllKeysEatNextChar = false;
				return 0;
			}

			if (!mbCookedMode)
				break;

			if (mCurrentMods != VDUIAccelerator::kModCooked
				|| mAccel.mModifiers != VDUIAccelerator::kModCooked
				|| mAccel.mVirtKey != wParam)
			{
				mCurrentMods = VDUIAccelerator::kModCooked;
				mAccel.mVirtKey = (uint32)wParam;
				mAccel.mModifiers = mCurrentMods;

				ClearAnyKeyCapture(false, true);

				UpdateText();
				UpdateCaretPosition();

				mEventOnChange.Raise(this, mAccel);
			}
			return 0;

		case WM_SYSKEYDOWN:
			if (!mbAllKeysEnabled)
				break;
			[[fallthrough]];
		case WM_KEYDOWN:
			mbAllKeysEatNextKeyUp = false;
			mbAllKeysEatNextChar = false;

			if (mbCookedMode)
				break;

			if (mbEatNextInjectedCaps && LOWORD(wParam) == VK_CAPITAL) {
				// drop injected CAPS LOCK keys
				if (!(lParam & 0x00ff0000)) {
					mbEatNextInjectedCaps = false;
					return 0;
				}
			}

			if (LOWORD(wParam) == VK_CAPITAL)
				mbTurnOffCaps = true;

			mAccel.mVirtKey = 0;
			mAccel.mModifiers = 0;

			mCurrentMods &= ~VDUIAccelerator::kModCooked;

			if (wParam == VK_CONTROL)
				mCurrentMods |= VDUIAccelerator::kModCtrl;
			else if (wParam == VK_SHIFT)
				mCurrentMods |= VDUIAccelerator::kModShift;
			else if (wParam == VK_MENU)
				mCurrentMods |= VDUIAccelerator::kModAlt;
			else {
				mAccel.mVirtKey = (uint32)wParam;

				if (lParam & (1 << 24))
					mAccel.mModifiers |= VDUIAccelerator::kModExtended;

				ClearAnyKeyCapture(true, true);
			}

			mAccel.mModifiers |= mCurrentMods;

			UpdateText();
			UpdateCaretPosition();

			mEventOnChange.Raise(this, mAccel);
			return 0;

		case WM_SYSKEYUP:
			if (!mbAllKeysEnabled && !mbAllKeysEatNextKeyUp)
				break;
			[[fallthrough]];
		case WM_KEYUP:
			if (mbAllKeysEatNextKeyUp) {
				mbAllKeysEatNextKeyUp = false;
				return 0;
			}

			if (mbCookedMode)
				break;

			if (LOWORD(wParam) == VK_CAPITAL && mbTurnOffCaps && (GetKeyState(VK_CAPITAL) & 1)) {
				mbTurnOffCaps = false;

				// force caps lock back off
				mbEatNextInjectedCaps = true;

				keybd_event(VK_CAPITAL, 0, 0, 0);
				keybd_event(VK_CAPITAL, 0, KEYEVENTF_KEYUP, 0);
			}

			if (wParam == VK_CONTROL)
				mCurrentMods &= ~VDUIAccelerator::kModCtrl;
			else if (wParam == VK_SHIFT)
				mCurrentMods &= ~VDUIAccelerator::kModShift;
			else if (wParam == VK_MENU)
				mCurrentMods &= ~VDUIAccelerator::kModAlt;
			else
				break;

			UpdateText();
			UpdateCaretPosition();

			mEventOnChange.Raise(this, mAccel);
			return 0;
	}

	return DefWindowProc(mhwnd, msg, wParam, lParam);
}

void VDUIHotKeyExControlW32::OnPaint() {
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(mhwnd, &ps);
	if (!hdc)
		return;

	RECT r;
	if (GetClientRect(mhwnd, &r)) {
		bool useSysClientEdge = (GetWindowLong(mhwnd, GWL_EXSTYLE) & WS_EX_CLIENTEDGE) != 0;

		if (!useSysClientEdge) {
			VDVERIFY(DrawEdge(hdc, &r, EDGE_SUNKEN, BF_ADJUST | BF_RECT));
		}

		uint32 bkColor = GetSysColor(COLOR_WINDOW);

		if (mbAllKeysEnabled) {
			const uint32 bkColor2 = GetSysColor(COLOR_HIGHLIGHT);

			bkColor = (bkColor | bkColor2) - (((bkColor ^ bkColor2) & 0xFEFEFE) >> 1);

			SetDCBrushColor(hdc, bkColor);
			VDVERIFY(FillRect(hdc, &r, (HBRUSH)GetStockObject(DC_BRUSH)));
		} else {
			VDVERIFY(FillRect(hdc, &r, (HBRUSH)(COLOR_WINDOW + 1)));
		}

		int cx = GetSystemMetrics(SM_CXEDGE);
		int cy = GetSystemMetrics(SM_CYEDGE);

		r.left += cx;
		r.top += cy;
		r.right -= cx;
		r.bottom -= cy;

		if (r.right > r.left && r.bottom > r.top) {			
			SetBkColor(hdc, bkColor);

			SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
			SetTextAlign(hdc, TA_TOP | TA_LEFT);

			HGDIOBJ holdFont = SelectObject(hdc, mhfont);
			if (holdFont) {
				ExtTextOutW(hdc, r.left, r.top, ETO_CLIPPED, &r, mBuffer.c_str(), mBuffer.size(), NULL);
				SelectObject(hdc, holdFont);
			}
		}
	}

	EndPaint(mhwnd, &ps);
}

void VDUIHotKeyExControlW32::SetFont(HFONT font) {
	if (!font)
		font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

	mhfont = font;
	mFontHeight = 16;

	HDC hdc = GetDC(mhwnd);
	if (hdc) {
		HGDIOBJ holdFont = SelectObject(hdc, mhfont);
		if (holdFont) {
			TEXTMETRIC tm = {0};

			if (GetTextMetrics(hdc, &tm))
				mFontHeight = tm.tmHeight;

			SelectObject(hdc, holdFont);
		}
	}
}

void VDUIHotKeyExControlW32::UpdateCaretPosition() {
	if (GetFocus() != mhwnd)
		return;
	
	int x = GetSystemMetrics(SM_CXEDGE);
	int y = GetSystemMetrics(SM_CYEDGE);

	if (!(GetWindowLong(mhwnd, GWL_EXSTYLE) & WS_EX_CLIENTEDGE)) {
		x += x;
		y += y;
	}

	HDC hdc = GetDC(mhwnd);
	if (hdc) {
		HGDIOBJ holdFont = SelectObject(hdc, mhfont);
		if (holdFont) {
			SIZE sz;

			if (GetTextExtentPoint32W(hdc, mBuffer.c_str(), mBuffer.size(), &sz))
				x += sz.cx;

			SelectObject(hdc, holdFont);
		}
	}

	SetCaretPos(x, y);
}

void VDUIHotKeyExControlW32::UpdateText() {
	if (!mAccel.mVirtKey && !(mAccel.mModifiers & (VDUIAccelerator::kModAlt | VDUIAccelerator::kModShift | VDUIAccelerator::kModCtrl)))
		mBuffer = L"-";
	else
		VDUIGetAcceleratorString(mAccel, mBuffer);

	InvalidateRect(mhwnd, NULL, TRUE);
	UpdateCaretPosition();
}

void VDUIHotKeyExControlW32::ClearAnyKeyCapture(bool eatNextChar, bool eatNextKeyUp) {
	if (mbAllKeysEnabled) {
		mbAllKeysEnabled = false;

		// Since we activate on a WM_KEYDOWN or WM_CHAR, there is potentially
		// a WM_KEYUP and/or WM_CHAR to arrive. In particular, we really want
		// to eat the WM_CHAR for a Tab, since otherwise it can cause the
		// dialog code to beep.
		mbAllKeysEatNextKeyUp = eatNextChar;
		mbAllKeysEatNextChar = eatNextKeyUp;

		InvalidateRect(mhwnd, nullptr, TRUE);
	} else {
		mbAllKeysEatNextKeyUp = false;
		mbAllKeysEatNextChar = false;
	}
}

void VDUIHotKeyExControlW32::EnableMessageHook() {
	if (!mhHook)
		mhHook = SetWindowsHookEx(WH_MSGFILTER, SuppressSysCharHook, nullptr, GetCurrentThreadId());
}

void VDUIHotKeyExControlW32::DisableMessageHook() {
	if (mhHook) {
		UnhookWindowsHookEx(mhHook);
		mhHook = nullptr;
	}
}

LRESULT CALLBACK VDUIHotKeyExControlW32::SuppressSysCharHook(int code, WPARAM wParam, LPARAM lParam) {
	if (code == MSGF_DIALOGBOX) {
		const MSG *msg = (const MSG *)lParam;

		// This is a workaround for annoying behavior in IsDialogMessage() as
		// implicitly called by DialogBox(): it issues a WM_GETDLGCODE message
		// to the target window, and then happily ignores it and processes
		// WM_SYSCHAR for Alt+keys anyway, which then activates shortcuts.
		// To fix this problem, we install this message hook while capturing
		// all keys and eat the WM_SYSCHAR messages via the dialog message hook
		// before IsDialogMessage() can see it.
		if (msg->message == WM_SYSCHAR) {
			return TRUE;
		}
	}

	return CallNextHookEx(nullptr, code, wParam, lParam);
}
