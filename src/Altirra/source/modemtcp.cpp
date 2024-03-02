//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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

#include "stdafx.h"
#include <vd2/system/thread.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "ui.h"
#include "modemtcp.h"
#include "rs232.h"

#ifdef _MSC_VER
	#pragma comment(lib, "ws2_32.lib")
#endif

class ATModemDriverTCP : public IATModemDriver, public VDThread {
public:
	ATModemDriverTCP();
	~ATModemDriverTCP();

	bool Init(const char *address, const char *service, uint32 port, IATModemDriverCallback *callback);
	void Shutdown();

	bool GetLastIncomingAddress(VDStringA& address, uint32& port);

	void SetConfig(const ATRS232Config& config);

	uint32 Write(const void *data, uint32 len);
	uint32 Write(const void *data, uint32 len, bool escapeChars);

	uint32 Read(void *buf, uint32 len);

protected:
	void ThreadRun();
	void WorkerShutdown();
	void OnCommand();
	void OnRead(uint32 bytes);
	void OnWrite();
	void OnOOB();
	void QueueRead();
	void QueueWrite();
	void FlushSpecialReplies();

	IATModemDriverCallback *mpCB;
	VDStringA mAddress;
	VDStringA mService;
	uint32 mPort;

	VDStringA mIncomingAddress;
	uint32 mIncomingPort;

	VDSignal	mThreadInited;
	SOCKET mSocket;
	SOCKET mSocket2;
	WSAEVENT mCommandEvent;
	WSAEVENT mReadEvent;
	WSAEVENT mWriteEvent;
	WSAEVENT mExtraEvent;
	bool	mbReadPending;
	bool	mbReadEOF;
	bool	mbConnected;
	bool	mbListenIPv6;
	WSABUF		mReadHeader;
	WSABUF		mWriteHeader;
	WSAOVERLAPPED mOverlappedRead;
	WSAOVERLAPPED mOverlappedWrite;

	VDAtomicInt	mbTelnetEmulation;

	// begin mutex protected members
	VDCriticalSection	mMutex;
	uint32	mWriteQueuedBytes;
	bool	mbWritePending;
	bool	mbExit;

	uint32	mReadIndex;
	uint32	mReadLevel;

	uint8	mReadBuffer[4096];
	uint8	mWriteBuffer[4096];
	// end mutex protected members

	vdfastvector<uint8> mSpecialReplies;
	uint32	mSpecialReplyIndex;

	enum TelnetState {
		kTS_Disabled,
		kTS_WaitingForIAC,
		kTS_WaitingForCommandByte,
		kTS_WaitingForDoOptionByte,
		kTS_WaitingForDontOptionByte,
		kTS_WaitingToDiscardWillOptionByte,
		kTS_WaitingToDiscardWontOptionByte
	};

	TelnetState mTelnetState;
	bool		mbTelnetReceivingSBParams;
	bool		mbTelnetListeningMode;
	bool		mbTelnetWaitingForEchoResponse;
	bool		mbTelnetWaitingForSGAResponse;

	bool		mbTelnetLFConversion;
	bool		mbTelnetSawIncomingCR;
	bool		mbTelnetSawOutgoingCR;
	bool		mbTelnetSawIncomingATASCII;
};

IATModemDriver *ATCreateModemDriverTCP() {
	return new ATModemDriverTCP;
}

ATModemDriverTCP::ATModemDriverTCP()
	: mSocket(INVALID_SOCKET)
	, mSocket2(INVALID_SOCKET)
	, mCommandEvent(WSA_INVALID_EVENT)
	, mReadEvent(WSA_INVALID_EVENT)
	, mWriteEvent(WSA_INVALID_EVENT)
	, mExtraEvent(WSA_INVALID_EVENT)
	, mbListenIPv6(true)
	, mbTelnetEmulation(false)
	, mbTelnetReceivingSBParams(false)
	, mbTelnetLFConversion(false)
	, mbTelnetSawIncomingCR(false)
	, mbTelnetSawOutgoingCR(false)
	, mbTelnetSawIncomingATASCII(false)
{
}

ATModemDriverTCP::~ATModemDriverTCP() {
	Shutdown();
}

bool ATModemDriverTCP::Init(const char *address, const char *service, uint32 port, IATModemDriverCallback *callback) {
	if (address)
		mAddress = address;
	else
		mAddress.clear();

	if (service)
		mService = service;
	else
		mService.clear();

	mPort = port;

	mIncomingAddress.clear();
	mIncomingPort = 0;

	mpCB = callback;
	mWriteQueuedBytes = 0;
	mReadIndex = 0;
	mReadLevel = 0;

	mbTelnetListeningMode = mAddress.empty();
	mbTelnetSawIncomingCR = false;
	mbTelnetSawOutgoingCR = false;

	mThreadInited.tryWait(0);

	mbExit = false;
	if (!ThreadStart())
		return false;

	// wait for initialization
	HANDLE h[2] = {mThreadInited.getHandle(), getThreadHandle()};
	WaitForMultipleObjects(2, h, FALSE, INFINITE);

	return true;
}

void ATModemDriverTCP::Shutdown() {
	mMutex.Lock();
	mbExit = true;
	mMutex.Unlock();
	WSASetEvent(mCommandEvent);
	ThreadWait();
}

bool ATModemDriverTCP::GetLastIncomingAddress(VDStringA& address, uint32& port) {
	mMutex.Lock();
	address = mIncomingAddress;
	port = mIncomingPort;
	mMutex.Unlock();

	return !address.empty();
}

void ATModemDriverTCP::SetConfig(const ATRS232Config& config) {
	mbTelnetEmulation = config.mbTelnetEmulation;
	mbTelnetLFConversion = mbTelnetEmulation && config.mbTelnetLFConversion;
	mbListenIPv6 = config.mbListenForIPv6;
}

uint32 ATModemDriverTCP::Read(void *buf, uint32 len) {
	if (!len)
		return 0;

	mMutex.Lock();
	uint32 tc = mReadLevel - mReadIndex;

	if (tc > len)
		tc = len;

	memcpy(buf, mReadBuffer + mReadIndex, tc);
	mReadIndex += tc;

	if (tc && mReadIndex >= mReadLevel)
		WSASetEvent(mCommandEvent);
	mMutex.Unlock();

	return tc;
}

uint32 ATModemDriverTCP::Write(const void *data, uint32 len) {
	return Write(data, len, true);
}

uint32 ATModemDriverTCP::Write(const void *data, uint32 len, bool escapeChars) {
	if (!len)
		return 0;

	mMutex.Lock();
	bool wasZero = (mWriteQueuedBytes == 0);

	uint32 tc;
	if (escapeChars) {
		const uint8 *data8 = (const uint8 *)data;

		while(len && mWriteQueuedBytes < sizeof mWriteBuffer) {
			const uint8 c = *data8++;
			--len;

			if (mbTelnetLFConversion && !mbTelnetSawIncomingATASCII) {
				if (c == 0x0D)
					mbTelnetSawOutgoingCR = true;
				else if (mbTelnetSawOutgoingCR) {
					mbTelnetSawOutgoingCR = false;

					// drop LF after CR (we would have already transmitted it)
					if (c == 0x0A)
						continue;
				}
			}
			
			if (c == 0xFF) {
				if (mWriteQueuedBytes < (sizeof mWriteBuffer) - 1)
					break;

				mWriteBuffer[mWriteQueuedBytes++] = 0xFF;
			}

			mWriteBuffer[mWriteQueuedBytes++] = c;

			if (c == 0x0D && mbTelnetLFConversion && !mbTelnetSawIncomingATASCII) {
				if (mWriteQueuedBytes < sizeof mWriteBuffer)
					mWriteBuffer[mWriteQueuedBytes++] = 0x0A;
			}
		}

		tc = data8 - (const uint8 *)data;
	} else {
		tc = sizeof mWriteBuffer - mWriteQueuedBytes;

		if (tc > len)
			tc = len;

		memcpy(mWriteBuffer + mWriteQueuedBytes, data, tc);
		mWriteQueuedBytes += tc;
	}

	if (wasZero)
		WSASetEvent(mCommandEvent);
	mMutex.Unlock();

	return tc;
}

void ATModemDriverTCP::ThreadRun() {
	mbReadPending = false;
	mbWritePending = false;
	mbConnected = false;
	mbReadEOF = false;
	mTelnetState = kTS_WaitingForIAC;
	mbTelnetReceivingSBParams = false;

	mSpecialReplies.clear();
	mSpecialReplyIndex = 0;

	mCommandEvent = WSACreateEvent();
	mReadEvent = WSACreateEvent();
	mWriteEvent = WSACreateEvent();
	mExtraEvent = WSACreateEvent();

	if (mCommandEvent == WSA_INVALID_EVENT ||
		mReadEvent == WSA_INVALID_EVENT ||
		mWriteEvent == WSA_INVALID_EVENT ||
		mExtraEvent == WSA_INVALID_EVENT)
	{
		VDDEBUG("ModemTCP: Unable to create events.\n");
		if (mpCB)
			mpCB->OnEvent(this, kATModemPhase_Init, kATModemEvent_AllocFail);

		WorkerShutdown();
		return;
	}

	mThreadInited.signal();

	if (mAddress.empty()) {
		// create IPv4 listening socket
		mSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (mSocket == INVALID_SOCKET) {
			VDDEBUG("ModemTCP: Unable to create socket.\n");
			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Init, kATModemEvent_AllocFail);

			WorkerShutdown();
			return;
		}

		sockaddr_in sa = {0};
		sa.sin_port = htons(mPort);
		sa.sin_addr.S_un.S_addr = INADDR_ANY;
		sa.sin_family = AF_INET;
		if (bind(mSocket, (sockaddr *)&sa, sizeof sa)) {
			VDDEBUG("ModemTCP: Unable to bind socket.\n");
			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Listen, kATModemEvent_GenericError);

			WorkerShutdown();
			return;
		}

		BOOL reuse = TRUE;
		setsockopt(mSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof reuse);

		if (listen(mSocket, 1)) {
			DWORD err = WSAGetLastError();

			VDDEBUG("ModemTCP: Unable to enable listening on socket.\n");
			if (mpCB) {
				ATModemEvent event = kATModemEvent_GenericError;

				if (err == WSAEADDRINUSE)
					event = kATModemEvent_LineInUse;
				else if (err == WSAENETDOWN)
					event = kATModemEvent_NoDialTone;

				mpCB->OnEvent(this, kATModemPhase_Listen, event);
			}

			WorkerShutdown();
			return;
		}

		if (SOCKET_ERROR == WSAEventSelect(mSocket, mReadEvent, FD_ACCEPT)) {
			VDDEBUG("ModemTCP: Unable to enable asynchronous accept.\n");
			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Accept, kATModemEvent_GenericError);

			WorkerShutdown();
			return;
		}

		// create IPv6 listening socket (OK for this to fail)
		if (mbListenIPv6) {
			mSocket2 = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

			if (mSocket2 != INVALID_SOCKET) {
				sockaddr_in6 sa6 = {0};
				sa6.sin6_port = htons(mPort);
				sa6.sin6_family = AF_INET6;
				if (!bind(mSocket2, (sockaddr *)&sa6, sizeof sa6)) {
					// hey... we successfully bound to IPv6!
					BOOL reuse = TRUE;
					setsockopt(mSocket2, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof reuse);

					if (!listen(mSocket2, 1)) {
						if (SOCKET_ERROR == WSAEventSelect(mSocket2, mWriteEvent, FD_ACCEPT)) {
							closesocket(mSocket2);
							mSocket2 = INVALID_SOCKET;
						}
					} else {
						closesocket(mSocket2);
						mSocket2 = INVALID_SOCKET;
					}
				} else {
					closesocket(mSocket2);
					mSocket2 = INVALID_SOCKET;
				}
			}
		}

		for(;;) {
			union {
				char buf[256];
				sockaddr addr;
			} sa2 = {0};
			int salen = sizeof(sa2);
			SOCKET sock2 = accept(mSocket, &sa2.addr, &salen);

			if (sock2 == INVALID_SOCKET && mSocket2 != INVALID_SOCKET)
				sock2 = accept(mSocket2, &sa2.addr, &salen);

			if (sock2 != INVALID_SOCKET) {
				closesocket(mSocket);

				if (mSocket2 != INVALID_SOCKET)
					closesocket(mSocket2);

				mSocket = sock2;

				// we're connected... grab the incoming address before we send the connected
				// event
				vdfastvector<char> namebuf(NI_MAXHOST, 0);
				vdfastvector<char> servbuf(NI_MAXSERV, 0);
				int revresult = getnameinfo(&sa2.addr, salen, namebuf.data(), NI_MAXHOST, servbuf.data(), NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);

				mMutex.Lock();
				if (!revresult) { 
					mIncomingAddress = namebuf.data();
					mIncomingPort = atoi(servbuf.data());
				} else {
					mIncomingAddress.clear();
					mIncomingPort = 0;
				}
				mMutex.Unlock();

				VDDEBUG("ModemTCP: Inbound connection accepted.\n");
				break;
			}

			if (WSAGetLastError() != WSAEWOULDBLOCK) {
				VDDEBUG("ModemTCP: accept() call failed.\n");

				if (mpCB)
					mpCB->OnEvent(this, kATModemPhase_Accept, kATModemEvent_GenericError);

				WorkerShutdown();
				return;
			}

			HANDLE h[3] = { mCommandEvent, mReadEvent, mWriteEvent };
			for(;;) {
				DWORD r = WSAWaitForMultipleEvents(mSocket2 != INVALID_SOCKET ? 3 : 2, h, FALSE, INFINITE, FALSE);

				if (r == WAIT_OBJECT_0) {
					OnCommand();

					mMutex.Lock();
					WSAResetEvent(mCommandEvent);
					bool exit = mbExit;
					mMutex.Unlock();

					if (exit) {
						WorkerShutdown();
						return;
					}
				} else if (r == WAIT_OBJECT_0 + 1) {
					WSANETWORKEVENTS events;
					WSAEnumNetworkEvents(mSocket, mReadEvent, &events);
					break;
				} else if (r == WAIT_OBJECT_0 + 2) {
					WSANETWORKEVENTS events;
					WSAEnumNetworkEvents(mSocket2, mWriteEvent, &events);

					std::swap(mSocket, mSocket2);
					break;
				} else {
					VDDEBUG("ModemTCP: WFME() failed.\n");

					if (mpCB)
						mpCB->OnEvent(this, kATModemPhase_Accept, kATModemEvent_GenericError);

					WorkerShutdown();
					return;
				}
			}
		}
	} else {
		VDDEBUG("ModemTCP: Looking up %s:%s\n", mAddress.c_str(), mService.c_str());

		addrinfo hint = {0};
		hint.ai_family = AF_INET;
		hint.ai_socktype = SOCK_STREAM;

		addrinfo *results = NULL;
		if (getaddrinfo(mAddress.c_str(), mService.c_str(), &hint, &results)) {
			VDDEBUG("ModemTCP: Name lookup failed.\n");

			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_NameLookup, kATModemEvent_NameLookupFailed);

			WorkerShutdown();
			return;
		}

		VDDEBUG("ModemTCP: Contacting %s:%s\n", mAddress.c_str(), mService.c_str());

		int cr = -1;
		
		for(addrinfo *p = results; p; p = p->ai_next) {
			mMutex.Lock();
			bool exit = mbExit;
			mMutex.Unlock();

			if (exit) {
				freeaddrinfo(results);
				WorkerShutdown();
				goto xit;
			}

			if (p->ai_socktype != SOCK_STREAM)
				continue;

			if (p->ai_family != PF_INET && p->ai_family != PF_INET6)
				continue;

			mSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
			if (mSocket != INVALID_SOCKET)
				cr = connect(mSocket, p->ai_addr, p->ai_addrlen);

			if (!cr)
				break;

			closesocket(mSocket);
			mSocket = INVALID_SOCKET;
		}

		freeaddrinfo(results);

		if (cr) {
			VDDEBUG("ModemTCP: Unable to connect.\n");
			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Connecting, kATModemEvent_ConnectFailed);
			
			WorkerShutdown();
			return;
		}

		VDDEBUG("ModemTCP: Contacted %s\n", mAddress.c_str());
	}

	BOOL nodelay = TRUE;
	if (setsockopt(mSocket, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof nodelay)) {
		VDDEBUG("ModemTCP: Unable to disable nagling.\n");
	}

	if (mpCB)
		mpCB->OnEvent(this, kATModemPhase_Connected, kATModemEvent_Connected);

	WSAEventSelect(mSocket, mExtraEvent, FD_CLOSE | FD_OOB);

	mbConnected = true;
	mbTelnetWaitingForEchoResponse = false;
	mbTelnetWaitingForSGAResponse = false;

	QueueRead();

	if (mbTelnetListeningMode && mbTelnetEmulation) {
		// Ask the client to begin line mode negotiation.
		mSpecialReplies.push_back(0xFF);	// IAC
		mSpecialReplies.push_back(0xFB);	// WILL
		mSpecialReplies.push_back(0x01);	// ECHO
		mSpecialReplies.push_back(0xFF);	// IAC
		mSpecialReplies.push_back(0xFD);	// DO
		mSpecialReplies.push_back(0x03);	// SUPPRESS-GO-AHEAD
		mSpecialReplies.push_back(0xFF);	// IAC
		mSpecialReplies.push_back(0xFD);	// DO
		mSpecialReplies.push_back(0x22);	// LINEMODE (RFC 1184)
		mbTelnetWaitingForEchoResponse = true;
		mbTelnetWaitingForSGAResponse = true;

		FlushSpecialReplies();
	}

	mbTelnetSawIncomingCR = false;
	mbTelnetSawIncomingATASCII = false;

	for(;;) {
		if (!mbConnected && mbReadEOF) {
			mMutex.Lock();
			bool readDone = (mReadIndex >= mReadLevel);
			mMutex.Unlock();

			if (readDone) {
				if (mpCB)
					mpCB->OnEvent(this, kATModemPhase_Connected, kATModemEvent_ConnectionDropped);

				break;
			}
		}

		WSAEVENT events[4];
		enum {
			kEventCommand,
			kEventExtra,
			kEventRead,
			kEventWrite
		} eventIds[4];
		DWORD numEvents = 0;

		events[numEvents] = mCommandEvent;
		eventIds[numEvents] = kEventCommand;
		++numEvents;

		if (mbReadPending) {
			events[numEvents] = mReadEvent;
			eventIds[numEvents] = kEventRead;
			++numEvents;
		}

		// this needs to be after the read event
		events[numEvents] = mExtraEvent;
		eventIds[numEvents] = kEventExtra;
		++numEvents;

		if (mbWritePending) {
			events[numEvents] = mWriteEvent;
			eventIds[numEvents] = kEventWrite;
			++numEvents;
		}

		DWORD waitResult = WSAWaitForMultipleEvents(numEvents, events, FALSE, INFINITE, TRUE);

		if (waitResult >= WSA_WAIT_EVENT_0 && waitResult < WSA_WAIT_EVENT_0 + numEvents) {
			switch(eventIds[waitResult - WSA_WAIT_EVENT_0]) {
				case kEventCommand:
					OnCommand();
					{
						mMutex.Lock();
						WSAResetEvent(mCommandEvent);
						bool exit = mbExit;

						if (exit) {
							mMutex.Unlock();
							goto xit;
						}

						bool shouldWrite = !mbWritePending && mWriteQueuedBytes;
						bool shouldRead = !mbReadPending && mReadIndex >= mReadLevel;
						mMutex.Unlock();

						if (shouldWrite)
							QueueWrite();

						if (shouldRead)
							QueueRead();
					}
					break;

				case kEventExtra:
					{
						WSANETWORKEVENTS events;

						if (!WSAEnumNetworkEvents(mSocket, mExtraEvent, &events)) {
							if (events.lNetworkEvents & FD_CLOSE) {
								DWORD err = events.iErrorCode[FD_CLOSE_BIT];

								mbConnected = false;
								mbReadEOF = true;

								if (err) {
									mReadIndex = 0;
									mReadLevel = 0;
								}

								if (mpCB)
									mpCB->OnEvent(this, kATModemPhase_Connected, kATModemEvent_ConnectionClosing);
							}

							if (events.lNetworkEvents & FD_OOB) {
								OnOOB();
							}
						} else {
							WSAResetEvent(mExtraEvent);
						}
					}
					break;

				case kEventRead:
					{
						DWORD actual = 0;
						DWORD flags = 0;

						mbReadPending = false;

						if (WSAGetOverlappedResult(mSocket, &mOverlappedRead, &actual, TRUE, &flags)) {
							if (actual)
								OnRead(actual);
						}

						QueueRead();
					}
					break;

				case kEventWrite:
					{
						DWORD actual;
						DWORD flags;
						
						mMutex.Lock();
						mbWritePending = false;
						mMutex.Unlock();
						
						if (WSAGetOverlappedResult(mSocket, &mOverlappedWrite, &actual, TRUE, &flags)) {
							mMutex.Lock();
							if (mWriteQueuedBytes > actual) {
								memmove(mWriteBuffer, mWriteBuffer + actual, mWriteQueuedBytes - actual);
								mWriteQueuedBytes -= actual;
							} else {
								mWriteQueuedBytes = 0;
							}
							mMutex.Unlock();

							OnWrite();
						} else {
							// Ugh... just pretend we sent everything.
							mMutex.Lock();
							mWriteQueuedBytes = 0;
							mMutex.Unlock();
						}

						QueueWrite();
					}
					break;
			}
		} else if (waitResult == WAIT_FAILED)
			break;
	}

xit:
	WorkerShutdown();
}

void ATModemDriverTCP::WorkerShutdown() {
	if (mSocket2 != INVALID_SOCKET) {
		shutdown(mSocket2, SD_SEND);
		closesocket(mSocket2);
		mSocket2 = INVALID_SOCKET;
	}

	if (mSocket != INVALID_SOCKET) {
		shutdown(mSocket, SD_SEND);
		closesocket(mSocket);
		mSocket = INVALID_SOCKET;
	}

	if (mCommandEvent != WSA_INVALID_EVENT) {
		WSACloseEvent(mCommandEvent);
		mCommandEvent = WSA_INVALID_EVENT;
	}

	if (mExtraEvent != WSA_INVALID_EVENT) {
		WSACloseEvent(mExtraEvent);
		mExtraEvent = WSA_INVALID_EVENT;
	}

	if (mWriteEvent != WSA_INVALID_EVENT) {
		if (mbWritePending)
			WSAWaitForMultipleEvents(1, &mWriteEvent, FALSE, INFINITE, FALSE);

		WSACloseEvent(mWriteEvent);
		mWriteEvent = WSA_INVALID_EVENT;
	}

	if (mReadEvent != WSA_INVALID_EVENT) {
		if (mbReadPending)
			WSAWaitForMultipleEvents(1, &mReadEvent, FALSE, INFINITE, FALSE);

		WSACloseEvent(mReadEvent);
		mReadEvent = WSA_INVALID_EVENT;
	}
}

void ATModemDriverTCP::OnCommand() {
}

void ATModemDriverTCP::OnRead(uint32 bytes) {
	if (!bytes) {
		mbReadEOF = true;
		return;
	}

	// Parse the read buffer and strip out any special commands. We immediately
	// queue replies for these.
	uint8 *dst = mReadBuffer;
	TelnetState state = mTelnetState;
	bool sbparams = mbTelnetReceivingSBParams;

	if (!mbTelnetEmulation) {
		state = kTS_WaitingForIAC;
	} else {
		for(uint32 i=0; i<bytes; ++i) {
			uint8 c = mReadBuffer[i];

			switch(state) {
				case kTS_WaitingForIAC:
					if (c == 0xFF) {
						state = kTS_WaitingForCommandByte;
						continue;
					}
					break;

				case kTS_WaitingForCommandByte:
					switch(c) {
						case 0xF0:	// SE
							sbparams = false;
							continue;
						case 0xFA:	// SB
							sbparams = true;
							continue;
						case 0xFB:	// WILL
							state = kTS_WaitingToDiscardWillOptionByte;
							continue;
						case 0xFC:	// WONT
							state = kTS_WaitingToDiscardWontOptionByte;
							continue;

						case 0xFD:	// DO
							state = kTS_WaitingForDoOptionByte;
							continue;

						case 0xFE:	// DONT
							state = kTS_WaitingForDontOptionByte;
							continue;

						case 0xFF:	// escape
							break;

						default:
							state = kTS_WaitingForIAC;
							continue;
					}
					break;

				case kTS_WaitingForDoOptionByte:
					switch(c) {
						case 0x01:	// ECHO
							if (mbTelnetWaitingForEchoResponse) {
								mbTelnetWaitingForEchoResponse = false;
								break;
							}

							if (mbTelnetListeningMode) {
								// This is a lie (we don't know what the Atari will do).
								mSpecialReplies.push_back(0xFF);	// IAC
								mSpecialReplies.push_back(0xFE);	// DONT
								mSpecialReplies.push_back(c);		// option
							} else {
								// We don't support local echoing.
								mSpecialReplies.push_back(0xFF);	// IAC
								mSpecialReplies.push_back(0xFC);	// WONT
								mSpecialReplies.push_back(c);		// option
							}
							break;

						case 0x03:	// SUPPRESS-GO-AHEAD
							// We do this.
							mSpecialReplies.push_back(0xFF);	// IAC
							mSpecialReplies.push_back(0xFB);	// WILL
							mSpecialReplies.push_back(c);		// option
							break;

						default:
							// Whatever it is, we won't do it.
							mSpecialReplies.push_back(0xFF);	// IAC
							mSpecialReplies.push_back(0xFC);	// WONT
							mSpecialReplies.push_back(c);		// option
							break;
					}

					state = kTS_WaitingForIAC;
					continue;

				case kTS_WaitingForDontOptionByte:
					switch(c) {
						case 0x03:
							mSpecialReplies.push_back(0xFF);	// IAC
							mSpecialReplies.push_back(0xFB);	// WILL
							mSpecialReplies.push_back(c);		// option
							break;

						default:
							// Whatever it is, we're already not doing it.
							mSpecialReplies.push_back(0xFF);	// IAC
							mSpecialReplies.push_back(0xFC);	// WONT
							mSpecialReplies.push_back(c);		// option
							break;
					}
					state = kTS_WaitingForIAC;
					continue;

				case kTS_WaitingToDiscardWillOptionByte:
					VDDEBUG("ModemTCP/Telnet: Client will %02x\n", c);

					switch(c) {
						case 0x01:	// ECHO
							if (mbTelnetListeningMode) {
								mSpecialReplies.push_back(0xFF);	// IAC
								mSpecialReplies.push_back(0xFD);	// DO
								mSpecialReplies.push_back(c);		// option
							} else {
								mSpecialReplies.push_back(0xFF);	// IAC
								mSpecialReplies.push_back(0xFD);	// DO
								mSpecialReplies.push_back(c);		// option
							}
							break;

						case 0x03:	// SUPPRESS-GO-AHEAD
							if (mbTelnetWaitingForSGAResponse) {
								mbTelnetWaitingForSGAResponse = false;
								break;
							}

							mSpecialReplies.push_back(0xFF);	// IAC
							mSpecialReplies.push_back(0xFD);	// DO
							mSpecialReplies.push_back(c);		// option
							break;

						case 0x22:	// LINEMODE
							if (mbTelnetListeningMode) {
								// Heeeey... turns out this telnet client supports line mode.
								// Let's turn it off.
								mSpecialReplies.push_back(0xFF);	// IAC
								mSpecialReplies.push_back(0xFA);	// SB
								mSpecialReplies.push_back(0x22);	// LINEMODE
								mSpecialReplies.push_back(0x01);	// MODE
								mSpecialReplies.push_back(0x00);	// 0
								mSpecialReplies.push_back(0xFF);	// IAC
								mSpecialReplies.push_back(0xF0);	// SE
							}

							// fall through

						default:
							mSpecialReplies.push_back(0xFF);	// IAC
							mSpecialReplies.push_back(0xFE);	// DONT
							mSpecialReplies.push_back(c);		// option
							break;
					}

					state = kTS_WaitingForIAC;
					continue;

				case kTS_WaitingToDiscardWontOptionByte:
					if (mbTelnetListeningMode) {
						VDDEBUG("ModemTCP/Telnet: Client wont %02x\n", c);
						switch(c) {
							case 0x01:	// ECHO
								mSpecialReplies.push_back(0xFF);	// IAC
								mSpecialReplies.push_back(0xFD);	// DONT
								mSpecialReplies.push_back(c);		// option
								break;
						}
					}

					state = kTS_WaitingForIAC;
					continue;
			}

			if (!sbparams) {
				if (mbTelnetListeningMode && mbTelnetLFConversion && !mbTelnetSawIncomingATASCII) {
					if (c == 0x9B)
						mbTelnetSawIncomingATASCII = true;
					else if (c == 0x0D)
						mbTelnetSawIncomingCR = true;
					else if (mbTelnetSawIncomingCR) {
						mbTelnetSawIncomingCR = false;

						if (c == 0x0A)
							continue;
					}
				}

				*dst++ = c;
			}
		}

		bytes = dst - mReadBuffer;
	}

	mTelnetState = state;
	mbTelnetReceivingSBParams = sbparams;

	mMutex.Lock();
	mReadIndex = 0;
	mReadLevel = bytes;
	mMutex.Unlock();

	FlushSpecialReplies();

	if (mpCB && bytes)
		mpCB->OnReadAvail(this, bytes);
}

void ATModemDriverTCP::OnWrite() {
	// Dump any special replies into the write buffer first; these have priority.
	FlushSpecialReplies();

	if (mpCB)
		mpCB->OnWriteAvail(this);
}

void ATModemDriverTCP::OnOOB() {
	char buf[128];

	WSABUF header;
	header.buf = buf;
	header.len = sizeof buf;

	DWORD actual;
	DWORD flags = MSG_OOB;
	WSARecv(mSocket, &header, 1, &actual, &flags, NULL, NULL);
}

void ATModemDriverTCP::QueueRead() {
	for(;;) {
		if (mbReadPending || mbReadEOF || mReadIndex < mReadLevel)
			break;

		mMutex.Lock();
		mReadIndex = 0;
		mReadLevel = 0;
		mReadHeader.buf = (char *)mReadBuffer;
		mReadHeader.len = sizeof mReadBuffer;
		mbReadPending = true;
		mMutex.Unlock();

		mOverlappedRead.hEvent = mReadEvent;

		DWORD actual = 0;
		DWORD flags = 0;
		WSAResetEvent(mReadEvent);
		int result = WSARecv(mSocket, &mReadHeader, 1, &actual, &flags, &mOverlappedRead, NULL);

		if (result == 0) {
			mMutex.Lock();
			mbReadPending = false;
			mMutex.Unlock();
			OnRead(actual);
			continue;
		}

		DWORD err = WSAGetLastError();
		if (err == WSA_IO_PENDING)
			break;

		mbReadPending = false;

		if (mpCB) {
			ATModemEvent ev = kATModemEvent_GenericError;

			if (err == WSAECONNABORTED || err == WSAECONNRESET)
				ev = kATModemEvent_ConnectionDropped;

			mpCB->OnEvent(this, kATModemPhase_Connected, ev);
			break;
		}
	}
}

void ATModemDriverTCP::QueueWrite() {
	mMutex.Lock();
	for(;;) {
		if (!mbConnected) {
			// just swallow data
			mWriteQueuedBytes = 0;
			break;
		}

		if (!mWriteQueuedBytes || mbWritePending)
			break;

		mWriteHeader.buf = (char *)mWriteBuffer;
		mWriteHeader.len = mWriteQueuedBytes;
		mbWritePending = true;

		mMutex.Unlock();

		mOverlappedWrite.hEvent = mWriteEvent;

		DWORD actual = 0;
		WSAResetEvent(mWriteEvent);
		int result = WSASend(mSocket, &mWriteHeader, 1, &actual, 0, &mOverlappedWrite, NULL);

		mMutex.Lock();

		if (result == 0) {
			mbWritePending = false;

			if (actual >= mWriteQueuedBytes) {
				mWriteQueuedBytes = 0;
			} else {
				memmove(mWriteBuffer, mWriteBuffer + actual, mWriteQueuedBytes - actual);
				mWriteQueuedBytes -= actual;
			}

			mMutex.Unlock();

			OnWrite();

			mMutex.Lock();
			continue;
		}

		DWORD err = WSAGetLastError();

		if (err == WSA_IO_PENDING)
			break;

		mbWritePending = false;

		if (mpCB) {
			ATModemEvent ev = kATModemEvent_GenericError;

			if (err == WSAECONNABORTED || err == WSAECONNRESET)
				ev = kATModemEvent_ConnectionDropped;

			mpCB->OnEvent(this, kATModemPhase_Connected, ev);
			break;
		}

	}
	mMutex.Unlock();
}

void ATModemDriverTCP::FlushSpecialReplies() {
	uint32 sn = mSpecialReplies.size();
	uint32 si = mSpecialReplyIndex;
	if (si < sn) {
		si += Write(mSpecialReplies.data() + si, sn - si, false);

		if (si >= sn) {
			si = 0;
			mSpecialReplies.clear();
		}

		mSpecialReplyIndex = si;
		QueueWrite();
	}
}
