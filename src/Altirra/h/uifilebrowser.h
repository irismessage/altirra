#ifndef f_AT_UIFILEBROWSER_H
#define f_AT_UIFILEBROWSER_H

#include "uicontainer.h"
#include "uibutton.h"
#include "uilistview.h"
#include "uitextedit.h"

class ATUIFileBrowser : public ATUIContainer {
public:
	ATUIFileBrowser();
	~ATUIFileBrowser();

public:
	virtual void OnCreate();
	virtual void OnDestroy();

protected:
	vdrefptr<ATUIListView> mpListView;
	vdrefptr<ATUIContainer> mpBottomContainer;
	vdrefptr<ATUIButton> mpButtonOK;
	vdrefptr<ATUIButton> mpButtonCancel;
	vdrefptr<ATUITextEdit> mpTextEdit;
};

#endif
