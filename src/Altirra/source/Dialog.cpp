//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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

#include "stdafx.h"
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <vd2/system/w32assist.h>
#include "Dialog.h"

extern HINSTANCE g_hInst;

///////////////////////////////////////////////////////////////////////////////

class VDUIDropFileListW32 : public IVDUIDropFileList {
public:
	VDUIDropFileListW32(VDZHDROP hdrop);

	bool GetFileName(int index, VDStringW& fileName);

protected:
	const HDROP mhdrop;
	const int mFileCount;
};

VDUIDropFileListW32::VDUIDropFileListW32(VDZHDROP hdrop)
	: mhdrop(hdrop)
	, mFileCount(DragQueryFile(mhdrop, 0xFFFFFFFF, NULL, 0))
{
}

bool VDUIDropFileListW32::GetFileName(int index, VDStringW& fileName) {
	if (index < 0 || index >= mFileCount)
		return false;

	if (VDIsWindowsNT()) {
		wchar_t fileBufW[MAX_PATH];

		if (!DragQueryFileW(mhdrop, index, fileBufW, MAX_PATH))
			return false;

		fileName = fileBufW;
		return true;
	} else {
		char fileBufA[MAX_PATH];

		if (!DragQueryFileA(mhdrop, index, fileBufA, MAX_PATH))
			return false;

		fileName = VDTextAToW(fileBufA);
		return true;
	}
}

///////////////////////////////////////////////////////////////////////////////

VDDialogFrameW32::VDDialogFrameW32(uint32 dlgid)
	: mpDialogResourceName(MAKEINTRESOURCE(dlgid))
	, mbIsModal(false)
	, mhdlg(NULL)
	, mMinWidth(0)
	, mMinHeight(0)
{
}

bool VDDialogFrameW32::Create(VDGUIHandle parent) {
	if (!mhdlg) {
		mbIsModal = false;

		if (VDIsWindowsNT())
			CreateDialogParamW(g_hInst, IS_INTRESOURCE(mpDialogResourceName) ? (LPCWSTR)mpDialogResourceName : VDTextAToW(mpDialogResourceName).c_str(), (HWND)parent, StaticDlgProc, (LPARAM)this);
		else
			CreateDialogParamA(g_hInst, mpDialogResourceName, (HWND)parent, StaticDlgProc, (LPARAM)this);
	}

	return mhdlg != NULL;
}

void VDDialogFrameW32::Destroy() {
	if (mhdlg)
		DestroyWindow(mhdlg);
}

sintptr VDDialogFrameW32::ShowDialog(VDGUIHandle parent) {
	mbIsModal = true;
	if (VDIsWindowsNT())
		return DialogBoxParamW(g_hInst, IS_INTRESOURCE(mpDialogResourceName) ? (LPCWSTR)mpDialogResourceName : VDTextAToW(mpDialogResourceName).c_str(), (HWND)parent, StaticDlgProc, (LPARAM)this);
	else
		return DialogBoxParamA(g_hInst, mpDialogResourceName, (HWND)parent, StaticDlgProc, (LPARAM)this);
}

void VDDialogFrameW32::Show() {
	if (mhdlg)
		ShowWindow(mhdlg, SW_SHOWNA);
}

void VDDialogFrameW32::Hide() {
	if (mhdlg)
		ShowWindow(mhdlg, SW_HIDE);
}

void VDDialogFrameW32::End(sintptr result) {
	EndDialog(mhdlg, result);
	mhdlg = NULL;
}

void VDDialogFrameW32::AddProxy(VDUIProxyControl *proxy, uint32 id) {
	HWND hwnd = GetControl(id);

	if (hwnd) {
		proxy->Attach(hwnd);
		mMsgDispatcher.AddControl(proxy);
	}
}

void VDDialogFrameW32::SetCurrentSizeAsMinSize() {
	RECT r;
	if (GetWindowRect(mhdlg, &r)) {
		if (r.right > r.left)
			mMinWidth = r.right - r.left;

		if (r.bottom > r.top)
			mMinHeight = r.bottom - r.top;
	}
}

VDZHWND VDDialogFrameW32::GetControl(uint32 id) {
	if (!mhdlg)
		return NULL;

	return GetDlgItem(mhdlg, id);
}

void VDDialogFrameW32::SetFocusToControl(uint32 id) {
	if (!mhdlg)
		return;

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (hwnd)
		SendMessage(mhdlg, WM_NEXTDLGCTL, (WPARAM)hwnd, TRUE);
}

void VDDialogFrameW32::EnableControl(uint32 id, bool enabled) {
	if (!mhdlg)
		return;
	HWND hwnd = GetDlgItem(mhdlg, id);
	if (hwnd)
		EnableWindow(hwnd, enabled);
}

bool VDDialogFrameW32::GetControlText(uint32 id, VDStringW& s) {
	HWND hwnd = GetDlgItem(mhdlg, id);
	if (!hwnd)
		return false;

	s = VDGetWindowTextW32(hwnd);
	return true;
}

void VDDialogFrameW32::SetCaption(uint32 id, const wchar_t *format) {
	if (mhdlg)
		VDSetWindowTextW32(mhdlg, format);
}

void VDDialogFrameW32::SetControlText(uint32 id, const wchar_t *s) {
	if (!mhdlg)
		return;

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (hwnd)
		VDSetWindowTextW32(hwnd, s);
}

void VDDialogFrameW32::SetControlTextF(uint32 id, const wchar_t *format, ...) {
	if (!mhdlg)
		return;

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (hwnd) {
		VDStringW s;
		va_list val;

		va_start(val, format);
		s.append_vsprintf(format, val);
		va_end(val);

		VDSetWindowTextW32(hwnd, s.c_str());
	}
}

uint32 VDDialogFrameW32::GetControlValueUint32(uint32 id) {
	if (!mhdlg) {
		FailValidation(id);
		return 0;
	}

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (!hwnd) {
		FailValidation(id);
		return 0;
	}

	VDStringW s(VDGetWindowTextW32(hwnd));
	unsigned val;
	wchar_t tmp;
	if (1 != swscanf(s.c_str(), L" %u %c", &val, &tmp)) {
		FailValidation(id);
		return 0;
	}

	return val;
}

double VDDialogFrameW32::GetControlValueDouble(uint32 id) {
	if (!mhdlg) {
		FailValidation(id);
		return 0;
	}

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (!hwnd) {
		FailValidation(id);
		return 0;
	}

	VDStringW s(VDGetWindowTextW32(hwnd));
	double val;
	wchar_t tmp;
	if (1 != swscanf(s.c_str(), L" %lg %c", &val, &tmp)) {
		FailValidation(id);
		return 0;
	}

	return val;
}

VDStringW VDDialogFrameW32::GetControlValueString(uint32 id) {
	if (!mhdlg) {
		FailValidation(id);
		return VDStringW();
	}

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (!hwnd) {
		FailValidation(id);
		return VDStringW();
	}

	return VDGetWindowTextW32(hwnd);
}

void VDDialogFrameW32::ExchangeControlValueBoolCheckbox(bool write, uint32 id, bool& val) {
	if (write) {
		val = IsButtonChecked(id);
	} else {
		CheckButton(id, val);
	}
}

void VDDialogFrameW32::ExchangeControlValueUint32(bool write, uint32 id, uint32& val, uint32 minVal, uint32 maxVal) {
	if (write) {
		val = GetControlValueUint32(id);
		if (val < minVal || val > maxVal)
			FailValidation(id);
	} else {
		SetControlTextF(id, L"%u", (unsigned)val);
	}
}

void VDDialogFrameW32::ExchangeControlValueDouble(bool write, uint32 id, const wchar_t *format, double& val, double minVal, double maxVal) {
	if (write) {
		val = GetControlValueDouble(id);
		if (val < minVal || val > maxVal)
			FailValidation(id);
	} else {
		SetControlTextF(id, format, val);
	}
}

void VDDialogFrameW32::ExchangeControlValueString(bool write, uint32 id, VDStringW& s) {
	if (write)
		s = GetControlValueString(id);
	else
		SetControlText(id, s.c_str());
}

void VDDialogFrameW32::CheckButton(uint32 id, bool checked) {
	CheckDlgButton(mhdlg, id, checked ? BST_CHECKED : BST_UNCHECKED);
}

bool VDDialogFrameW32::IsButtonChecked(uint32 id) {
	return IsDlgButtonChecked(mhdlg, id) != 0;
}

void VDDialogFrameW32::BeginValidation() {
	mbValidationFailed = false;
}

bool VDDialogFrameW32::EndValidation() {
	if (mbValidationFailed) {
		SignalFailedValidation(mFailedId);
		return false;
	}

	return true;
}

void VDDialogFrameW32::FailValidation(uint32 id) {
	mbValidationFailed = true;
	mFailedId = id;
}

void VDDialogFrameW32::SignalFailedValidation(uint32 id) {
	if (!mhdlg)
		return;

	HWND hwnd = GetDlgItem(mhdlg, id);

	MessageBeep(MB_ICONEXCLAMATION);
	if (hwnd)
		SetFocus(hwnd);
}

void VDDialogFrameW32::SetPeriodicTimer(uint32 id, uint32 msperiod) {
	::SetTimer(mhdlg, id, msperiod, NULL);
}

bool VDDialogFrameW32::Confirm(const wchar_t *message, const wchar_t *caption) {
	int result;
	
	if (VDIsWindowsNT())
		result = ::MessageBoxW(mhdlg, message, caption, MB_OKCANCEL | MB_ICONEXCLAMATION);
	else
		result = ::MessageBoxA(mhdlg, VDTextWToA(message).c_str(), VDTextWToA(caption).c_str(), MB_OKCANCEL | MB_ICONEXCLAMATION);

	return result == IDOK;
}

void VDDialogFrameW32::LBClear(uint32 id) {
	SendDlgItemMessage(mhdlg, id, LB_RESETCONTENT, 0, 0);
}

sint32 VDDialogFrameW32::LBGetSelectedIndex(uint32 id) {
	return SendDlgItemMessage(mhdlg, id, LB_GETCURSEL, 0, 0);
}

void VDDialogFrameW32::LBSetSelectedIndex(uint32 id, sint32 idx) {
	SendDlgItemMessage(mhdlg, id, LB_SETCURSEL, idx, 0);
}

void VDDialogFrameW32::LBAddString(uint32 id, const wchar_t *s) {
	if (VDIsWindowsNT()) {
		SendDlgItemMessageW(mhdlg, id, LB_ADDSTRING, 0, (LPARAM)s);
	} else {
		SendDlgItemMessageA(mhdlg, id, LB_ADDSTRING, 0, (LPARAM)VDTextWToA(s).c_str());		
	}
}

void VDDialogFrameW32::LBAddStringF(uint32 id, const wchar_t *format, ...) {
	VDStringW s;
	va_list val;

	va_start(val, format);
	s.append_vsprintf(format, val);
	va_end(val);

	LBAddString(id, s.c_str());
}

void VDDialogFrameW32::CBClear(uint32 id) {
	SendDlgItemMessage(mhdlg, id, CB_RESETCONTENT, 0, 0);
}

sint32 VDDialogFrameW32::CBGetSelectedIndex(uint32 id) {
	return SendDlgItemMessage(mhdlg, id, CB_GETCURSEL, 0, 0);
}

void VDDialogFrameW32::CBSetSelectedIndex(uint32 id, sint32 idx) {
	SendDlgItemMessage(mhdlg, id, CB_SETCURSEL, idx, 0);
}

void VDDialogFrameW32::CBAddString(uint32 id, const wchar_t *s) {
	if (VDIsWindowsNT()) {
		SendDlgItemMessageW(mhdlg, id, CB_ADDSTRING, 0, (LPARAM)s);
	} else {
		SendDlgItemMessageA(mhdlg, id, CB_ADDSTRING, 0, (LPARAM)VDTextWToA(s).c_str());		
	}
}

sint32 VDDialogFrameW32::TBGetValue(uint32 id) {
	return SendDlgItemMessage(mhdlg, id, TBM_GETPOS, 0, 0);
}

void VDDialogFrameW32::TBSetValue(uint32 id, sint32 value) {
	SendDlgItemMessage(mhdlg, id, TBM_SETPOS, TRUE, value);
}

void VDDialogFrameW32::TBSetRange(uint32 id, sint32 minval, sint32 maxval) {
	SendDlgItemMessage(mhdlg, id, TBM_SETRANGEMIN, FALSE, minval);
	SendDlgItemMessage(mhdlg, id, TBM_SETRANGEMAX, TRUE, maxval);
}

void VDDialogFrameW32::OnDataExchange(bool write) {
}

bool VDDialogFrameW32::OnLoaded() {
	OnDataExchange(false);
	return false;
}

bool VDDialogFrameW32::OnOK() {
	BeginValidation();
	OnDataExchange(true);
	return !EndValidation();
}

bool VDDialogFrameW32::OnCancel() {
	return false;
}

void VDDialogFrameW32::OnSize() {
}

void VDDialogFrameW32::OnDestroy() {
	mMsgDispatcher.RemoveAllControls();
}

bool VDDialogFrameW32::OnErase(VDZHDC hdc) {
	return false;
}

bool VDDialogFrameW32::OnTimer(uint32 id) {
	return false;
}

bool VDDialogFrameW32::OnCommand(uint32 id, uint32 extcode) {
	return false;
}

void VDDialogFrameW32::OnHScroll(uint32 code, int id) {
}

void VDDialogFrameW32::OnVScroll(uint32 code, int id) {
}

void VDDialogFrameW32::OnDropFiles(VDZHDROP hdrop) {
	VDUIDropFileListW32 dropList(hdrop);

	OnDropFiles(&dropList);
	DragFinish(hdrop);
}

void VDDialogFrameW32::OnDropFiles(IVDUIDropFileList *dropFileList) {
}

bool VDDialogFrameW32::PreNCDestroy() {
	return false;
}

VDZINT_PTR VDZCALLBACK VDDialogFrameW32::StaticDlgProc(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	VDDialogFrameW32 *pThis = (VDDialogFrameW32 *)GetWindowLongPtr(hwnd, DWLP_USER);

	if (msg == WM_INITDIALOG) {
		SetWindowLongPtr(hwnd, DWLP_USER, lParam);
		pThis = (VDDialogFrameW32 *)lParam;
		pThis->mhdlg = hwnd;
	} else if (msg == WM_NCDESTROY) {
		if (pThis) {
			bool deleteMe = pThis->PreNCDestroy();

			pThis->mhdlg = NULL;
			SetWindowLongPtr(hwnd, DWLP_USER, (LONG_PTR)(void *)NULL);

			if (deleteMe)
				delete pThis;

			pThis = NULL;
			return FALSE;
		}
	}

	return pThis ? pThis->DlgProc(msg, wParam, lParam) : FALSE;
}

VDZINT_PTR VDDialogFrameW32::DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	switch(msg) {
		case WM_INITDIALOG:
			return !OnLoaded();

		case WM_COMMAND:
			{
				uint32 id = LOWORD(wParam);

				if (id == IDOK) {
					// needed to work around ListView label editing stupidity
					if (HIWORD(wParam) == BN_CLICKED) {
						if (!OnOK())
							End(true);

						return TRUE;
					}
				} else if (id == IDCANCEL) {
					if (!OnCancel())
						End(false);

					return TRUE;
				} else {
					if (OnCommand(id, HIWORD(wParam)))
						return TRUE;
				}
			}

			SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, mMsgDispatcher.Dispatch_WM_COMMAND(wParam, lParam));
			return TRUE;

		case WM_NOTIFY:
			SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, mMsgDispatcher.Dispatch_WM_NOTIFY(wParam, lParam));
			return TRUE;

		case WM_DESTROY:
			OnDestroy();
			break;

		case WM_SIZE:
			OnSize();
			return FALSE;

		case WM_TIMER:
			return OnTimer((uint32)wParam);

		case WM_DROPFILES:
			OnDropFiles((VDZHDROP)wParam);
			return 0;

		case WM_HSCROLL:
			OnHScroll(lParam ? GetWindowLong((HWND)lParam, GWL_ID) : 0, LOWORD(wParam));
			return 0;

		case WM_VSCROLL:
			OnVScroll(lParam ? GetWindowLong((HWND)lParam, GWL_ID) : 0, LOWORD(wParam));
			return 0;

		case WM_ERASEBKGND:
			if (OnErase((HDC)wParam)) {
				SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, TRUE);
				return TRUE;
			}
			break;

		case WM_GETMINMAXINFO:
			{
				MINMAXINFO& mmi = *(MINMAXINFO *)lParam;

				if (mmi.ptMinTrackSize.x < mMinWidth)
					mmi.ptMinTrackSize.x = mMinWidth;

				if (mmi.ptMinTrackSize.y < mMinHeight)
					mmi.ptMinTrackSize.y = mMinHeight;
			}
			return 0;
	}

	return FALSE;
}

///////////////////////////////////////////////////////////////////////////////

VDDialogResizerW32::VDDialogResizerW32() {
}

VDDialogResizerW32::~VDDialogResizerW32() {
}

void VDDialogResizerW32::Init(HWND hwnd) {
	mhwndBase = hwnd;
	mWidth = 1;
	mHeight = 1;

	RECT r;
	if (GetClientRect(hwnd, &r)) {
		mWidth = r.right;
		mHeight = r.bottom;
	}
}

void VDDialogResizerW32::Relayout() {
	RECT r;

	if (GetClientRect(mhwndBase, &r))
		Relayout(r.right, r.bottom);
}

void VDDialogResizerW32::Relayout(int width, int height) {
	HDWP hdwp = BeginDeferWindowPos(mControls.size());

	mWidth = width;
	mHeight = height;

	const int xAnchors[4]={ 0, width >> 1, width, width };
	const int yAnchors[4]={ 0, height >> 1, height, height };

	Controls::const_iterator it(mControls.begin()), itEnd(mControls.end());
	for(; it!=itEnd; ++it) {
		const ControlEntry& ent = *it;
		uint32 flags = SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS;
		const uint8 alignment = ent.mAlignment;

		if (!(alignment & kX1Y1Mask))
			flags |= SWP_NOMOVE;

		if ((alignment & kX1Y1Mask) == (alignment & kX2Y2Mask))
			flags |= SWP_NOSIZE;

		int x1 = ent.mX1 + xAnchors[(alignment >> 0) & 3];
		int x2 = ent.mX2 + xAnchors[(alignment >> 2) & 3];
		int y1 = ent.mY1 + yAnchors[(alignment >> 4) & 3];
		int y2 = ent.mY2 + yAnchors[(alignment >> 6) & 3];

		int w = x2 - x1;
		int h = y2 - y1;

		if (w < 0)
			w = 0;

		if (h < 0)
			h = 0;

		if (hdwp) {
			HDWP hdwp2 = DeferWindowPos(hdwp, ent.mhwnd, NULL, x1, y1, w, h, flags);

			if (hdwp2) {
				hdwp = hdwp2;
				continue;
			}
		}

		SetWindowPos(ent.mhwnd, NULL, x1, y1, w, h, flags);
	}

	if (hdwp)
		EndDeferWindowPos(hdwp);
}

void VDDialogResizerW32::Add(uint32 id, int alignment) {
	HWND hwndControl = GetDlgItem(mhwndBase, id);
	if (!hwndControl)
		return;

	RECT r;
	if (!GetWindowRect(hwndControl, &r))
		return;

	if (!MapWindowPoints(NULL, mhwndBase, (LPPOINT)&r, 2))
		return;

	ControlEntry& ce = mControls.push_back();

	ce.mhwnd		= hwndControl;
	ce.mAlignment	= alignment;
	ce.mX1			= r.left   - ((mWidth  * ((alignment >> 0) & 3)) >> 1);
	ce.mY1			= r.top    - ((mHeight * ((alignment >> 4) & 3)) >> 1);
	ce.mX2			= r.right  - ((mWidth  * ((alignment >> 2) & 3)) >> 1);
	ce.mY2			= r.bottom - ((mHeight * ((alignment >> 6) & 3)) >> 1);
}

void VDDialogResizerW32::Erase() {
	HDC hdc = GetDC(mhwndBase);
	if (hdc) {
		Controls::const_iterator it(mControls.begin()), itEnd(mControls.end());
		for(; it!=itEnd; ++it) {
			const ControlEntry& ce = *it;

			if (ce.mAlignment & kAvoidFlicker) {
				RECT rChild;

				if (GetWindowRect(ce.mhwnd, &rChild)) {
					MapWindowPoints(NULL, mhwndBase, (LPPOINT)&rChild, 2);
					ExcludeClipRect(hdc, rChild.left, rChild.top, rChild.right, rChild.bottom);
				}
			}
		}

		RECT rClient;
		if (GetClientRect(mhwndBase, &rClient))
			FillRect(hdc, &rClient, (HBRUSH)(COLOR_3DFACE + 1));

		ReleaseDC(mhwndBase, hdc);
	}
}
