#ifndef f_AT2_UICONTAINER_H
#define f_AT2_UICONTAINER_H

#include "uiwidget.h"

class ATUIContainer : public ATUIWidget {
public:
	ATUIContainer();
	~ATUIContainer();

	void AddChild(ATUIWidget *w);
	void RemoveChild(ATUIWidget *w);
	void RemoveAllChildren();

	void SendToBack(ATUIWidget *w);
	void BringToFront(ATUIWidget *w);

	void InvalidateLayout();
	void UpdateLayout();

	virtual ATUIWidget *HitTest(vdpoint32 pt);

	virtual void OnDestroy();
	virtual void OnSize();

	virtual void OnSetFocus();

protected:
	virtual void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h);

	bool mbLayoutInvalid;
	bool mbDescendantLayoutInvalid;

	typedef vdfastvector<ATUIWidget *> Widgets;
	Widgets mWidgets;
};

#endif
