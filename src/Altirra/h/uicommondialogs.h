#ifndef f_AT_UICOMMONDIALOGS_H
#define f_AT_UICOMMONDIALOGS_H

#include <vd2/system/refcount.h>
#include <vd2/system/VDString.h>
#include "uiqueue.h"

bool ATUIGetNativeDialogMode();
void ATUISetNativeDialogMode(bool enabled);

///////////////////////////////////////////////////////////////////////////

vdrefptr<ATUIFutureWithResult<bool> > ATUIShowAlert(const wchar_t *text, const wchar_t *caption);

///////////////////////////////////////////////////////////////////////////

struct ATUIFileDialogResult : public ATUIFuture {
	bool mbAccepted;
	VDStringW mPath;
};

vdrefptr<ATUIFileDialogResult>  ATUIShowOpenFileDialog(uint32 id, const wchar_t *title, const wchar_t *filters);

#endif
