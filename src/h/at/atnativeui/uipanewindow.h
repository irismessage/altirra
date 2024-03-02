//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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

#ifndef f_AT_ATNATIVEUI_UIPANEWINDOW_H
#define f_AT_ATNATIVEUI_UIPANEWINDOW_H

#include <at/atnativeui/uipane.h>
#include <at/atnativeui/uinativewindow.h>

class ATUIPaneWindowBase : public ATUINativeWindow {
public:
	bool CreatePaneWindow(ATFrameWindow *w);

protected:
	ATUIPaneWindowBase();

	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	virtual ATUIPane& AsPane() = 0;

	virtual bool OnCreate();
	virtual void OnDestroy();
	virtual void OnSize();
	virtual void OnSetFocus();
	virtual void OnFontsUpdated();
	virtual void OnThemeUpdated();
	virtual bool OnCommand(uint32 id, uint32 extcode);

	bool IsVisibleFast() const { return mbVisibleCached; }
	virtual void OnShow();
	virtual void OnHide();

	void DeferUpdate();
	virtual void OnDeferredUpdate();

private:
	void PostDeferredUpdate();

	uint32 mDefaultWindowStyles;
	bool mbVisibleCached = false;
	bool mbDeferredUpdatePending = false;
	bool mbDeferredUpdatePosted = false;
};

extern template ATUIPaneWindowT<ATUIPane, ATUIPaneWindowBase>;

using ATUIPaneWindow = ATUIPaneWindowT<ATUIPane, ATUIPaneWindowBase>;

#endif
