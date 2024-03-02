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
#include <windowsx.h>
#include <vd2/system/error.h>
#include <vd2/system/function.h>
#include <vd2/system/registry.h>
#include <vd2/system/w32assist.h>
#include <at/atappbase/exceptionfilter.h>
#include <at/atnativeui/messageloop.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/uiframe.h>
#include <at/atcore/logging.h>
#include <at/atcore/media.h>
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
extern ATLogChannel g_ATLCHostDisp;

void ATUIRegisterDragDropHandler(VDGUIHandle h);
void ATUIRevokeDragDropHandler(VDGUIHandle h);
bool ATUIIsActiveModal();
void ATUICloseAdjustColorsDialog();
void ATUICloseAdjustScreenEffectsDialog();
void ATUISetCommandLine(const wchar_t *s);
void ATUISaveMainWindowPlacement();

void DoBootWithConfirm(const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper);
bool ATUIConfirmDiscardAll(VDGUIHandle h, const wchar_t *title, const wchar_t *prompt);
void ATUIShowDialogEditAccelerators(const char *preSelectedCommand);

///////////////////////////////////////////////////////////////////////////

class ATMainWindow final : public ATContainerWindow {
public:
	ATMainWindow();
	~ATMainWindow();

	void TestMenuAutoShow(const vdpoint32& pointerScreenPos) override;

private:
	static constexpr UINT kTimerID_VerifyMenu = 100;

	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc2(UINT msg, WPARAM wParam, LPARAM lParam);

	void OnCopyData(HWND hwndReply, const COPYDATASTRUCT& cds);
	bool OnCommand(UINT id);
	bool OnMenuRButtonUp(uint32 index, HMENU hmenu);
	void OnActivateApp(WPARAM wParam);

	void SetIcons();
	void UpdateCachedMetrics();

	void UpdateMonitorDpi(unsigned dpiY) override {
		UpdateCachedMetrics();
		ATConsoleSetFontDpi(dpiY);
	}

	bool IsAutoHideMenuEnabled() const override {
		return ATUIIsMenuAutoHideEnabled();
	}

	void TrackNonClientLeave();

	bool mbTrackingNonClient = false;
	bool mbMouseInNonClientArea = false;
	sint32 mMenuHeight = 0;

	static inline constexpr UINT ATWM_SUBCLASS_CUSTOMIZE_MENU_ITEM = ATWM_SUBCLASS_PRIVATE + 1;
};

ATMainWindow::ATMainWindow() {
}

ATMainWindow::~ATMainWindow() {
}

void ATMainWindow::TestMenuAutoShow(const vdpoint32& pointerScreenPos) {
	if (GetActiveWindow() != mhwnd)
		return;

	const vdpoint32& cpt = TransformScreenToClient(pointerScreenPos);

	if (cpt.y < mMenuHeight) {
		ATUISetMenuAutoHidden(false);
		TrackNonClientLeave();

		// TrackPopupMenu(TME_NONCLIENT) has an annoying bug we need to work around here.
		// It seems that a leave is only detected when the mouse transitions from being
		// within the non-client area to outside, where 'in' is detected by a mouse event.
		// Problem is, if the mouse is currently outside of the non-client area and the
		// non-client area is extended under the mouse by adding a menu, and the mouse
		// then moves out of the NC area on the next event, it is never considered 'in'
		// for leave detection. To work around this, we require an NC mouse move within
		// 100ms and re-hide the menu otherwise.
		SetTimer(mhwnd, kTimerID_VerifyMenu, 100, nullptr);
	}
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

			ATUIRegisterTopLevelWindow(mhwnd);
			ATUIRegisterDragDropHandler((VDGUIHandle)mhwnd);
			SetIcons();
			UpdateCachedMetrics();
			return 0;

		case WM_CLOSE:
			if (ATUIIsActiveModal()) {
				MessageBeep(MB_ICONASTERISK);
				return 0;
			}

			if (!ATUIConfirmDiscardAll((VDGUIHandle)mhwnd, L"About to exit", L"Are you sure you want to exit?"))
				return 0;

			ATSavePaneLayout(NULL);

			// We need to save this here as SaveSettings() is too late -- we will already have
			// switched out of FS mode.
			ATSaveSettings(kATSettingsCategory_FullScreen);
			break;

		case WM_DESTROY:
			ATUIUnregisterTopLevelWindow(mhwnd);
			ATUIDestroyModelessDialogs(mhwnd);

			// We can't use the normal save placement function because the non-fullscreen state
			// is saved off in the UI layer. This allows the non-fullscreen window size and maximized
			// state to be saved properly when exiting in fullscreen mode.
			ATUISaveMainWindowPlacement();

			ATUIRevokeDragDropHandler((VDGUIHandle)mhwnd);
			ATUICloseAdjustColorsDialog();
			ATUICloseAdjustScreenEffectsDialog();

			PostQuitMessage(0);
			break;

		case WM_ACTIVATEAPP:
			OnActivateApp(wParam);
			break;

		case WM_SYSCOMMAND:
			// Check if the menu is being accessed by Alt+Key, and unhide it if the key is valid. If Alt
			// is just being pressed by itself, we'll get 0.
			if (wParam == SC_KEYMENU && (!lParam || ATUIIsMenuCharacter((wchar_t)lParam))) {
				ATUISetMenuAutoHidden(false);
			}

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

		case WM_MENURBUTTONUP:
			if (OnMenuRButtonUp(wParam, (HMENU)lParam))
				return 0;

			break;

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
			{
				auto *jm = g_sim.GetJoystickManager();

				if (jm)
					jm->RescanForDevices();
			}
			break;

		case WM_ENABLE:
			if (!wParam) {
				if (ATUIGetDisplayFullscreen())
					ATSetFullscreen(false);
			}

			ATUISetGlobalEnableState(wParam != 0);
			break;

		case WM_ENTERIDLE:
			if (wParam == MSGF_MENU) {
				if (g_pATIdle(true))
					PostThreadMessage(GetCurrentThreadId(), WM_NULL, 0, 0);
				return 0;
			}

			break;

		case WM_SYSCOLORCHANGE:
			ATUIUpdateThemeColors();
			ATUINotifyThemeChanged();
			break;

		case WM_EXITMENULOOP:
			SetTimer(mhwnd, kTimerID_VerifyMenu, 100, nullptr);
			break;

		case WM_NCMOUSEMOVE:
			mbMouseInNonClientArea = true;

			if (wParam == HTCAPTION || wParam == HTMENU) {
				ATUISetMenuAutoHidden(false);
				TrackNonClientLeave();
			}
			break;

		case WM_NCMOUSELEAVE:
			mbTrackingNonClient = false;
			mbMouseInNonClientArea = false;

			if (ATUIIsMenuAutoHideActive()) {
				if (ATUIIsMenuAutoHidden()) {
					DWORD pos = GetMessagePos();
					int x = GET_X_LPARAM(pos);
					int y = GET_Y_LPARAM(pos);

					vdpoint32 cpt = TransformScreenToClient(vdpoint32(x, y));
					if (GetClientArea().contains(cpt) && cpt.y < ATUIGetDpiScaledSystemMetricForWindowW32(mhwnd, SM_CYMENU))
						TestMenuAutoShow(vdpoint32(x, y));
				} else {
					GUITHREADINFO gti { sizeof(GUITHREADINFO) };

					if (GetGUIThreadInfo(GetCurrentThreadId(), &gti) && !(gti.flags & (GUI_INMENUMODE | GUI_INMOVESIZE))) {
						// Yet another bug in TME_NONCLIENT, clicks count, so for the leave detection we also
						// use the lazy hide timer.
						SetTimer(mhwnd, kTimerID_VerifyMenu, 100, nullptr);
					}
				}
			}
			break;

		case WM_TIMER:
			if (wParam == kTimerID_VerifyMenu) {
				KillTimer(mhwnd, kTimerID_VerifyMenu);
				if (!mbMouseInNonClientArea)
					ATUISetMenuAutoHidden(true);
				return 0;
			}
			break;

		case ATWM_SUBCLASS_PRIVATE:
			g_sim.OnWake();
			return 0;

		case ATWM_SUBCLASS_CUSTOMIZE_MENU_ITEM:
			if (const char *cmd = ATUIGetCommandForMenuItem(wParam))
				ATUIShowDialogEditAccelerators(cmd);
			return 0;
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

bool ATMainWindow::OnMenuRButtonUp(uint32 index, HMENU hmenu) {
	UINT id = GetMenuItemID(hmenu, index);

	if (!id)
		return false;

	const DWORD mousePos = GetMessagePos();
	const sint32 x = GET_X_LPARAM(mousePos);
	const sint32 y = GET_Y_LPARAM(mousePos);

	HMENU hmenuPopup = LoadMenu(nullptr, MAKEINTRESOURCE(IDR_CUSTOMIZE_MENU));
	if (!hmenuPopup)
		return false;

	const bool canCustomize = ATUIIsCommandMappedMenuItem(id);

	if (!canCustomize)
		VDEnableMenuItemByCommandW32(hmenuPopup, ID_ASSIGNSHORTCUT, false);

	if (ID_ASSIGNSHORTCUT == TrackPopupMenu(GetSubMenu(hmenuPopup, 0), TPM_RECURSE | TPM_RETURNCMD, x, y, 0, mhwnd, nullptr)) {
		// The window manager in Windows 10 starts acting very oddly if we spawn
		// a modal dialog from a nested menu command, preventing other windows from
		// coming to top. To avoid this, we need to trampoline out with a posted
		// message.
		if (canCustomize) {
			EndMenu();
			PostMessage(mhwnd, ATWM_SUBCLASS_CUSTOMIZE_MENU_ITEM, id, 0);
		}
	}

	DestroyMenu(hmenuPopup);
	return true;
}

void ATMainWindow::OnActivateApp(WPARAM wParam) {
	ATUISetAppActive(wParam != 0);

	if (!wParam) {
		IATDisplayPane *pane = ATGetUIPaneAs<IATDisplayPane>(kATUIPaneId_Display);
		if (pane)
			pane->ReleaseMouse();

		if (ATUIGetDisplayFullscreen()) {
			g_ATLCHostDisp("Disabling full-screen mode because program was deactivated.\n");
			ATSetFullscreen(false);
		}
	}
}

void ATMainWindow::SetIcons() {
	HICON hIcon = (HICON)LoadImage(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
	if (hIcon)
		SendMessage(mhwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

	HICON hSmallIcon = (HICON)LoadImage(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
	if (hSmallIcon)
		SendMessage(mhwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);
}

void ATMainWindow::UpdateCachedMetrics() {
	mMenuHeight = ATUIGetDpiScaledSystemMetricW32(SM_CYMENU, mMonitorDpi);
}

void ATMainWindow::TrackNonClientLeave() {
	if (!mbTrackingNonClient) {
		TRACKMOUSEEVENT tme { sizeof(TRACKMOUSEEVENT) };
		tme.dwFlags = TME_NONCLIENT;
		tme.hwndTrack = mhwnd;

		if (TrackMouseEvent(&tme))
			mbTrackingNonClient = true;
	}
}

///////////////////////////////////////////////////////////////////////////

void ATUICreateMainWindow(ATContainerWindow **pp) {
	auto *p = new ATMainWindow;
	p->AddRef();

	*pp = p;
}

void ATUILinkMainWindowToSimulator(ATContainerWindow& w) {
	g_sim.SetWakeFunction([hwnd = w.GetWindowHandle()] { PostMessage(hwnd, ATWM_SUBCLASS_PRIVATE, 0, 0); });
}

void ATUIUnlinkMainWindowFromSimulator() {
	g_sim.SetWakeFunction(nullptr);
}
