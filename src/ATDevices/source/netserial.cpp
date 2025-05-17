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

#include <stdafx.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/timerservice.h>
#include <at/atnetworksockets/nativesockets.h>

void ATCreateDeviceNetSerial(const ATPropertySet& pset, IATDevice **dev);

extern const ATDeviceDefinition g_ATDeviceDefNetSerial = { "netserial", "netserial", L"Networked serial port", ATCreateDeviceNetSerial };

class ATDeviceNetSerial final : public ATDeviceT<IATDeviceSerial>
{
public:
	ATDeviceNetSerial();
	~ATDeviceNetSerial();

public:
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void Init() override;
	void Shutdown() override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void GetSettings(ATPropertySet& pset) override;
	bool SetSettings(const ATPropertySet& pset) override;
	void ColdReset() override;
	bool GetErrorStatus(uint32 idx, VDStringW& error) override;

public:
	void SetOnStatusChange(const vdfunction<void(const ATDeviceSerialStatus&)>& fn) override;
	void SetTerminalState(const ATDeviceSerialTerminalState&) override;
	ATDeviceSerialStatus GetStatus() override;
	void SetOnReadReady(vdfunction<void()> fn) override;
	bool Read(uint32& baudRate, uint8& c) override;
	bool Read(uint32 baudRate, uint8& c, bool& framingError) override;
	void Write(uint32 baudRate, uint8 c) override;
	void FlushBuffers() override;

private:
	void OnIncomingConnectionIPv4(const ATSocketStatus& status);
	void OnIncomingConnectionIPv6(const ATSocketStatus& status);
	void OnAcceptedConnection(vdrefptr<IATStreamSocket> socket);
	void OnDataEvent(const ATSocketStatus& status);

	void OnFailedListen4();
	void OnFailedListen6();
	void OnFailedConnect();
	void TryReconnect();
	void UpdateSockets();
	void CloseSockets();

	void UpdateStatus();

	vdfunction<void()> mpOnReadReady;
	IATAsyncDispatcher *mpAsyncDispatcher = nullptr;
	uint64 mAsyncCallback = 0;

	IATTimerService *mpTimerService = nullptr;
	uint64 mTimerToken = 0;

	VDStringW mConnectAddress;
	bool mbListenEnabled = false;
	bool mbCanRead = false;
	uint32 mPort = 0;
	uint32 mBaudRate = 1;

	enum class Status : uint8 {
		NotConnected,
		Connecting,
		Connected
	};

	Status mStatus = Status::NotConnected;

	vdrefptr<IATListenSocket> mpListenSocket4;
	vdrefptr<IATListenSocket> mpListenSocket6;
	vdrefptr<IATStreamSocket> mpDataSocket;
};

ATDeviceNetSerial::ATDeviceNetSerial() {
}

ATDeviceNetSerial::~ATDeviceNetSerial() {
	Shutdown();
}

void ATDeviceNetSerial::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefNetSerial;
}

void ATDeviceNetSerial::Init() {
	mpAsyncDispatcher = GetService<IATAsyncDispatcher>();
	mpTimerService = GetService<IATTimerService>();
	UpdateSockets();
}

void ATDeviceNetSerial::Shutdown() {
	CloseSockets();

	if (mpTimerService) {
		mpTimerService->Cancel(&mTimerToken);
		mpTimerService = nullptr;
	}

	if (mpAsyncDispatcher) {
		mpAsyncDispatcher->Cancel(&mAsyncCallback);
		mpAsyncDispatcher = nullptr;
	}
}

void ATDeviceNetSerial::GetSettingsBlurb(VDStringW& buf) {
	if (mbListenEnabled)
		buf.sprintf(L"listen on port %u/tcp at %u baud", mPort, mBaudRate);
	else if (mConnectAddress.empty())
		buf = L"<not set up>";
	else if (mConnectAddress.find(L':') != mConnectAddress.npos)
		buf.sprintf(L"connect to [%ls]:%u at %u baud", mConnectAddress.c_str(), mPort, mBaudRate);
	else
		buf.sprintf(L"connect to %ls:%u at %u baud", mConnectAddress.c_str(), mPort, mBaudRate);
}

void ATDeviceNetSerial::GetSettings(ATPropertySet& pset) {
	pset.Clear();

	if (!mConnectAddress.empty())
		pset.SetString("connect_addr", mConnectAddress.c_str());

	pset.SetUint32("port", mPort);
	pset.SetUint32("baud_rate", mBaudRate);

	if (mbListenEnabled)
		pset.SetBool("listen", true);
}

bool ATDeviceNetSerial::SetSettings(const ATPropertySet& pset) {
	VDStringW connectAddress(pset.GetString("connect_addr", L""));
	uint32 port = pset.GetUint32("port", 0);
	bool listen = pset.GetBool("listen", false);

	mBaudRate = std::clamp<uint32>(pset.GetUint32("baud_rate", 31250), 1, 1000000);

	if (mConnectAddress != connectAddress || mPort != port || mbListenEnabled != listen) {
		mConnectAddress = connectAddress;
		mPort = port;
		mbListenEnabled = listen;
		UpdateSockets();
	}

	return true;
}

void ATDeviceNetSerial::ColdReset() {
	FlushBuffers();
}

bool ATDeviceNetSerial::GetErrorStatus(uint32 idx, VDStringW& error) {
	if (mStatus != Status::Connected && !idx--) {
		switch(mStatus) {
			case Status::Connected:
				break;

			case Status::Connecting:
				error = L"Connecting";
				break;

			case Status::NotConnected:
				if (mbListenEnabled)
					error = L"No incoming connection";
				else
					error = L"Unable to connect";
				break;
		}

		return true;
	}

	if (mbListenEnabled) {
		if (!mpListenSocket4 && !idx--) {
			error = L"Cannot bind IPv4 socket";
			return true;
		}

		if (!mpListenSocket6 && !idx--) {
			error = L"Cannot bind IPv6 socket";
			return true;
		}
	}

	return false;
}

void ATDeviceNetSerial::SetOnStatusChange(const vdfunction<void(const ATDeviceSerialStatus&)>& fn) {
}

void ATDeviceNetSerial::SetTerminalState(const ATDeviceSerialTerminalState& state) {
}

ATDeviceSerialStatus ATDeviceNetSerial::GetStatus() {
	return {};
}

void ATDeviceNetSerial::SetOnReadReady(vdfunction<void()> fn) {
	mpOnReadReady = std::move(fn);
}

bool ATDeviceNetSerial::Read(uint32& baudRate, uint8& c) {
	baudRate = 0;

	if (!mpDataSocket || mpDataSocket->Recv(&c, 1) < 1) {
		mbCanRead = false;
		return false;
	}

	baudRate = mBaudRate;
	return true;
}

bool ATDeviceNetSerial::Read(uint32 baudRate, uint8& c, bool& framingError) {
	framingError = false;

	uint32 transmitRate;
	if (!Read(transmitRate, c))
		return false;

	// check for more than a 5% discrepancy in baud rates between modem and serial port
	if (abs((int)baudRate - (int)transmitRate) * 20 > (int)transmitRate) {
		// baud rate mismatch -- return some bogus character and flag a framing error
		c = 'U';
		framingError = true;
	}

	return true;
}

void ATDeviceNetSerial::Write(uint32 baudRate, uint8 c) {
	if (mpDataSocket) {
		// drop byte if it is >5% from baud rate
		if (!baudRate || abs((int)mBaudRate - (int)baudRate) * 20 <= (int)mBaudRate)
			mpDataSocket->Send(&c, 1);
	}
}

void ATDeviceNetSerial::FlushBuffers() {
	if (mpDataSocket) {
		char buf[64];

		while(mpDataSocket->Recv(buf, sizeof buf) > 0) {
			// eat data
		}
	}
}

void ATDeviceNetSerial::OnIncomingConnectionIPv4(const ATSocketStatus& status) {
	if (!mpListenSocket4)
		return;

	if (status.mbClosed) {
		OnFailedListen4();
		mpListenSocket4 = nullptr;
		return;
	}

	if (status.mbCanAccept) {
		vdrefptr<IATStreamSocket> socket = mpListenSocket4->Accept();
		if (socket)
			OnAcceptedConnection(std::move(socket));
	}
}

void ATDeviceNetSerial::OnIncomingConnectionIPv6(const ATSocketStatus& status) {
	if (!mpListenSocket6)
		return;

	if (status.mbClosed) {
		OnFailedListen6();
		mpListenSocket6 = nullptr;
		return;
	}

	if (status.mbCanAccept) {
		vdrefptr<IATStreamSocket> socket = mpListenSocket6->Accept();
		if (socket)
			OnAcceptedConnection(std::move(socket));
	}
}

void ATDeviceNetSerial::OnAcceptedConnection(vdrefptr<IATStreamSocket> socket) {
	if (mpDataSocket) {
		// We already have a connection. If the current connection hasn't been
		// gracefully closed, then ditch the new connection. Otherwise, replace
		// the old connection.
		if (!mpDataSocket->GetSocketStatus().mbRemoteClosed) {
			socket->CloseSocket(true);
			return;
		}

		mpDataSocket->SetOnEvent(nullptr, nullptr, false);
		mpDataSocket->CloseSocket(true);
		mpDataSocket = nullptr;
		UpdateStatus();

		if (!mbListenEnabled)
			OnFailedConnect();
	}

	mpDataSocket = std::move(socket);
	mpDataSocket->SetOnEvent(
		mpAsyncDispatcher,
		std::bind_front(&ATDeviceNetSerial::OnDataEvent, this),
		true
	);
}

void ATDeviceNetSerial::OnDataEvent(const ATSocketStatus& status) {
	if (!mpDataSocket)
		return;

	if (status.mbClosed) {
		mpDataSocket->SetOnEvent(nullptr, nullptr, false);
		mpDataSocket->CloseSocket(true);
		mpDataSocket = nullptr;
		UpdateStatus();

		if (!mbListenEnabled)
			OnFailedConnect();
		return;
	}

	UpdateStatus();

	if (status.mbCanRead) {
		mbCanRead = true;

		if (mpOnReadReady)
			mpOnReadReady();
	}
}

void ATDeviceNetSerial::OnFailedListen4() {
	NotifyStatusChanged();
}

void ATDeviceNetSerial::OnFailedListen6() {
	NotifyStatusChanged();
}

void ATDeviceNetSerial::OnFailedConnect() {
	mpTimerService->Request(&mTimerToken, 3.0f, std::bind_front(&ATDeviceNetSerial::TryReconnect, this));
}

void ATDeviceNetSerial::TryReconnect() {
	if (mbListenEnabled || mpDataSocket)
		return;

	UpdateSockets();
}

void ATDeviceNetSerial::UpdateSockets() {
	CloseSockets();

	if (!mpAsyncDispatcher)
		return;

	if (mPort >= 1 && mPort <= 65535) {
		if (mbListenEnabled) {
			mpListenSocket4 = ATNetListen(ATSocketAddressType::IPv4, mPort);
			mpListenSocket6 = ATNetListen(ATSocketAddressType::IPv6, mPort);

			if (mpListenSocket4) {
				mpListenSocket4->SetOnEvent(
					mpAsyncDispatcher,
					std::bind_front(&ATDeviceNetSerial::OnIncomingConnectionIPv4, this),
					true
				);
			}

			if (mpListenSocket6) {
				mpListenSocket6->SetOnEvent(
					mpAsyncDispatcher,
					std::bind_front(&ATDeviceNetSerial::OnIncomingConnectionIPv6, this),
					true
				);
			}
		} else if (!mConnectAddress.empty()) {
			VDStringW portStr;
			portStr.sprintf(L"%u", mPort);
			mpDataSocket = ATNetConnect(mConnectAddress.c_str(), portStr.c_str());

			if (mpDataSocket) {
				mpDataSocket->SetOnEvent(
					mpAsyncDispatcher,
					std::bind_front(&ATDeviceNetSerial::OnDataEvent, this),
					true
				);
			} else {
				OnFailedConnect();
			}
		}
	}

	UpdateStatus();
}

void ATDeviceNetSerial::CloseSockets() {
	if (mpDataSocket) {
		mpDataSocket->CloseSocket(true);
		mpDataSocket = nullptr;
	}

	if (mpListenSocket4) {
		mpListenSocket4->CloseSocket(true);
		mpListenSocket4 = nullptr;
	}

	if (mpListenSocket6) {
		mpListenSocket6->CloseSocket(true);
		mpListenSocket6 = nullptr;
	}
}

void ATDeviceNetSerial::UpdateStatus() {
	Status status = Status::NotConnected;

	if (mpDataSocket) {
		status = Status::Connected;

		auto sockStatus = mpDataSocket->GetSocketStatus();
		if (sockStatus.mbConnecting)
			status = Status::Connecting;
	}

	if (mStatus != status) {
		mStatus = status;

		NotifyStatusChanged();
	}
}

///////////////////////////////////////////////////////////////////////////

void ATCreateDeviceNetSerial(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceNetSerial> p(new ATDeviceNetSerial);

	*dev = p;
	(*dev)->AddRef();
}
