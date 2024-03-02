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
#include <winsock2.h>
#include <at/atnetworksockets/nativesockets.h>
#include <at/atnetworksockets/internal/lookupworker.h>
#include <at/atnetworksockets/internal/socketworker.h>

bool g_ATWinSockInited = false;
ATNetLookupWorker *g_pATNetLookupWorker;
ATNetSocketWorker *g_pATNetSocketWorker;

bool ATSocketInit() {
	if (!g_ATWinSockInited) {
		WSADATA wsa {};
		if (0 != WSAStartup(MAKEWORD(2, 0), &wsa))
			return false;

		g_ATWinSockInited = true;
	}

	if (!g_pATNetLookupWorker) {
		g_pATNetLookupWorker = new ATNetLookupWorker();
		if (!g_pATNetLookupWorker->Init()) {
			delete g_pATNetLookupWorker;
			g_pATNetLookupWorker = nullptr;
		}
	}

	if (!g_pATNetSocketWorker) {
		g_pATNetSocketWorker = new ATNetSocketWorker();
		if (!g_pATNetSocketWorker->Init()) {
			delete g_pATNetSocketWorker;
			g_pATNetSocketWorker = nullptr;
		}
	}

	return g_ATWinSockInited;
}

void ATSocketPreShutdown() {
	if (g_pATNetSocketWorker) {
		delete g_pATNetSocketWorker;
		g_pATNetSocketWorker = nullptr;
	}

	if (g_pATNetLookupWorker) {
		delete g_pATNetLookupWorker;
		g_pATNetLookupWorker = nullptr;
	}
}

void ATSocketShutdown() {
	ATSocketPreShutdown();

	if (g_ATWinSockInited) {
		g_ATWinSockInited = false;

		WSACleanup();
	}
}

vdrefptr<IATNetLookupResult> ATNetLookup(const wchar_t *hostname, const wchar_t *service) {
	if (!g_pATNetLookupWorker)
		return nullptr;

	return g_pATNetLookupWorker->Lookup(hostname, service);
}

vdrefptr<IATStreamSocket> ATNetConnect(const wchar_t *hostname, const wchar_t *service) {
	if (!g_pATNetLookupWorker || !g_pATNetSocketWorker)
		return nullptr;

	auto socket = g_pATNetSocketWorker->CreateStreamSocket();
	if (!socket)
		return nullptr;

	auto lookup = g_pATNetLookupWorker->Lookup(hostname, service);
	if (!lookup)
		return nullptr;

	lookup->SetOnCompleted(nullptr,
		[lookup, socket](const ATSocketAddress& addr) {
			socket->Connect(addr);
		},
		true
	);

	return socket;
}

vdrefptr<IATStreamSocket> ATNetConnect(const ATSocketAddress& address) {
	if (!g_pATNetSocketWorker)
		return nullptr;

	vdrefptr<ATNetStreamSocket> s = g_pATNetSocketWorker->CreateStreamSocket();
	if (!s)
		return nullptr;

	s->Connect(address);
	return s;
}

vdrefptr<IATListenSocket> ATNetListen(const ATSocketAddress& address) {
	if (!g_pATNetSocketWorker)
		return nullptr;

	vdrefptr<ATNetListenSocket> s = g_pATNetSocketWorker->CreateListenSocket(address);
	if (!s)
		return nullptr;
	
	return s;
}

vdrefptr<IATListenSocket> ATNetListen(ATSocketAddressType addressType, uint16 port) {
	ATSocketAddress address;
	address.mType = addressType;
	address.mPort = port;

	if (addressType == ATSocketAddressType::IPv4) {
		address.mIPv4Address = ntohl(INADDR_ANY);
	} else if (addressType == ATSocketAddressType::IPv6) {
		memset(address.mIPv6.mAddress, 0, sizeof address.mIPv6.mAddress);
		address.mIPv6.mScopeId = 0;
	}

	return ATNetListen(address);
}

vdrefptr<IATDatagramSocket> ATNetBind(const ATSocketAddress& address, bool dualStack) {
	if (!g_pATNetSocketWorker)
		return nullptr;

	vdrefptr<ATNetDatagramSocket> s = g_pATNetSocketWorker->CreateDatagramSocket(address, dualStack);
	if (!s)
		return nullptr;
	
	return s;
}
