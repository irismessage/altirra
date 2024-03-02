#include "stdafx.h"
#include <vd2/system/binary.h>
#include <vd2/system/int128.h>
#include <vd2/system/time.h>
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
	, mPortCounter(49152)
{
}

void ATNetTcpStack::Init(ATNetIpStack *ipStack) {
	mpIpStack = ipStack;
	mpClock = ipStack->GetClock();

	mXmitInitialSequenceSalt = VDGetCurrentProcessId() ^ (VDGetPreciseTick() / 147);
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

 bool ATNetTcpStack::Connect(uint32 dstIpAddr, uint16 dstPort, IATSocketHandler *handler, IATSocket **newSocket) {
	// find an unused port in dynamic range
	for(uint32 i=49152; i<65535; ++i) {
		if (++mPortCounter == 0)
			mPortCounter = 49152;

		bool valid = true;

		if (mListeningSockets.find(mPortCounter) != mListeningSockets.end()) {
			valid = false;
		} else {
			for(const auto& conn : mConnections) {
				if (conn.first.mLocalPort == mPortCounter) {
					valid = false;
					break;
				}
			}
		}

		if (valid)
			goto found_free_port;
	}

	// doh... no free ports!
	return false;

found_free_port:

	// create connection key
	ATNetTcpConnectionKey connKey;
	connKey.mLocalAddress = mpIpStack->GetIpAddress();
	connKey.mLocalPort = mPortCounter;
	connKey.mRemoteAddress = dstIpAddr;
	connKey.mRemotePort = dstPort;

	// initialize new connection
	vdrefptr<ATNetTcpConnection> conn(new ATNetTcpConnection(this, connKey));

	conn->InitOutgoing(handler, mXmitInitialSequenceSalt);

	mConnections[connKey] = conn;
	conn->AddRef();

	// Send SYN+ACK
	conn->Transmit(false);

	*newSocket = conn.release();
	return true;
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
		SendReset(iphdr, tcpHdr.mSrcPort, tcpHdr.mDstPort, tcpHdr);
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
		SendReset(iphdr, tcpHdr.mSrcPort, tcpHdr.mDstPort, tcpHdr);
		return;
	}

	// Socket is listening -- establish a new connection in SYN_RCVD state
	vdrefptr<ATNetTcpConnection> conn(new ATNetTcpConnection(this, connKey));

	conn->Init(packet, iphdr, tcpHdr);

	vdrefptr<IATSocketHandler> socketHandler;
	if (!listener->OnSocketIncomingConnection(iphdr.mSrcAddr, tcpHdr.mSrcPort, iphdr.mDstAddr, tcpHdr.mDstPort, conn, ~socketHandler)) {
		// Uh oh... we can't accept this connection.
		SendReset(iphdr, tcpHdr.mSrcPort, tcpHdr.mDstPort, tcpHdr);
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

void ATNetTcpStack::SendReset(const ATIPv4HeaderInfo& iphdr, uint16 srcPort, uint16 dstPort, const ATTcpHeaderInfo& origTcpHdr) {
	SendReset(iphdr.mSrcAddr, iphdr.mDstAddr, srcPort, dstPort, origTcpHdr);
}

void ATNetTcpStack::SendReset(uint32 srcIpAddr, uint32 dstIpAddr, uint16 srcPort, uint16 dstPort, const ATTcpHeaderInfo& origTcpHdr) {
	VDALIGN(4) uint8 rstPacket[42 + 2];

	ATTcpHeaderInfo tcpHeader = {};
	tcpHeader.mSrcPort = dstPort;
	tcpHeader.mDstPort = srcPort;
	tcpHeader.mSequenceNo = origTcpHdr.mAckNo;
	tcpHeader.mbRST = true;

	// For a SYN packet, we haven't established a sequence yet, so we need to ACK
	// the SYN instead.
	if (origTcpHdr.mbSYN) {
		tcpHeader.mbACK = true;
		tcpHeader.mAckNo = origTcpHdr.mSequenceNo + 1;
	}

	VDVERIFY(EncodePacket(rstPacket + 2, 42, dstIpAddr, srcIpAddr, tcpHeader, NULL, 0));

	SendFrame(srcIpAddr, rstPacket + 2, 42);
}

void ATNetTcpStack::SendFrame(uint32 dstIpAddr, const void *data, uint32 len) {
	mpIpStack->SendFrame(dstIpAddr, data, len);
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
	} else {
		VDASSERT(!"Attempt to delete a nonexistent TCP connection.");
	}
}

///////////////////////////////////////////////////////////////////////////

ATNetTcpConnection::ATNetTcpConnection(ATNetTcpStack *stack, const ATNetTcpConnectionKey& connKey)
	: mpTcpStack(stack)
	, mConnKey(connKey)
	, mbLocalOpen(true)
	, mbSynQueued(false)
	, mbSynAcked(false)
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
	ClearEvents();
}

void ATNetTcpConnection::GetInfo(ATNetTcpConnectionInfo& info) const {
	info.mConnKey = mConnKey;
	info.mConnState = mConnState;
}

void ATNetTcpConnection::Init(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& ipHdr, const ATTcpHeaderInfo& tcpHdr) {
	mConnState = kATNetTcpConnectionState_SYN_RCVD;

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

void ATNetTcpConnection::InitOutgoing(IATSocketHandler *h, uint32 isnSalt) {
	mpSocketHandler = h;

	mConnState = kATNetTcpConnectionState_SYN_SENT;

	// Compute initial sequence number (ISN) [RFC6528].
	// We're not currently doing hashing, so this isn't a very secure
	// implementation yet.
	mXmitNext = (uint32)(vduint128(VDGetPreciseTick()) * vduint128(250000) / vduint128(VDGetPreciseTicksPerSecondI()));
	mXmitNext ^= isnSalt;
	mXmitNext += mConnKey.mLocalAddress;
	mXmitNext += VDRotateLeftU32(mConnKey.mRemoteAddress, 14);
	mXmitNext += mConnKey.mLocalPort;
	mXmitNext += (uint32)mConnKey.mRemotePort << 16;

	// Init placeholder values for the remote sequence. The zero window
	// will prevent us from sending until we get the real values from
	// the SYN+ACK reply.
	mXmitLastAck = 0;
	mXmitWindowLimit = 0;

	// Queue the SYN packet and the bogus data for the sequence number
	// it occupies.
	mXmitRing.Reset(mXmitNext);
	mXmitRing.Write("", 1);

	mbSynQueued = true;
}

void ATNetTcpConnection::SetSocketHandler(IATSocketHandler *h) {
	mpSocketHandler = h;
}

void ATNetTcpConnection::OnPacket(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& iphdr, const ATTcpHeaderInfo& tcpHdr, const uint8 *data, const uint32 len) {
	VDASSERT(mpTcpStack);

	// check if RST is set
	if (tcpHdr.mbRST) {
		// if we are in SYN-SENT, the RST is valid if it ACKs the SYN; otherwise, it is
		// valid if it is within the window
		if (mConnState == kATNetTcpConnectionState_SYN_SENT) {
			if (!tcpHdr.mbACK || tcpHdr.mAckNo != mXmitNext)
				return;
		} else {
			// Note that we may have advertised a zero window. Pretend that the RST takes
			// no space, so it can fit at the end.
			if ((uint32)(tcpHdr.mSequenceNo - mRecvRing.GetBaseSeq()) > mRecvRing.GetSpace())
				return;
		}

		// mark both ends closed so we don't send a RST in response to a RST and so that we
		// don't respond to a local close
		mbLocalOpen = false;
		mbFinReceived = true;

		// delete the connection :-/
		if (mpSocketHandler)
			mpSocketHandler->OnSocketError();

		Shutdown();
		return;
	}

	// check if we're getting a SYN (only valid in this path if we are connecting out)
	if (mConnState == kATNetTcpConnectionState_SYN_SENT) {
		// We had better get a SYN or SYN+ACK. RST was already handled above.
		//
		// HOWEVER:
		// - It is valid to receive a SYN in response to a SYN, instead of a SYN+ACK.
		//   This happens if both sides simultaneously attempt to connect to each other.
		//   This results in a single connection and is explicitly allowed by RFC793 3.4
		//   and reaffirmed by RFC1122 4.2.2.10.
		//
		// - We can also get FIN. One-packet SYN+ACK+FIN is allowed....
		//
		if (!tcpHdr.mbSYN) {
			if (mpSocketHandler)
				mpSocketHandler->OnSocketError();

			Shutdown();
			return;
		}

		mConnState = kATNetTcpConnectionState_SYN_RCVD;

		// initialize receive state to received sequence number
		mRecvRing.Reset(tcpHdr.mSequenceNo + 1);
	}

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
				if ((uint32)(timer.mSequenceEnd - tcpHdr.mAckNo - 1) < 0x7FFFFFFFU)
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
				auto *pClock = mpTcpStack->GetClock();
				mEventRetransmit = pClock->AddClockEvent(pClock->GetTimestamp(3000), this, kEventId_Retransmit);
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
				if (mpSocketHandler && mbLocalOpen)
					mpSocketHandler->OnSocketWriteReady(mXmitRing.GetSpace());

				if (!mpTcpStack)
					return;
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

						if (mpSocketHandler)
							mpSocketHandler->OnSocketClose();

						if (!mpTcpStack)
							return;
					}
				} else
					mRecvRing.Write(data + tcpHdr.mDataOffset, tc);

				if (mpSocketHandler)
					mpSocketHandler->OnSocketReadReady(mRecvRing.GetLevel());

				if (!mpTcpStack)
					return;
			}
		}
	}

	// if the connection is dead, delete it now -- this should be done before we send a reply
	// packet, in case we got both an ACK and the last FIN
	if (mConnState == kATNetTcpConnectionState_CLOSED) {
		Shutdown();
		return;
	}

	// check if we need a reply packet or if we received an ACK and can transmit now
	if (ackNeeded || (tcpHdr.mbACK && CanTransmitMore())) {
		if (mConnState == kATNetTcpConnectionState_ESTABLISHED && !mbSynAcked) {
			Transmit(true);
			mbSynAcked = true;
		}

		Transmit(true);
	}
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
	uint32 dataLen = syn || !mbSynAcked ? 0 : mXmitRing.GetLevel() - xmitOffset;
	bool sendingLast = true;

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
//	replyTcpHeader.mSequenceNo = mXmitNext;
	replyTcpHeader.mSequenceNo = syn || !mbSynAcked ? mXmitRing.GetBaseSeq() : mXmitNext;
	replyTcpHeader.mWindow = mRecvRing.GetSpace();
	
	if (dataLen)
		mXmitRing.Read(xmitOffset, data, dataLen);

	uint32 replyLen = mpTcpStack->EncodePacket(replyPacket + 2, sizeof replyPacket - 2, mConnKey.mLocalAddress, mConnKey.mRemoteAddress, replyTcpHeader, data, dataLen);

	mpTcpStack->SendFrame(mConnKey.mRemoteAddress, replyPacket + 2, replyLen);

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

			Shutdown();
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
	if (!mpTcpStack)
		return;

	// If one side hasn't been closed, send an RST.
	if (mbLocalOpen || !mbFinReceived) {
		ATTcpHeaderInfo dummyHdr = {};
		dummyHdr.mAckNo = mXmitNext;
		mpTcpStack->SendReset(mConnKey.mRemoteAddress, mConnKey.mLocalAddress, mConnKey.mRemotePort, mConnKey.mLocalPort, dummyHdr);
	}

	// Delete us.
	ClearEvents();
	mpTcpStack->DeleteConnection(mConnKey);
	mpTcpStack = nullptr;
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
	VDASSERT(mbLocalOpen);

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

		mpSocketHandler = nullptr;

		if (mXmitRing.GetSpace()) {
			mXmitRing.Write("", 1);
			mbFinQueued = true;
		}

		if (mConnState == kATNetTcpConnectionState_ESTABLISHED)
			mConnState = kATNetTcpConnectionState_FIN_WAIT_1;
		else if (mConnState == kATNetTcpConnectionState_CLOSE_WAIT)
			mConnState = kATNetTcpConnectionState_CLOSED;

		Transmit(false);
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
	if (!mpTcpStack)
		return;

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
