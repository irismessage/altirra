#ifndef f_AT_RMTBYPASS_H
#define f_AT_RMTBYPASS_H

class IATRMTBypassLink {
public:
	virtual void LinkInit(uint32 t0) = 0;
	virtual uint8 LinkDebugReadByte(uint32 t, uint32 address) = 0;
	virtual uint8 LinkReadByte(uint32 t, uint32 address) = 0;
	virtual void LinkWriteByte(uint32 t, uint32 address, uint8 v) = 0;
};

#endif
