#include "stdafx.h"
#include <at/atnetwork/ethernetframe.h>
#include "ipstack.h"

ATNetIpStack::ATNetIpStack()
	: mpEthSegment(NULL)
	, mEthClockId(0)
	, mEthEndpointId(0)
	, mIpCounter(1)
{
}

IATEthernetClock *ATNetIpStack::GetClock() const {
	return mpEthSegment->GetClock(mEthClockId);
}

bool ATNetIpStack::IsLocalOrBroadcastAddress(uint32 addr) const {
	return addr == 0xFFFFFFFFU || addr == (mIpAddress | ~mIpNetMask) || addr == mIpAddress;
}

void ATNetIpStack::Init(const ATEthernetAddr& hwaddr, uint32 ipaddr, uint32 netmask, IATEthernetSegment *segment, uint32 clockId, uint32 endpointId) {
	mHwAddress = hwaddr;
	mpEthSegment = segment;
	mEthClockId = clockId;
	mEthEndpointId = endpointId;
	mIpAddress = ipaddr;
	mIpNetMask = netmask;
}

void ATNetIpStack::Shutdown() {
	mpEthSegment = NULL;
	mEthClockId = 0;
	mEthEndpointId = 0;
}

void ATNetIpStack::InitHeader(ATIPv4HeaderInfo& iphdr) {
	iphdr.mFlags = 0;
	iphdr.mTTL = 127;
	iphdr.mTOS = 0;
	iphdr.mId = ++mIpCounter;
	iphdr.mFragmentOffset = 0;
}

void ATNetIpStack::ClearArpCache() {
	mArpCache.clear();
}

void ATNetIpStack::AddArpEntry(uint32 ipaddr, const ATEthernetAddr& hwaddr) {
	mArpCache[ipaddr] = hwaddr;
}

void ATNetIpStack::SendFrame(const ATEthernetAddr& dstAddr, const void *data, uint32 len) {
	ATEthernetPacket newPacket;
	newPacket.mClockIndex = mEthClockId;
	newPacket.mSrcAddr = mHwAddress;
	newPacket.mDstAddr = dstAddr;
	newPacket.mTimestamp = 100;
	newPacket.mpData = (const uint8 *)data;
	newPacket.mLength = len;
	mpEthSegment->TransmitFrame(mEthEndpointId, newPacket);
}

void ATNetIpStack::SendFrame(uint32 dstIpAddr, const void *data, uint32 len) {
	ArpCache::const_iterator it = mArpCache.find(dstIpAddr);

	if (it != mArpCache.end())
		SendFrame(it->second, data, len);
}
