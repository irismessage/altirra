#ifndef f_AT_UIFILEBROWSER_H
#define f_AT_UIFILEBROWSER_H

#include "uicontainer.h"
#include "uilabel.h"
#include "uibutton.h"
#include "uilistview.h"
#include "uitextedit.h"
#include "callback.h"

class ATUIFileBrowser : public ATUIContainer {
public:
	ATUIFileBrowser();
	~ATUIFileBrowser();

	void LoadPersistentData(uint32 id);
	void SavePersistentData(uint32 id);

	const wchar_t *GetPath() const;
	void SetPath(const wchar_t *path);
	void SetDirectory(const wchar_t *path);

	void SetTitle(const wchar_t *title);

	void ShowModal();

	void Ascend();
	void Descend(const wchar_t *folder);

	ATCallbackHandler2<void, ATUIFileBrowser *, bool>& OnCompletedEvent() { return mCompletedEvent; }

public:
	virtual void OnCreate();
	virtual void OnDestroy();

protected:
	void OnGoUpPressed(ATUIButton *);
	void OnOKPressed(ATUIButton *);
	void OnCancelPressed(ATUIButton *);
	void OnItemSelected(ATUIListView *, sint32);
	void OnItemActivated(ATUIListView *, sint32);
	void OnRootItemActivated(ATUIListView *, sint32);
	void OnNewPathEntered(ATUITextEdit *);

	void Repopulate();

	bool mbModal;
	VDStringW mPath;
	VDStringW mTitle;

	vdrefptr<ATUILabel> mpLabel;
	vdrefptr<ATUIListView> mpListView;
	vdrefptr<ATUIListView> mpRootListView;
	vdrefptr<ATUIContainer> mpBottomContainer;
	vdrefptr<ATUIContainer> mpTopContainer;
	vdrefptr<ATUIButton> mpButtonUp;
	vdrefptr<ATUIButton> mpButtonOK;
	vdrefptr<ATUIButton> mpButtonCancel;
	vdrefptr<ATUITextEdit> mpTextEdit;
	vdrefptr<ATUITextEdit> mpTextEditPath;

	ATCallbackHandler2<void, ATUIFileBrowser *, bool> mCompletedEvent;
};

#endif
