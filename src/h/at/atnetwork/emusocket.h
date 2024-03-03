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

#ifndef f_ATNETWORK_EMUSOCKET_H
#define f_ATNETWORK_EMUSOCKET_H

#include <vd2/system/vdtypes.h>

struct ATEthernetAddr;
class IATStreamSocket;
class IATSocketHandler;

class IATEmuNetSocketListener {
public:
	virtual bool OnSocketIncomingConnection(uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, IATStreamSocket *socket, IATSocketHandler **handler) = 0;
};

class IATEmuNetUdpSocketListener {
public:
	virtual void OnUdpDatagram(const ATEthernetAddr& srcHwAddr, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, const void *data, uint32 dataLen) = 0;
};

class IATEmuNetIpStack {
public:
	virtual uint32 GetIpAddress() const = 0;
	virtual uint32 GetIpNetMask() const = 0;

	virtual bool IsLocalOrBroadcastAddress(uint32 ip) const = 0;
};

class IATEmuNetUdpStack {
public:
	virtual IATEmuNetIpStack *GetIpStack() const = 0;

	virtual bool Bind(uint16 port, IATEmuNetUdpSocketListener *listener) = 0;
	virtual uint16 Bind(IATEmuNetUdpSocketListener *listener) = 0;
	virtual void Unbind(uint16 port, IATEmuNetUdpSocketListener *listener) = 0;

	virtual void SendDatagram(uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, const void *data, uint32 dataLen) = 0;
	virtual void SendDatagram(uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, const ATEthernetAddr& dstHwAddr, const void *data, uint32 dataLen) = 0;
};

class IATEmuNetTcpStack {
public:
	virtual bool Connect(uint32 dstIpAddr, uint16 dstPort, IATSocketHandler& handler, IATStreamSocket **newSocket) = 0;
};

#endif

