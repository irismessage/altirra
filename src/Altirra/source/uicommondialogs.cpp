//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2019 Avery Lee
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
#include <windows.h>
#include <vd2/system/error.h>
#include <vd2/system/text.h>
#include <vd2/Dita/services.h>
#include <at/atnativeui/genericdialog.h>
#include <at/atui/uianchor.h>
#include <at/atui/uimanager.h>
#include "uiaccessors.h"
#include "uicommondialogs.h"
#include "uifilebrowser.h"
#include "uimessagebox.h"
#include "uiqueue.h"

extern ATUIManager g_ATUIManager;
extern HWND g_hwnd;

bool g_ATUINativeDialogsEnabled = true;

bool ATUIGetNativeDialogMode() {
	return g_ATUINativeDialogsEnabled;
}

void ATUISetNativeDialogMode(bool enabled) {
	g_ATUINativeDialogsEnabled = enabled;
}

///////////////////////////////////////////////////////////////////////////

void ATUIShowInfo(VDGUIHandle h, const wchar_t *text) {
	MessageBoxW((HWND)h, text, L"Altirra", MB_OK | MB_ICONINFORMATION);
}

void ATUIShowWarning(VDGUIHandle h, const wchar_t *text, const wchar_t *caption) {
	MessageBoxW((HWND)h, text, caption, MB_OK | MB_ICONWARNING);
}

bool ATUIShowWarningConfirm(VDGUIHandle h, const wchar_t *text, const wchar_t *title) {
	ATUIGenericDialogOptions opts;
	opts.mhParent = h;
	opts.mpCaption = L"Altirra Warning";
	opts.mpMessage = text;
	opts.mpTitle = title;
	opts.mResultMask = kATUIGenericResultMask_OKCancel;
	opts.mIconType = kATUIGenericIconType_Warning;

	return ATUIShowGenericDialog(opts) == kATUIGenericResult_OK;
}

void ATUIShowError2(VDGUIHandle h, const wchar_t *text, const wchar_t *title) {
	ATUIGenericDialogOptions opts;
	opts.mhParent = h;
	opts.mpCaption = L"Altirra Error";
	opts.mpMessage = text;
	opts.mpTitle = title;
	opts.mResultMask = kATUIGenericResultMask_OK;
	opts.mIconType = kATUIGenericIconType_Error;

	ATUIShowGenericDialog(opts);
}

void ATUIShowError(VDGUIHandle h, const wchar_t *text) {
	ATUIShowError2(h, text, nullptr);
}

void ATUIShowError(VDGUIHandle h, const MyError& e) {
	ATUIShowError(h, VDTextAToW(e.c_str()).c_str());
}

void ATUIShowError(const MyError& e) {
	ATUIShowError(ATUIGetNewPopupOwner(), e);
}

///////////////////////////////////////////////////////////////////////////

class ATUIStageAlert : public ATUIFutureWithResult<bool> {
public:
	void Start(const wchar_t *text, const wchar_t *title, bool allowChoice) {
		if (ATUIGetNativeDialogMode()) {

			if (allowChoice) {
				mResult = ATUIShowWarningConfirm(ATUIGetNewPopupOwner(), text, title);
			} else {
				ATUIShowError2(ATUIGetNewPopupOwner(), text, title);
				mResult = true;
			}

			MarkCompleted();
			return;
		}

		ATUIMessageBox *mbox = new ATUIMessageBox;
		mbox->AddRef();
		mbox->SetOwner(g_ATUIManager.GetFocusWindow());
		g_ATUIManager.GetMainWindow()->AddChild(mbox);

		mbox->OnCompletedEvent() = ATBINDCALLBACK(this, &ATUIStageAlert::OnResult);
		mbox->SetCaption(allowChoice ? L"Altirra Warning" : L"Altirra Error");
		mbox->SetText(text);
		mbox->SetFrameMode(kATUIFrameMode_Raised);
		mbox->SetQueryMode(allowChoice);

		mbox->SetPlacement(vdrect32f(0.5f, 0.5f, 0.5f, 0.5f), vdpoint32(0, 0), vdfloat2{0.5f, 0.5f});

		mbox->ShowModal();
		mbox->Release();
	}

	void OnResult(uint32 id) {
		mResult = (id == ATUIMessageBox::kResultOK);
		MarkCompleted();
	}
};

vdrefptr<ATUIFutureWithResult<bool> > ATUIShowAlertWarningConfirm(const wchar_t *text, const wchar_t *caption) {
	vdrefptr<ATUIStageAlert> future(new ATUIStageAlert);

	future->Start(text, caption, true);

	return vdrefptr<ATUIFutureWithResult<bool> >(&*future);
}

vdrefptr<ATUIFutureWithResult<bool> > ATUIShowAlertError(const wchar_t *text, const wchar_t *caption) {
	vdrefptr<ATUIStageAlert> future(new ATUIStageAlert);

	future->Start(text, caption, false);

	return vdrefptr<ATUIFutureWithResult<bool> >(&*future);
}

///////////////////////////////////////////////////////////////////////////

class ATUIStageOpenFileDialog : public ATUIFileDialogResult {
public:
	void Start(uint32 id, const wchar_t *title, const wchar_t *filters) {
		if (ATUIGetNativeDialogMode()) {
			mPath = VDGetLoadFileName(id, (VDGUIHandle)g_hwnd, title, filters, NULL);
			mbAccepted = !mPath.empty();
			MarkCompleted();
		} else {
			mPersistId = id;

			ATUIFileBrowser *fb = new ATUIFileBrowser;
			fb->AddRef();
			g_ATUIManager.GetMainWindow()->AddChild(fb);
			fb->SetTitle(title);
			fb->SetPlacementFill();
			fb->SetOwner(g_ATUIManager.GetFocusWindow());
			fb->SetCompletionFn([this, fb](bool succeeded) { OnCompleted(fb, succeeded); });
			fb->LoadPersistentData(id);
			fb->ShowModal();
			fb->Release();
		}
	}

	void OnCompleted(ATUIFileBrowser *sender, bool accepted) {
		sender->SavePersistentData(mPersistId);

		mbAccepted = accepted;

		if (accepted)
			mPath = sender->GetPath();

		MarkCompleted();
	}

	uint32 mPersistId;
};

vdrefptr<ATUIFileDialogResult> ATUIShowOpenFileDialog(uint32 id, const wchar_t *title, const wchar_t *filters) {
	vdrefptr<ATUIStageOpenFileDialog> stage(new ATUIStageOpenFileDialog);

	stage->Start(id, title, filters);

	return vdrefptr<ATUIFileDialogResult>(stage);
}

///////////////////////////////////////////////////////////////////////////

class ATUIStageSaveFileDialog : public ATUIFileDialogResult {
public:
	void Start(uint32 id, const wchar_t *title, const wchar_t *filters) {
		if (ATUIGetNativeDialogMode()) {
			mPath = VDGetSaveFileName(id, (VDGUIHandle)g_hwnd, title, filters, NULL);
			mbAccepted = !mPath.empty();
			MarkCompleted();
		} else {
			mPersistId = id;

			ATUIFileBrowser *fb = new ATUIFileBrowser;
			fb->AddRef();
			g_ATUIManager.GetMainWindow()->AddChild(fb);
			fb->SetTitle(title);
			fb->SetPlacementFill();
			fb->SetOwner(g_ATUIManager.GetFocusWindow());
			fb->SetCompletionFn([this, fb](bool succeeded) { OnCompleted(fb, succeeded); });
			fb->LoadPersistentData(id);
			fb->ShowModal();
			fb->Release();
		}
	}

	void OnCompleted(ATUIFileBrowser *sender, bool accepted) {
		sender->SavePersistentData(mPersistId);

		mbAccepted = accepted;

		if (accepted)
			mPath = sender->GetPath();

		MarkCompleted();
	}

	uint32 mPersistId;
};

vdrefptr<ATUIFileDialogResult> ATUIShowSaveFileDialog(uint32 id, const wchar_t *title, const wchar_t *filters) {
	vdrefptr<ATUIStageSaveFileDialog> stage(new ATUIStageSaveFileDialog);

	stage->Start(id, title, filters);

	return vdrefptr<ATUIFileDialogResult>(stage);
}
