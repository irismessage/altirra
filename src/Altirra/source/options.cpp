//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2011 Avery Lee
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
#include <vd2/system/registry.h>
#include "options.h"

ATOptions::ATOptions()
	: mbDisplayDDraw(true)
	, mbDisplayD3D9(true)
	, mbDisplay3D(false)
	, mbDisplayOpenGL(false)
	, mbSingleInstance(false)
	, mErrorMode(kATErrorMode_Dialog)
	, mFullScreenWidth(0)
	, mFullScreenHeight(0)
	, mFullScreenRefreshRate(0)
{
}

void ATOptionsExchange(VDRegistryKey& key, bool write, const char *name, bool& val) {
	if (write)
		key.setBool(name, val);
	else
		val = key.getBool(name, val);
}

void ATOptionsExchange(VDRegistryKey& key, bool write, const char *name, uint32& val) {
	if (write)
		key.setInt(name, val);
	else
		val = key.getInt(name, val);
}

void ATOptionsExchange(VDRegistryKey& key, bool write, const char *name, VDStringA& val) {
	if (write)
		key.setString(name, val.c_str());
	else
		key.getString(name, val);
}

template<class T>
void ATOptionsExchangeEnum(VDRegistryKey& key, bool write, const char *name, T& val, T limit) {
	if (write)
		key.setInt(name, val);
	else
		val = (T)key.getEnumInt(name, limit, val);
}

void ATOptionsExchange(VDRegistryKey& key, bool write, ATOptions& opts) {
	ATOptionsExchange(key, write, "Startup: Reuse program instance", opts.mbSingleInstance);
	ATOptionsExchange(key, write, "Display: DirectDraw", opts.mbDisplayDDraw);
	ATOptionsExchange(key, write, "Display: Direct3D9", opts.mbDisplayD3D9);
	ATOptionsExchange(key, write, "Display: 3D", opts.mbDisplay3D);
	ATOptionsExchange(key, write, "Display: OpenGL", opts.mbDisplayOpenGL);
	ATOptionsExchangeEnum(key, write, "Simulator: Error mode", opts.mErrorMode, kATErrorModeCount);
	ATOptionsExchange(key, write, "Display: Full screen mode width", opts.mFullScreenWidth);
	ATOptionsExchange(key, write, "Display: Full screen mode height", opts.mFullScreenHeight);
	ATOptionsExchange(key, write, "Display: Full screen mode refresh rate", opts.mFullScreenRefreshRate);
	ATOptionsExchange(key, write, "Flash: SIC! cartridge flash mode", opts.mSICFlashChip);
}

void ATOptionsLoad() {
	VDRegistryAppKey key("Settings", false);

	g_ATOptions = ATOptions();
	ATOptionsExchange(key, false, g_ATOptions);
}

void ATOptionsSave() {
	if (!g_ATOptions.mbDirty)
		return;

	VDRegistryAppKey key("Settings");

	ATOptionsExchange(key, true, g_ATOptions);
	g_ATOptions.mbDirty = false;
}

///////////////////////////////////////////////////////////////////////////

namespace {
	struct UpdateEntry {
		ATOptionsUpdateCallback mpCallback;
		void *mpData;
	};
}

vdfastvector<UpdateEntry> g_ATOptionsUpdateEntries;

void ATOptionsAddUpdateCallback(bool runNow, ATOptionsUpdateCallback cb, void *data) {
	UpdateEntry& e = g_ATOptionsUpdateEntries.push_back();
	e.mpCallback = cb;
	e.mpData = data;

	if (runNow)
		cb(g_ATOptions, NULL, data);
}

void ATOptionsRunUpdateCallbacks(const ATOptions *prevOpts) {
	for(vdfastvector<UpdateEntry>::const_iterator it = g_ATOptionsUpdateEntries.begin(), itEnd = g_ATOptionsUpdateEntries.end();
		it != itEnd;
		++it)
	{
		it->mpCallback(g_ATOptions, prevOpts, it->mpData);
	}
}

///////////////////////////////////////////////////////////////////////////

ATOptions g_ATOptions;
