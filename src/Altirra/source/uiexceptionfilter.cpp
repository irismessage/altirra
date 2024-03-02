//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
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
#include <tchar.h>
#include <windows.h>
#include <dbghelp.h>
#include <vd2/system/w32assist.h>

extern HWND g_hwnd;

int ATExceptionFilter(DWORD code, EXCEPTION_POINTERS *exp) {
	if (IsDebuggerPresent())
		return EXCEPTION_CONTINUE_SEARCH;

	TCHAR buf[1024];

	HMODULE hmodDbgHelp = VDLoadSystemLibraryW32("dbghelp");
	if (hmodDbgHelp) {
		typedef BOOL (WINAPI *tpMiniDumpWriteDump)(
			  HANDLE hProcess,
			  DWORD ProcessId,
			  HANDLE hFile,
			  MINIDUMP_TYPE DumpType,
			  PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
			  PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
			  PMINIDUMP_CALLBACK_INFORMATION CallbackParam
			);

		tpMiniDumpWriteDump pMiniDumpWriteDump = (tpMiniDumpWriteDump)GetProcAddress(hmodDbgHelp, "MiniDumpWriteDump");

		if (pMiniDumpWriteDump) {
			MINIDUMP_EXCEPTION_INFORMATION exInfo;

			exInfo.ThreadId = GetCurrentThreadId();
			exInfo.ExceptionPointers = exp;
			exInfo.ClientPointers = TRUE;

			static const TCHAR kFilename[] = _T("AltirraCrash.mdmp");
			if (GetModuleFileName(NULL, buf, sizeof buf / sizeof buf[0])) {
				size_t len = _tcslen(buf);

				while(len > 0) {
					TCHAR c = buf[len - 1];

					if (c == _T(':') || c == _T('\\') || c == _T('/'))
						break;

					--len;
				}

				if (len < MAX_PATH - sizeof(kFilename)) {
					_tcscpy(buf + len, kFilename);

					HANDLE hFile = CreateFile(buf, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

					if (hFile != INVALID_HANDLE_VALUE) {
						pMiniDumpWriteDump(
								GetCurrentProcess(),
								GetCurrentProcessId(),
								hFile,
								MiniDumpNormal,
								&exInfo,
								NULL,
								NULL);

						CloseHandle(hFile);
					}
				}
			}
		}

		FreeLibrary(hmodDbgHelp);
	}

	if (g_hwnd) {
		EnableWindow(g_hwnd, FALSE);
		SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)(IsWindowUnicode(g_hwnd) ? DefWindowProcW : DefWindowProcA));
	}

	wsprintf(buf, _T("A fatal error has occurred in the emulator. A minidump file called AltirraCrash.mdmp has been written for diagnostic purposes.\n")
		_T("\n")
#ifdef VD_CPU_X86
		_T("Exception code: %08x  PC: %08x"), code, exp->ContextRecord->Eip);
#else
		_T("Exception code: %08x  PC: %08x`%08x"), code, (uint32)(exp->ContextRecord->Rip >> 32), (uint32)exp->ContextRecord->Rip);
#endif
	MessageBox(g_hwnd, buf, _T("Altirra Program Failure"), MB_OK | MB_ICONERROR);

	TerminateProcess(GetCurrentProcess(), code);
	return 0;
}
