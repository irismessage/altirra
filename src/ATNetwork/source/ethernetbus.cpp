#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <at/atnetwork/ethernetbus.h>
#include <at/atnetwork/ethernetframe.h>

ATEthernetBus::ATEthernetBus()
	: mNextEndpoint(1)
	, mNextPacketId(1)
{
}

ATEthernetBus::~ATEthernetBus() {
	ClearPendingFrames();
}

uint32 ATEthernetBus::AddEndpoint(IATEthernetEndpoint *endpoint) {
	uint32 endpointId = mNextEndpoint++;

	if (!mNextEndpoint)
		mNextEndpoint = 1;

	Endpoint& ep = mEndpoints.push_back();
	ep.mpEndpoint = endpoint;
	ep.mId = endpointId;

	return endpointId;
}

void ATEthernetBus::RemoveEndpoint(uint32 endpointId) {
	Endpoints::iterator it(mEndpoints.begin()), itEnd(mEndpoints.end());

	for(; it != itEnd; ++it) {
		const Endpoint& ep = *it;

		if (ep.mId == endpointId) {
			mEndpoints.erase(it);
			break;
		}
	}
}

IATEthernetClock *ATEthernetBus::GetClock(uint32 clockId) const {
	if (clockId >= mClocks.size())
		return NULL;

	return mClocks[clockId];
}

uint32 ATEthernetBus::AddClock(IATEthernetClock *clock) {
	Clocks::iterator it = std::find(mClocks.begin(), mClocks.end(), (IATEthernetClock *)NULL);

	if (it == mClocks.end()) {
		mClocks.push_back(clock);
		return (uint32)(mClocks.size() - 1);
	} else {
		*it = clock;
		return (uint32)(it - mClocks.begin());
	}
}

void ATEthernetBus::RemoveClock(uint32 clockid) {
	VDASSERT(clockid < mClocks.size());

	mClocks[clockid] = NULL;

	Packets::const_iterator it(mPackets.begin()), itEnd(mPackets.end());
	while(it != itEnd) {
		const QueuedPacket& qp = *it->second;

		if (qp.mPacket.mClockIndex == clockid) {
			it = mPackets.erase(it);

			mClocks[qp.mPacket.mClockIndex]->RemoveClockEvent(qp.mClockEventId);
			qp.~QueuedPacket();
			free((void *)&qp);
		} else {
			++it;
		}
	}
}

void ATEthernetBus::ClearPendingFrames() {
	for(Packets::const_iterator it(mPackets.begin()), itEnd(mPackets.end());
		it != itEnd;
		++it)
	{
		const QueuedPacket& qp = *it->second;
		mClocks[qp.mPacket.mClockIndex]->RemoveClockEvent(qp.mClockEventId);
	}

	mPackets.clear();
}

void ATEthernetBus::TransmitFrame(uint32 source, const ATEthernetPacket& packet) {
	uint32 packetId = mNextPacketId;

	while(mPackets.find(packetId) != mPackets.end())
		++packetId;

	mNextPacketId = packetId + 1;

	if (!mNextPacketId)
		mNextPacketId = 1;

	void *mem = malloc(sizeof(QueuedPacket) + packet.mLength);
	if (!mem)
		throw MyMemoryError();

	QueuedPacket *qp = new(mem) QueuedPacket;

	memcpy(qp + 1, packet.mpData, packet.mLength);

	qp->mSourceId = source;
	qp->mPacket = packet;
	qp->mPacket.mTimestamp = mClocks[packet.mClockIndex]->GetTimestamp((sint32)packet.mTimestamp);
	qp->mPacket.mpData = (const uint8 *)(qp + 1);

	qp->mClockEventId = mClocks[packet.mClockIndex]->AddClockEvent(packet.mTimestamp, this, packetId);
	VDASSERT(qp->mClockEventId);

	mPackets[packetId] = qp;
}

void ATEthernetBus::OnClockEvent(uint32 eventid, uint32 userid) {
	Packets::iterator it(mPackets.find(userid));

	if (it == mPackets.end()) {
		VDASSERT(!"Received clock event for unmatched packet ID.");
		return;
	}

	QueuedPacket *qp = it->second;
	mPackets.erase(it);

	union {
		ATEthernetArpFrameInfo arpInfo;
		ATIPv4HeaderInfo ipv4Info;
	} dec;

	ATEthernetFrameDecodedType decType = kATEthernetFrameDecodedType_None;
	const void *decInfo = NULL;

	if (qp->mPacket.mLength >= 2) {
		const uint8 *data = qp->mPacket.mpData;

		switch(VDReadUnalignedBEU16(data)) {
			case kATEthernetFrameType_ARP:
				if (ATEthernetDecodeArpPacket(dec.arpInfo, data + 2, qp->mPacket.mLength - 2)) {
					decInfo = &dec.arpInfo;
					decType = kATEthernetFrameDecodedType_ARP;
				}
				break;

			case kATEthernetFrameType_IP:
				if (ATIPv4DecodeHeader(dec.ipv4Info, data + 2, qp->mPacket.mLength - 2)) {
					decInfo = &dec.ipv4Info;
					decType = kATEthernetFrameDecodedType_IPv4;
				}
				break;
		}
	}

	for(Endpoints::const_iterator itEP(mEndpoints.begin()), itEPEnd(mEndpoints.end());
		itEP != itEPEnd;
		++itEP)
	{
		const Endpoint& ep = *itEP;

		if (ep.mId != qp->mSourceId)
			ep.mpEndpoint->ReceiveFrame(qp->mPacket, decType, decInfo);
	}

	qp->~QueuedPacket();
	free(qp);
}
