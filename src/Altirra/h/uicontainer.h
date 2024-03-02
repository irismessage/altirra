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

	ATUIWidget *HitTest(vdpoint32 pt);

	virtual void OnSize();

protected:
	virtual void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h);

	typedef vdfastvector<ATUIWidget *> Widgets;
	Widgets mWidgets;
};

#endif
