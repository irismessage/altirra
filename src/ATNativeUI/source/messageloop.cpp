//	Altirra - Atari 800/800XL/5200 emulator
//	Native UI library - system message loop support
//	Copyright (C) 2008-2015 Avery Lee
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
#include <at/atcore/profile.h>
#include <at/atnativeui/uiframe.h>

bool ATUIProcessMessages(bool waitForMessage, int& returnCode) {
	ATProfileBeginRegion(kATProfileRegion_NativeEvents);

	HWND hwndChain[16];

	for(int i=0; i<2; ++i) {
		DWORD flags = i ? PM_REMOVE : PM_QS_INPUT | PM_REMOVE;

		MSG msg;
		while(PeekMessage(&msg, NULL, 0, 0, flags)) {
			if (msg.message == WM_QUIT) {
				ATProfileEndRegion(kATProfileRegion_NativeEvents);
				PostQuitMessage((int)msg.wParam);
				returnCode = (int)msg.wParam;
				return false;
			}

			if (msg.hwnd) {
				HWND hwndOwner;
				switch(msg.message) {
					case WM_KEYDOWN:
					case WM_SYSKEYDOWN:
					case WM_KEYUP:
					case WM_SYSKEYUP:
					case WM_CHAR:
						{
							hwndOwner = GetAncestor(msg.hwnd, GA_ROOT);
							if (hwndOwner) {
								if (SendMessage(hwndOwner, ATWM_PRETRANSLATE, 0, (LPARAM)&msg))
									continue;
							}

							UINT preMsg = 0;

							switch(msg.message) {
								case WM_KEYDOWN:	preMsg = ATWM_PREKEYDOWN;		break;
								case WM_SYSKEYDOWN:	preMsg = ATWM_PRESYSKEYDOWN;	break;
								case WM_KEYUP:		preMsg = ATWM_PREKEYUP;			break;
								case WM_SYSKEYUP:	preMsg = ATWM_PRESYSKEYUP;		break;
							}

							if (preMsg) {
								size_t n = 0;

								for(HWND hwndTarget = msg.hwnd; hwndTarget && hwndTarget != hwndOwner; hwndTarget = GetAncestor(hwndTarget, GA_PARENT)) {
									hwndChain[n++] = hwndTarget;

									if (n >= vdcountof(hwndChain))
										break;
								}

								bool eat = false;
								while(n > 0) {
									if (SendMessage(hwndChain[--n], preMsg, msg.wParam, msg.lParam)) {
										eat = true;
										break;
									}
								}

								if (eat)
									continue;
							}
						}
						break;

					case WM_SYSCHAR:
						if (msg.hwnd) {
							HWND hwndRoot = GetAncestor(msg.hwnd, GA_ROOT);

							if (hwndRoot && SendMessage(hwndRoot, ATWM_QUERYSYSCHAR, msg.wParam, msg.lParam))
								msg.hwnd = hwndRoot;
						}
						break;

					case WM_MOUSEWHEEL:
						{
							POINT pt = { (short)LOWORD(msg.lParam), (short)HIWORD(msg.lParam) };
							HWND hwndUnder = WindowFromPoint(pt);

							if (hwndUnder && GetWindowThreadProcessId(hwndUnder, NULL) == GetCurrentThreadId())
								msg.hwnd = hwndUnder;
						}
						break;
				}
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	ATProfileEndRegion(kATProfileRegion_NativeEvents);

	if (waitForMessage)
		WaitMessage();

	return true;
}
