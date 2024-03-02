#ifndef f_AT_ATNETWORKSOCKETS_WORKER_H
#define f_AT_ATNETWORKSOCKETS_WORKER_H

class IATNetSockWorker : public IVDRefCount {
public:
	virtual IATSocketListener *AsSocketListener() = 0;
	virtual IATUdpSocketListener *AsUdpListener() = 0;

	virtual void ResetAllConnections() = 0;

	virtual bool GetHostAddressForLocalAddress(bool tcp, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, uint32& hostIp, uint16& hostPort) = 0;
};

void ATCreateNetSockWorker(IATNetUdpStack *udp, bool externalAccess, IATNetSockWorker **pp);

#endif
