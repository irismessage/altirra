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

#ifndef f_AT_ATNETWORKSOCKETS_INTERNAL_LOOKUPWORKER_H
#define f_AT_ATNETWORKSOCKETS_INTERNAL_LOOKUPWORKER_H

#include <vd2/system/refcount.h>
#include <vd2/system/thread.h>
#include <vd2/system/VDString.h>
#include <at/atnetwork/socket.h>
#include <at/atnetworksockets/nativesockets.h>
#include <at/atnetworksockets/internal/socketutils.h>

class ATNetLookupResult final : public vdrefcounted<IATNetLookupResult> {
public:
	ATNetLookupResult(ATNetSyncContext& ctx, const wchar_t *nodename, const wchar_t *service);

	bool IsAbandoned() const;
	void SetAddress(const ATSocketAddress& addr);

	void SetOnCompleted(IATAsyncDispatcher *dispatcher, vdfunction<void(const ATSocketAddress&)> fn, bool callIfReady) override;
	bool Completed() const override;
	bool Succeeded() const override;
	const ATSocketAddress& Address() const override;

private:
	friend class ATNetLookupWorker;

	enum class State : int {
		Pending,
		PendingWithCallback,
		Completed
	};

	ATNetSyncContext& mSyncContext;
	IATAsyncDispatcher *mpOnCompleteDispatcher = nullptr;
	uint64 mOnCompleteDispatcherToken = 0;
	vdfunction<void(const ATSocketAddress&)> mpOnCompleteFn = nullptr;

	VDAtomicInt mState;
	ATSocketAddress mAddress;
	VDStringW mNodeName;
	VDStringW mService;
};

class ATNetLookupWorker final : public VDThread {
public:
	ATNetLookupWorker();
	~ATNetLookupWorker();

	bool Init();
	void Shutdown();

	vdrefptr<IATNetLookupResult> Lookup(const wchar_t *nodename, const wchar_t *service);

private:
	void ThreadRun() override;

	vdrefptr<ATNetSyncContext> mpSyncContext;

	VDCriticalSection mMutex;
	VDSignal mWakeSignal;
	bool mbExitRequested = false;

	vdfastdeque<ATNetLookupResult *> mPendingQueue;
};

#endif
