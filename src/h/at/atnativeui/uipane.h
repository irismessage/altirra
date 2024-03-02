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

#ifndef f_AT_ATNATIVEUI_UIPANE_H
#define f_AT_ATNATIVEUI_UIPANE_H

#include <vd2/system/unknown.h>

class ATFrameWindow;
class ATUINativeWindow;

class ATUIPane : public IVDRefUnknown {
public:
	enum { kTypeID = 'uipn' };

	ATUIPane(uint32 paneId, const wchar_t *name);
	~ATUIPane();

	void *AsInterface(uint32 iid);
	virtual ATUINativeWindowProxy& AsNativeWindow() = 0;
	virtual bool Create(ATFrameWindow *w) = 0;

	uint32 GetUIPaneId() const { return mPaneId; }
	const wchar_t *GetUIPaneName() const { return mpName; }
	int GetPreferredDockCode() const { return mPreferredDockCode; }

	void RegisterUIPane();
	void UnregisterUIPane();

	void SetFrameWindow(ATFrameWindow *w);

protected:
	void SetName(const wchar_t *name);

	const wchar_t *		mpName;
	uint32 const		mPaneId;
	int					mPreferredDockCode;
	ATFrameWindow		*mpFrameWindow;
};

///////////////////////////////////////////////////////////////////////////////

template<typename T_Pane, typename T_WindowBase>
class ATUIPaneWindowT : public T_Pane, public T_WindowBase {
public:
	template<typename... T_WindowBaseArgs>
	ATUIPaneWindowT(uint32 paneId, const wchar_t *name, const T_WindowBaseArgs&... windowBaseArgs);

public:
	int AddRef() override;
	int Release() override;
	void *AsInterface(uint32 iid) override;

	ATUIPane& AsPane() override;
	ATUINativeWindowProxy& AsNativeWindow() override;

	bool Create(ATFrameWindow *w) override;
};

template<typename T_Pane, typename T_WindowBase>
template<typename... T_WindowBaseArgs>
ATUIPaneWindowT<T_Pane, T_WindowBase>::ATUIPaneWindowT(uint32 paneId, const wchar_t *name, const T_WindowBaseArgs&... windowBaseArgs)
	: T_Pane(paneId, name)
	, T_WindowBase(windowBaseArgs...)
{
}

template<typename T_Pane, typename T_WindowBase>
int ATUIPaneWindowT<T_Pane, T_WindowBase>::AddRef() {
	return T_WindowBase::AddRef();
}

template<typename T_Pane, typename T_WindowBase>
int ATUIPaneWindowT<T_Pane, T_WindowBase>::Release() {
	return T_WindowBase::Release();
}

template<typename T_Pane, typename T_WindowBase>
void *ATUIPaneWindowT<T_Pane, T_WindowBase>::AsInterface(uint32 iid) {
	void *p = T_Pane::AsInterface(iid);

	return p ? p : T_WindowBase::AsInterface(iid);
}

template<typename T_Pane, typename T_WindowBase>
ATUIPane& ATUIPaneWindowT<T_Pane, T_WindowBase>::AsPane() {
	return *this;
}

template<typename T_Pane, typename T_WindowBase>
ATUINativeWindowProxy& ATUIPaneWindowT<T_Pane, T_WindowBase>::AsNativeWindow() {
	return *this;
}

template<typename T_Pane, typename T_WindowBase>
bool ATUIPaneWindowT<T_Pane, T_WindowBase>::Create(ATFrameWindow *w) {
	return T_WindowBase::CreatePaneWindow(w);
}

#endif
