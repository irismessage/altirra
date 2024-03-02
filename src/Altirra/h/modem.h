#ifndef f_AT_MODEM_H
#define f_AT_MODEM_H

#include <vd2/system/thread.h>
#include <vd2/system/VDString.h>
#include "rs232.h"
#include "scheduler.h"
#include "modemtcp.h"

class IATUIRenderer;

class ATModemEmulator : public IATRS232Device, public IATModemDriverCallback, public IATSchedulerCallback {
	ATModemEmulator(const ATModemEmulator&);
	ATModemEmulator& operator=(const ATModemEmulator&);
public:
	ATModemEmulator();
	~ATModemEmulator();

	void Init(ATScheduler *sched, IATUIRenderer *uir);
	void Shutdown();

	void SetConfig(const ATRS232Config& config);

	bool Read(uint8& c);
	void Write(uint8 c);

protected:
	void ParseCommand();

	void SendResponseOK();
	void SendResponseError();
	void SendResponseConnect();
	void SendResponseNoCarrier();
	void SendResponseNoAnswer();

	void TerminateCall();

	void OnScheduledEvent(uint32 id);
	void OnReadAvail(IATModemDriver *sender, uint32 len);
	void OnWriteAvail(IATModemDriver *sender);
	void OnEvent(IATModemDriver *sender, ATModemPhase phase, ATModemEvent event);

	ATScheduler *mpScheduler;
	IATUIRenderer *mpUIRenderer;
	IATModemDriver *mpDriver;
	ATEvent *mpEventEnterCommandMode;
	ATEvent *mpEventCommandModeTimeout;
	uint8	mGuardCharCounter;
	bool	mbCommandMode;
	bool	mbEchoMode;
	bool	mbConnected;
	bool	mbSuppressNoCarrier;
	VDAtomicInt mbNewConnectedState;
	VDAtomicInt mbConnectionFailed;
	uint32	mLastWriteTime;

	VDStringA	mAddress;
	uint32		mPort;

	ATRS232Config	mConfig;

	uint32	mTransmitIndex;
	uint32	mTransmitLength;
	uint8	mTransmitBuffer[128];

	VDStringA	mLastCommand;
	uint8	mCommandBuffer[128];
	uint32	mCommandLength;

	VDCriticalSection mMutex;
	uint32	mDeviceTransmitReadOffset;
	uint32	mDeviceTransmitWriteOffset;
	uint32	mDeviceTransmitLevel;
	bool	mbDeviceTransmitUnderflow;
	uint8	mDeviceTransmitBuffer[4096];
};

#endif
