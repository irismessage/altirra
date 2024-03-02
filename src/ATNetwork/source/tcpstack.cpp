#include "stdafx.h"
#include <vd2/system/binary.h>
#include <vd2/system/vdalloc.h>
#include <at/atnetwork/ethernetframe.h>
#include <at/atnetwork/tcp.h>
#include "ipstack.h"
#include "tcpstack.h"

ATNetTcpRingBuffer::ATNetTcpRingBuffer()
	: mReadPtr(0)
	, mWritePtr(0)
	, mLevel(0)
	, mBaseSeq(0)
{
}

void ATNetTcpRingBuffer::Init(char *buf, uint32 size) {
	mpBuffer = buf;
	mSize = size;
}

void ATNetTcpRingBuffer::Reset(uint32 seq) {
	mBaseSeq = seq;
	mReadPtr = 0;
	mWritePtr = 0;
	mLevel = 0;
}

uint32 ATNetTcpRingBuffer::Write(const void *p, uint32 n) {
	if (mSize - mLevel < n)
		n = mSize - mLevel;

	mLevel += n;

	uint32 toWrap = mSize - mWritePtr;
	if (toWrap < n) {
		memcpy(mpBuffer + mWritePtr, p, toWrap);
		p = (const char *)p + toWrap;
		n -= toWrap;
		mWritePtr = 0;
	}

	memcpy(mpBuffer + mWritePtr, p, n);
	mWritePtr += n;
	return n;
}

void ATNetTcpRingBuffer::Read(uint32 offset, void *p, uint32 n) {
	VDASSERT(offset <= mLevel && mLevel - offset >= n);

	uint32 readPtr = mReadPtr + offset;

	while(n) {
		if (readPtr >= mSize)
			readPtr -= mSize;

		uint32 tc = mSize - readPtr;
		if (tc > n)
			tc = n;

		n -= tc;

		memcpy(p, mpBuffer + readPtr, tc);
		p = (char *)p + tc;
		readPtr += tc;
	}
}

void ATNetTcpRingBuffer::Ack(uint32 n) {
	VDASSERT(n <= mLevel);

	mReadPtr += n;
	if (mReadPtr >= mSize)
		mReadPtr -= mSize;

	mLevel -= n;
	mBaseSeq += n;
}

///////////////////////////////////////////////////////////////////////////

ATNetTcpStack::ATNetTcpStack()
	: mpIpStack(NULL)
	, mpBridgeListener(NULL)
{
}

void ATNetTcpStack::Init(ATNetIpStack *ipStack) {
	mpIpStack = ipStack;
	mpClock = ipStack->GetClock();
}

void ATNetTcpStack::Shutdown() {
	mListeningSockets.clear();

	CloseAllConnections();

	mpClock = NULL;
	mpIpStack = NULL;
}

void ATNetTcpStack::SetBridgeListener(IATSocketListener *p) {
	mpBridgeListener = p;
}

bool ATNetTcpStack::Bind(uint16 port, IATSocketListener *listener) {
	ListeningSockets::insert_return_type r = mListeningSockets.insert(port);

	if (!r.second)
		return false;

	r.first->second.mpHandler = listener;
	return true;
}

void ATNetTcpStack::Unbind(uint16 port, IATSocketListener *listener) {
	ListeningSockets::iterator it = mListeningSockets.find(port);

	if (it != mListeningSockets.end() && it->second.mpHandler == listener)
		mListeningSockets.erase(it);
}

void ATNetTcpStack::CloseAllConnections() {
	for(Connections::const_iterator it = mConnections.begin(), itEnd = mConnections.end();
		it != itEnd;
		++it)
	{
		delete it->second;
	}

	mConnections.clear();
}

void ATNetTcpStack::GetConnectionInfo(vdfastvector<ATNetTcpConnectionInfo>& conns) const {
	conns.resize(mConnections.size());

	ATNetTcpConnectionInfo *dst = conns.data();

	for(Connections::const_iterator it = mConnections.begin(), itEnd = mConnections.end();
		it != itEnd;
		++it)
	{
		it->second->GetInfo(*dst++);
	}
}

void ATNetTcpStack::OnPacket(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& iphdr, const uint8 *data, const uint32 len) {
	ATTcpHeaderInfo tcpHdr;

	if (!ATTcpDecodeHeader(tcpHdr, iphdr, data, len))
		return;

	// attempt to lookup connection
	ATNetTcpConnectionKey connKey;
	connKey.mLocalAddress = iphdr.mDstAddr;
	connKey.mRemoteAddress = iphdr.mSrcAddr;
	connKey.mRemotePort = tcpHdr.mSrcPort;
	connKey.mLocalPort = tcpHdr.mDstPort;

	Connections::iterator itConn = mConnections.find(connKey);

	if (itConn != mConnections.end()) {
		ATNetTcpConnection& conn = *itConn->second;

		conn.AddRef();
		conn.OnPacket(packet, iphdr, tcpHdr, data, len);
		conn.Release();

		return;
	}

	// if this is an RST, drop it on the floor -- we have no connection, so nothing to reset
	if (tcpHdr.mbRST)
		return;

	// if this isn't a SYN packet, send RST
	if (!tcpHdr.mbSYN) {
		SendReset(packet, iphdr, tcpHdr.mSrcPort, tcpHdr.mDstPort, tcpHdr.mSequenceNo);
		return;
	}

	// check if this is a connection to the gateway or if we are bridging/NATing
	IATSocketListener *listener = mpBridgeListener;

	if (iphdr.mDstAddr == mpIpStack->GetIpAddress()) {
		// see if we have a listening socket for this port
		ListeningSockets::const_iterator itListen = mListeningSockets.find(tcpHdr.mDstPort);

		if (itListen != mListeningSockets.end())
			listener = itListen->second.mpHandler;
	}

	if (!listener) {
		// No socket is listening on this port -- send RST
		SendReset(packet, iphdr, tcpHdr.mSrcPort, tcpHdr.mDstPort, tcpHdr.mSequenceNo);
		return;
	}

	// Socket is listening -- establish a new connection in SYN_RCVD state
	vdrefptr<ATNetTcpConnection> conn(new ATNetTcpConnection(this, connKey));

	conn->Init(packet, iphdr, tcpHdr);

	vdrefptr<IATSocketHandler> socketHandler;
	if (!listener->OnSocketIncomingConnection(iphdr.mSrcAddr, tcpHdr.mSrcPort, iphdr.mDstAddr, tcpHdr.mDstPort, conn, ~socketHandler)) {
		// Uh oh... we can't accept this connection.
		SendReset(packet, iphdr, tcpHdr.mSrcPort, tcpHdr.mDstPort, tcpHdr.mSequenceNo);
		return;
	}

	conn->SetSocketHandler(socketHandler);

	mConnections[connKey] = conn;
	ATNetTcpConnection *conn2 = conn.release();

	// Send SYN+ACK
	conn2->Transmit(true);
}

uint32 ATNetTcpStack::EncodePacket(uint8 *dst, uint32 len, uint32 srcIpAddr, uint32 dstIpAddr, const ATTcpHeaderInfo& hdrInfo, const void *data, uint32 dataLen) {
	if (len < 22 + 20 + dataLen)
		return 0;

	// encode EtherType and IPv4 header
	ATIPv4HeaderInfo iphdr;
	mpIpStack->InitHeader(iphdr);
	iphdr.mSrcAddr = srcIpAddr;
	iphdr.mDstAddr = dstIpAddr;
	iphdr.mProtocol = 6;
	iphdr.mDataOffset = 0;
	iphdr.mDataLength = 20 + dataLen;
	VDVERIFY(ATIPv4EncodeHeader(dst, 22, iphdr));
	dst += 22;

	// encode TCP header
	VDWriteUnalignedBEU16(dst + 0, hdrInfo.mSrcPort);
	VDWriteUnalignedBEU16(dst + 2, hdrInfo.mDstPort);
	VDWriteUnalignedBEU32(dst + 4, hdrInfo.mSequenceNo);
	VDWriteUnalignedBEU32(dst + 8, hdrInfo.mAckNo);
	dst[12] = 0x50;
	dst[13] = 0;
	if (hdrInfo.mbURG) dst[13] |= 0x20;
	if (hdrInfo.mbACK) dst[13] |= 0x10;
	if (hdrInfo.mbPSH) dst[13] |= 0x08;
	if (hdrInfo.mbRST) dst[13] |= 0x04;
	if (hdrInfo.mbSYN) dst[13] |= 0x02;
	if (hdrInfo.mbFIN) dst[13] |= 0x01;

	VDWriteUnalignedBEU16(dst + 14, hdrInfo.mWindow);
	dst[16] = 0;	// checksum lo (temp)
	dst[17] = 0;	// checksum hi (temp)
	VDWriteUnalignedBEU16(dst + 18, hdrInfo.mUrgentPtr);

	// compute TCP checksum
	uint64 newSum64 = iphdr.mSrcAddr;
	newSum64 += iphdr.mDstAddr;
	newSum64 += VDToBE32(0x60000 + 20 + dataLen);

	const uint8 *chksrc = (const uint8 *)data;
	for(uint32 dataLen4 = dataLen >> 2; dataLen4; --dataLen4) {
		newSum64 += VDReadUnalignedU32(chksrc);
		chksrc += 4;
	}

	if (dataLen & 2) {
		newSum64 += VDReadUnalignedU16(chksrc);
		chksrc += 2;
	}

	if (dataLen & 1)
		newSum64 += VDFromLE16(*chksrc);

	VDWriteUnalignedU16(dst + 16, ATIPComputeChecksum(newSum64, dst, 5));

	dst += 20;

	if (dataLen)
		memcpy(dst, data, dataLen);

	return 22 + 20 + dataLen;
}

void ATNetTcpStack::SendReset(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& iphdr, uint16 srcPort, uint16 dstPort, uint32 seqNo) {
	SendReset(iphdr.mSrcAddr, packet.mSrcAddr, iphdr.mDstAddr, srcPort, dstPort, seqNo);
}

void ATNetTcpStack::SendReset(uint32 srcIpAddr, const ATEthernetAddr& dstHwAddr, uint32 dstIpAddr, uint16 srcPort, uint16 dstPort, uint32 seqNo) {
	VDALIGN(4) uint8 rstPacket[42 + 2];

	ATTcpHeaderInfo tcpHeader = {};
	tcpHeader.mSrcPort = dstPort;
	tcpHeader.mDstPort = srcPort;
	tcpHeader.mAckNo = seqNo;
	tcpHeader.mbRST = true;

	VDVERIFY(EncodePacket(rstPacket + 2, 42, dstIpAddr, srcIpAddr, tcpHeader, NULL, 0));

	SendFrame(dstHwAddr, rstPacket + 2, 42);
}

void ATNetTcpStack::SendFrame(const ATEthernetAddr& dstAddr, const void *data, uint32 len) {
	mpIpStack->SendFrame(dstAddr, data, len);
}

void ATNetTcpStack::DeleteConnection(const ATNetTcpConnectionKey& connKey) {
	Connections::iterator it = mConnections.find(connKey);

	if (it != mConnections.end()) {
		ATNetTcpConnection *conn = it->second;

		mConnections.erase(it);
		conn->Release();
	}
}

///////////////////////////////////////////////////////////////////////////

ATNetTcpConnection::ATNetTcpConnection(ATNetTcpStack *stack, const ATNetTcpConnectionKey& connKey)
	: mpTcpStack(stack)
	, mConnKey(connKey)
	, mRemoteWindow(0)
	, mbLocalOpen(true)
	, mbSynQueued(false)
	, mbFinQueued(false)
	, mbFinReceived(false)
	, mEventClose(0)
	, mEventTransmit(0)
	, mEventRetransmit(0)
	, mXmitNext(0)
	, mXmitLastAck(0)
	, mXmitWindowLimit(0)
{
	mRecvRing.Init(mRecvBuf, sizeof mRecvBuf);
	mXmitRing.Init(mXmitBuf, sizeof mXmitBuf);

	PacketTimer& root = mPacketTimers.push_back();
	root.mNext = 0;
	root.mPrev = 0;
	root.mRetransmitTimestamp = 0;
	root.mSequenceStart = 0;
	root.mSequenceEnd = 0;
}

ATNetTcpConnection::~ATNetTcpConnection() {
	IATEthernetClock *clk = mpTcpStack->GetClock();

	if (mEventClose) {
		clk->RemoveClockEvent(mEventClose);
		mEventClose = 0;
	}

	if (mEventTransmit) {
		clk->RemoveClockEvent(mEventTransmit);
		mEventTransmit = 0;
	}

	if (mEventRetransmit) {
		clk->RemoveClockEvent(mEventRetransmit);
		mEventRetransmit = 0;
	}
}

void ATNetTcpConnection::GetInfo(ATNetTcpConnectionInfo& info) const {
	info.mConnKey = mConnKey;
	info.mConnState = mConnState;
}

void ATNetTcpConnection::Init(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& ipHdr, const ATTcpHeaderInfo& tcpHdr) {
	mConnState = kATNetTcpConnectionState_SYN_RCVD;
	mRemoteHwAddr = packet.mSrcAddr;
	mRemoteWindow = tcpHdr.mWindow;

	// initialize receive state to received sequence number
	mRecvRing.Reset(tcpHdr.mSequenceNo + 1);

	// initialize our sequence number
	mXmitNext = 1;

	// initialize transmit window
	mXmitLastAck = tcpHdr.mSequenceNo + 1;
	mXmitWindowLimit = mXmitLastAck + tcpHdr.mWindow;

	// queue bogus data for SYN packet
	mXmitRing.Reset(mXmitNext);
	mXmitRing.Write("", 1);

	mbSynQueued = true;
}

void ATNetTcpConnection::SetSocketHandler(IATSocketHandler *h) {
	mpSocketHandler = h;
}

void ATNetTcpConnection::OnPacket(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& iphdr, const ATTcpHeaderInfo& tcpHdr, const uint8 *data, const uint32 len) {
	// check if RST is set
	if (tcpHdr.mbRST) {
		// delete the connection :-/
		if (mpSocketHandler)
			mpSocketHandler->OnSocketError();

		ClearEvents();
		mpTcpStack->DeleteConnection(mConnKey);
		return;
	}

	// update the hardware address for this connection
	mRemoteHwAddr = packet.mSrcAddr;

	// update window
	if (tcpHdr.mbACK)
		mXmitLastAck = tcpHdr.mAckNo;

	mXmitWindowLimit = mXmitLastAck + tcpHdr.mWindow;

	// check if data is being ACKed
	if (tcpHdr.mbACK) {
		uint32 xmitBaseSeq = mXmitRing.GetBaseSeq();
		uint32 ackOffset = (uint32)(tcpHdr.mAckNo - xmitBaseSeq);

		if (ackOffset <= mXmitRing.GetLevel()) {
			// update the retransmit queue
			PacketTimer& root = mPacketTimers[0];
			while(root.mNext) {
				const uint32 timerIdx = root.mNext;
				PacketTimer& timer = mPacketTimers[timerIdx];

				// stop removing packet timers once we reach an entry that isn't fully ACK'd
				if ((uint32)(timer.mSequenceEnd - tcpHdr.mAckNo) < 0x80000000U)
					break;

				if (mEventRetransmit) {
					mpTcpStack->GetClock()->RemoveClockEvent(mEventRetransmit);
					mEventRetransmit = 0;
				}

				root.mNext = timer.mNext;
				mPacketTimers[root.mNext].mPrev = 0;

				timer.mNext = root.mRetransmitTimestamp;
				root.mRetransmitTimestamp = timerIdx;
			}

			if (root.mNext && !mEventRetransmit) {
				mEventRetransmit = mpTcpStack->GetClock()->AddClockEvent(mPacketTimers[root.mNext].mRetransmitTimestamp, this, kEventId_Retransmit);
			}

			if (ackOffset) {
				mXmitRing.Ack(ackOffset);
				mbSynQueued = false;

				// if our side is closed but we haven't yet allocated the sequence number
				// for the FIN packet, do so now as now we have space
				if (!mbLocalOpen && !mbFinQueued) {
					mbFinQueued = true;
					mXmitRing.Write("", 1);
				}

				// check if we were in SYN_RCVD status -- if so, we just got the ACK we were waiting for
				if (mConnState == kATNetTcpConnectionState_SYN_RCVD)
					mConnState = kATNetTcpConnectionState_ESTABLISHED;

				// notify the socket handler that more space is available; however, note
				// that this is internal buffer, not window space
				mpSocketHandler->OnSocketWriteReady(mXmitRing.GetSpace());
			}

			// if the ACK just emptied the buffer and we already queued the FIN, then the FIN
			// has been ACKed
			if (mbFinQueued && !mXmitRing.GetLevel()) {
				switch(mConnState) {
					case kATNetTcpConnectionState_CLOSING:
						mConnState = kATNetTcpConnectionState_TIME_WAIT;

						VDASSERT(!mEventClose);
						{
							IATEthernetClock *clk = mpTcpStack->GetClock();

							mEventClose = clk->AddClockEvent(clk->GetTimestamp(500), this, kEventId_Close);
						}
						break;

					case kATNetTcpConnectionState_LAST_ACK:
						mConnState = kATNetTcpConnectionState_CLOSED;
						break;

					case kATNetTcpConnectionState_FIN_WAIT_1:
						mConnState = kATNetTcpConnectionState_FIN_WAIT_2;
						break;
				}
			}
		}
	}

	// check if new data is coming in; note that FIN takes a sequence number slot
	uint32 ackLen = tcpHdr.mDataLength + (tcpHdr.mbFIN ? 1 : 0);
	bool ackNeeded = false;

	if (ackLen) {
		// we always need to reply if data is coming in
		ackNeeded = true;

		// check if the new data is where we expect it to be
		if (tcpHdr.mSequenceNo == mRecvRing.GetBaseSeq()) {
			// check how much space we have
			const uint32 recvSpace = mRecvRing.GetSpace();
			uint32 tc = ackLen;

			// truncate the new data if we don't have enough space; note that we
			// explicitly must ACK with win=0 on one byte on closed window, as this
			// is required for the sender to probe the window
			if (tc > recvSpace)
				tc = recvSpace;

			if (tc) {
				// check for special case where we are ending with FIN
				if (tc > tcpHdr.mDataLength) {
					mRecvRing.Write(data + tcpHdr.mDataOffset, tcpHdr.mDataLength);

					if (!mbFinReceived) {
						mbFinReceived = true;

						// advance state since we have now received and processed the FIN
						switch(mConnState) {
							case kATNetTcpConnectionState_ESTABLISHED:
								mConnState = kATNetTcpConnectionState_CLOSE_WAIT;
								ackNeeded = true;
								break;

							case kATNetTcpConnectionState_FIN_WAIT_2:
								mConnState = kATNetTcpConnectionState_TIME_WAIT;
								ackNeeded = true;

								VDASSERT(!mEventClose);
								mEventClose = mpTcpStack->GetClock()->AddClockEvent(500, this, kEventId_Close);
								break;
						}

						mpSocketHandler->OnSocketClose();
					}
				} else
					mRecvRing.Write(data + tcpHdr.mDataOffset, tc);

				mpSocketHandler->OnSocketReadReady(mRecvRing.GetLevel());
			}
		}
	}

	// if the connection is dead, delete it now -- this should be done before we send a reply
	// packet, in case we got both an ACK and the last FIN
	if (mConnState == kATNetTcpConnectionState_CLOSED) {
		ClearEvents();
		mpTcpStack->DeleteConnection(mConnKey);
		return;
	}

	// check if we need a reply packet or if we received an ACK and can transmit now
	if (ackNeeded || (tcpHdr.mbACK && CanTransmitMore()))
		Transmit(true);
}

void ATNetTcpConnection::Transmit(bool ack) {
	uint8 replyPacket[576];
	uint8 data[512];

	// check if we are sending a SYN packet
	const uint32 xmitOffset = mXmitNext - mXmitRing.GetBaseSeq();
	bool syn = mbSynQueued && xmitOffset == 0;
	bool fin = false;

	// compute how much data payload to send; we avoid doing so for a SYN packet
	// because it's considered unusual behavior, although normal
	uint32 dataLen = syn ? 0 : mXmitRing.GetLevel() - xmitOffset;
	bool sendingLast = true;
	bool finresend = false;

	// limit data send according to window
	uint32 windowSpace = mXmitWindowLimit - mXmitNext;
	if (windowSpace >= 0x80000000U) {
		// The receive window has shrunk. This is discouraged but valid according
		// to RFC 793.
		windowSpace = 0;
	}

	if (dataLen > windowSpace) {
		dataLen = windowSpace;
		sendingLast = false;
	}

	// limit data send according to MSS
	if (dataLen > 512) {
		dataLen = 512;
		sendingLast = false;
	} else if (syn) {
		dataLen = 0;
	} else if (mbFinQueued && sendingLast && dataLen) {
		// If we have already sent a FIN, we must not send one again short of a
		// resend, as it takes a sequence number. This means that once we send
		// a FIN and it's been ACKed, any subsequent ACKs we send will not have
		// FIN set.
		fin = true;

		// FIN takes a sequence number and we queue a ring byte for it, but we
		// don't actually send a byte for it.
		--dataLen;
	}

	ATTcpHeaderInfo replyTcpHeader = {};

	replyTcpHeader.mSrcPort = mConnKey.mLocalPort;
	replyTcpHeader.mDstPort = mConnKey.mRemotePort;
	replyTcpHeader.mbACK = ack;
	replyTcpHeader.mbSYN = syn;
	replyTcpHeader.mbFIN = fin;
	replyTcpHeader.mAckNo = ack ? mRecvRing.GetBaseSeq() + mRecvRing.GetLevel() + (mbFinReceived ? 1 : 0) : 0;
	replyTcpHeader.mSequenceNo = mXmitNext;
	replyTcpHeader.mWindow = mRecvRing.GetSpace();

	if (dataLen)
		mXmitRing.Read(xmitOffset, data, dataLen);

	uint32 replyLen = mpTcpStack->EncodePacket(replyPacket + 2, sizeof replyPacket - 2, mConnKey.mLocalAddress, mConnKey.mRemoteAddress, replyTcpHeader, data, dataLen);

	mpTcpStack->SendFrame(mRemoteHwAddr, replyPacket + 2, replyLen);

	mXmitNext += dataLen + replyTcpHeader.mbFIN + replyTcpHeader.mbSYN;
	VDASSERT(mXmitNext - mXmitRing.GetBaseSeq() <= mXmitRing.GetLevel());

	// if we sent data or a FIN, queue a packet timer
	if (dataLen || replyTcpHeader.mbFIN) {
		uint32 free = mPacketTimers[0].mRetransmitTimestamp;

		if (!free) {
			free = (uint32)mPacketTimers.size();
			mPacketTimers.push_back();
		} else {
			mPacketTimers[0].mRetransmitTimestamp = mPacketTimers[free].mNext;
		}

		PacketTimer& root = mPacketTimers[0];
		PacketTimer& timer = mPacketTimers[free];

		timer.mPrev = root.mPrev;
		timer.mNext = 0;
		mPacketTimers[root.mPrev].mNext = free;
		root.mPrev = free;

		IATEthernetClock *clk = mpTcpStack->GetClock();
		timer.mRetransmitTimestamp = clk->GetTimestamp(3000);
		timer.mSequenceStart = replyTcpHeader.mSequenceNo;
		timer.mSequenceEnd = mXmitNext;

		if (!mEventRetransmit)
			mEventRetransmit = clk->AddClockEvent(timer.mRetransmitTimestamp, this, kEventId_Retransmit);

		// If we can still transmit more, enable the transmit timer.
		if (!mEventTransmit)
			mEventTransmit = clk->AddClockEvent(clk->GetTimestamp(1), this, kEventId_Transmit);
	}
}

void ATNetTcpConnection::OnClockEvent(uint32 eventid, uint32 userid) {
	switch(userid) {
		case kEventId_Close:
			VDASSERT(mConnState == kATNetTcpConnectionState_TIME_WAIT);
			VDASSERT(mEventClose == eventid);
			mEventClose = 0;

			ClearEvents();
			mpTcpStack->DeleteConnection(mConnKey);
			return;

		case kEventId_Transmit:
			mEventTransmit = 0;

			if (CanTransmitMore())
				Transmit(true);
			return;

		case kEventId_Retransmit:
			mEventRetransmit = 0;

			{
				PacketTimer& root = mPacketTimers.front();
				VDASSERT(root.mNext);

				// go back to the first packet
				PacketTimer& head = mPacketTimers[root.mNext];

				// check if we've gotten an ACK partway
				const uint32 xmitBase = mXmitRing.GetBaseSeq();

				if ((uint32)(head.mSequenceStart - xmitBase) < mXmitRing.GetLevel())
					mXmitNext = head.mSequenceStart;
				else
					mXmitNext = xmitBase;

				// clear the ring
				root.mPrev = root.mNext = root.mRetransmitTimestamp = 0;
				mPacketTimers.resize(1);

				Transmit(true);
			}
			return;
	}
}

void ATNetTcpConnection::Shutdown() {
	// If one side hasn't been closed, send an RST.
	if (mbLocalOpen || !mbFinReceived)
		mpTcpStack->SendReset(mConnKey.mLocalAddress, mRemoteHwAddr, mConnKey.mRemoteAddress, mConnKey.mLocalPort, mConnKey.mRemotePort, mXmitNext);

	// Delete us.
	ClearEvents();
	mpTcpStack->DeleteConnection(mConnKey);
}

uint32 ATNetTcpConnection::Read(void *buf, uint32 len) {
	uint32 tc = mRecvRing.GetLevel();

	if (tc > len)
		tc = len;

	if (tc) {
		mRecvRing.Read(0, buf, tc);
		mRecvRing.Ack(tc);
	}

	return tc;
}

uint32 ATNetTcpConnection::Write(const void *buf, uint32 len) {
	if (len) {
		uint32 space = mXmitRing.GetSpace();
		if (len > space) {
			len = space;
			if (!len)
				return 0;
		}

		VDVERIFY(mXmitRing.Write(buf, len));

		// If this means we can transmit now, enable the transmit timer.
		if (!mEventTransmit && CanTransmitMore()) {
			IATEthernetClock *clk = mpTcpStack->GetClock();
			mEventTransmit = clk->AddClockEvent(clk->GetTimestamp(1), this, kEventId_Transmit);
		}
	}

	return len;
}

void ATNetTcpConnection::Close() {
	if (mbLocalOpen) {
		mbLocalOpen = false;

		if (mXmitRing.GetSpace()) {
			mXmitRing.Write("", 1);
			mbFinQueued = true;
		}

		if (mConnState == kATNetTcpConnectionState_ESTABLISHED)
			mConnState = kATNetTcpConnectionState_FIN_WAIT_1;
		else if (mConnState == kATNetTcpConnectionState_CLOSE_WAIT)
			mConnState = kATNetTcpConnectionState_CLOSED;
	}
}

bool ATNetTcpConnection::CanTransmitMore() const {
	// We can transmit if:
	// - there is space in the window
	// - there is data waiting to send
	//
	// Note that it is discouraged but legal to shrink the receive window,
	// so we must handle the case where the window limit is rewound behind
	// where we have already sent.

	const uint32 sendLimit = mXmitRing.GetBaseSeq() + mXmitRing.GetLevel();

	return sendLimit != mXmitNext && (uint32)(mXmitWindowLimit - mXmitNext - 1) < 0x20000U;
}

void ATNetTcpConnection::ClearEvents() {
	IATEthernetClock *clk = mpTcpStack->GetClock();

	if (mEventClose) {
		clk->RemoveClockEvent(mEventClose);
		mEventClose = 0;
	}

	if (mEventTransmit) {
		clk->RemoveClockEvent(mEventTransmit);
		mEventTransmit = 0;
	}

	if (mEventRetransmit) {
		clk->RemoveClockEvent(mEventRetransmit);
		mEventRetransmit = 0;
	}
}
