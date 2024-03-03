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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_ASYNCDISPATCHER_H
#define f_AT_ATCORE_ASYNCDISPATCHER_H

#include <vd2/system/function.h>

class ATAsyncCallback;

// AsyncDispatcher
//
// An async dispatcher allows for cross-thread queuing of callbacks. Callbacks
// queued on an async dispatcher are executed at periodic intervals by the
// owner of the dispatcher, as follows.
//
// An async dispatcher is also available as a device service. The device async
// dispatcher has the following additional guarantees:
//
// - The dispatcher is always called on the emulation thread. It is therefore
//   inherently synchronized with any device methods. An important implication
//   is that it is safe to Cancel() the last token returned by Queue() to
//   ensure that no callbacks will occur after Cancel() returns, because the
//   dispatcher could not be running in parallel to Cancel(). In other words,
//   Cancel(&token) ensures completion or cancellation of any previous calls
//   to Queue(&token).
//
// - Dispatch timing is best-effort but typically runs on a per-scanline basis
//   (~114 cycles) when emulation is running. When it is not running, dispatch
//   can occur immediately, at whatever cycle the emulation is stopped at.
//
// - The dispatcher is available for the lifetime of the device. This does mean
//   that devices either need to ensure they Cancel() any callbacks that may be
//   outstanding. (Callbacks can also be resilient and no-op themselves, but
//   this is considered bad device practice.)

class IATAsyncDispatcher 
{
public:
	static constexpr uint32 kTypeID = 'asdp';

	virtual ~IATAsyncDispatcher() = default;

	// Queue a callback. The callback will be called the next time that the
	// async dispatcher is run, or just deleted if it is still outstanding when
	// the dispatcher shuts down. Callbacks are executed FIFO. There is no
	// immediate evaluation of the callback even if queuing occurs on the
	// dispatching thread; callbacks are always queued and executed at dispatch
	// time.
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
	virtual void Queue(uint64 *token, vdfunction<void()> fn) = 0;

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

IATAsyncDispatcher *ATCreateAsyncDispatcher();

#endif
