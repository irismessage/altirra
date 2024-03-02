//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef f_AT_CUSTOMDEVICE_H
#define f_AT_CUSTOMDEVICE_H

#include <vd2/system/filewatcher.h>
#include <vd2/system/vdstl_vectorview.h>
#include <at/atcore/devicecart.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceindicators.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcore/devicepbi.h>
#include "customdevicevm.h"
#include "customdevicecompiler.h"

class ATMemoryLayer;
class ATMemoryManager;
class IATDeviceCustomNetworkEngine;
class ATCustomDeviceCompiler;
class ATPortController;
class IATAsyncDispatcher;

class ATDeviceCustom final
	: public ATDevice
	, public IATDeviceMemMap
	, public IATDeviceCartridge
	, public IATDeviceIndicators
	, public IATDeviceScheduling
	, public IATSchedulerCallback
	, public ATDeviceSIO
	, public IATDeviceRawSIO
	, public IATDevicePBIConnection
	, public IATPBIDevice
	, public IVDFileWatcherCallback
{
public:
	ATDeviceCustom();

	void *AsInterface(uint32 iid) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;

	void Init() override;
	void Shutdown() override;

	void ColdReset() override;
	void WarmReset() override;

	bool GetErrorStatus(uint32 idx, VDStringW& error) override;

public:		// IATDeviceMemMap
	void InitMemMap(ATMemoryManager *memmap);
	bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const;

public:		// IATDeviceCartridge
	void InitCartridge(IATDeviceCartridgePort *cartPort) override;
	bool IsLeftCartActive() const override;
	void SetCartEnables(bool leftEnable, bool rightEnable, bool cctlEnable) override;
	void UpdateCartSense(bool leftActive) override;

public:
	void InitIndicators(IATDeviceIndicatorManager *r) override;

public:
	void InitScheduling(ATScheduler *sch, ATScheduler *slowsch) override;

public:
	void OnScheduledEvent(uint32 id) override;

public:
	void InitSIO(IATDeviceSIOManager *mgr) override;
	CmdResponse OnSerialAccelCommand(const ATDeviceSIORequest& request);
	CmdResponse OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) override;
	void OnSerialAbortCommand() override;
	void OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) override;
	void OnSerialFence(uint32 id) override;

public:
	void OnCommandStateChanged(bool asserted) override;
	void OnMotorStateChanged(bool asserted) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnSendReady() override;

public:
	void InitPBI(IATDevicePBIManager *pbiman) override;

public:
	void GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const override;
	void SelectPBIDevice(bool enable) override;
	bool IsPBIOverlayActive() const override;
	uint8 ReadPBIStatus(uint8 busData, bool debugOnly) override;

public:
	bool OnFileUpdated(const wchar_t *path) override;

private:
	static constexpr uint32 kMaxRawConfigSize = 256*1024*1024;
	static constexpr uint32 kMaxTotalSegmentData = 256*1024*1024;

	enum : uint32 {
		kEventId_Sleep = 1,
		kEventId_RawSend
	};

	struct Segment final : public ATCDVMObject {
		static const ATCDVMObjectClass kVMObjectClass;

		uint8 *mpData = nullptr;
		uint8 *mpInitData = nullptr;
		uint32 mSize = 0;
		uint32 mInitSize = 0;
		bool mbNonVolatile = false;
		bool mbMappable = false;
		bool mbReadOnly = false;
		bool mbSpecial = false;

		void VMCallClear(sint32 value);
		void VMCallFill(sint32 offset, sint32 value, sint32 size);
		void VMCallCopy(sint32 destOffset, Segment *srcSegment, sint32 srcOffset, sint32 size);
		void VMCallWriteByte(sint32 offset, sint32 value);
		sint32 VMCallReadByte(sint32 offset);
	};

	enum class NetworkCommand : uint8 {
		None,
		DebugReadByte,
		ReadByte,
		WriteByte,
		ColdReset,
		WarmReset,
		Error,
		ScriptEventSend,
		ScriptEventPost,
	};

	enum class NetworkReply : uint8 {
		None,
		ReturnValue,
		EnableMemoryLayer,
		SetMemoryLayerOffset,
		SetMemoryLayerSegmentOffset,
		SetMemoryLayerReadOnly,
		ReadSegmentMemory,
		WriteSegmentMemory,
		CopySegmentMemory,
		ScriptInterrupt
	};

	enum class AddressAction : uint8 {
		None,
		ConstantData,
		Block,
		Network,
		Script,
		Variable
	};

	enum class SerialFenceId : uint32 {
		AutoReceive = 1,
		ScriptReceive,
		ScriptDelay
	};

	enum class SpecialVarIndex : uint8 {
		Address,
		Value,
	};

	enum class ThreadVarIndex : uint8 {
		Timestamp,
		Device,
		Command,
		Aux1,
		Aux2,
		Aux
	};

	struct AddressBinding {
		AddressAction mAction = AddressAction::None;

		union {
			uint8 mByteData;
			uint16 mScriptFunctions[2];
			uint32 mVariableIndex;
		};
	};

	struct MemoryLayer final : public ATCDVMObject {
		static const ATCDVMObjectClass kVMObjectClass;

		ATDeviceCustom *mpParent = nullptr;
		uint32 mId = 0;
		ATMemoryLayer *mpPhysLayer = nullptr;

		uint32 mAddressBase = 0;
		uint32 mSize = 0;
		AddressBinding *mpReadBindings = nullptr;
		AddressBinding *mpWriteBindings = nullptr;
		Segment *mpSegment = nullptr;
		uint32 mMaxOffset = 0;

		bool mbAutoRD4 = false;
		bool mbAutoRD5 = false;
		bool mbAutoCCTL = false;
		bool mbAutoPBI = false;
		bool mbRD5Active = false;

		uint8 mEnabledModes = 0;

		void VMCallSetOffset(sint32 offset);
		void VMCallSetSegmentAndOffset(Segment *seg, sint32 offset);
		void VMCallSetModes(sint32 read, sint32 write);
		void VMCallSetReadOnly(sint32 ro);
	};

	struct Network final : public ATCDVMObject {
		static const ATCDVMObjectClass kVMObjectClass;

		ATDeviceCustom *mpParent = nullptr;

		sint32 VMCallSendMessage(sint32 param1, sint32 param2);
		sint32 VMCallPostMessage(sint32 param1, sint32 param2);
	};

	struct SIO final : public ATCDVMObject {
		static const ATCDVMObjectClass kVMObjectClass;

		ATDeviceCustom *mpParent = nullptr;
		bool mbValid = false;
		bool mbDataFrameReceived = false;
		uint32 mSendChecksum = 0;
		uint32 mRecvChecksum = 0;
		uint8 mRecvLast = 0;

		void Reset();
		void VMCallAck();
		void VMCallNak();
		void VMCallError();
		void VMCallComplete();
		void VMCallSendFrame(Segment *seg, sint32 offset, sint32 length);
		void VMCallRecvFrame(sint32 length);
		void VMCallDelay(sint32 cycles);
		void VMCallEnableRaw(sint32 enable);
		void VMCallSetProceed(sint32 asserted);
		void VMCallSetInterrupt(sint32 asserted);
		sint32 VMCallCommandAsserted();
		sint32 VMCallMotorAsserted();
		void VMCallSendRawByte(sint32 c, sint32 cyclesPerBit, ATCDVMDomain& domain);
		sint32 VMCallRecvRawByte(ATCDVMDomain& domain);
		sint32 VMCallWaitCommand(ATCDVMDomain& domain);
		sint32 VMCallWaitCommandOff(ATCDVMDomain& domain);
		sint32 VMCallWaitMotorChanged(ATCDVMDomain& domain);
		void VMCallResetRecvChecksum();
		void VMCallResetSendChecksum();
		sint32 VMCallGetRecvChecksum();
		sint32 VMCallCheckRecvChecksum();
		sint32 VMCallGetSendChecksum();
	};

	struct SIOCommand {
		Segment *mpAutoTransferSegment = nullptr;
		uint32 mAutoTransferOffset = 0;
		uint32 mAutoTransferLength = 0;
		bool mbAutoTransferWrite = false;
		bool mbAllowAccel = false;
		const ATCDVMFunction *mpScript = nullptr;
	};

	struct SIODevice final : public ATCDVMObject {
		static const ATCDVMObjectClass kVMObjectClass;

		bool mbAllowAccel = false;
		SIOCommand *mpCommands[256] {};
	};

	struct SIODeviceTable {
		SIODevice *mpDevices[256] {};
	};

	struct PBIDevice final : public ATCDVMObject {
		static const ATCDVMObjectClass kVMObjectClass;
	};

	template<bool T_DebugOnly>
	sint32 ReadControl(MemoryLayer& ml, uint32 addr);
	bool WriteControl(MemoryLayer& ml, uint32 addr, uint8 value);

	bool PostNetCommand(uint32 address, sint32 value, NetworkCommand cmd);
	sint32 SendNetCommand(uint32 address, sint32 value, NetworkCommand cmd);
	sint32 ExecuteNetRequests(bool waitingForReply);
	void PostNetError(const char *msg);
	void OnNetRecvOOB();

	void ResetCustomDevice();
	void ShutdownCustomDevice();
	void ReinitSegmentData(bool clearNonVolatile);

	void SendNextRawByte();
	void AbortRawSend(const ATCDVMThread& thread);

	void AbortThreadSleep(const ATCDVMThread& thread);
	void UpdateThreadSleep();
	void RunReadyThreads();

	void ReloadConfig();

	class MemberParser;

	void ProcessDesc(const void *buf, size_t len);
	bool OnSetOption(ATCustomDeviceCompiler&, const char *, const ATCDVMDataValue&);
	bool OnDefineSegment(ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers);
	bool OnDefineMemoryLayer(ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers);
	bool OnDefineSIODevice(ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers);
	bool OnDefineControllerPort(ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers);
	bool OnDefinePBIDevice(ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers);

	const ATCDVMFunction *ParseScriptOpt(const ATCDVMTypeInfo& returnType, const ATCDVMDataValue *value);
	const ATCDVMFunction *ParseScript(const ATCDVMTypeInfo& returnType, const ATCDVMDataValue& value, ATCDConditionalMask conditionalMask);
	vdvector_view<uint8> ParseBlob(const ATCDVMDataValue& value);
	uint8 ParseRequiredUint8(const ATCDVMDataValue& value);
	uint32 ParseRequiredUint32(const ATCDVMDataValue& value);
	static const char *ParseRequiredString(const ATCDVMDataValue& valueRef);
	static bool ParseBool(const ATCDVMDataValue& value);
	void LoadDependency(const ATCDVMDataValue& value, ATVFSFileView **view);

	void ClearFileTracking();
	void OpenViewWithTracking(const wchar_t *path, ATVFSFileView **view);
	bool CheckForTrackedChanges();
	void UpdateLayerModes(MemoryLayer& ml);

	ATMemoryManager *mpMemMan = nullptr;
	ATScheduler *mpScheduler = nullptr;
	IATDeviceCartridgePort *mpCartPort = nullptr;
	IATDeviceIndicatorManager *mpIndicators = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	IATDevicePBIManager *mpPBIMgr = nullptr;
	bool mbInited = false;
	bool mbInitedSIO = false;
	bool mbInitedRawSIO = false;
	bool mbInitedPBI = false;
	bool mbInitedCart = false;
	bool mbInitedCustomDevice = false;
	bool mbHotReload = false;
	uint64 mLastReloadAttempt = 0;
	uint32 mCartId = 0;

	IATAsyncDispatcher *mpAsyncDispatcher = nullptr;
	uint64 mAsyncNetCallback = 0;

	vdfastvector<MemoryLayer *> mMemoryLayers;
	vdfastvector<Segment *> mSegments;

	uint16 mNetworkPort = 0;

	vdrefptr<IATDeviceCustomNetworkEngine> mpNetworkEngine;
	uint32 mLastConnectionErrorCycle = 0;

	const SIOCommand *mpActiveCommand = nullptr;

	uint8 mPBIDeviceId = 0;
	bool mbPBIDeviceSelected = false;

	ATCustomDeviceCompiler *mpCompiler = nullptr;

	Segment mSIOFrameSegment {};
	SIODeviceTable *mpSIODeviceTable = nullptr;

	vdfastvector<const ATCDVMFunction *> mScriptFunctions;
	ATCDVMThread mVMThread;
	ATCDVMThread mVMThreadSIO;
	ATCDVMThread mVMThreadScriptInterrupt;

	struct Domain final : public ATCDVMDomain {
		ATDeviceCustom *mpParent;
	} mVMDomain;

	const ATCDVMFunction *mpScriptEventInit = nullptr;
	const ATCDVMFunction *mpScriptEventColdReset = nullptr;
	const ATCDVMFunction *mpScriptEventWarmReset = nullptr;
	const ATCDVMFunction *mpScriptEventVBLANK = nullptr;
	const ATCDVMFunction *mpScriptEventSIOCommandChanged = nullptr;
	const ATCDVMFunction *mpScriptEventSIOMotorChanged = nullptr;
	const ATCDVMFunction *mpScriptEventSIOReceivedByte = nullptr;
	const ATCDVMFunction *mpScriptEventPBISelect = nullptr;
	const ATCDVMFunction *mpScriptEventPBIDeselect = nullptr;
	const ATCDVMFunction *mpScriptEventNetworkInterrupt = nullptr;

	uint32 mEventBindingVBLANK = 0;

	Network mNetwork;
	SIO mSIO;

	struct Clock final : public ATCDVMObject {
	public:
		static const ATCDVMObjectClass kVMObjectClass;

		ATDeviceCustom *mpParent = nullptr;
		uint64 mLocalTimeCaptureTimestamp = 0;
		VDExpandedDate mLocalTime;

		void Reset();

		void VMCallCaptureLocalTime();
		sint32 VMCallLocalYear();
		sint32 VMCallLocalMonth();
		sint32 VMCallLocalDay();
		sint32 VMCallLocalDayOfWeek();
		sint32 VMCallLocalHour();
		sint32 VMCallLocalMinute();
		sint32 VMCallLocalSecond();
	};

	Clock mClock;

	struct ControllerPort final : public ATCDVMObject {
	public:
		static const ATCDVMObjectClass kVMObjectClass;

		ATPortController *mpPortController = nullptr;
		bool mbPort2 = false;
		bool mbEnabled = false;
		bool mbPaddleASet = false;
		bool mbPaddleBSet = false;
		uint32 mInputMask = ~UINT32_C(0);
		int mPortInput = -1;
		sint32 mPotA = 228;
		sint32 mPotB = 228;

		void Init();
		void Shutdown();

		void Enable();
		void Disable();

		void Reapply();
		void ResetPotPositions();

		void VMCallSetPaddleA(sint32 pos);
		void VMCallSetPaddleB(sint32 pos);
		void VMCallSetTrigger(sint32 asserted);
		void VMCallSetDirs(sint32 directionMask);
	};

	ControllerPort *mpControllerPorts[4] {};

	struct Debug final : public ATCDVMObject {
	public:
		static const ATCDVMObjectClass kVMObjectClass;

		static void VMCallLog(const char *str);
		static void VMCallLogInt(const char *str, sint32 v);
	};

	Debug mDebug;

	struct ScriptThread final : public ATCDVMObject {
	public:
		static const ATCDVMObjectClass kVMObjectClass;

		ATDeviceCustom *mpParent = nullptr;
		ATCDVMThread mVMThread;

		sint32 VMCallIsRunning();
		void VMCallRun(const ATCDVMFunction *function);
		void VMCallInterrupt();
		static void VMCallSleep(sint32 cycles, ATCDVMDomain& domain);
	};

	vdfastvector<ScriptThread *> mScriptThreads;

	ATCDVMThreadWaitQueue mVMThreadRunQueue;

	struct SleepInfo {
		uint32 mThreadIndex;
		uint64 mWakeTime;
	};

	struct SleepInfoPred {
		bool operator()(const SleepInfo& x, const SleepInfo& y) {
			if (x.mWakeTime != y.mWakeTime)
				return x.mWakeTime > y.mWakeTime;

			return x.mThreadIndex > y.mThreadIndex;
		}
	};

	vdfastvector<SleepInfo> mSleepHeap;
	ATEvent *mpEventThreadSleep = nullptr;

	struct RawSendInfo {
		sint32 mThreadIndex;
		uint32 mCyclesPerBit;
		uint8 mByte;
	};

	ATCDVMThreadWaitQueue mVMThreadRawRecvQueue;
	ATCDVMThreadWaitQueue mVMThreadSIOCommandAssertQueue;
	ATCDVMThreadWaitQueue mVMThreadSIOCommandOffQueue;
	ATCDVMThreadWaitQueue mVMThreadSIOMotorChangedQueue;
	vdfastdeque<RawSendInfo> mRawSendQueue;

	ATEvent *mpEventRawSend = nullptr;

	vdfunction<void(ATCDVMThread&)> mpSleepAbortFn;
	vdfunction<void(ATCDVMThread&)> mpRawSendAbortFn;

	VDStringW mDeviceName;
	VDStringW mConfigPath;
	VDStringW mResourceBasePath;
	VDStringW mLastError;
	VDLinearAllocator mConfigAllocator;

	struct FileTrackingInfo {
		uint64 mSize;
		uint64 mTimestamp;
	};

	vdhashmap<VDStringW, FileTrackingInfo, vdhash<VDStringW>, vdstringpred> mTrackedFiles;
	VDFileWatcher mTrackedFileWatcher;
};

#endif
