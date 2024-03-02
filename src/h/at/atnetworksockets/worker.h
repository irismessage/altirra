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

#ifndef f_AT_ATNETWORKSOCKETS_WORKER_H
#define f_AT_ATNETWORKSOCKETS_WORKER_H

class IATEmuNetUdpStack;
class IATEmuNetTcpStack;
class IATEmuNetSocketListener;
class IATEmuNetUdpSocketListener;

class IATNetSockWorker : public IVDRefCount {
public:
	virtual IATEmuNetSocketListener *AsSocketListener() = 0;
	virtual IATEmuNetUdpSocketListener *AsUdpListener() = 0;

	virtual void ResetAllConnections() = 0;

	virtual bool GetHostAddressesForLocalAddress(bool tcp, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, ATSocketAddress& hostAddr, ATSocketAddress& remoteAddr) const = 0;
};

void ATCreateNetSockWorker(IATEmuNetUdpStack *udp, IATEmuNetTcpStack *tcp, bool externalAccess, uint32 forwardingAddr, uint16 forwardingPort, IATNetSockWorker **pp);

#endif
