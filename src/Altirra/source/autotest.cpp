//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009 Avery Lee
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
#include "autotest.h"
#include <vd2/system/cmdline.h>
#include <vd2/system/VDString.h>
#include "simulator.h"
#include "console.h"

extern ATSimulator g_sim;

uint32 ATExecuteAutotestCommand(const wchar_t *cmd, IATAutotestReplyPort *replyPort) {
	VDCommandLine cmdLine(cmd);
	const VDStringSpanW& s = cmdLine(0);
	uint32 n = cmdLine.GetCount();

	if (s == L"setmode") {
		for(uint32 i=1; i<n; ++i) {
			const VDStringSpanW& t = cmdLine(i);

			if (t == L"ntsc")
				g_sim.SetPALMode(false);
			else if (t == L"pal")
				g_sim.SetPALMode(true);
			else if (t == L"800") {
				g_sim.SetHardwareMode(kATHardwareMode_800);
				g_sim.SetKernelMode(kATKernelMode_OSB);
			} else if (t == L"800XL") {
				g_sim.SetHardwareMode(kATHardwareMode_800XL);
				g_sim.SetKernelMode(kATKernelMode_XL);
			} else if (t == L"48K") {
				g_sim.SetMemoryMode(kATMemoryMode_48K);
			} else if (t == L"64K") {
				g_sim.SetMemoryMode(kATMemoryMode_64K);
			} else if (t == L"128K") {
				g_sim.SetMemoryMode(kATMemoryMode_128K);
			} else if (t == L"320K") {
				g_sim.SetMemoryMode(kATMemoryMode_320K);
			} else if (t == L"siopatch") {
				g_sim.SetDiskSIOPatchEnabled(true);
			} else if (t == L"nosiopatch") {
				g_sim.SetDiskSIOPatchEnabled(false);
			} else if (t == L"fpaccel") {
				g_sim.SetFPPatchEnabled(true);
			} else if (t == L"nofpaccel") {
				g_sim.SetFPPatchEnabled(false);
			} else if (t == L"turbo") {
				g_sim.SetTurboModeEnabled(true);
			} else if (t == L"noturbo") {
				g_sim.SetTurboModeEnabled(false);
			} else {
				return 0;
			}
		}

		return 1;
	} else if (s == L"coldreset") {
		g_sim.ColdReset();
		return 1;
	} else if (s == L"warmreset") {
		g_sim.WarmReset();
		return 1;
	} else if (s == L"suspend") {
		g_sim.Suspend();
	} else if (s == L"resume") {
		g_sim.Resume();
	} else if (s == L"load") {
		const wchar_t *fn = cmdLine[1];

		if (!fn)
			return 0;

		g_sim.Load(fn, false, false, NULL);
		return 1;
	} else if (s == L"closedebugger") {
		ATCloseConsole();
		return 1;
	} else if (s == L"querystate") {
		if (g_sim.IsRunning())
			return 2;
		else
			return 1;
	} else if (s == L"querytime") {
		return g_sim.GetAntic().GetTimestamp();
	}

	return 0;
}
