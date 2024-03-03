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

#ifndef f_AT_ATNETWORKSOCKETS_NATIVESOCKETS_H
#define f_AT_ATNETWORKSOCKETS_NATIVESOCKETS_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/refcount.h>
#include <at/atnetwork/socket.h>

class IATAsyncDispatcher;

bool ATSocketInit();
void ATSocketPreShutdown();
void ATSocketShutdown();

class IATNetLookupResult : public IVDRefCount {
public:
	virtual void SetOnCompleted(IATAsyncDispatcher *dispatcher, vdfunction<void(const ATSocketAddress&)> fn, bool callIfReady) = 0;

	virtual bool Completed() const = 0;
	virtual bool Succeeded() const = 0;
	virtual const ATSocketAddress& Address() const = 0;
};

vdrefptr<IATNetLookupResult> ATNetLookup(const wchar_t *hostname, const wchar_t *service);
vdrefptr<IATStreamSocket> ATNetConnect(const wchar_t *hostname, const wchar_t *service);
vdrefptr<IATStreamSocket> ATNetConnect(const ATSocketAddress& address);
vdrefptr<IATListenSocket> ATNetListen(const ATSocketAddress& address);
vdrefptr<IATListenSocket> ATNetListen(ATSocketAddressType addressType, uint16 port);

// Create a datagram socket for UDP traffic. Address must be an IPv4 or IPv6 socket,
// though it can bind to 0:0 for unspecified port and 0:port for specified port on
// unspecified address.
//
// If address is an IPv6 address and dualStack=true, then a dual stack IPv6 socket is
// created that can also send and receive IPv4 traffic. In this case, addresses may
// be received as IPv4 wrapped within IPv6. The networking layer will automatically
// wrap IPv4 addresses in IPv6 when sending as needed by the native API.
//
vdrefptr<IATDatagramSocket> ATNetBind(const ATSocketAddress& address, bool dualStack);

#endif
