#ifndef f_AT_SCSI_H
#define f_AT_SCSI_H

#include <vd2/system/unknown.h>

class ATSCSIBusEmulator;

enum ATSCSICtrlState {
	kATSCSICtrlState_RST = 0x0100,
	kATSCSICtrlState_BSY = 0x0200,
	kATSCSICtrlState_SEL = 0x0400,
	kATSCSICtrlState_IO = 0x0800,
	kATSCSICtrlState_CD = 0x1000,
	kATSCSICtrlState_MSG = 0x2000,
	kATSCSICtrlState_ACK = 0x4000,
	kATSCSICtrlState_REQ = 0x8000,
	kATSCSICtrlState_All = 0xFF00
};

class IATSCSIBusMonitor {
public:
	virtual void OnSCSIControlStateChanged(uint32 state) = 0;
};

class IATSCSIDevice : public IVDRefUnknown {
public:
	enum { kTypeID = 'scdv' };

	virtual void Attach(ATSCSIBusEmulator *bus) = 0;
	virtual void Detach() = 0;
	virtual void BeginCommand(const uint8 *command, uint32 length) = 0;
	virtual void AdvanceCommand() = 0;
	virtual void AbortCommand() = 0;

	virtual void SetBlockSize(uint32 blockSize) = 0;
};

class ATSCSIBusEmulator {
	ATSCSIBusEmulator(const ATSCSIBusEmulator&);
	ATSCSIBusEmulator& operator=(const ATSCSIBusEmulator&);
public:
	ATSCSIBusEmulator();
	~ATSCSIBusEmulator();

	void Shutdown();

	void SetBusMonitor(IATSCSIBusMonitor *monitor) {
		mpBusMonitor = monitor;
	}

	uint32 GetBusState() const { return mBusState; }
	void SetControl(uint32 idx, uint32 state, uint32 mask = kATSCSICtrlState_All);

	void AttachDevice(uint32 id, IATSCSIDevice *dev);
	void DetachDevice(IATSCSIDevice *dev);
	void SwapDevices(uint32 id1, uint32 id2);

	void CommandAbort();

	/// Release BSY to end the information transfer phases.
	void CommandEnd();
	void CommandDelay(uint32 cycles);

	enum SendMode {
		kSendMode_DataIn,
		kSendMode_Status,
		kSendMode_MessageIn
	};

	void CommandSendData(SendMode mode, const void *data, uint32 length);

	enum ReceiveMode {
		kReceiveMode_DataOut,
		kReceiveMode_MessageOut
	};

	void CommandReceiveData(ReceiveMode mode, void *buf, uint32 length);

protected:
	enum BusPhase {
		kBusPhase_BusFree,
		kBusPhase_Selection,
		kBusPhase_Command,
		kBusPhase_DataIn,
		kBusPhase_DataOut,
		kBusPhase_Status,
		kBusPhase_MessageIn,
		kBusPhase_MessageOut
	};

	void UpdateBusState();
	void SetBusPhase(BusPhase phase);

	IATSCSIBusMonitor *mpBusMonitor;

	uint32 mEndpointState[2];
	uint32 mBusState;
	BusPhase mBusPhase;

	bool mbCommandActive;
	IATSCSIDevice *mpTargetDevice;

	const uint8 *mpTransferBuffer;
	bool mbTransferInActive;
	bool mbTransferOutActive;
	uint32 mTransferIndex;
	uint32 mTransferLength;

	uint8 mCommandBuffer[16];

	IATSCSIDevice *mpDevices[8];
};

#endif
