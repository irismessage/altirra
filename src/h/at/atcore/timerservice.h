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

#ifndef f_AT_ATCORE_TIMERQUEUE_H
#define f_AT_ATCORE_TIMERQUEUE_H

#include <vd2/system/function.h>
#include <vd2/system/unknown.h>

class IATAsyncDispatcher;

// TimerService
//
// The timer service allows for queuing callbacks in real time, as opposed to
// emulation time. This is useful for lazy flush delays that aren't in the
// closed loop emulation path, such as flushing data out to disk. It differs
// from the other scheduling services in a few ways:
//
// - Delays are in real time, not emulation time.
// - Not deterministic or strictly ordered, like Scheduler. TimerService is
//   deliberately not exact so that timers can be adjusted if the time base
//   suddenly jumps or the system is suspended for an extended amount of time.
// - Deliberately has low guaranteed resolution so as to allow for coalescing
//   callbacks.
//
// Currently, the timer service uses a low precision of 50ms, to keep timer
// overhead low.
//
class IATTimerService
{
public:
	static constexpr uint32 kTypeID = "IATTimerService"_vdtypeid;

	virtual ~IATTimerService() = default;

	// Queue a callback to be called approximately the given number of seconds
	// in real time. There is no guarantee of order.
	//
	// An optional token variable may be supplied to receive a cancellation
	// token. This should be initialized to zero. If supplied, it is set to the
	// token before Queue() receives or the callback is called, whichever is
	// first. Cancellation tokens don't hold onto anything and don't need to
	// be 'freed'.
	//
	// If the token already referred to a callback that has not been called yet,
	// it is cancelled first before the new function is queued. However, it is
	// possible for the new callback to be queued while the previous one is
	// executing.
	//
	virtual void Request(uint64 *token, float delay, vdfunction<void()> fn) = 0;

	// Attempt to cancel a queued callback, if it has not been called yet.
	// Regardless of whether it is successful, the token variable is reset to
	// 0. It is safe to Cancel() with a null pointer, a zero (invalid) token,
	// or a token for a callback that has already occurred or been cancelled.
	//
	// If Cancel() is executed on the dispatching thread, it is guaranteed that
	// the callback is not called after Cancel() returns. Otherwise, this is
	// not guaranteed as the callback may be executing concurrently, and
	// Cancel() does not block on the executing callback.
	//
	virtual void Cancel(uint64 *token) = 0;
};

IATTimerService *ATCreateTimerService(IATAsyncDispatcher&);

#endif
