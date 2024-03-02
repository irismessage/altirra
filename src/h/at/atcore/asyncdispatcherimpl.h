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

#ifndef f_AT_ATCORE_ASYNCDISPATCHERIMPL_H
#define f_AT_ATCORE_ASYNCDISPATCHERIMPL_H

#include <deque>
#include <vd2/system/thread.h>
#include <at/atcore/asyncdispatcher.h>

class ATAsyncDispatcher final : public IATAsyncDispatcher 
{
public:
	void SetWakeCallback(vdfunction<void()> fn);

	void RunCallbacks();

public:
	void Queue(uint64 *token, vdfunction<void()> fn) override;
	void Cancel(uint64 *token) override;

private:
	void InternalCancel(uint64 *token);

	vdfunction<void()> mWakeCallback;

	VDCriticalSection mMutex;
	std::deque<vdfunction<void()>> mCallbacks;
	uint64 mHeadToken = 1;
};

#endif
