//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
#include <vd2/system/vdstl.h>
#include <at/atcore/asyncdispatcherimpl.h>

void ATAsyncDispatcher::SetWakeCallback(vdfunction<void()> fn) {
	mWakeCallback = std::move(fn);
}

void ATAsyncDispatcher::RunCallbacks() {
	vdfunction<void()> fn;

	for(;;) {
		// Ordinarily we swap to a temp list under lock and then process it out of lock,
		// but that would prevent cancellation of requests within that window, so for
		// now we eat the cost of locking for each callback.
		vdsynchronized(mMutex) {
			if (mCallbacks.empty())
				break;

			mHeadToken += 2;
			fn = std::move(mCallbacks.front());
			mCallbacks.pop_front();
		}

		if (fn)
			fn();
	}
}

void ATAsyncDispatcher::Queue(uint64 *token, vdfunction<void()> fn) {
	bool isFirst = false;

	vdsynchronized(mMutex) {
		if (mCallbacks.empty())
			isFirst = true;

		InternalCancel(token);

		*token = mHeadToken + 2 * (uint64)mCallbacks.size();

		mCallbacks.emplace_back(std::move(fn));
	}

	if (isFirst && mWakeCallback)
		mWakeCallback();
}

void ATAsyncDispatcher::Cancel(uint64 *token) {
	vdsynchronized(mMutex) {
		InternalCancel(token);
	}
}

void ATAsyncDispatcher::InternalCancel(uint64 *token) {
	if (!(*token & 1)) {
		VDASSERT(*token == 0);
		return;
	}

	const uint64 offset = (uint64)((*token - mHeadToken) >> 1);
	*token = 0;

	if (offset < mCallbacks.size())
		mCallbacks[offset] = nullptr;
}

IATAsyncDispatcher *ATCreateAsyncDispatcher() {
	return new ATAsyncDispatcher;
}
