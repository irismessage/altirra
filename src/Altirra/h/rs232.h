#ifndef f_AT_RS232_H
#define f_AT_RS232_H

class ATCPUEmulator;
class ATCPUEmulatorMemory;
class ATScheduler;
class IATUIRenderer;

struct ATRS232Config {
	bool	mbTelnetEmulation;

	ATRS232Config() : mbTelnetEmulation(true) {}
};

class IATRS232Emulator {
public:
	virtual ~IATRS232Emulator() {}

	virtual void Init(ATCPUEmulatorMemory *mem, ATScheduler *sched, IATUIRenderer *uir) = 0;
	virtual void Shutdown() = 0;

	virtual void GetConfig(ATRS232Config& config) = 0;
	virtual void SetConfig(const ATRS232Config& config) = 0;

	virtual void OnCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset) = 0;
};

class IATRS232Device {
public:
	virtual ~IATRS232Device() {}
	virtual bool Read(uint8& c) = 0;
	virtual void Write(uint8 c) = 0;
	virtual void SetConfig(const ATRS232Config&) = 0;
};

IATRS232Emulator *ATCreateRS232Emulator();

#endif
