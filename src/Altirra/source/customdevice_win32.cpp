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
#include <winsock2.h>
#include "customdevice_win32.h"

class ATDeviceCustomNetworkEngine final : public VDThread, public vdrefcounted<IATDeviceCustomNetworkEngine> {
public:
	ATDeviceCustomNetworkEngine(uint16 port);
	~ATDeviceCustomNetworkEngine();

	bool Init();
	void Shutdown() override;

	bool IsConnected() override;
	bool WaitForFirstConnectionAttempt() override;
	bool Restore() override;
	bool Send(const void *data, uint32 len) override;
	bool Recv(void *data, uint32 len) override;
	void SetRecvHandler(vdfunction<void()> fn) override;
	bool SetRecvNotifyEnabled(bool queueCall) override;
	uint32 Peek() override;

public:
	void ThreadRun() override;

private:
	void SignalConnectionAttempt();
	uint16 mPort;

	SOCKET mSocket = INVALID_SOCKET;

	vdfunction<void()> mReceiveHandler;

	VDCriticalSection mMutex;

	VDSignalPersistent mSocketEvent;
	VDSignal mExitRequested;
	VDSignal mConnectionAttempted;
	bool mbRestoreRequired = true;
	bool mbRestorePossible = false;
	bool mbRecvNotifyNeeded = false;

	struct Buffer {
		static constexpr uint32 kBufferSize = 4096;

		void Reset();

		std::pair<const void *, uint32> BeginRead(uint32 n = kBufferSize);
		bool EndRead(uint32 n);

		std::pair<void *, uint32> BeginWrite(uint32 n = kBufferSize);
		bool EndWrite(uint32 n);

		uint32 mLevel = 0;
		uint32 mReadIndex = 0;
		uint32 mWriteIndex = 0;

		bool mWaitingForRead = false;
		bool mWaitingForWrite = false;

		VDSignal mReadEvent;
		VDSignal mWriteEvent;

		uint8 mBuffer[kBufferSize];
	};

	Buffer mRecvBuffer;
	Buffer mSendBuffer;
};

////////////////////////////////////////////////////////////////////////////

void ATDeviceCustomNetworkEngine::Buffer::Reset() {
	mLevel = 0;
	mReadIndex = 0;
	mWriteIndex = 0;
}

std::pair<const void *, uint32> ATDeviceCustomNetworkEngine::Buffer::BeginRead(uint32 n) {
	return { mBuffer + mReadIndex, std::min(n, std::min(mLevel, kBufferSize - mReadIndex)) };
}

bool ATDeviceCustomNetworkEngine::Buffer::EndRead(uint32 n) {
	mLevel -= n;

	mReadIndex += n;
	if (mReadIndex >= kBufferSize)
		mReadIndex = 0;

	if (!mWaitingForRead)
		return false;

	mWaitingForRead = false;
	return true;
}

std::pair<void *, uint32> ATDeviceCustomNetworkEngine::Buffer::BeginWrite(uint32 n) {
	return { mBuffer + mWriteIndex, std::min(n, kBufferSize - std::max(mLevel, mWriteIndex)) };
}

bool ATDeviceCustomNetworkEngine::Buffer::EndWrite(uint32 n) {
	mLevel += n;
	mWriteIndex += n;
	if (mWriteIndex >= kBufferSize)
		mWriteIndex = 0;

	if (!mWaitingForWrite)
		return false;

	mWaitingForWrite = false;
	return true;
}

////////////////////////////////////////////////////////////////////////////

ATDeviceCustomNetworkEngine::ATDeviceCustomNetworkEngine(uint16 port)
	: mPort(port)
{
}

ATDeviceCustomNetworkEngine::~ATDeviceCustomNetworkEngine() {
	Shutdown();
}

bool ATDeviceCustomNetworkEngine::Init() {
	if (!mSocketEvent.getHandle()
		|| !mExitRequested.getHandle()
		|| !mSendBuffer.mReadEvent.getHandle()
		|| !mSendBuffer.mWriteEvent.getHandle()
		|| !mRecvBuffer.mReadEvent.getHandle()
		|| !mRecvBuffer.mWriteEvent.getHandle())
		return false;

	if (!ThreadStart()) {
		return false;
	}

	return true;
}

void ATDeviceCustomNetworkEngine::Shutdown() {
	mExitRequested.signal();
	ThreadWait();
}

bool ATDeviceCustomNetworkEngine::IsConnected() {
	mMutex.Lock();
	bool success = mbRestorePossible;
	mMutex.Unlock();

	return success;
}

bool ATDeviceCustomNetworkEngine::WaitForFirstConnectionAttempt() {
	mConnectionAttempted.wait();

	mMutex.Lock();
	bool success = mbRestorePossible;
	mMutex.Unlock();

	return success;
}

bool ATDeviceCustomNetworkEngine::Restore() {
	bool success = true;

	mMutex.Lock();
	if (mbRestoreRequired) {
		if (mbRestorePossible)
			mbRestoreRequired = false;
		else
			success = false;
	}
	mMutex.Unlock();

	return success;
}

bool ATDeviceCustomNetworkEngine::Send(const void *data, uint32 len) {
	if (!len)
		return true;

	bool success = true;

	mMutex.Lock();

	for(;;) {
		if (mbRestoreRequired) {
			success = false;
			break;
		}

		auto [dst, tc] = mSendBuffer.BeginWrite(len);

		if (tc) {
			if (data) {
				memcpy(dst, data, tc);
				data = (const char *)data + tc;
			} else
				memset(dst, 0, tc);

			len -= tc;

			if (mSendBuffer.EndWrite(tc))
				mSendBuffer.mWriteEvent.signal();

			if (!len)
				break;

		} else {
			mSendBuffer.mWaitingForRead = true;
			mMutex.Unlock();

			mSendBuffer.mReadEvent.wait();

			mMutex.Lock();
		}
	}

	mMutex.Unlock();

	return success;
}

bool ATDeviceCustomNetworkEngine::Recv(void *data, uint32 len) {
	if (!len)
		return true;

	bool success = true;

	mMutex.Lock();

	for(;;) {
		if (mbRestoreRequired) {
			success = false;
			break;
		}

		auto [src, tc] = mRecvBuffer.BeginRead(len);

		if (tc) {
			memcpy(data, src, tc);
			data = (char *)data + tc;
			len -= tc;

			if (mRecvBuffer.EndRead(tc))
				mRecvBuffer.mReadEvent.signal();

			if (!len)
				break;

		} else {
			mRecvBuffer.mWaitingForWrite = true;

			mMutex.Unlock();

			mRecvBuffer.mWriteEvent.wait();

			mMutex.Lock();
		}
	}

	mMutex.Unlock();

	return success;
}

void ATDeviceCustomNetworkEngine::SetRecvHandler(vdfunction<void()> fn) {
	mReceiveHandler = std::move(fn);
}

bool ATDeviceCustomNetworkEngine::SetRecvNotifyEnabled(bool queueCall) {
	bool hadDataAlready;
	bool doCall = false;

	vdsynchronized(mMutex) {
		hadDataAlready = (mRecvBuffer.mLevel > 0);

		if (!hadDataAlready)
			mbRecvNotifyNeeded = true;
		else if (queueCall)
			doCall = true;
	}

	if (doCall)
		mReceiveHandler();

	return !hadDataAlready;
}

uint32 ATDeviceCustomNetworkEngine::Peek() {
	mMutex.Lock();
	uint32 level = mRecvBuffer.mLevel;
	mMutex.Unlock();

	return level;
}

void ATDeviceCustomNetworkEngine::ThreadRun() {
	HANDLE handleTable[] = {
		mSocketEvent.getHandle(),
		mExitRequested.getHandle(),
		mRecvBuffer.mReadEvent.getHandle(),
		mSendBuffer.mWriteEvent.getHandle(),
	};

	bool closeRequested = false;

	for(;;) {
		if (closeRequested) {
			closeRequested = false;
			
			// mark restore required and flush all buffers
			mMutex.Lock();
			mbRestoreRequired = true;
			mbRestorePossible = false;

			mSendBuffer.Reset();
			mRecvBuffer.Reset();

			mRecvBuffer.mWaitingForRead = false;
			mSendBuffer.mWaitingForWrite = false;

			if (mRecvBuffer.mWaitingForWrite)
				mRecvBuffer.mWriteEvent.signal();

			if (mSendBuffer.mWaitingForRead)
				mSendBuffer.mReadEvent.signal();

			mMutex.Unlock();

			mSocketEvent.unsignal();

			if (mSocket != INVALID_SOCKET) {
				closesocket(mSocket);
				mSocket = INVALID_SOCKET;
			}
		}

		DWORD result = WaitForMultipleObjects(4, handleTable, FALSE, mSocket == INVALID_SOCKET ? 1000 : INFINITE);

		bool trySend = false;
		bool tryRecv = false;

		if (result == WAIT_OBJECT_0) {
			// socket event
			if (mSocket == INVALID_SOCKET) {
				// this is a manual event, so if we're in this situation we need to reset the event
				// to prevent a spin loop
				mSocketEvent.unsignal();
				continue;
			}

			WSANETWORKEVENTS events;
			events.lNetworkEvents = 0;

			if (WSAEnumNetworkEvents(mSocket, mSocketEvent.getHandle(), &events)) {
				// if we can't get network events, we can't tell if the socket has been closed either, so
				// we should just nuke everything and restart
				closeRequested = true;
				continue;
			}

			if (events.lNetworkEvents & FD_CLOSE) {
				closeRequested = true;
				continue;
			}

			if (events.lNetworkEvents & FD_CONNECT) {
				SignalConnectionAttempt();

				if (events.iErrorCode[FD_CONNECT_BIT]) {
					// connection failed
					closeRequested = true;
					continue;
				}

				// connection is up -- set flag indicating that restore is possible
				mMutex.Lock();
				mbRestorePossible = true;
				mMutex.Unlock();

				trySend = true;
			}

			if (events.lNetworkEvents & FD_READ) {
				tryRecv = true;
			}

			if (events.lNetworkEvents & FD_WRITE) {
				trySend = true;
			}

		} else if (result == WAIT_OBJECT_0 + 1) {
			// exit requested
			break;
		} else if (result == WAIT_OBJECT_0 + 2) {
			// send buffer has been drained -- queue a recv on the socket if we can
			if (mSocket == INVALID_SOCKET)
				continue;

			tryRecv = true;
		} else if (result == WAIT_OBJECT_0 + 3) {
			// recv buffer has been filled -- queue a send on the socket if we can
			if (mSocket == INVALID_SOCKET)
				continue;

			trySend = true;
		} else if (result == WAIT_TIMEOUT) {
			if (mSocket != INVALID_SOCKET)
				continue;

			mSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

			if (mSocket == INVALID_SOCKET) {
				SignalConnectionAttempt();
				continue;
			}

			// disable nagling on the socket
			BOOL nodelay = TRUE;
			setsockopt(mSocket, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof nodelay);

			// set up for async comm
			if (WSAEventSelect(mSocket, mSocketEvent.getHandle(), FD_READ | FD_WRITE | FD_CONNECT | FD_CLOSE)) {
				closeRequested = true;
				SignalConnectionAttempt();
				continue;
			}

			// queue connect
			sockaddr_in addr {};

			addr.sin_family = AF_INET;
			addr.sin_port = htons(mPort);
			addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			if (connect(mSocket, (const sockaddr *)&addr, sizeof addr) && WSAGetLastError() != WSAEWOULDBLOCK) {
				SignalConnectionAttempt();
				closeRequested = true;
				continue;
			}
		}

		if (tryRecv) {
			for(;;) {
				mMutex.Lock();
				auto span = mRecvBuffer.BeginWrite();

				if (!span.second)
					mRecvBuffer.mWaitingForRead = true;
				else
					mRecvBuffer.mWaitingForRead = false;
				mMutex.Unlock();

				if (!span.second)
					break;

				int actual = recv(mSocket, (char *)span.first, span.second, 0);

				if (actual == SOCKET_ERROR) {
					if (WSAGetLastError() != WSAEWOULDBLOCK)
						closeRequested = true;

					break;
				} else if (actual > 0) {
					mMutex.Lock();
					bool signal = mRecvBuffer.EndWrite((uint32)actual);
					bool notifyNeeded = mbRecvNotifyNeeded;
					if (notifyNeeded)
						mbRecvNotifyNeeded = false;
					mMutex.Unlock();
					if (signal)
						mRecvBuffer.mWriteEvent.signal();

					if (notifyNeeded)
						mReceiveHandler();
				}
			}
		}

		if (trySend) {
			for(;;) {
				mMutex.Lock();
				auto span = mSendBuffer.BeginRead();

				if (!span.second)
					mSendBuffer.mWaitingForWrite = true;
				else
					mSendBuffer.mWaitingForWrite = false;
				mMutex.Unlock();

				if (!span.second)
					break;

				int actual = send(mSocket, (const char *)span.first, span.second, 0);

				if (actual == SOCKET_ERROR) {
					if (WSAGetLastError() != WSAEWOULDBLOCK)
						closeRequested = true;

					break;
				} else {
					mMutex.Lock();
					bool signal = mSendBuffer.EndRead((uint32)actual);
					mMutex.Unlock();
					if (signal)
						mSendBuffer.mReadEvent.signal();
				}
			}
		}
	}

	if (mSocket != INVALID_SOCKET) {
		closesocket(mSocket);
		mSocket = INVALID_SOCKET;
	}
}

void ATDeviceCustomNetworkEngine::SignalConnectionAttempt() {
	mConnectionAttempted.signal();
}

////////////////////////////////////////////////////////////////////////////

vdrefptr<IATDeviceCustomNetworkEngine> ATCreateDeviceCustomNetworkEngine(uint16 localhostPort) {
	vdrefptr<ATDeviceCustomNetworkEngine> p(new ATDeviceCustomNetworkEngine(localhostPort));

	if (!p->Init())
		return nullptr;

	return vdrefptr<IATDeviceCustomNetworkEngine>(p.get());
}
