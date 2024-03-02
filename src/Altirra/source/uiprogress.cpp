//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2011 Avery Lee
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
#include <windows.h>
#include <commctrl.h>
#include <vd2/system/error.h>
#include <at/atnativeui/dialog.h>
#include "resource.h"
#include "uiprogress.h"

VDZHWND g_hwndATProgressParent;

VDZHWND ATUISetProgressWindowParentW32(VDZHWND hwnd) {
	HWND prev = g_hwndATProgressParent;
	g_hwndATProgressParent = hwnd;

	return prev;
}

///////////////////////////////////////////////////////////////////////////

class ATUIProgressDialogW32 : public VDDialogFrameW32 {
public:
	ATUIProgressDialogW32();

	void Init(const wchar_t *desc, const wchar_t *statusFormat, uint32 total, const VDGUIHandle *parent);
	void Update(uint32 value);
	void Shutdown();

	bool OnLoaded();
	bool OnClose();

protected:
	HWND mhwndParent;
	HWND mhwndProgress;
	HWND mhwndStatus;
	bool mbParentWasEnabled;
	bool mbAborted;
	int mValueShift;
	uint32 mValue;
	uint32 mTotal;
	const wchar_t *mpDesc;
	const wchar_t *mpStatusFormat;
	VDStringW mStatusBuffer;
	DWORD mLastUpdateTime;
};

ATUIProgressDialogW32::ATUIProgressDialogW32()
	: VDDialogFrameW32(IDD_PROGRESS)
	, mhwndParent(NULL)
	, mhwndProgress(NULL)
	, mhwndStatus(NULL)
	, mbAborted(false)
	, mLastUpdateTime(0)
{
}

void ATUIProgressDialogW32::Init(const wchar_t *desc, const wchar_t *statusFormat, uint32 total, const VDGUIHandle *parent) {
	mpDesc = desc;
	mpStatusFormat = statusFormat;

	mValueShift = 0;
	for(uint32 t = total; t > 0xFFFF; t >>= 1)
		++mValueShift;

	mTotal = total;
	mValue = 0;

	Create(parent ? *parent : (VDGUIHandle)g_hwndATProgressParent);
}

void ATUIProgressDialogW32::Update(uint32 value) {
	if (mbAborted)
		throw MyUserAbortError();

	DWORD t = GetTickCount();

	if (t - mLastUpdateTime < 100)
		return;

	mLastUpdateTime = t;

	if (value > mTotal)
		value = mTotal;

	if (mValue != value) {
		mValue = value;

		if (mhwndProgress) {
			const uint32 pos = mValue >> mValueShift;

			// Workaround for progress bar lagging behind in Vista/Win7.
			if (pos < 0xFFFFFFFFUL)
				SendMessage(mhwndProgress, PBM_SETPOS, (WPARAM)(pos + 1), 0);

			SendMessage(mhwndProgress, PBM_SETPOS, (WPARAM)pos, 0);
		}

		if (mhwndStatus && mpStatusFormat) {
			mStatusBuffer.sprintf(mpStatusFormat, mValue, mTotal);

			SetWindowTextW(mhwndStatus, mStatusBuffer.c_str());
		}
	}

	MSG msg;
	while(!mbAborted && PeekMessage(&msg, NULL, 0, 0, PM_REMOVE | PM_NOYIELD)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

void ATUIProgressDialogW32::Shutdown() {
	Close();
}

bool ATUIProgressDialogW32::OnLoaded() {
	mhwndParent = g_hwndATProgressParent;
	g_hwndATProgressParent = mhdlg;

	mbParentWasEnabled = mhwndParent && !(GetWindowLong(mhwndParent, GWL_STYLE) & WS_DISABLED);

	if (mhwndParent)
		EnableWindow(mhwndParent, FALSE);

	SetControlText(IDC_STATIC_DESC, mpDesc);

	mhwndProgress = GetControl(IDC_PROGRESS);
	if (mhwndProgress)
		SendMessage(mhwndProgress, PBM_SETRANGE32, (WPARAM)0, (LPARAM)(mTotal >> mValueShift));

	mhwndStatus = GetControl(IDC_STATIC_STATUS);

	return VDDialogFrameW32::OnLoaded();
}

bool ATUIProgressDialogW32::OnClose() {
	if (mhwndParent) {
		if (mbParentWasEnabled) {
			EnableWindow(mhwndParent, TRUE);
			SetWindowLong(mhdlg, GWL_STYLE, GetWindowLong(mhdlg, GWL_STYLE) | WS_POPUP);
		}

		VDASSERT(g_hwndATProgressParent == mhdlg);
		g_hwndATProgressParent = mhwndParent;
		mhwndParent = NULL;
	}

	mbAborted = true;
	Destroy();
	return true;
}

///////////////////////////////////////////////////////////////////////////

ATUIProgressDialogW32 *g_pATProgressDialog;

bool ATUIBeginProgressDialog(const wchar_t *desc, const wchar_t *statusFormat, uint32 total, const VDGUIHandle *parent) {
	g_pATProgressDialog = new_nothrow ATUIProgressDialogW32;
	if (!g_pATProgressDialog)
		return false;

	g_pATProgressDialog->Init(desc, statusFormat, total, parent);
	return true;
}

void ATUIUpdateProgressDialog(uint32 count) {
	if (g_pATProgressDialog)
		g_pATProgressDialog->Update(count);
}

void ATUIEndProgressDialog() {
	if (g_pATProgressDialog) {
		g_pATProgressDialog->Shutdown();
		delete g_pATProgressDialog;
		g_pATProgressDialog = NULL;
	}
}
///////////////////////////////////////////////////////////////////////////

ATUIProgress::ATUIProgress()
	: mbCreated(false)
{
}

ATUIProgress::~ATUIProgress() {
	Shutdown();
}

void ATUIProgress::InitF(VDGUIHandle parent, uint32 n, const wchar_t *statusFormat, const wchar_t *descFormat, ...) {
	Shutdown();

	va_list val;
	va_start(val, descFormat);
	mDesc.append_vsprintf(descFormat, val);
	va_end(val);

	if (statusFormat)
		mStatusFormat = statusFormat;

	mbCreated = ATUIBeginProgressDialog(mDesc.c_str(), statusFormat ? mStatusFormat.c_str() : NULL, n, &parent);
}

void ATUIProgress::InitF(uint32 n, const wchar_t *statusFormat, const wchar_t *descFormat, ...) {
	Shutdown();

	va_list val;
	va_start(val, descFormat);
	mDesc.append_vsprintf(descFormat, val);
	va_end(val);

	if (statusFormat)
		mStatusFormat = statusFormat;

	mbCreated = ATUIBeginProgressDialog(mDesc.c_str(), statusFormat ? mStatusFormat.c_str() : NULL, n);
}

void ATUIProgress::Shutdown() {
	if (mbCreated) {
		mbCreated = false;

		ATUIEndProgressDialog();
	}
}
