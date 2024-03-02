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

#ifndef f_AT_UIDBGPANE_H
#define f_AT_UIDBGPANE_H

#include <at/atnativeui/uiframe.h>
#include <at/atnativeui/uipanewindow.h>
#include <at/atnativeui/uipanedialog.h>
#include "console.h"

class ATUIDebuggerPane : public ATUIPane, public IATUIDebuggerPane {
public:
	using ATUIPane::ATUIPane;

	void *AsInterface(uint32 iid);

protected:
	virtual bool OnPaneCommand(ATUIPaneCommandId id);

	std::optional<LRESULT> TryHandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
};

class ATUIDebuggerPaneWindow : public ATUIPaneWindowT<ATUIDebuggerPane, ATUIPaneWindowBase> {
public:
	using ATUIPaneWindowT::ATUIPaneWindowT;

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) override;
};

class ATUIDebuggerPaneDialog : public ATUIPaneWindowT<ATUIDebuggerPane, ATUIPaneDialogBase> {
public:
	using Base = ATUIDebuggerPaneDialog;
	using ATUIPaneWindowT::ATUIPaneWindowT;

protected:
	VDZINT_PTR DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) override;
};

#endif
