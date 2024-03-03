#ifndef f_AT_ATNETWORK_GATEWAYSERVER_H
#define f_AT_ATNETWORK_GATEWAYSERVER_H

#include <vd2/system/refcount.h>

class IATEmuNetSocketListener;
class IATEmuNetUdpSocketListener;
class IATEmuNetUdpStack;
class IATEmuNetTcpStack;

struct ATNetConnectionInfo {
	uint8 mRemoteAddr[4];
	uint8 mLocalAddr[4];
	uint16 mRemotePort;
	uint16 mLocalPort;
	const char *mpState;
	const char *mpProtocol;
};

class IATEthernetGatewayServer : public IVDRefCount {
public:
	virtual void Init(IATEthernetSegment *seg, uint32 clockIndex, uint32 netaddr, uint32 netmask, bool natEnabled) = 0;
	virtual void Shutdown() = 0;

	virtual void ColdReset() = 0;

	virtual IATEmuNetUdpStack *GetUdpStack() = 0;
	virtual IATEmuNetTcpStack *GetTcpStack() = 0;

	virtual void SetBridgeListener(IATEmuNetSocketListener *p, IATEmuNetUdpSocketListener *udp) = 0;

	virtual void GetConnectionInfo(vdfastvector<ATNetConnectionInfo>& connInfo) const = 0;
};

void ATCreateEthernetGatewayServer(IATEthernetGatewayServer **);

#endif
