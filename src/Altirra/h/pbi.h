#ifndef f_AT_PBI_H
#define f_AT_PBI_H

#include <vd2/system/vdstl.h>

class ATMemoryManager;
class ATMemoryLayer;

struct ATPBIDeviceInfo {
	uint8	mDeviceId;
	const wchar_t *mpName;
};

class IATPBIDevice {
public:
	virtual void AttachDevice(ATMemoryManager *memman) = 0;
	virtual void DetachDevice() = 0;

	virtual void GetDeviceInfo(ATPBIDeviceInfo& devInfo) const = 0;
	virtual void Select(bool enable) = 0;

	virtual bool IsPBIOverlayActive() const = 0;

	virtual void ColdReset() = 0;
	virtual void WarmReset() = 0;
};

class ATPBIManager {
public:
	ATPBIManager();
	~ATPBIManager();

	void Init(ATMemoryManager *memman);
	void Shutdown();

	bool IsROMOverlayActive() const;

	void AddDevice(IATPBIDevice *dev);
	void RemoveDevice(IATPBIDevice *dev);

	void ColdReset();
	void WarmReset();

	void Select(uint8 sel);

protected:
	void RebuildSelList();

	static sint32 OnControlDebugRead(void *thisptr, uint32 addr);
	static sint32 OnControlRead(void *thisptr, uint32 addr);
	static bool OnControlWrite(void *thisptr, uint32 addr, uint8 value);

	ATMemoryManager	*mpMemMan;
	ATMemoryLayer *mpMemLayerPBISel;

	uint8	mSelRegister;
	IATPBIDevice *mpSelDevice;
	IATPBIDevice *mpSelectList[8];

	typedef vdfastvector<IATPBIDevice *> Devices;
	Devices mDevices;
};

#endif
