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
#include <vd2/system/error.h>
#include <vd2/Dita/services.h>
#include "console.h"
#include "debugger.h"
#include "simulator.h"
#include "uiaccessors.h"

extern ATSimulator g_sim;

void ATUIShowDialogDebugFont(VDGUIHandle hParent);
void ATUIShowDialogVerifier(VDGUIHandle h, ATSimulator& sim);

void OnCommandDebuggerOpenSourceFile() {
	VDStringW fn(VDGetLoadFileName('src ', ATUIGetMainWindow(), L"Load source file", L"All files (*.*)\0*.*\0", NULL));

	if (!fn.empty()) {
		ATOpenSourceWindow(fn.c_str());
	}
}

void OnCommandDebuggerToggleBreakAtExeRun() {
	IATDebugger *dbg = ATGetDebugger();

	dbg->SetBreakOnEXERunAddrEnabled(!dbg->IsBreakOnEXERunAddrEnabled());
}

void OnCommandDebugToggleAutoReloadRoms() {
	g_sim.SetROMAutoReloadEnabled(!g_sim.IsROMAutoReloadEnabled());
}

void OnCommandDebugToggleAutoLoadKernelSymbols() {
	g_sim.SetAutoLoadKernelSymbolsEnabled(!g_sim.IsAutoLoadKernelSymbolsEnabled());
}

void OnCommandDebugChangeFontDialog() {
	ATUIShowDialogDebugFont(ATUIGetMainWindow());
}

void OnCommandDebugToggleDebugger() {
	if (ATIsDebugConsoleActive()) {
		if (!g_sim.IsRunning())
			ATGetDebugger()->Detach();

		ATCloseConsole();
	} else
		ATOpenConsole();
}

void OnCommandDebugRun() {
	ATGetDebugger()->Run(kATDebugSrcMode_Same);
}

void OnCommandDebugBreak() {
	ATOpenConsole();
	ATGetDebugger()->Break();
}

void OnCommandDebugRunStop() {
	if (g_sim.IsRunning() || ATGetDebugger()->AreCommandsQueued())
		OnCommandDebugBreak();
	else
		OnCommandDebugRun();
}

ATDebugSrcMode ATUIGetDebugSrcMode() {
	ATDebugSrcMode mode = kATDebugSrcMode_Same;

	const uint32 activePaneId = ATUIGetActivePaneId();

	if (activePaneId == kATUIPaneId_Disassembly)
		mode = kATDebugSrcMode_Disasm;

	if (activePaneId >= kATUIPaneId_Source)
		mode = kATDebugSrcMode_Source;

	return mode;
}

void OnCommandDebugStepInto() {
	IATUIDebuggerPane *dbgp = ATUIGetActivePaneAs<IATUIDebuggerPane>();

	if (!dbgp || !dbgp->OnPaneCommand(kATUIPaneCommandId_DebugStepInto)) {
		try {
			ATGetDebugger()->StepInto(ATUIGetDebugSrcMode());
		} catch(const MyError& e) {
			ATConsolePrintf("%s\n", e.c_str());
		}
	}
}

void OnCommandDebugStepOut() {
	IATUIDebuggerPane *dbgp = ATUIGetActivePaneAs<IATUIDebuggerPane>();

	if (!dbgp || !dbgp->OnPaneCommand(kATUIPaneCommandId_DebugStepOut)) {
		try {
			ATGetDebugger()->StepOut(ATUIGetDebugSrcMode());
		} catch(const MyError& e) {
			ATConsolePrintf("%s\n", e.c_str());
		}
	}
}

void OnCommandDebugStepOver() {
	IATUIDebuggerPane *dbgp = ATUIGetActivePaneAs<IATUIDebuggerPane>();

	if (!dbgp || !dbgp->OnPaneCommand(kATUIPaneCommandId_DebugStepOver)) {
		try {
			ATGetDebugger()->StepOver(ATUIGetDebugSrcMode());
		} catch(const MyError& e) {
			ATConsolePrintf("%s\n", e.c_str());
		}
	}
}

void OnCommandDebugToggleBreakpoint() {
	IATUIDebuggerPane *dbgp = ATUIGetActivePaneAs<IATUIDebuggerPane>();

	if (dbgp) {
		try {
			dbgp->OnPaneCommand(kATUIPaneCommandId_DebugToggleBreakpoint);
		} catch(const MyError& e) {
			ATConsolePrintf("%s\n", e.c_str());
		}
	}
}

void OnCommandDebugVerifierDialog() {
	ATUIShowDialogVerifier(ATUIGetMainWindow(), g_sim);
}
