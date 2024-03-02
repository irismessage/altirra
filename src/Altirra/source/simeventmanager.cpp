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

#include <stdafx.h>
#include "simeventmanager.h"

ATSimulatorEventManager::ATSimulatorEventManager()
	: mCallbacksBusy(0)
	, mbCallbacksChanged(false)
{
}

ATSimulatorEventManager::~ATSimulatorEventManager() {
}

void ATSimulatorEventManager::Shutdown() {
}

void ATSimulatorEventManager::AddCallback(IATSimulatorCallback *cb) {
	Callbacks::const_iterator it(std::find(mCallbacks.begin(), mCallbacks.end(), cb));
	if (it == mCallbacks.end())
		mCallbacks.push_back(cb);
}

void ATSimulatorEventManager::RemoveCallback(IATSimulatorCallback *cb) {
	Callbacks::iterator it(std::find(mCallbacks.begin(), mCallbacks.end(), cb));
	if (it != mCallbacks.end()) {
		if (mCallbacksBusy) {
			*it = NULL;
			mbCallbacksChanged = true;
		} else {
			*it = mCallbacks.back();
			mCallbacks.pop_back();
		}
	}
}

void ATSimulatorEventManager::NotifyEvent(ATSimulatorEvent ev) {
	if (ev == kATSimEvent_AnonymousInterrupt)
		return;

	VDVERIFY(++mCallbacksBusy < 100);

	// Note that this list may change on the fly.
	size_t n = mCallbacks.size();
	for(uint32 i=0; i<n; ++i) {
		IATSimulatorCallback *cb = mCallbacks[i];

		if (cb)
			cb->OnSimulatorEvent(ev);
	}

	VDVERIFY(--mCallbacksBusy >= 0);

	if (!mCallbacksBusy && mbCallbacksChanged) {
		Callbacks::iterator src = mCallbacks.begin();
		Callbacks::iterator dst = src;
		Callbacks::iterator end = mCallbacks.end();

		for(; src != end; ++src) {
			IATSimulatorCallback *cb = *src;

			if (cb) {
				*dst = cb;
				++dst;
			}
		}

		if (dst != end)
			mCallbacks.erase(dst, end);

		mbCallbacksChanged = false;
	}
}
