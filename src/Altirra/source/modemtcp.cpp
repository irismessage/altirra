#include "stdafx.h"
#include <vd2/system/thread.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <windows.h>
#include <winsock2.h>
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

	bool Init(const char *address, uint32 port, IATModemDriverCallback *callback);
	void Shutdown();

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
	void QueueRead();
	void QueueWrite();
	void FlushSpecialReplies();

	IATModemDriverCallback *mpCB;
	VDStringA mAddress;
	uint32 mPort;

	SOCKET mSocket;
	WSAEVENT mCommandEvent;
	WSAEVENT mReadEvent;
	WSAEVENT mWriteEvent;
	WSAEVENT mCloseEvent;
	bool	mbReadPending;
	bool	mbReadEOF;
	bool	mbConnected;
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
		kTS_WaitingForOptionByte
	};

	TelnetState mTelnetState;
};

IATModemDriver *ATCreateModemDriverTCP() {
	return new ATModemDriverTCP;
}

ATModemDriverTCP::ATModemDriverTCP()
	: mSocket(INVALID_SOCKET)
	, mCommandEvent(WSA_INVALID_EVENT)
	, mReadEvent(WSA_INVALID_EVENT)
	, mWriteEvent(WSA_INVALID_EVENT)
	, mCloseEvent(WSA_INVALID_EVENT)
	, mbTelnetEmulation(false)
{
}

ATModemDriverTCP::~ATModemDriverTCP() {
	Shutdown();
}

bool ATModemDriverTCP::Init(const char *address, uint32 port, IATModemDriverCallback *callback) {
	if (address)
		mAddress = address;
	else
		mAddress.clear();

	mPort = port;

	mpCB = callback;
	mWriteQueuedBytes = 0;
	mReadIndex = 0;
	mReadLevel = 0;

	mbExit = false;
	return ThreadStart();
}

void ATModemDriverTCP::Shutdown() {
	mMutex.Lock();
	mbExit = true;
	mMutex.Unlock();
	WSASetEvent(mCommandEvent);
	ThreadWait();
}

void ATModemDriverTCP::SetConfig(const ATRS232Config& config) {
	mbTelnetEmulation = config.mbTelnetEmulation;
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

	if (mReadIndex >= mReadLevel)
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

			if (c == 0xFF) {
				if (mWriteQueuedBytes < (sizeof mWriteBuffer) - 1)
					break;

				mWriteBuffer[mWriteQueuedBytes++] = 0xFF;
			}

			mWriteBuffer[mWriteQueuedBytes++] = c;
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

	mSpecialReplies.clear();
	mSpecialReplyIndex = 0;

	mCommandEvent = WSACreateEvent();
	mReadEvent = WSACreateEvent();
	mWriteEvent = WSACreateEvent();
	mCloseEvent = WSACreateEvent();

	if (mCommandEvent == WSA_INVALID_EVENT ||
		mReadEvent == WSA_INVALID_EVENT ||
		mWriteEvent == WSA_INVALID_EVENT ||
		mCloseEvent == WSA_INVALID_EVENT)
	{
		VDDEBUG("ModemTCP: Unable to create events.\n");
		if (mpCB)
			mpCB->OnEvent(this, kATModemPhase_Init, kATModemEvent_AllocFail);

		WorkerShutdown();
		return;
	}

	mSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (mSocket == INVALID_SOCKET) {
		VDDEBUG("ModemTCP: Unable to create socket.\n");
		if (mpCB)
			mpCB->OnEvent(this, kATModemPhase_Init, kATModemEvent_AllocFail);

		WorkerShutdown();
		return;
	}

	if (mAddress.empty()) {
		sockaddr_in sa = {0};
		sa.sin_port = htons(9000);
		sa.sin_addr.S_un.S_addr = ADDR_ANY;
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
			VDDEBUG("ModemTCP: Unable to enable asyncronous accept.\n");
			if (mpCB)
				mpCB->OnEvent(this, kATModemPhase_Accept, kATModemEvent_GenericError);

			WorkerShutdown();
			return;
		}

		for(;;) {
			sockaddr sa2 = {0};
			int salen = sizeof(sa2);
			SOCKET sock2 = accept(mSocket, &sa2, &salen);

			if (sock2 != INVALID_SOCKET) {
				closesocket(mSocket);
				mSocket = sock2;
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

			for(;;) {
				HANDLE h[2] = { mCommandEvent, mReadEvent };
				DWORD r = WSAWaitForMultipleEvents(2, h, FALSE, INFINITE, FALSE);

				if (r == WAIT_OBJECT_0) {
					OnCommand();

					mMutex.Lock();
					bool exit = mbExit;
					mMutex.Unlock();

					if (exit) {
						WorkerShutdown();
						return;
					}
				} else if (r == WAIT_OBJECT_0 + 1) {
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
		unsigned long ipaddr = inet_addr(mAddress.c_str());

		sockaddr_in sin;
		sin.sin_family = AF_INET;
		sin.sin_port = htons(mPort);

		if (ipaddr != INADDR_NONE) {
			sin.sin_addr.s_addr = ipaddr;
		} else {
			VDDEBUG("ModemTCP: Looking up %s\n", mAddress.c_str());

			const hostent *he = gethostbyname(mAddress.c_str());
			if (he && he->h_addrtype == AF_INET && he->h_length == 4) {
				memcpy(&sin.sin_addr.S_un.S_addr, he->h_addr_list[0], 4);
			} else {
				if (mpCB)
					mpCB->OnEvent(this, kATModemPhase_NameLookup, kATModemEvent_NameLookupFailed);

				WorkerShutdown();
				return;
			}
		}

		VDDEBUG("ModemTCP: Contacting %s\n", mAddress.c_str());

		int cr = connect(mSocket, (const sockaddr *)&sin, sizeof sin);
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
	if (setsockopt(mSocket, SOL_SOCKET, TCP_NODELAY, (const char *)&nodelay, sizeof nodelay)) {
		VDDEBUG("ModemTCP: Unable to disable nagling.\n");
	}

	if (mpCB)
		mpCB->OnEvent(this, kATModemPhase_Connected, kATModemEvent_Connected);

	WSAEventSelect(mSocket, mCloseEvent, FD_CLOSE);

	mbConnected = true;

	QueueRead();

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
			kEventClose,
			kEventRead,
			kEventWrite
		} eventIds[4];
		DWORD numEvents = 0;

		events[numEvents] = mCommandEvent;
		eventIds[numEvents] = kEventCommand;
		++numEvents;

		events[numEvents] = mCloseEvent;
		eventIds[numEvents] = kEventClose;
		++numEvents;

		if (mbReadPending) {
			events[numEvents] = mReadEvent;
			eventIds[numEvents] = kEventRead;
			++numEvents;
		}

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

				case kEventClose:
					{
						WSANETWORKEVENTS events;

						if (!WSAEnumNetworkEvents(mSocket, mCloseEvent, &events)) {
							if (events.lNetworkEvents & FD_CLOSE) {
								DWORD err = events.iErrorCode[FD_CLOSE_BIT];

								mbConnected = false;
							}
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
						mWriteQueuedBytes = 0;
						mMutex.Unlock();
						
						if (WSAGetOverlappedResult(mSocket, &mOverlappedWrite, &actual, TRUE, &flags)) {
							OnWrite();
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
	if (mSocket != INVALID_SOCKET) {
		closesocket(mSocket);
		mSocket = INVALID_SOCKET;
	}

	if (mCommandEvent != WSA_INVALID_EVENT) {
		WSACloseEvent(mCommandEvent);
		mCommandEvent = WSA_INVALID_EVENT;
	}

	if (mCloseEvent != WSA_INVALID_EVENT) {
		WSACloseEvent(mCloseEvent);
		mCloseEvent = WSA_INVALID_EVENT;
	}

	if (mWriteEvent != WSA_INVALID_EVENT) {
		WSACloseEvent(mWriteEvent);
		mWriteEvent = WSA_INVALID_EVENT;
	}

	if (mReadEvent != WSA_INVALID_EVENT) {
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
						case 0xFD:	// DO
						case 0xFE:	// DONT
							state = kTS_WaitingForOptionByte;
							continue;

						default:
							state = kTS_WaitingForIAC;
							continue;
					}
					break;

				case kTS_WaitingForOptionByte:
					// Whatever it is, we won't do it.
					mSpecialReplies.push_back(0xFF);	// IAC
					mSpecialReplies.push_back(0xFC);	// WONT
					mSpecialReplies.push_back(c);		// option
					state = kTS_WaitingForIAC;
					continue;
			}

			*dst++ = c;
		}
		bytes = dst - mReadBuffer;
	}

	mTelnetState = state;

	mMutex.Lock();
	mReadIndex = 0;
	mReadLevel = bytes;
	OutputDebugStringA(VDStringA((const char *)mReadBuffer, (const char *)mReadBuffer + mReadLevel).c_str());
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
		mWriteQueuedBytes = 0;

		if (result == 0) {
			mbWritePending = false;
			OnWrite();
			continue;
		}

		DWORD err = WSAGetLastError();

		if (err == WSA_IO_PENDING)
			break;

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
