//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_TIMERSERVICEIMPL_H
#define f_AT_ATCORE_TIMERSERVICEIMPL_H

#include <windows.h>
#include <threadpoolapiset.h>
#include <vd2/system/thread.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/timerservice.h>

class IATAsyncDispatcher;

class ATTimerService final : public IATTimerService
{
public:
	ATTimerService(IATAsyncDispatcher& dispatcher);
	~ATTimerService();

public:
	using Callback = vdfunction<void()>;
	void Request(uint64 *token, float delay, Callback fn) override;
	void Cancel(uint64 *token) override;

private:
	void InternalCancel(uint64 token, Callback& cb);
	void RunCallbacks();
	uint32 Sink(uint32 pos, uint64 val);

	void RearmTimerForTickDelay(uint64 ticks);
	static void CALLBACK StaticTimerCallback(PTP_CALLBACK_INSTANCE inst, PVOID ctx, PTP_TIMER timer);
	void TimerCallback();

	VDCriticalSection mMutex;

	PTP_TIMER mTimer = nullptr;
	uint64 mRunToken = 0;
	IATAsyncDispatcher *mpAsyncDispatcher = nullptr;

	struct Slot {
		uint64 mDeadline = 0;
		uint32 mSequenceNo = 0x1234ABCD;
		sint32 mHeapIndex = -1;
		Callback mCallback;
	};

	vdfastvector<uint32> mHeap;
	vdvector<Slot> mSlots;
	vdfastvector<uint32> mFreeSlots;
};

#endif
