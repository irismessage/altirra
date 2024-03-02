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
	IATSocketListener *mpHandler;
};

class ATNetTcpRingBuffer {
public:
	ATNetTcpRingBuffer();

	void Init(char *buf, uint32 size);

	void Reset(uint32 seq);

	uint32 Write(const void *p, uint32 n);
	void Read(uint32 offset, void *p, uint32 n);
	void Ack(uint32 n);

	uint32 GetSpace() const { return mSize - mLevel; }
	uint32 GetLevel() const { return mLevel; }
	uint32 GetBaseSeq() const { return mBaseSeq; }

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

class ATNetTcpStack final : public IATNetTcpStack {
public:
	ATNetTcpStack();

	IATEthernetClock *GetClock() const { return mpClock; }

	void Init(ATNetIpStack *ipStack);
	void Shutdown();

	void SetBridgeListener(IATSocketListener *p);

	bool Bind(uint16 port, IATSocketListener *listener);
	void Unbind(uint16 port, IATSocketListener *listener);

	bool Connect(uint32 dstIpAddr, uint16 dstPort, IATSocketHandler *handler, IATSocket **newSocket);

	void CloseAllConnections();

	void GetConnectionInfo(vdfastvector<ATNetTcpConnectionInfo>& conns) const;

public:
	void OnPacket(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& iphdr, const uint8 *data, const uint32 len);

	uint32 EncodePacket(uint8 *dst, uint32 len, uint32 srcIpAddr, uint32 dstIpAddr, const ATTcpHeaderInfo& hdrInfo, const void *data, uint32 dataLen);
	void SendReset(const ATIPv4HeaderInfo& iphdr, uint16 srcPort, uint16 dstPort, const ATTcpHeaderInfo& origTcpHdr);
	void SendReset(uint32 srcIpAddr, uint32 dstIpAddr, uint16 srcPort, uint16 dstPort, const ATTcpHeaderInfo& origTcpHdr);
	void SendFrame(uint32 dstIpAddr, const void *data, uint32 len);
	void SendFrame(const ATEthernetAddr& dstAddr, const void *data, uint32 len);

	void DeleteConnection(const ATNetTcpConnectionKey& connKey);

protected:
	ATNetIpStack *mpIpStack;
	IATEthernetClock *mpClock;
	uint16 mPortCounter;
	uint32 mXmitInitialSequenceSalt;

	IATSocketListener *mpBridgeListener;

	typedef vdhashmap<uint32, ATNetTcpListeningSocket> ListeningSockets;
	ListeningSockets mListeningSockets;

	typedef vdhashmap<ATNetTcpConnectionKey, ATNetTcpConnection *, ATNetTcpConnectionKeyHash, ATNetTcpConnectionKeyEq> Connections;
	Connections mConnections;
};

struct ATNetTcpConnection : public vdrefcounted<IATSocket>, public IATEthernetClockEventSink {
public:
	ATNetTcpConnection(ATNetTcpStack *stack, const ATNetTcpConnectionKey& connKey);
	~ATNetTcpConnection();

	void GetInfo(ATNetTcpConnectionInfo& info) const;

	void Init(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& ipHdr, const ATTcpHeaderInfo& tcpHdr);
	void InitOutgoing(IATSocketHandler *h, uint32 isnSalt);

	void SetSocketHandler(IATSocketHandler *h);

	void OnPacket(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& iphdr, const ATTcpHeaderInfo& tcpHdr, const uint8 *data, const uint32 len);

	void Transmit(bool ack);

public:
	virtual void OnClockEvent(uint32 eventid, uint32 userid);

public:
	virtual void Shutdown();
	virtual uint32 Read(void *buf, uint32 len);
	virtual uint32 Write(const void *buf, uint32 len);
	virtual void Close();

protected:
	bool CanTransmitMore() const;
	void ClearEvents();

	enum {
		kEventId_Close = 1,
		kEventId_Transmit,
		kEventId_Retransmit,
	};

	const ATNetTcpConnectionKey mConnKey;
	ATNetTcpStack *mpTcpStack;
	vdrefptr<IATSocketHandler> mpSocketHandler;
	uintptr_t mSocketHandlerData;
	bool mbLocalOpen;
	bool mbSynQueued;
	bool mbSynAcked;
	bool mbFinQueued;
	bool mbFinReceived;

	ATNetTcpConnectionState mConnState;

	uint32	mEventClose;
	uint32	mEventTransmit;
	uint32	mEventRetransmit;

	struct PacketTimer {
		uint32 mNext;
		uint32 mPrev;
		uint32 mRetransmitTimestamp;
		uint32 mSequenceStart;
		uint32 mSequenceEnd;
	};

	typedef vdfastvector<PacketTimer> PacketTimers;
	PacketTimers mPacketTimers;

	ATNetTcpRingBuffer mRecvRing;
	ATNetTcpRingBuffer mXmitRing;
	uint32 mXmitLastAck;
	uint32 mXmitWindowLimit;
	uint32 mXmitNext;

	char mRecvBuf[32768];
	char mXmitBuf[32768];
};

#endif	// f_ATNETWORK_TCPSTACK_H
