#ifndef f_ATNETWORK_TCPSTACK_H
#define f_ATNETWORK_TCPSTACK_H

#include <vd2/system/vdstl.h>
#include <vd2/system/vdstl_hashmap.h>
#include <at/atnetwork/ethernet.h>
#include <at/atnetwork/socket.h>

struct ATIPv4HeaderInfo;
struct ATTcpHeaderInfo;
class ATNetIpStack;
struct ATNetTcpConnection;

struct ATNetTcpListeningSocket {
	IATEmuNetSocketListener *mpHandler;
};

class ATNetTcpRingBuffer {
public:
	ATNetTcpRingBuffer();

	void Init(char *buf, uint32 size);

	void Reset(uint32 seq);

	uint32 Write(const void *p, uint32 n);
	void Read(uint32 offset, void *p, uint32 n) const;
	void Ack(uint32 n);

	uint32 GetSpace() const { return mSize - mLevel; }
	uint32 GetLevel() const { return mLevel; }
	uint32 GetBaseSeq() const { return mBaseSeq; }
	uint32 GetTailSeq() const { return mBaseSeq + mLevel; }

protected:
	uint32 mReadPtr;
	uint32 mWritePtr;
	uint32 mLevel;
	uint32 mSize;
	uint32 mBaseSeq;
	char *mpBuffer;
};

enum ATNetTcpConnectionState {
	// SYN sent
	//	SYN -> OUT_SYN_RCVD
	//	SYN+ACK -> ESTABLISHED
	//
	kATNetTcpConnectionState_SYN_SENT,

	// SYN received
	//	ACK -> ESTABLISHED
	//
	kATNetTcpConnectionState_SYN_RCVD,

	// Connection established
	//	CLOSE -> send FIN, go to FIN_WAIT_1
	//	FIN -> send ACK, go to CLOSE_WAIT
	//
	kATNetTcpConnectionState_ESTABLISHED,

	// Local side closed - FIN sent, waiting for ACK
	//	ACK -> FIN_WAIT_2
	//	FIN -> send ACK, go to CLOSING
	//
	kATNetTcpConnectionState_FIN_WAIT_1,

	// Remote side closed after local closed - FIN sent, received FIN, waiting for ACK
	//	ACK -> TIME_WAIT
	//
	kATNetTcpConnectionState_CLOSING,

	// Local side closed - FIN sent and ACKed, waiting for FIN from remote side
	//	FIN -> send ACK, go to TIME_WAIT
	//
	kATNetTcpConnectionState_FIN_WAIT_2,

	// Waiting 2MSL
	//	Timer expires -> delete connection
	//
	kATNetTcpConnectionState_TIME_WAIT,

	// Remote side closed - FIN received, ACK sent
	//	CLOSE -> send FIN
	//
	kATNetTcpConnectionState_CLOSE_WAIT,

	// Local side closed after remote closed - FIN sent, waiting for ACK
	//	ACK -> delete connection
	kATNetTcpConnectionState_LAST_ACK,

	// Not a TCP state -- indicates the TCB should be deleted
	kATNetTcpConnectionState_CLOSED
};

struct ATNetTcpConnectionKey {
	uint32 mLocalAddress;		// This is necessary when forwarding.
	uint32 mRemoteAddress;
	uint16 mRemotePort;
	uint16 mLocalPort;
};

struct ATNetTcpConnectionKeyHash {
	size_t operator()(const ATNetTcpConnectionKey& x) const {
		return x.mLocalAddress + x.mRemoteAddress + x.mRemotePort + ((uint32)x.mLocalPort << 16);
	}
};

struct ATNetTcpConnectionKeyEq {
	bool operator()(const ATNetTcpConnectionKey& x, const ATNetTcpConnectionKey& y) const {
		return x.mLocalAddress == y.mLocalAddress
			&& x.mRemoteAddress == y.mRemoteAddress
			&& x.mRemotePort == y.mRemotePort
			&& x.mLocalPort == y.mLocalPort;
	}
};

struct ATNetTcpConnectionInfo {
	ATNetTcpConnectionKey mConnKey;
	ATNetTcpConnectionState mConnState;
};

class ATNetTcpStack final : public IATEmuNetTcpStack, private IATEthernetClockEventSink {
public:
	ATNetTcpStack();
	~ATNetTcpStack();

	IATEthernetClock *GetClock() const { return mpClock; }

	void Init(ATNetIpStack *ipStack);
	void Shutdown();

	void SetBridgeListener(IATEmuNetSocketListener *p);

	bool Bind(uint16 port, IATEmuNetSocketListener *listener);
	void Unbind(uint16 port, IATEmuNetSocketListener *listener);

	bool Connect(uint32 dstIpAddr, uint16 dstPort, IATSocketHandler& handler, IATStreamSocket **newSocket);

	void CloseAllConnections();

	void GetConnectionInfo(vdfastvector<ATNetTcpConnectionInfo>& conns) const;

public:
	void OnPacket(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& iphdr, const uint8 *data, const uint32 len);

	uint32 EncodePacket(uint8 *dst, uint32 len, uint32 srcIpAddr, uint32 dstIpAddr, const ATTcpHeaderInfo& hdrInfo, const void *data, uint32 dataLen, const void *opts = nullptr, uint32 optLen = 0);
	void SendReset(const ATIPv4HeaderInfo& iphdr, uint16 srcPort, uint16 dstPort, const ATTcpHeaderInfo& origTcpHdr);
	void SendReset(uint32 srcIpAddr, uint32 dstIpAddr, uint16 srcPort, uint16 dstPort, const ATTcpHeaderInfo& origTcpHdr);
	void SendFrame(uint32 dstIpAddr, const void *data, uint32 len);
	void SendFrame(const ATEthernetAddr& dstAddr, const void *data, uint32 len);

	void NotifyAbandonedConnection();
	void DeleteConnection(const ATNetTcpConnectionKey& connKey);

public:
	void OnClockEvent(uint32 eventid, uint32 userid) override;

private:
	uint16 FindUnusedDynamicPort();

	ATNetIpStack *mpIpStack = nullptr;
	IATEthernetClock *mpClock = nullptr;
	uint32 mAbandonedSocketEvent = 0;
	uint16 mPortCounter = 49152;
	uint32 mXmitInitialSequenceSalt = 0;

	IATEmuNetSocketListener *mpBridgeListener = nullptr;

	typedef vdhashmap<uint32, ATNetTcpListeningSocket> ListeningSockets;
	ListeningSockets mListeningSockets;

	typedef vdhashmap<ATNetTcpConnectionKey, ATNetTcpConnection *, ATNetTcpConnectionKeyHash, ATNetTcpConnectionKeyEq> Connections;
	Connections mConnections;
};

struct ATNetTcpConnection final : public vdrefcounted<IATStreamSocket>, public IATEthernetClockEventSink {
public:
	ATNetTcpConnection(ATNetTcpStack *stack, const ATNetTcpConnectionKey& connKey);
	~ATNetTcpConnection();

	int Release() override;

	void CheckAbandoned();

	void GetInfo(ATNetTcpConnectionInfo& info) const;

	void Init(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& ipHdr, const ATTcpHeaderInfo& tcpHdr, const uint8 *data);
	void InitOutgoing(IATSocketHandler *h, uint32 isnSalt);

	void SetSocketHandler(IATSocketHandler *h);

	void OnPacket(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& iphdr, const ATTcpHeaderInfo& tcpHdr, const uint8 *data, const uint32 len);

	void TryTransmitMore(bool immediate);
	void Transmit(bool ack, int retransmitCount = 0, bool enableWindowProbe = false);

public:
	void OnClockEvent(uint32 eventid, uint32 userid) override;

public:
	void SetOnEvent(IATAsyncDispatcher *dispatcher, vdfunction<void(const ATSocketStatus&)> fn, bool callIfReady) override;

	ATSocketStatus GetSocketStatus() const override { return {}; }
	void CloseSocket(bool force) override;
	ATSocketAddress GetLocalAddress() const override;
	ATSocketAddress GetRemoteAddress() const override;
	sint32 Recv(void *buf, uint32 len) override;
	sint32 Send(const void *buf, uint32 len) override;
	void ShutdownSocket(bool send, bool receive) override;

private:
	enum TransmitStatus {
		kTransmitStatus_No,
		kTransmitStatus_Deferred,
		kTransmitStatus_DeferredZeroWindow,
		kTransmitStatus_Yes
	};

	TransmitStatus GetTransmitStatus() const;
	void ClearEvents();
	void ProcessSynOptions(const ATEthernetPacket& packet, const ATTcpHeaderInfo& tcpHdr, const uint8 *data);
	void ChangeToTimeWait();
	void ResetConnection();

	enum {
		kEventId_Close = 1,
		kEventId_Transmit,
		kEventId_Retransmit,
		kEventId_WindowProbe,
		kEventId_ZeroWindowProbe,
	};

	const ATNetTcpConnectionKey mConnKey;
	ATNetTcpStack *mpTcpStack = nullptr;
	vdrefptr<IATSocketHandler> mpSocketHandler;
	bool mbLocalSendOpen = true;
	bool mbLocalRecvOpen = true;
	bool mbSynQueued = false;
	bool mbFinQueued = false;
	bool mbFinReceived = false;

	ATNetTcpConnectionState mConnState = {};

	uint32	mEventClose = 0;
	uint32	mEventTransmit = 0;
	uint32	mEventRetransmit = 0;
	uint32	mEventWindowProbe = 0;
	uint32	mEventZeroWindowProbe = 0;

	struct PacketTimer {
		uint32 mNext;
		uint32 mPrev;
		uint32 mSequenceStart;		// free next for root
		uint32 mSequenceEnd;
		uint32 mRetransmitCount;
	};

	typedef vdfastvector<PacketTimer> PacketTimers;
	PacketTimers mPacketTimers;

	ATNetTcpRingBuffer mRecvRing;
	ATNetTcpRingBuffer mXmitRing;
	uint32 mXmitLastAck = 0;
	uint32 mXmitWindowLimit = 0;
	uint32 mXmitNext = 0;

	// This is the minimum window we require before sending data, to mitigate SWS.
	// It is set to min(max_window/2, mss).
	uint32 mXmitWindowThreshold = 0;

	// This is the maximum window size we've ever seen. We need to update this as
	// we go due to TCP slow start.
	uint32 mXmitMaxWindow = 0;

	// MSS we can receive on our end. We set this. The max for Ethernet is 1460
	// (1500 frame - 20 IP header - 20 TCP header).
	static constexpr uint32 kMaxMSS = 1460;
	uint32 mRecvMaxSegment = kMaxMSS;

	// MSS we can transmit (other side can receive). We get adjust this if the MSS
	// option arrives. If not, we can only assume 536, the minimum required by the
	// spec.
	uint32 mXmitMaxSegment = 512;

	vdfunction<void(const ATSocketStatus&)> mpOnEvent;

	char mRecvBuf[32768];
	char mXmitBuf[32768];

	static const uint32 kMaxRetransmits = 5;
};

#endif	// f_ATNETWORK_TCPSTACK_H
