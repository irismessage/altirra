#ifndef f_AT_ATNETWORKSOCKETS_INTERNAL_WORKER_H
#define f_AT_ATNETWORKSOCKETS_INTERNAL_WORKER_H

#include <vd2/system/vdstl.h>
#include <at/atnetworksockets/worker.h>

class ATNetSockWorker;

class ATNetSockBridgeHandler : public vdrefcounted<IATSocketHandler> {
public:
	ATNetSockBridgeHandler(ATNetSockWorker *parent, SOCKET s, IATSocket *s2, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort);

	uint32 GetSrcIpAddr() const { return mSrcIpAddr; }
	uint16 GetSrcPort() const { return mSrcPort; }
	uint32 GetDstIpAddr() const { return mDstIpAddr; }
	uint16 GetDstPort() const { return mDstPort; }

	void Shutdown();

	void OnNativeSocketConnect();
	void OnNativeSocketReadReady();
	void OnNativeSocketWriteReady();
	void OnNativeSocketClose();
	void OnNativeSocketError();

public:
	virtual void OnSocketOpen();
	virtual void OnSocketReadReady(uint32 len);
	virtual void OnSocketWriteReady(uint32 len);
	virtual void OnSocketClose();
	virtual void OnSocketError();
	
protected:
	void TryCopyToNative();
	void TryCopyFromNative();

	uint32 mSrcIpAddr;
	uint16 mSrcPort;
	uint32 mDstIpAddr;
	uint16 mDstPort;
	ATNetSockWorker *mpParent;
	SOCKET mNativeSocket;
	vdrefptr<IATSocket> mpLocalSocket;
	bool mbLocalClosed;
	bool mbNativeConnected;
	bool mbNativeClosed;
	uint32 mLocalReadAvail;
	uint32 mLocalWriteAvail;

	uint32 mRecvBase;
	uint32 mRecvLimit;
	uint32 mSendBase;
	uint32 mSendLimit;

	char mRecvBuf[1024];
	char mSendBuf[1024];
};

class ATNetSockWorker : public vdrefcounted<IATNetSockWorker>, public IATSocketListener, public IATUdpSocketListener {
	friend class ATNetSockBridgeHandler;
public:
	ATNetSockWorker();
	~ATNetSockWorker();

	virtual IATSocketListener *AsSocketListener() { return this; }
	virtual IATUdpSocketListener *AsUdpListener() { return this; }

	bool Init(IATNetUdpStack *udp, bool externalAccess);
	void Shutdown();

	void ResetAllConnections();

	virtual bool GetHostAddressForLocalAddress(bool tcp, uint32 srcIp, uint16 srcPort, uint32 dstIp, uint16 dstPort, uint32& hostIp, uint16& hostPort);

public:
	virtual bool OnSocketIncomingConnection(uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, IATSocket *socket, IATSocketHandler **handler);
	virtual void OnUdpDatagram(const ATEthernetAddr& srcHwAddr, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort, const void *data, uint32 dataLen);

protected:
	void DeleteConnection(SOCKET s);

protected:
	enum {
		MYWM_TCP_SOCKET = WM_USER,
		MYWM_UDP_SOCKET
	};

	LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void ProcessUdpDatagram(SOCKET s, uint32 srcIpAddr, uint16 srcPort, uint32 dstIpAddr, uint16 dstPort);

	VDFunctionThunk *mpWndThunk;
	ATOM mWndClass;
	HWND mhwnd;

	IATNetUdpStack *mpUdpStack;
	bool mbAllowExternalAccess;

	typedef vdhashmap<SOCKET, ATNetSockBridgeHandler *> TcpConnections;
	TcpConnections mTcpConnections;

	struct UdpConnection {
		uint32 mSrcIpAddr;
		uint32 mDstIpAddr;
		uint16 mSrcPort;
		uint16 mDstPort;
	};

	struct UdpConnectionHash {
		size_t operator()(const UdpConnection& conn) const {
			return conn.mSrcIpAddr
				+ conn.mDstIpAddr
				+ conn.mSrcPort
				+ conn.mDstPort
				;
		}
	};

	struct UdpConnectionEqual {
		bool operator()(const UdpConnection& x, const UdpConnection& y) const {
			return x.mSrcIpAddr == y.mSrcIpAddr
				&& x.mDstIpAddr == y.mDstIpAddr
				&& x.mSrcPort == y.mSrcPort
				&& x.mDstPort == y.mDstPort
				;
		}
	};

	typedef vdhashmap<UdpConnection, SOCKET, UdpConnectionHash, UdpConnectionEqual> UdpSourceMap;
	UdpSourceMap mUdpSourceMap;

	typedef vdhashmap<SOCKET, UdpConnection> UdpSocketMap;
	UdpSocketMap mUdpSocketMap;
};

#endif
