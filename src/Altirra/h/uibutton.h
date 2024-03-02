#ifndef f_AT_UIBUTTON_H
#define f_AT_UIBUTTON_H

#include <vd2/system/VDString.h>
#include <vd2/VDDisplay/font.h>
#include "uiwidget.h"

class ATUIButton : public ATUIWidget {
public:
	ATUIButton();
	~ATUIButton();

	void SetText(const wchar_t *s);
	void SetDepressed(bool depressed);

public:
	virtual void OnMouseDownL(sint32 x, sint32 y);
	virtual void OnMouseUpL(sint32 x, sint32 y);

	virtual bool OnKeyDown(uint32 vk);
	virtual bool OnKeyUp(uint32 vk);

	virtual void OnCreate();
	virtual void OnSize();

protected:
	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h);
	void Relayout();

	bool mbDepressed;
	sint32 mTextX;
	sint32 mTextY;
	VDStringW mText;
	vdrefptr<IVDDisplayFont> mpFont;
};

#endif
