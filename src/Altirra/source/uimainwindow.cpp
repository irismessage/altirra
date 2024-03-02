//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2015 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <windows.h>
#include <vd2/system/error.h>
#include <vd2/system/function.h>
#include <vd2/system/registry.h>
#include <at/atappbase/exceptionfilter.h>
#include <at/atnativeui/messageloop.h>
#include <at/atnativeui/uiframe.h>
#include <at/atcore/media.h>
#include "autotest.h"
#include "console.h"
#include "joystick.h"
#include "oshelper.h"
#include "resource.h"
#include "settings.h"
#include "simulator.h"
#include "uiaccessors.h"
#include "uidisplay.h"
#include "uiinstance.h"
#include "uikeyboard.h"
#include "uimenu.h"
#include "uimrulist.h"
#include "uiportmenus.h"

extern vdfunction<bool(bool)> g_pATIdle;
extern ATSimulator g_sim;

void ATUIRegisterDragDropHandler(VDGUIHandle h);
void ATUIRevokeDragDropHandler(VDGUIHandle h);
bool ATUIIsActiveModal();
void ATUICloseAdjustColorsDialog();
void ATUISetCommandLine(const wchar_t *s);

void DoBootWithConfirm(const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper);
bool ATUIConfirmDiscardAllStorage(VDGUIHandle h, const wchar_t *prompt, bool includeUnmountables = false);

///////////////////////////////////////////////////////////////////////////

class ATMainWindow : public ATContainerWindow {
public:
	ATMainWindow();
	~ATMainWindow();

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc2(UINT msg, WPARAM wParam, LPARAM lParam);

	void OnCopyData(HWND hwndReply, const COPYDATASTRUCT& cds);
	bool OnCommand(UINT id);
	void OnActivateApp(WPARAM wParam);

	virtual void UpdateMonitorDpi(unsigned dpiY) {
		ATConsoleSetFontDpi(dpiY);
	}
};

ATMainWindow::ATMainWindow() {
}

ATMainWindow::~ATMainWindow() {
}

LRESULT ATMainWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	LRESULT r;
	__try {
		r = WndProc2(msg, wParam, lParam);
	} __except(ATExceptionFilter(GetExceptionCode(), GetExceptionInformation())) {
	}

	return r;
}

LRESULT ATMainWindow::WndProc2(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			if (ATContainerWindow::WndProc(msg, wParam, lParam) < 0)
				return -1;

			ATUIRegisterDragDropHandler((VDGUIHandle)mhwnd);
			return 0;

		case WM_CLOSE:
			if (ATUIIsActiveModal()) {
				MessageBeep(MB_ICONASTERISK);
				return 0;
			}

			if (!ATUIConfirmDiscardAllStorage((VDGUIHandle)mhwnd, L"Exit without saving?", true))
				return 0;

			ATSavePaneLayout(NULL);

			// We need to save this here as SaveSettings() is too late -- we will already have
			// switched out of FS mode.
			ATSaveSettings(kATSettingsCategory_FullScreen);
			break;

		case WM_DESTROY:
			ATUIDestroyModelessDialogs(mhwnd);
			ATUISaveWindowPlacement(mhwnd, "Main window");
			ATUIRevokeDragDropHandler((VDGUIHandle)mhwnd);
			ATUICloseAdjustColorsDialog();

			PostQuitMessage(0);
			break;

		case WM_ACTIVATEAPP:
			OnActivateApp(wParam);
			break;

		case WM_SYSCOMMAND:
			// Need to drop capture for Alt+F4 to work.
			ReleaseCapture();
			break;

		case WM_COMMAND:
			if (ATUIIsActiveModal())
				return 0;

			if (OnCommand(LOWORD(wParam)))
				return 0;
			break;

		case WM_INITMENU:
			ATUIUpdateMenu();
			ATUpdatePortMenus();
			return 0;

		case WM_SETCURSOR:
			break;

		case ATWM_PRETRANSLATE:
			if (!ATUIIsActiveModal()) {
				MSG& globalMsg = *(MSG *)lParam;

				const bool ctrl = GetKeyState(VK_CONTROL) < 0;
				const bool shift = GetKeyState(VK_SHIFT) < 0;
				const bool alt = GetKeyState(VK_MENU) < 0;
				const bool ext = (globalMsg.lParam & (1 << 24)) != 0;

				switch(globalMsg.message) {
					case WM_KEYDOWN:
					case WM_SYSKEYDOWN:
						if (ATUIActivateVirtKeyMapping((uint32)globalMsg.wParam, alt, ctrl, shift, ext, false, kATUIAccelContext_Global))
							return TRUE;
						break;

					case WM_KEYUP:
					case WM_SYSKEYUP:
						if (ATUIActivateVirtKeyMapping((uint32)globalMsg.wParam, alt, ctrl, shift, ext, true, kATUIAccelContext_Global))
							return TRUE;
						break;

					case WM_CHAR:
						// Currently we have no char-based mappings.
						break;
				}
			}
			break;

		case ATWM_QUERYSYSCHAR:
			return true;

		case WM_COPYDATA:
			{
				HWND hwndReply = (HWND)wParam;
				COPYDATASTRUCT *cds = (COPYDATASTRUCT *)lParam;

				OnCopyData(hwndReply, *cds);
			}
			return TRUE;

		case WM_DEVICECHANGE:
			g_sim.GetJoystickManager().RescanForDevices();
			break;

		case WM_ENABLE:
			if (!wParam) {
				if (ATUIGetFullscreen())
					ATSetFullscreen(false);
			}

			ATUIEnableModelessDialogs(mhwnd, wParam != 0);
			break;

		case WM_ENTERIDLE:
			if (wParam == MSGF_MENU) {
				if (g_pATIdle(true))
					PostThreadMessage(GetCurrentThreadId(), WM_NULL, 0, 0);
				return 0;
			}

			break;
	}

	return ATContainerWindow::WndProc(msg, wParam, lParam);
}

void ATMainWindow::OnCopyData(HWND hwndReply, const COPYDATASTRUCT& cds) {
	if (!cds.cbData || !cds.lpData)
		return;

	if (cds.dwData == 0xA7000001) {
		// The format of the data we are looking for is as follows:
		//	- validation GUID
		//	- command line
		//	- zero or more properties:
		//		- name
		//		- value
		// All strings are wide char and are null terminated. Note that some 2.x test releases
		// do not send properties and do not null terminate the command string. Also, we want
		// to avoid crashing if the block is malformed.
		//	
		if (cds.cbData < 16 || (cds.cbData - 16) % sizeof(wchar_t))
			return;

		if (memcmp(cds.lpData, kATGUID_CopyDataCmdLine, 16))
			return;

		const wchar_t *s = (const wchar_t *)((const char *)cds.lpData + 16);
		const wchar_t *t;
		const wchar_t *end = s + (cds.cbData - 16) / sizeof(wchar_t);

		// parse out command line string
		for(t = s; t != end && *t; ++t)
			;

		const VDStringW cmdLineStr(s, t);
		if (t != end) {
			s = t + 1;

			VDStringW name;
			VDStringW value;
			for(;;) {
				for(t = s; t != end && *t; ++t)
					;

				if (t == end)
					break;

				name.assign(s, t);

				s = t + 1;
				
				for(t = s; t != end && *t; ++t)
					;

				if (t == end)
					break;

				value.assign(s, t);

				// interpret the string
				if (name == L"chdir") {
					::SetCurrentDirectoryW(value.c_str());
				} else if (name.size() == 3 && name[0] == L'=' && (name[1] >= L'A' && name[1] <= L'Z') && name[2] == L':') {
					::SetEnvironmentVariableW(name.c_str(), value.c_str());
				}
			}
		}

		ATUISetCommandLine(cmdLineStr.c_str());
		return;
	}

	if (cds.dwData != 0xA7000000 || !ATGetAutotestEnabled())
		return;

	vdfastvector<wchar_t> s;

	s.resize(cds.cbData / sizeof(wchar_t) + 1, 0);
	memcpy(s.data(), cds.lpData, (s.size() - 1) * sizeof(wchar_t));

	uint32 rval = 0;
	MyError e;

	try {
		rval = ATExecuteAutotestCommand(s.data(), NULL);
	} catch(MyError& f) {
		e.swap(f);
	}

	if (hwndReply) {
		VDStringW err(VDTextAToW(e.gets()));

		vdfastvector<char> buf(sizeof(uint32) + sizeof(wchar_t) * err.size());
		memcpy(buf.data(), &rval, sizeof(uint32));
		memcpy(buf.data() + sizeof(uint32), err.data(), err.size() * sizeof(wchar_t));

		COPYDATASTRUCT cds2;
		cds2.dwData = 0xA7000001;
		cds2.cbData = (DWORD)buf.size();
		cds2.lpData = buf.data();

		::SendMessage(hwndReply, WM_COPYDATA, (WPARAM)mhwnd, (LPARAM)&cds2);
	}
}

bool ATMainWindow::OnCommand(UINT id) {
	if (!id)
		return false;

	if (ATUIHandleMenuCommand(id))
		return true;

	if (ATUIHandlePortMenuCommand(id))
		return true;

	if ((uint32)(id - ID_FILE_MRU_BASE) < 100) {
		int index = id - ID_FILE_MRU_BASE;

		if (index == 99) {
			ATClearMRUList();
		} else {
			VDStringW s(ATGetMRUListItem(index));

			if (!s.empty())
				DoBootWithConfirm(s.c_str(), nullptr, 0);
		}
	}

	return false;
}

void ATMainWindow::OnActivateApp(WPARAM wParam) {
	ATUISetAppActive(wParam != 0);

	if (!wParam) {
		IATDisplayPane *pane = ATGetUIPaneAs<IATDisplayPane>(kATUIPaneId_Display);
		if (pane)
			pane->ReleaseMouse();

		ATSetFullscreen(false);
	}
}

///////////////////////////////////////////////////////////////////////////

void ATUICreateMainWindow(ATContainerWindow **pp) {
	auto *p = new ATMainWindow;
	p->AddRef();

	*pp = p;
}
