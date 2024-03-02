//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2001 Avery Lee
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

#include "stdafx.h"

#include <windows.h>
#include <richedit.h>

#include "resource.h"

#include <vd2/system/thread.h>
#include <vd2/system/profile.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/w32assist.h>

extern HINSTANCE g_hInst;

namespace {
	struct StreamInData {
		const char *pos;
		int len;
	};

#pragma pack(push, 4)
	struct EDITSTREAM_fixed {
		DWORD_PTR	dwCookie;
		DWORD	dwError;
		EDITSTREAMCALLBACK pfnCallback;		// WinXP x64 build 1290 calls this at [rax+0Ch]!
	};
#pragma pack(pop)

	DWORD CALLBACK TextToRichTextControlCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb) {
		StreamInData& sd = *(StreamInData *)dwCookie;

		if (cb > sd.len)
			cb = sd.len;

		memcpy(pbBuff, sd.pos, cb);
		sd.pos += cb;
		sd.len -= cb;

		*pcb = cb;
		return 0;
	}

	typedef vdfastvector<char> tTextStream;

	void append(tTextStream& stream, const char *string) {
		stream.insert(stream.end(), string, string+strlen(string));
	}

	void append_cooked(tTextStream& stream, const char *string, const char *stringEnd, bool rtfEscape) {
		while(string != stringEnd) {
			const char *s = string;

			if (*s == '%') {
				const char *varbase = ++s;

				while(s != stringEnd && *s != '%')
					++s;

				const ptrdiff_t len = s - varbase;

				VDASSERT(len == 0);

				stream.push_back('%');

				if (s != stringEnd)
					++s;

				string = s;
				continue;
			}

			if (rtfEscape) {
				if (*s == '{' || *s == '\\' || *s == '}')
					stream.push_back('\\');

				++s;
				while(s != stringEnd && *s != '{' && *s != '\\' && *s != '}' && *s != '%')
					++s;
			} else {
				++s;
				while(s != stringEnd && *s != '%')
					++s;
			}

			stream.insert(stream.end(), string, s);
			string = s;
		}
	}

	void TextToRichTextControl(LPCTSTR resName, HWND hdlg, HWND hwndText) {
		HRSRC hResource = FindResource(NULL, resName, _T("STUFF"));

		if (!hResource)
			return;

		HGLOBAL hGlobal = LoadResource(NULL, hResource);
		if (!hGlobal)
			return;

		LPVOID lpData = LockResource(hGlobal);
		if (!lpData)
			return;

		DWORD len = SizeofResource(NULL, hResource);

		VDString tmp((const char *)lpData, (const char *)lpData + len);

		const char *const title = (const char *)tmp.c_str();
		const char *s = title;

		while(*s!='\r') ++s;

		SetWindowTextA(hdlg, VDString(title, s-title).c_str());
		s+=2;

		tTextStream rtf;

		static const char header[]=
					"{\\rtf"
					"{\\fonttbl{\\f0\\fswiss;}{\\f1\\fnil\\fcharset2 Symbol;}}"
					"{\\colortbl;\\red0\\green64\\blue128;}"
					"\\fs20 "
					;
		static const char listStart[]="{\\*\\pn\\pnlvlblt\\pnindent0{\\pntxtb\\'B7}}\\fi-240\\li540 ";
		static const char bulletCompat[]="{\\pntext\\f1\\'B7\\tab}";

		append(rtf, header);

		bool list_active = false;

		while(*s) {
			// parse line
			int spaces = 0;

			while(*s == ' ') {
				++s;
				++spaces;
			}

			const char *end = s, *t;
			while(*end && *end != '\r' && *end != '\n')
				++end;

			// check for header, etc.
			if (*s == '[') {
				t = ++s;
				while(t != end && *t != ']')
					++t;

				append(rtf, "\\cf1\\li300\\i ");
				append_cooked(rtf, s, t, true);
				append(rtf, "\\i0\\cf0\\par ");
			} else {
				if (*s == '*') {
					if (!list_active) {
						list_active = true;
						append(rtf, listStart);
					} else
						append(rtf, "\\par ");

					append_cooked(rtf, s + 2, end, true);
				} else {
					if (list_active) {
						rtf.push_back(' ');
						if (s == end) {
							list_active = false;
							append(rtf, "\\par\\pard");
						}
					}

					if (!list_active) {
						if (spaces)
							append(rtf, "\\li300 ");
						else
							append(rtf, "\\li0 ");
					}

					append_cooked(rtf, s, end, true);

					if (!list_active)
						append(rtf, "\\par ");
				}
			}

			// skip line termination
			s = end;
			if (*s == '\r' || *s == '\n') {
				++s;
				if ((s[0] ^ s[-1]) == ('\r' ^ '\n'))
					++s;
			}
		}

		rtf.push_back('}');

		SendMessage(hwndText, EM_EXLIMITTEXT, 0, (LPARAM)rtf.size());

		EDITSTREAM_fixed es;

		StreamInData sd={rtf.data(), rtf.size()};

		es.dwCookie = (DWORD_PTR)&sd;
		es.dwError = 0;
		es.pfnCallback = (EDITSTREAMCALLBACK)TextToRichTextControlCallback;

		SendMessage(hwndText, EM_STREAMIN, SF_RTF, (LPARAM)&es);
		SendMessage(hwndText, EM_SETSEL, 0, 0);
		SetFocus(hwndText);
	}
}

INT_PTR CALLBACK ATShowChangeLogDlgProcW32(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
	case WM_INITDIALOG:
		TextToRichTextControl((LPCTSTR)lParam, hdlg, GetDlgItem(hdlg, IDC_TEXT));
		return FALSE;
	case WM_COMMAND:
		switch(LOWORD(wParam)) {
		case IDOK: case IDCANCEL:
			EndDialog(hdlg, 0);
			return TRUE;
		}
		break;
	}

	return FALSE;
}

void ATShowChangeLog(VDGUIHandle hParent) {
	HMODULE hmod = VDLoadSystemLibraryW32("riched32.dll");
	DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_CHANGE_LOG), (HWND)hParent, ATShowChangeLogDlgProcW32, (LPARAM)MAKEINTRESOURCE(IDR_CHANGES));
	FreeLibrary(hmod);
}
