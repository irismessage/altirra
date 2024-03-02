//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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

#ifndef f_AT_HLEPROGRAMLOADER_H
#define f_AT_HLEPROGRAMLOADER_H

class ATCPUEmulator;
class ATCPUHookManager;
class ATSimulator;
class ATSimulatorEventManager;
struct ATCPUHookNode;

class ATHLEProgramLoader {
	ATHLEProgramLoader(const ATHLEProgramLoader&);
	ATHLEProgramLoader& operator=(const ATHLEProgramLoader&);
public:
	ATHLEProgramLoader();
	~ATHLEProgramLoader();

	void Init(ATCPUEmulator *cpu, ATSimulatorEventManager *simEventMan, ATSimulator *sim);
	void Shutdown();

	void SetRandomizeMemoryOnLoad(bool enable) { mbRandomizeMemoryOnLoad = enable; }

	bool IsLaunchPending() const { return mbLaunchPending; }

	void LoadProgram(const wchar_t *symbolHintPath, IVDRandomAccessStream& stream);

protected:
	uint8 OnDSKINV(uint16);
	uint8 OnLoadContinue(uint16);

	uint8 LoadProgramHookCont();
	void UnloadProgramSymbols();

	ATCPUEmulator *mpCPU;
	ATCPUHookManager *mpCPUHookMgr;
	ATSimulatorEventManager *mpSimEventMgr;
	ATSimulator *mpSim;
	ATCPUHookNode *mpLaunchHook;
	ATCPUHookNode *mpLoadContinueHook;

	vdfastvector<uint8>		mProgramToLoad;
	ptrdiff_t	mProgramLoadIndex;

	bool		mbRandomizeMemoryOnLoad;
	bool		mbLaunchPending;
	uint32		mProgramModuleIds[4];
};

#endif	// f_AT_HLEPROGRAMLOADER_H
