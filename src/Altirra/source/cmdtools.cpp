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
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/Dita/services.h>
#include <at/atio/cassetteimage.h>
#include "uifilefilters.h"
#include "uiaccessors.h"
#include "cassette.h"
#include "simulator.h"

extern ATSimulator g_sim;

void ATUIShowDialogAdvancedConfigurationModeless(VDGUIHandle h);

void ATUIShowDialogCompatDB(VDGUIHandle hParent);

void OnCommandToolsCompatDBDialog() {
	ATUIShowDialogCompatDB(ATUIGetNewPopupOwner());
}

void OnCommandToolsAnalyzeTapeDecoding() {
	VDGUIHandle h = ATUIGetNewPopupOwner();
	VDStringW fn1(VDGetLoadFileName('cass', h, L"Load cassette tape to analyze", g_ATUIFileFilter_LoadTapeAudio, L"wav"));
	if (fn1.empty())
		return;

	VDStringW fn2(VDGetSaveFileName('casa', h, L"Save cassette tape analysis", g_ATUIFileFilter_SaveTapeAnalysis, L"wav"));
	if (fn2.empty())
		return;

	if (VDFileIsPathEqual(fn1.c_str(), fn2.c_str()))
		throw MyError("The analysis file needs to be different from the source tape file.");

	VDFileStream f2(fn2.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kSequential | nsVDFile::kCreateAlways);

	ATCassetteLoadContext ctx;
	g_sim.GetCassette().GetLoadOptions(ctx);
	(void)ATLoadCassetteImage(fn1.c_str(), &f2, ctx);
}

void OnCommandToolsAdvancedConfiguration() {
	ATUIShowDialogAdvancedConfigurationModeless(nullptr);
}
