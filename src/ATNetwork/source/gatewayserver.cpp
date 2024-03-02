#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <at/atnetwork/ethernetframe.h>
#include <at/atnetwork/ethernet.h>
#include <at/atnetwork/gatewayserver.h>
#include <at/atnetwork/tcp.h>
#include <at/atnetwork/internal/dhcpd.h>
#include "ipstack.h"
#include "tcpstack.h"
#include "udpstack.h"

struct ATEthernetArpFrameInfo;
struct ATIPv4HeaderInfo;
struct ATTcpHeaderInfo;

///////////////////////////////////////////////////////////////////////////

class ATTestServer : public vdrefcounted<IATSocketHandler> {
public:
	ATTestServer(IATStreamSocket *socket);

	virtual void OnSocketOpen();
	virtual void OnSocketReadReady(uint32 len);
	virtual void OnSocketWriteReady(uint32 len);
	virtual void OnSocketClose();
	virtual void OnSocketError();

protected:
	void ProcessHeaderLine(const char *s);

	vdrefptr<IATStreamSocket> mpSocket;

	vdfastvector<char> mHeaderBuffer;
	uint32 mHeaderHead;
	uint32 mHeaderTail;
	bool mbHeaderReceived;
	VDStringA mReply;
	uint32 mReplyOffset;
};

ATTestServer::ATTestServer(IATStreamSocket *socket)
	: mpSocket(socket)
	, mHeaderHead(0)
	, mHeaderTail(0)
	, mbHeaderReceived(false)
	, mReplyOffset(0)
{
}

void ATTestServer::OnSocketOpen() {
}

void ATTestServer::OnSocketReadReady(uint32 len) {
	AddRef();

	uint32 len0 = (uint32)mHeaderBuffer.size();
	mHeaderBuffer.resize(len0 + len);

	sint32 actual = mpSocket->Recv(mHeaderBuffer.data() + len0, len);

	if (actual < 0)
		actual = 0;

	if (actual != len)
		mHeaderBuffer.resize(len0 + actual);

	char *buf = mHeaderBuffer.data();
	uint32 head = mHeaderHead;
	uint32 tail = mHeaderTail;
	uint32 limit = len0 + actual;

	while(tail + 1 < limit) {
		if (buf[tail] == 0x0D && buf[tail + 1] == 0x0A) {
			buf[tail] = 0;

			ProcessHeaderLine(&buf[head]);

			head = tail + 2;
			++tail;
		}

		++tail;
	}

	if (head >= 2048) {
		mHeaderBuffer.erase(mHeaderBuffer.begin(), mHeaderBuffer.begin() + 2048);

		head -= 2048;
		tail -= 2048;
	}

	mHeaderHead = head;
	mHeaderTail = tail;

	Release();
}

void ATTestServer::OnSocketWriteReady(uint32 len) {
	if (!mbHeaderReceived)
		return;

	uint32 rlen = (uint32)mReply.size() - mReplyOffset;

	if (rlen > len)
		rlen = len;

	sint32 actual = mpSocket->Send(mReply.data() + mReplyOffset, rlen);
	if (actual < 0)
		actual = 0;

	mReplyOffset += actual;

	if (mReplyOffset >= mReply.size())
		mpSocket->CloseSocket(false);
}

void ATTestServer::OnSocketClose() {
}

void ATTestServer::OnSocketError() {
	mpSocket.clear();
}

void ATTestServer::ProcessHeaderLine(const char *s) {
	if (!*s) {
		static const char kContent[]=
			"<html>"
			"<head><title>Test</title></head>"
			"<body>"
			"<h1>Hello, world!</h1>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"<p>This is a test of TCP/IP and HTTP communications over emulation.</p>"
			"</body>"
			"</html>";

		static const char kHeader[]=
			"HTTP/1.0 200 OK\r\n"
			"Server: Altirra/2.50\r\n"
			"Connection: close\r\n"
			"Content-Length: %u\r\n"
			"Content-Type: text/html; charset=utf-8\r\n"
			"\r\n";

		mReply.sprintf(kHeader, sizeof(kContent)-1);
		mReply += kContent;
		mbHeaderReceived = true;

		OnSocketWriteReady((uint32)mReply.size());
	}
}

class ATTestListener : public IATEmuNetSocketListener {
	virtual bool OnSocketIncomingConnection(uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, IATStreamSocket *socket, IATSocketHandler **handler) {
		ATTestServer *srv = new ATTestServer(socket);

		*handler = srv;
		srv->AddRef();
		return true;
	}
} gTestListener;

///////////////////////////////////////////////////////////////////////////

class ATEthernetGatewayServer final : public vdrefcounted<IATEthernetGatewayServer>, public IATEthernetEndpoint {
	ATEthernetGatewayServer(const ATEthernetGatewayServer&) = delete;
	ATEthernetGatewayServer& operator=(const ATEthernetGatewayServer&) = delete;
public:
	ATEthernetGatewayServer();
	~ATEthernetGatewayServer();

	void Init(IATEthernetSegment *seg, uint32 clockIndex, uint32 netaddr, uint32 netmask, bool natEnabled) override;
	void Shutdown() override;

	void ColdReset() override;

	IATEmuNetUdpStack *GetUdpStack() override { return &mUdpStack; }
	IATEmuNetTcpStack *GetTcpStack() override { return &mTcpStack; }

	void SetBridgeListener(IATEmuNetSocketListener *tcp, IATEmuNetUdpSocketListener *udp) override;
	void GetConnectionInfo(vdfastvector<ATNetConnectionInfo>& connInfo) const override;

	void Listen(IATEmuNetSocketListener *listener, uint16 port);
	void Unlisten(IATEmuNetSocketListener *listener, uint16 port);

public:
	void ReceiveFrame(const ATEthernetPacket& packet, ATEthernetFrameDecodedType decType, const void *decInfo) override;

protected:
	void OnArpPacket(const ATEthernetPacket& packet, const ATEthernetArpFrameInfo& decInfo);
	void OnIPv4Datagram(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& decInfo);
	void OnIPv4Packet(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& decInfo, const uint8 *data, const uint32 len);
	void OnICMPPacket(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& decInfo, const uint8 *data, const uint32 len);

	void CloseAllConnections();

	IATEthernetSegment *mpEthSegment;
	uint32	mEthClockId;
	uint32	mEthEndpointId;

	ATEthernetAddr	mHwAddress;
	uint32	mIpAddress;
	uint32	mIpNetMask;
	uint32	mIpNetBroadcastAddress;

	ATNetIpStack mIpStack;
	ATNetTcpStack mTcpStack;
	ATNetUdpStack mUdpStack;
	ATNetDhcpDaemon mDhcpd;
};

ATEthernetGatewayServer::ATEthernetGatewayServer()
	: mpEthSegment(NULL)
	, mEthEndpointId(0)
	, mEthClockId(0)
{
	// use a (one-time) randomized locally administered address
	static const uint8 kHwAddress[6] = { 0x02, 0xE6, 0x27, 0xC1, 0x32, 0x11 };

	memcpy(mHwAddress.mAddr, kHwAddress, 6);
}

ATEthernetGatewayServer::~ATEthernetGatewayServer() {
	Shutdown();
}

void ATEthernetGatewayServer::Init(IATEthernetSegment *seg, uint32 clockId, uint32 netaddr, uint32 netmask, bool natEnabled) {
	mpEthSegment = seg;
	mEthClockId = clockId;
	mEthEndpointId = seg->AddEndpoint(this);

	mIpAddress = netaddr | VDToBE32(1);
	mIpNetMask = netmask;
	mIpNetBroadcastAddress = mIpAddress | ~mIpNetMask;

	mIpStack.Init(mHwAddress, mIpAddress, mIpNetMask, seg, clockId, mEthEndpointId);
	mTcpStack.Init(&mIpStack);
	mUdpStack.Init(&mIpStack);

	VDVERIFY(mTcpStack.Bind(80, &gTestListener));

	mDhcpd.Init(&mUdpStack, natEnabled);
}

void ATEthernetGatewayServer::Shutdown() {
	CloseAllConnections();

	mDhcpd.Shutdown();

	mTcpStack.Shutdown();
	mIpStack.Shutdown();

	if (mEthEndpointId) {
		mpEthSegment->RemoveEndpoint(mEthEndpointId);
		mEthEndpointId = 0;
	}

	mEthClockId = 0;
	mpEthSegment = NULL;
}

void ATEthernetGatewayServer::ColdReset() {
	CloseAllConnections();

	mIpStack.ClearArpCache();
	mDhcpd.Reset();
}

void ATEthernetGatewayServer::SetBridgeListener(IATEmuNetSocketListener *tcp, IATEmuNetUdpSocketListener *udp) {
	mTcpStack.SetBridgeListener(tcp);
	mUdpStack.SetBridgeListener(udp);
}

void ATEthernetGatewayServer::GetConnectionInfo(vdfastvector<ATNetConnectionInfo>& connInfo) const {
	vdfastvector<ATNetTcpConnectionInfo> tcpConnInfo;

	mTcpStack.GetConnectionInfo(tcpConnInfo);

	size_t n = tcpConnInfo.size();
	connInfo.resize(n);

	for(size_t i=0; i<n; ++i) {
		const ATNetTcpConnectionInfo& tcpInfo = tcpConnInfo[i];
		ATNetConnectionInfo& info = connInfo[i];

		VDWriteUnalignedU32(info.mRemoteAddr, tcpInfo.mConnKey.mRemoteAddress);
		VDWriteUnalignedU32(info.mLocalAddr, tcpInfo.mConnKey.mLocalAddress);
		info.mRemotePort = tcpInfo.mConnKey.mRemotePort;
		info.mLocalPort = tcpInfo.mConnKey.mLocalPort;
		info.mpProtocol = "TCP";

		switch(tcpInfo.mConnState) {
			case kATNetTcpConnectionState_SYN_SENT:		info.mpState = "SYN_SENT"; break;
			case kATNetTcpConnectionState_SYN_RCVD:		info.mpState = "SYN_RCVD"; break;
			case kATNetTcpConnectionState_ESTABLISHED:	info.mpState = "ESTABLISHED"; break;
			case kATNetTcpConnectionState_FIN_WAIT_1:	info.mpState = "FIN_WAIT_1"; break;
			case kATNetTcpConnectionState_CLOSING:		info.mpState = "CLOSING"; break;
			case kATNetTcpConnectionState_FIN_WAIT_2:	info.mpState = "FIN_WAIT_2"; break;
			case kATNetTcpConnectionState_TIME_WAIT:	info.mpState = "TIME_WAIT"; break;
			case kATNetTcpConnectionState_CLOSE_WAIT:	info.mpState = "CLOSE_WAIT"; break;
			case kATNetTcpConnectionState_LAST_ACK:		info.mpState = "LAST_ACK"; break;
			case kATNetTcpConnectionState_CLOSED:		info.mpState = "CLOSED"; break;
			default:									info.mpState = "???"; break;
		}
	}

}

void ATEthernetGatewayServer::Listen(IATEmuNetSocketListener *listener, uint16 port) {
	mTcpStack.Bind(port, listener);
}

void ATEthernetGatewayServer::Unlisten(IATEmuNetSocketListener *listener, uint16 port) {
	mTcpStack.Unbind(port, listener);
}

void ATEthernetGatewayServer::ReceiveFrame(const ATEthernetPacket& packet, ATEthernetFrameDecodedType decType, const void *decInfo) {
	// discard packet if not for us
	if (!ATEthernetIsBroadcastAddr(packet.mDstAddr) && memcmp(packet.mDstAddr.mAddr, mHwAddress.mAddr, 6))
		return;

	switch(decType) {
		case kATEthernetFrameDecodedType_ARP:
			OnArpPacket(packet, *(const ATEthernetArpFrameInfo *)decInfo);
			break;

		case kATEthernetFrameDecodedType_IPv4:
			OnIPv4Datagram(packet, *(const ATIPv4HeaderInfo *)decInfo);
			break;
	}
}

void ATEthernetGatewayServer::OnArpPacket(const ATEthernetPacket& packet, const ATEthernetArpFrameInfo& decInfo) {
	if (decInfo.mTargetProtocolAddr != mIpAddress)
		return;

	if (decInfo.mOp == ATEthernetArpFrameInfo::kOpRequest) {
		ATEthernetArpFrameInfo encInfo;
		
		encInfo.mOp = ATEthernetArpFrameInfo::kOpReply;
		
		encInfo.mTargetHardwareAddr = decInfo.mSenderHardwareAddr;
		encInfo.mTargetProtocolAddr = decInfo.mSenderProtocolAddr;

		encInfo.mSenderHardwareAddr = mHwAddress;
		encInfo.mSenderProtocolAddr = mIpAddress;

		uint8 buf[32];
		uint32 len = ATEthernetEncodeArpPacket(buf, sizeof buf, encInfo);
		if (len) {
			ATEthernetPacket replyPacket;

			replyPacket.mSrcAddr = mHwAddress;
			replyPacket.mDstAddr = packet.mSrcAddr;
			replyPacket.mClockIndex = mEthClockId;
			replyPacket.mpData = buf;
			replyPacket.mLength = len;
			replyPacket.mTimestamp = 100;

			mpEthSegment->TransmitFrame(mEthEndpointId, replyPacket);
		}
	} else if (decInfo.mOp == ATEthernetArpFrameInfo::kOpReply) {
		mIpStack.AddArpEntry(decInfo.mSenderProtocolAddr, decInfo.mSenderHardwareAddr, true);
	}
}

void ATEthernetGatewayServer::OnIPv4Datagram(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& decInfo) {
	// check if destination is broadcast
	if (decInfo.mDstAddr == 0xFFFFFFFFU || decInfo.mDstAddr == mIpNetBroadcastAddress) {
		// drop if TCP -- only unicast allowed
		if (decInfo.mProtocol == 6)
			return;
	} else if ((decInfo.mDstAddr & VDToBE32(0xF0000000)) == VDToBE32(0xE0000000)) {
		// drop all multicast packets
		return;
	} else if (!decInfo.mDstAddr) {
		// Drop 0.0.0.0 -- invalid as a destination address per RFC1700 since it refers to
		// localhost
		return;
	} else {
		// drop if packet destination isn't us but is on our subnet (we accept other addresses for gateway reasons)
		if (decInfo.mDstAddr != mIpAddress) {
			if (!((mIpAddress ^ decInfo.mDstAddr) & mIpNetMask))
				return;
		}

		// drop if packet came from the LAN side but has a non-LAN address
		if ((decInfo.mSrcAddr ^ mIpAddress) & mIpNetMask)
			return;

		// drop if packet says it came from us
		if (decInfo.mSrcAddr == mIpAddress)
			return;

		// check if packet is fragmented
		if (decInfo.mFragmentOffset || (decInfo.mFlags & 1)) {
			VDASSERT(!"Fragments not yet supported.");
			return;
		}
	}

	// update ARP cache
	mIpStack.AddArpEntry(decInfo.mSrcAddr, packet.mSrcAddr, false);

	// forward to TCP or UDP layer
	OnIPv4Packet(packet, decInfo, packet.mpData + 2 + decInfo.mDataOffset, decInfo.mDataLength);
}

void ATEthernetGatewayServer::OnIPv4Packet(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& decInfo, const uint8 *data, const uint32 len) {
	switch(decInfo.mProtocol) {
		case 1:		// ICMP
			OnICMPPacket(packet, decInfo, data, len);
			break;

		case 6:		// TCP
			mTcpStack.OnPacket(packet, decInfo, data, len);
			break;

		case 17:	// UDP
			mUdpStack.OnPacket(packet, decInfo, data, len);
			break;
	}
}

void ATEthernetGatewayServer::OnICMPPacket(const ATEthernetPacket& packet, const ATIPv4HeaderInfo& decInfo, const uint8 *data, const uint32 len) {
	// drop packets shorter than ICMP header
	if (len < 8)
		return;

	// compute checksum
	uint64 sum64 = 0;

	for(uint32 i = 0; i < len - 3; i += 4)
		sum64 += VDReadUnalignedU32(data + i);

	switch(len & 3) {
		case 0:
		default:
			break;

		case 1:
			sum64 += VDFromLE16(data[len - 1]);
			break;

		case 2:
			sum64 += VDReadUnalignedU16(&data[len - 2]);
			break;

		case 3:
			sum64 += VDReadUnalignedU16(&data[len - 3]);
			sum64 += VDFromLE16(data[len - 1]);
			break;
	}

	// fold checksum
	sum64 = (uint32)sum64 + (sum64 >> 32);

	uint32 sum32 = (uint32)sum64 + (uint32)(sum64 >> 32);
	sum32 = (sum32 & 0xffff) + (sum32 >> 16);

	const uint16 sum16 = (uint16)(~((sum32 & 0xffff) + (sum32 >> 16)));

	// verify checksum
	if (sum16 != 0)
		return;

	// check ICMP type -- we only support type 8 (echo request)
	const uint8 type = data[0];

	if (type != 8)
		return;

	// drop echo requests that are too large
	if (len > 576)
		return;

	// copy request frame
	uint8 reply[576 + 22];

	// encode IPv4 header
	ATIPv4HeaderInfo iphdr;
	mIpStack.InitHeader(iphdr);
	iphdr.mSrcAddr = mIpStack.GetIpAddress();
	iphdr.mDstAddr = decInfo.mSrcAddr;
	iphdr.mProtocol = 1;	// ICMP
	iphdr.mDataOffset = 0;
	iphdr.mDataLength = len;
	VDVERIFY(ATIPv4EncodeHeader(reply, 22, iphdr));

	memcpy(reply + 22, data, len);

	// change echo request to echo reply
	reply[22] = 0;
	reply[23] = 0;

	// update ICMP checksum (HC' = ~(~HC + ~m + m') in 1's per RFC1624)
	uint32 chk = VDReadUnalignedU16(reply + 24);

	chk = (chk ^ 0xFFFF) + (VDReadUnalignedU16(data) ^ 0xFFFF);

	chk = (chk & 0xFFFF) + (chk >> 16);
	chk = (chk & 0xFFFF) + (chk >> 16);

	chk ^= 0xFFFF;
	VDWriteUnalignedU16(reply + 24, chk);

	// send reply frame
	mIpStack.SendFrame(decInfo.mSrcAddr, reply, len + 22);
}

void ATEthernetGatewayServer::CloseAllConnections() {
	mTcpStack.CloseAllConnections();
}

void ATCreateEthernetGatewayServer(IATEthernetGatewayServer **p) {
	*p = new ATEthernetGatewayServer;
	(*p)->AddRef();
}
