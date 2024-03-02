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
#include <at/atcore/progress.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/progress.h>
#include "resource.h"
#include "uiaccessors.h"
#include "uicommondialogs.h"
#include "uiprogress.h"

///////////////////////////////////////////////////////////////////////////

class ATUIProgressDialogW32 : public VDDialogFrameW32 {
public:
	ATUIProgressDialogW32();

	void Init(const wchar_t *desc, const wchar_t *statusFormat, uint32 total, VDGUIHandle parent);
	void Update(uint32 value);
	bool CheckForCancellationOrStatus();
	void UpdateStatus(const wchar_t *msg);
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
	VDStringW mDesc;
	VDStringW mStatusFormat;
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

void ATUIProgressDialogW32::Init(const wchar_t *desc, const wchar_t *statusFormat, uint32 total, VDGUIHandle parent) {
	mDesc = desc;

	if (statusFormat)
		mStatusFormat = statusFormat;

	mValueShift = 0;
	for(uint32 t = total; t > 0xFFFF; t >>= 1)
		++mValueShift;

	mTotal = total;
	mValue = 0;
	mhwndParent = (HWND)parent;

	Create(parent);
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

		if (mhwndStatus && !mStatusFormat.empty()) {
			mStatusBuffer.sprintf(mStatusFormat.c_str(), mValue, mTotal);

			SetWindowTextW(mhwndStatus, mStatusBuffer.c_str());
		}
	}

	MSG msg;
	while(!mbAborted && PeekMessage(&msg, NULL, 0, 0, PM_REMOVE | PM_NOYIELD)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

bool ATUIProgressDialogW32::CheckForCancellationOrStatus() {
	if (mbAborted)
		throw MyUserAbortError();

	DWORD t = GetTickCount();
	bool shouldUpdate = false;

	if (t - mLastUpdateTime >= 100) {
		shouldUpdate = true;
		mLastUpdateTime = t;
	}

	MSG msg;
	while(!mbAborted && PeekMessage(&msg, NULL, 0, 0, PM_REMOVE | PM_NOYIELD)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return shouldUpdate;
}

void ATUIProgressDialogW32::UpdateStatus(const wchar_t *msg) {
	mStatusFormat.clear();

	SetWindowTextW(mhwndStatus, msg);
}

void ATUIProgressDialogW32::Shutdown() {
	Close();
}

bool ATUIProgressDialogW32::OnLoaded() {
	mbParentWasEnabled = mhwndParent && !(GetWindowLong(mhwndParent, GWL_STYLE) & WS_DISABLED);

	if (mhwndParent)
		EnableWindow(mhwndParent, FALSE);

	SetControlText(IDC_STATIC_DESC, mDesc.c_str());

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

		mhwndParent = NULL;
	}

	mbAborted = true;
	Destroy();
	return true;
}

/////////////////////////////////////////////////////////////////////////////

class ATUIProgressBackgroundTaskDialogW32 final : public VDDialogFrameW32, public IATTaskProgressContext, public VDThread {
public:
	ATUIProgressBackgroundTaskDialogW32(const wchar_t *desc,  const vdfunction<void(IATTaskProgressContext&)>& fn);

	MyError& GetPendingError() { return mPendingError; }

	bool OnLoaded() override;
	void OnDestroy() override;
	bool OnClose() override;
	bool OnTimer(uint32 id) override;

	void OnCancelClicked();

public:
	bool CheckForCancellationOrStatus() override;
	void SetProgress(double progress) override;
	void SetProgressF(double progress, const wchar_t *format, ...) override;

public:
	void ThreadRun() override;

protected:
	void SetProgressInternal(double progress);
	void UpdateProgressAndStatus();

	VDUIProxyButtonControl mCancelButton;
	ATUINativeWindowProxy mDescLabel;
	ATUINativeWindowProxy mStatusLabel;
	VDStringW mDesc;
	VDStringW mStatusTextPending;
	int mLastIntProgress = 0;
	HWND mhwndProgress = nullptr;

	const vdfunction<void(IATTaskProgressContext&)>& mpFn;
	MyError mPendingError;

	VDAtomicInt mUpdateRequested;
	VDAtomicInt mCancellationRequested;

	VDCriticalSection mMutex;
	bool mbProgressUpdatePending = false;
	bool mbStatusUpdatePending = false;
	double mProgress = 0;
	VDStringW mStatusText;
};

ATUIProgressBackgroundTaskDialogW32::ATUIProgressBackgroundTaskDialogW32(const wchar_t *desc, const vdfunction<void(IATTaskProgressContext&)>& fn)
	: VDDialogFrameW32(IDD_PROGRESS)
	, mDesc(desc)
	, mpFn(fn)
{
	mCancelButton.SetOnClicked([this] { OnCancelClicked(); });
}

bool ATUIProgressBackgroundTaskDialogW32::OnLoaded() {
	mbProgressUpdatePending = false;
	mbStatusUpdatePending = false;
	mProgress = -1;
	mUpdateRequested = 0;
	mCancellationRequested = 0;
	mLastIntProgress = -1;

	AddProxy(&mCancelButton, IDCANCEL);
	mhwndProgress = GetControl(IDC_PROGRESS);

	if (mhwndProgress) {
		SendMessage(mhwndProgress, PBM_SETMARQUEE, TRUE, 0);
	}

	mDescLabel = ATUINativeWindowProxy(GetControl(IDC_STATIC_DESC));
	mStatusLabel = ATUINativeWindowProxy(GetControl(IDC_STATIC_STATUS));

	SetPeriodicTimer(1, 100);

	mDescLabel.SetCaption(mDesc.c_str());

	if (!ThreadStart()) {
		mCancellationRequested = 1;
		mpFn(*this);
	}

	return false;
}

void ATUIProgressBackgroundTaskDialogW32::OnDestroy() {
	mCancellationRequested = 1;
	ThreadWait();
}

bool ATUIProgressBackgroundTaskDialogW32::OnClose() {
	mCancellationRequested = 1;
	return true;
}

bool ATUIProgressBackgroundTaskDialogW32::OnTimer(uint32 id) {
	if (id == 1) {
		mUpdateRequested = 1;
		return true;
	}

	return false;
}

void ATUIProgressBackgroundTaskDialogW32::OnCancelClicked() {
	mCancellationRequested = true;
}

bool ATUIProgressBackgroundTaskDialogW32::CheckForCancellationOrStatus() {
	if (mCancellationRequested)
		throw MyUserAbortError();

	return mUpdateRequested.compareExchange(0, 1) == 1;
}

void ATUIProgressBackgroundTaskDialogW32::SetProgress(double progress) {
	vdsynchronized(mMutex) {
		SetProgressInternal(progress);
	}
}

void ATUIProgressBackgroundTaskDialogW32::SetProgressF(double progress, const wchar_t *format, ...) {
	va_list val;
	va_start(val, format);

	vdsynchronized(mMutex) {
		SetProgressInternal(progress);

		mStatusText.clear();
		mStatusText.append_vsprintf(format, val);

		if (!mbStatusUpdatePending) {
			mbStatusUpdatePending = true;

			if (!mbProgressUpdatePending)
				PostCall([this] { UpdateProgressAndStatus(); });
		}
	}

	va_end(val);
}

void ATUIProgressBackgroundTaskDialogW32::ThreadRun() {
	try {
		mpFn(*this);
	} catch(const MyUserAbortError&) {
	} catch(MyError& e) {
		mPendingError.TransferFrom(e);
	}

	PostCall([this] { End(1); });
}

void ATUIProgressBackgroundTaskDialogW32::SetProgressInternal(double progress) {
	if (mProgress != progress) {
		mProgress = progress;

		if (!mbProgressUpdatePending) {
			mbProgressUpdatePending = true;

			if (!mbStatusUpdatePending)
				PostCall([this] { UpdateProgressAndStatus(); });
		}
	}
}

void ATUIProgressBackgroundTaskDialogW32::UpdateProgressAndStatus() {
	bool statusUpdate = false;
	bool progressUpdate = false;
	double progress;

	vdsynchronized(mMutex) {
		if (mbProgressUpdatePending) {
			mbProgressUpdatePending = false;

			progressUpdate = true;
			progress = mProgress;
		}

		if (mbStatusUpdatePending) {
			mbStatusUpdatePending = false;

			statusUpdate = true;
			mStatusTextPending.swap(mStatusText);
		}
	}

	if (progressUpdate && mhwndProgress) {
		int ipos = -1;

		if (progress >= 0) {
			ipos = (int)(std::min<double>(progress, 1.0) * 4096.0 + 0.5);
		}

		if (mLastIntProgress != ipos) {
			if (ipos < 0) {
				SendMessage(mhwndProgress, PBM_SETMARQUEE, TRUE, 0);
			} else {
				if (mLastIntProgress < 0) {
					SendMessage(mhwndProgress, PBM_SETMARQUEE, FALSE, 0);
					SendMessage(mhwndProgress, PBM_SETRANGE32, 0, MAKELONG(0, 4096));
				}

				SendMessage(mhwndProgress, PBM_SETPOS, ipos, 0);
			}

			mLastIntProgress = ipos;
		}
	}

	if (statusUpdate) {
		mStatusLabel.SetCaption(mStatusTextPending.c_str());
	}
}

/////////////////////////////////////////////////////////////////////////////

class ATUIProgressHandler final : public IATProgressHandler {
public:
	ATUIProgressHandler();

	void Begin(uint32 total, const wchar_t *status, const wchar_t *desc) override;
	void BeginF(uint32 total, const wchar_t *status, const wchar_t *descFormat, va_list descArgs) override;
	void Update(uint32 value) override;
	bool CheckForCancellationOrStatus() override;
	void UpdateStatus(const wchar_t *statusMessage) override;
	void End() override;

	bool RunTask(const wchar_t *desc, const vdfunction<void(IATTaskProgressContext&)>&) override;

private:
	ATUIProgressDialogW32 *mpDialog = nullptr;
	uint32 mNestingCount = 0;
};

ATUIProgressHandler::ATUIProgressHandler() {
}

void ATUIProgressHandler::Begin(uint32 total, const wchar_t *status, const wchar_t *desc) {
	if (!mNestingCount++) {
		if (ATUIGetNativeDialogMode()) {
			mpDialog = new_nothrow ATUIProgressDialogW32;
			if (mpDialog) {
				mpDialog->Init(desc, status, total, ATUIGetNewPopupOwner());
			}
		}
	}
}

void ATUIProgressHandler::BeginF(uint32 total, const wchar_t *status, const wchar_t *descFormat, va_list descArgs) {
	VDStringW desc;
	desc.append_vsprintf(descFormat, descArgs);

	Begin(total, status, desc.c_str());
}

void ATUIProgressHandler::Update(uint32 value) {
	if (mpDialog && mNestingCount == 1)
		mpDialog->Update(value);
}

bool ATUIProgressHandler::CheckForCancellationOrStatus() {
	return mpDialog && mNestingCount == 1 && mpDialog->CheckForCancellationOrStatus();
}

void ATUIProgressHandler::UpdateStatus(const wchar_t *statusMessage) {
	if (mpDialog && mNestingCount == 1)
		mpDialog->UpdateStatus(statusMessage);
}

void ATUIProgressHandler::End() {
	VDASSERT(mNestingCount > 0);

	if (!--mNestingCount) {
		if (mpDialog) {
			auto p = mpDialog;
			mpDialog = nullptr;

			p->Shutdown();
			delete p;
		}
	}
}

bool ATUIProgressHandler::RunTask(const wchar_t *desc, const vdfunction<void(IATTaskProgressContext&)>& fn) {
	ATUIProgressBackgroundTaskDialogW32 dlg(desc, fn);

	if (!dlg.ShowDialog(ATUIGetNewPopupOwner()))
		return false;

	MyError& e = dlg.GetPendingError();

	if (!e.empty())
		throw e;

	return true;
}

/////////////////////////////////////////////////////////////////////////////

ATUIProgressHandler& ATUIGetProgressHandler() {
	static ATUIProgressHandler sHandler;

	return sHandler;
}

void ATUIInitProgressDialog() {
	ATSetProgressHandler(&ATUIGetProgressHandler());
}

void ATUIShutdownProgressDialog() {
	ATSetProgressHandler(nullptr);
}
