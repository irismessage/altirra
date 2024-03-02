#ifndef f_AT_ATNETWORK_INTERNAL_DHCPD_H
#define f_AT_ATNETWORK_INTERNAL_DHCPD_H

#include <at/atnetwork/emusocket.h>
#include <at/atnetwork/ethernet.h>

class IATEmuNetUdpStack;

class ATNetDhcpDaemon : public IATEmuNetUdpSocketListener {
public:
	ATNetDhcpDaemon();

	void Init(IATEmuNetUdpStack *ip, bool enableRouter);
	void Shutdown();

	void Reset();

	void OnUdpDatagram(const ATEthernetAddr& srcHwAddr, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, const void *data, uint32 dataLen);

protected:
	IATEmuNetUdpStack *mpUdpStack = nullptr;
	bool mbRouterEnabled = false;
	uint32 mNextLeaseIdx = 0;

	struct Lease {
		bool mbValid;
		uint32 mXid;
		ATEthernetAddr mAddr;
	};

	Lease mLeases[100] {};
};

#endif	// f_AT_ATNETWORK_INTERNAL_DHCPD_H
