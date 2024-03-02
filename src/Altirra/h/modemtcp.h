#ifndef f_AT_MODEMTCP_H
#define f_AT_MODEMTCP_H

class IATModemDriverCallback;
struct ATRS232Config;

enum ATModemEvent {
	kATModemEvent_None,
	kATModemEvent_GenericError,
	kATModemEvent_AllocFail,
	kATModemEvent_NameLookupFailed,
	kATModemEvent_ConnectFailed,
	kATModemEvent_ConnectionDropped,
	kATModemEvent_LineInUse,
	kATModemEvent_NoDialTone,
	kATModemEvent_Connected
};

enum ATModemPhase {
	kATModemPhase_Init,
	kATModemPhase_NameLookup,
	kATModemPhase_Connecting,
	kATModemPhase_Connect,
	kATModemPhase_Listen,
	kATModemPhase_Accept,
	kATModemPhase_Connected
};

class IATModemDriver {
public:
	virtual ~IATModemDriver() {}

	virtual bool Init(const char *address, uint32 port, IATModemDriverCallback *callback) = 0;
	virtual void Shutdown() = 0;

	virtual uint32 Write(const void *data, uint32 len) = 0;
	virtual uint32 Read(void *buf, uint32 len) = 0;

	virtual void SetConfig(const ATRS232Config& config) = 0;
};

class IATModemDriverCallback {
public:
	virtual void OnReadAvail(IATModemDriver *sender, uint32 len) = 0;
	virtual void OnWriteAvail(IATModemDriver *sender) = 0;
	virtual void OnEvent(IATModemDriver *sender, ATModemPhase phase, ATModemEvent event) = 0;
};

IATModemDriver *ATCreateModemDriverTCP();

#endif
