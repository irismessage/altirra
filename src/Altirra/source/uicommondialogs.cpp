#include "stdafx.h"
#include <windows.h>
#include <vd2/Dita/services.h>
#include "uianchor.h"
#include "uicommondialogs.h"
#include "uifilebrowser.h"
#include "uimanager.h"
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

class ATUIStageAlert : public ATUIFutureWithResult<bool> {
public:
	void Start(const wchar_t *text, const wchar_t *caption) {
		if (ATUIGetNativeDialogMode()) {
			if (IDOK == MessageBoxW(g_hwnd, text, caption, MB_OKCANCEL | MB_ICONERROR))
				mResult = true;
			else
				mResult = false;

			MarkCompleted();
			return;
		}

		ATUIMessageBox *mbox = new ATUIMessageBox;
		mbox->AddRef();
		g_ATUIManager.GetMainWindow()->AddChild(mbox);

		mbox->OnCompletedEvent() = ATBINDCALLBACK(this, &ATUIStageAlert::OnResult);
		mbox->SetOwner(g_ATUIManager.GetFocusWindow());
		mbox->SetCaption(caption);
		mbox->SetText(text);
		mbox->SetFrameMode(kATUIFrameMode_Raised);
		mbox->AutoSize();

		vdrefptr<IATUIAnchor> anchor;
		ATUICreateTranslationAnchor(0.5f, 0.5f, ~anchor);
		mbox->SetAnchor(anchor);

		mbox->ShowModal();
		mbox->Release();
	}

	void OnResult(uint32 id) {
		mResult = (id == ATUIMessageBox::kResultOK);
		MarkCompleted();
	}
};

vdrefptr<ATUIFutureWithResult<bool> > ATUIShowAlert(const wchar_t *text, const wchar_t *caption) {
	vdrefptr<ATUIStageAlert> future(new ATUIStageAlert);

	future->Start(text, caption);

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
			fb->SetDockMode(kATUIDockMode_Fill);
			fb->SetOwner(g_ATUIManager.GetFocusWindow());
			fb->OnCompletedEvent() = ATBINDCALLBACK(this, &ATUIStageOpenFileDialog::OnCompleted);
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
