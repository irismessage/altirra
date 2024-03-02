//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2012 Avery Lee
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

#ifndef f_AT_SIMEVENTMANAGER_H
#define f_AT_SIMEVENTMANAGER_H

#include <vd2/system/vdstl.h>

enum ATSimulatorEvent {
	kATSimEvent_None,
	kATSimEvent_AnonymousInterrupt,
	kATSimEvent_CPUSingleStep,
	kATSimEvent_CPUStackBreakpoint,
	kATSimEvent_CPUPCBreakpoint,
	kATSimEvent_CPUPCBreakpointsUpdated,
	kATSimEvent_CPUIllegalInsn,
	kATSimEvent_CPUNewPath,
	kATSimEvent_ReadBreakpoint,
	kATSimEvent_WriteBreakpoint,
	kATSimEvent_DiskSectorBreakpoint,
	kATSimEvent_EndOfFrame,
	kATSimEvent_ScanlineBreakpoint,
	kATSimEvent_VerifierFailure,
	kATSimEvent_ColdReset,
	kATSimEvent_WarmReset,
	kATSimEvent_FrameTick,
	kATSimEvent_EXELoad,
	kATSimEvent_EXEInitSegment,
	kATSimEvent_EXERunSegment,
	kATSimEvent_StateLoaded,
	kATSimEvent_AbnormalDMA
};

class IATSimulatorCallback {
public:
	virtual void OnSimulatorEvent(ATSimulatorEvent ev) = 0;
};

class ATSimulatorEventManager {
	ATSimulatorEventManager(const ATSimulatorEventManager&);
	ATSimulatorEventManager& operator=(const ATSimulatorEventManager&);
public:
	ATSimulatorEventManager();
	~ATSimulatorEventManager();

	void Shutdown();

	void AddCallback(IATSimulatorCallback *cb);
	void RemoveCallback(IATSimulatorCallback *cb);

	void NotifyEvent(ATSimulatorEvent ev);

protected:
	typedef vdfastvector<IATSimulatorCallback *> Callbacks;
	Callbacks	mCallbacks;
	int			mCallbacksBusy;
	bool		mbCallbacksChanged;
};

#endif	// f_AT_SIMEVENTMANAGER_H
