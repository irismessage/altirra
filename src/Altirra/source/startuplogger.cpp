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

#include <stdafx.h>
#include <windows.h>
#include <vd2/system/w32assist.h>
#include <at/atappbase/exceptionfilter.h>
#include <at/atcore/logging.h>
#include "startuplogger.h"

class ATStartupLogger {
public:
	~ATStartupLogger();

	void Init(const wchar_t *channels);
	bool IsEnabled() const;

	void Log(const char *msg);
	void Log(VDStringSpanA msg);

	static LRESULT CALLBACK LogWindowMessage(int code, WPARAM localSendFlag, LPARAM msgInfo);
	static LRESULT CALLBACK LogWindowMessageRet(int code, WPARAM localSendFlag, LPARAM msgInfo);

	static void ExceptionPreFilter(DWORD code, const EXCEPTION_POINTERS *exptrs);

	static BOOL WINAPI CtrlHandler(DWORD CtrlType);

	static ATStartupLogger *sCrashLogger;
	static ATStartupLogger *sHookLogger;
	static HHOOK sMsgHook;
	static HHOOK sMsgHookRet;

	bool mbEnabled = false;
	bool mbShowTimeDeltas = false;
	uint64 mStartTick = 0;
	uint64 mLastMsgTick = 0;
};

ATStartupLogger *ATStartupLogger::sCrashLogger;
ATStartupLogger *ATStartupLogger::sHookLogger;
HHOOK ATStartupLogger::sMsgHook;
HHOOK ATStartupLogger::sMsgHookRet;

ATStartupLogger::~ATStartupLogger() {
	sCrashLogger = nullptr;
	sHookLogger = nullptr;
}

void ATStartupLogger::Init(const wchar_t *channels) {
	mbEnabled = true;

	// attach to parent console if we can, otherwise allocate a new one
	if (!AttachConsole(ATTACH_PARENT_PROCESS))
		AllocConsole();

	// disable ctrl+C handling
	SetConsoleCtrlHandler(CtrlHandler, TRUE);

	DWORD actual;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "\r\n", 2, &actual, nullptr);

	mStartTick = VDGetPreciseTick();
	mLastMsgTick = mStartTick;

	ATSetExceptionPreFilter(ExceptionPreFilter);

	sCrashLogger = this;

	OSVERSIONINFOW osinfo { sizeof(OSVERSIONINFOW) };
#pragma warning(push)
#pragma warning(disable:4996)
	if (GetVersionExW(&osinfo)) {
#pragma warning(pop)
		VDStringA s;
		s.sprintf("Windows %u.%u.%u"
			, osinfo.dwMajorVersion
			, osinfo.dwMinorVersion
			, osinfo.dwBuildNumber
		);

		Log(s.c_str());
	}

	Log((VDStringA("Command line: ") + VDTextWToA(GetCommandLineW())).c_str());

	if (channels && *channels) {
		VDStringRefW s(channels);
		VDStringA channelName;

		for(;;) {
			VDStringRefW token;
			bool last = !s.split(L',', token);

			if (last)
				token = s;

			bool found = false;
			if (token.comparei(L"hostwinmsg") == 0) {
				// special case -- hook message loop and log window messages
				if (!sHookLogger) {
					sHookLogger = this;
					sMsgHook = SetWindowsHookExW(WH_CALLWNDPROC, LogWindowMessage, VDGetLocalModuleHandleW32(), GetCurrentThreadId());
					sMsgHookRet = SetWindowsHookExW(WH_CALLWNDPROCRET, LogWindowMessageRet, VDGetLocalModuleHandleW32(), GetCurrentThreadId());
				}

				found = true;
			} else if (token.comparei(L"time") == 0) {
				mbShowTimeDeltas = true;
				found = true;
			} else {
				bool enable = true;

				if (!token.empty() && token[0] == '-') {
					token = token.subspan(1);
					enable = false;
				}

				for(ATLogChannel *p = ATLogGetFirstChannel();
					p;
					p = ATLogGetNextChannel(p))
				{
					if (token.comparei(VDTextU8ToW(VDStringSpanA(p->GetName()))) == 0) {
						p->SetEnabled(enable);
						found = true;
						break;
					}
				}
			}

			if (!found)
				Log("Warning: A log channel specified in /startuplog was not found.");

			if (last)
				break;
		}
	}
}

bool ATStartupLogger::IsEnabled() const {
	return mbEnabled;
}

void ATStartupLogger::Log(const char *msg) {
	Log(VDStringSpanA(msg));
}

void ATStartupLogger::Log(VDStringSpanA msg) {
	if (mbEnabled) {
		DWORD actual;
		HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
		char buf[32];

		const uint64 t = VDGetPreciseTick();
		const double scale = VDGetPreciseSecondsPerTick();
		snprintf(buf, 32, "[%6.3f] ", (double)(t - mStartTick) * scale);
		buf[31] = 0;

		WriteFile(h, buf, (DWORD)strlen(buf), &actual, nullptr);

		if (mbShowTimeDeltas) {
			snprintf(buf, 32, "[%+6.3f] ", (double)(t - mLastMsgTick) * scale);
			buf[31] = 0;

			WriteFile(h, buf, (DWORD)strlen(buf), &actual, nullptr);

			mLastMsgTick = t;
		}

		WriteFile(h, msg.data(), (DWORD)msg.size(), &actual, nullptr);
		WriteFile(h, "\r\n", 2, &actual, nullptr);
	}
}

LRESULT CALLBACK ATStartupLogger::LogWindowMessage(int code, WPARAM localSendFlag, LPARAM msgInfo) {
	if (sHookLogger) {
		CWPSTRUCT& cwp = *(CWPSTRUCT *)msgInfo;

		VDStringA s;

#if VD_PTR_SIZE > 4
		s.sprintf("HOSTWINMSG: %c %08X %08X %016llX %016llX", localSendFlag ? L'S' : L'P',
			(unsigned)(uintptr)cwp.hwnd,
			(unsigned)cwp.message,
			(unsigned long long)cwp.wParam,
			(unsigned long long)cwp.lParam);
#else
		s.sprintf("HOSTWINMSG: %c %08X %08X %08X %08X", localSendFlag ? L'S' : L'P',
			(unsigned)cwp.hwnd,
			(unsigned)cwp.message,
			(unsigned)cwp.wParam,
			(unsigned)cwp.lParam);
#endif

		sHookLogger->Log(s);
	}

	return CallNextHookEx(sMsgHook, code, localSendFlag, msgInfo);
}

LRESULT CALLBACK ATStartupLogger::LogWindowMessageRet(int code, WPARAM localSendFlag, LPARAM msgInfo) {
	if (sHookLogger) {
		CWPRETSTRUCT& cwp = *(CWPRETSTRUCT *)msgInfo;

		VDStringA s;

#if VD_PTR_SIZE > 4
		s.sprintf("HOSTWINMSG: R %08X %08X %016llX %016llX -> %016llX",
			(unsigned)(uintptr)cwp.hwnd,
			(unsigned)cwp.message,
			(unsigned long long)cwp.wParam,
			(unsigned long long)cwp.lParam,
			(unsigned long long)cwp.lResult
		);
#else
		s.sprintf("HOSTWINMSG: R %08X %08X %08X %08X -> %08X",
			(unsigned)cwp.hwnd,
			(unsigned)cwp.message,
			(unsigned)cwp.wParam,
			(unsigned)cwp.lParam,
			(unsigned)cwp.lResult
		);
#endif

		sHookLogger->Log(s);
	}

	return CallNextHookEx(sMsgHookRet, code, localSendFlag, msgInfo);
}

void ATStartupLogger::ExceptionPreFilter(DWORD code, const EXCEPTION_POINTERS *exptrs) {
	// kill window message hook logging, we do not want to log any messages that
	// might arrive due to the message box
	sHookLogger = nullptr;

	HMODULE hmod = VDGetLocalModuleHandleW32();
	VDStringA s;

#ifdef VD_CPU_X86
	s.sprintf("CRASH: Code: %08X  PC: %08X  ExeBase: %08X", code, exptrs->ContextRecord->Eip, (unsigned)hmod);
#elif defined(VD_CPU_AMD64)
	s.sprintf("CRASH: Code: %08X  PC: %08X`%08X  ExeBase: %08X`%08X"
		, code
		, (unsigned)(exptrs->ContextRecord->Rip >> 32)
		, (unsigned)exptrs->ContextRecord->Rip
		, (unsigned)((uintptr)hmod >> 32)
		, (unsigned)(uintptr)hmod
	);
#elif defined(VD_CPU_ARM64)
	s.sprintf("CRASH: Code: %08X  PC: %08X`%08X  ExeBase: %08X`%08X"
		, code
		, (unsigned)(exptrs->ContextRecord->Pc >> 32)
		, (unsigned)exptrs->ContextRecord->Pc
		, (unsigned)((uintptr)hmod >> 32)
		, (unsigned)(uintptr)hmod
	);
#else
#error Platform not supported
#endif

	sCrashLogger->Log(s);

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
	MEMORY_BASIC_INFORMATION mbi {};
	if (VirtualQuery(hmod, &mbi, sizeof mbi)) {
		// try to determine extent
		uintptr extent = 0;
		while(mbi.AllocationBase == hmod && mbi.RegionSize > 0) {
			extent += mbi.RegionSize;

			if (!VirtualQuery((char *)hmod + extent, &mbi, sizeof mbi))
				break;

			// we shouldn't be this big, if we are then something's wrong
			if ((extent | mbi.RegionSize) >= 0x10000000)
				break;
		}

#ifdef VD_CPU_X86
		const uintptr *sp = (const uintptr *)exptrs->ContextRecord->Esp;
#elif defined(VD_CPU_AMD64)
		const uintptr *sp = (const uintptr *)exptrs->ContextRecord->Rsp;
#endif

		int n = 0;
		for(int i=0; i<500; ++i) {
			bool valid = true;

			// can't mix EH types in the same function, so...
			uintptr v = [p = &sp[i], &valid]() -> uintptr {
				__try {
					return *p;
				} __except(EXCEPTION_EXECUTE_HANDLER) {
					valid = false;
					return 0;
				}
			}();

			if (!valid)
				break;

			// check if stack entry is within EXE range
			uintptr offset = v - (uintptr)hmod;
			if (offset < extent) {
				// check if stack entry is in executable code
				if (VirtualQuery((LPCVOID)v, &mbi, sizeof mbi) && (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
					s.sprintf("CRASH: [%2d] exe+%08X", n, (unsigned)offset);
					sCrashLogger->Log(s);

					if (++n >= 30)
						break;
				}
			}
		}
	}
#endif
}

BOOL WINAPI ATStartupLogger::CtrlHandler(DWORD CtrlType) {
	return TRUE;
}

///////////////////////////////////////////////////////////////////////////

ATStartupLogger *g_pATStartupLogger;

void ATStartupLogInit(const wchar_t *args) {
	if (!g_pATStartupLogger) {
		g_pATStartupLogger = new ATStartupLogger;
		g_pATStartupLogger->Init(args);
	}
}

void ATStartupLogShutdown() {
	if (g_pATStartupLogger) {
		delete g_pATStartupLogger;
		g_pATStartupLogger = nullptr;
	}
}

bool ATStartupLogIsInited() {
	return g_pATStartupLogger != nullptr;
}

void ATStartupLog(const char *msg) {
	if (g_pATStartupLogger)
		g_pATStartupLogger->Log(msg);
}

void ATStartupLog(VDStringSpanA msg) {
	if (g_pATStartupLogger)
		g_pATStartupLogger->Log(msg);
}
