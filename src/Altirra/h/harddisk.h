#ifndef AT_HARDDISK_H
#define AT_HARDDISK_H

class ATCPUEmulator;
class ATCPUEmulatorMemory;
class IATUIRenderer;

class IATHardDiskEmulator {
public:
	virtual ~IATHardDiskEmulator() {}

	virtual void SetUIRenderer(IATUIRenderer *uir) = 0;

	virtual bool IsEnabled() const = 0;
	virtual void SetEnabled(bool enabled) = 0;

	virtual bool IsReadOnly() const = 0;
	virtual void SetReadOnly(bool enabled) = 0;

	virtual bool IsBurstIOEnabled() const = 0;
	virtual void SetBurstIOEnabled(bool enabled) = 0;

	virtual const wchar_t *GetBasePath() const = 0;
	virtual void SetBasePath(const wchar_t *s) = 0;

	virtual void WarmReset() = 0;
	virtual void ColdReset() = 0;
	virtual void OnCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset) = 0;
};

IATHardDiskEmulator *ATCreateHardDiskEmulator();

#endif
