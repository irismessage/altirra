#ifndef f_AT_IDE_H
#define f_AT_IDE_H

#include <vd2/system/vdstl.h>
#include <vd2/system/file.h>
#include <vd2/system/VDString.h>

class ATScheduler;
class IATUIRenderer;

class ATIDEEmulator {
	ATIDEEmulator(const ATIDEEmulator&);
	ATIDEEmulator& operator=(const ATIDEEmulator&);
public:
	ATIDEEmulator();
	~ATIDEEmulator();

	void Init(ATScheduler *mpScheduler, IATUIRenderer *uirenderer);
	void OpenImage(bool write, bool fast, uint32 cylinders, uint32 heads, uint32 sectors, const wchar_t *filename);
	void CloseImage();

	const wchar_t *GetImagePath() const;
	bool IsWriteEnabled() const { return mbWriteEnabled; }
	bool IsFastDevice() const { return mbFastDevice; }
	uint32 GetImageSizeMB() const { return mSectorCount >> 11; }
	uint32 GetCylinderCount() const { return mCylinderCount; }
	uint32 GetHeadCount() const { return mHeadCount; }
	uint32 GetSectorsPerTrack() const { return mSectorsPerTrack; }

	uint8 DebugReadByte(uint8 address);
	uint8 ReadByte(uint8 address);
	void WriteByte(uint8 address, uint8 value);

protected:
	struct DecodedCHS;

	void UpdateStatus();
	void StartCommand(uint8 cmd);
	void BeginReadTransfer(uint32 bytes);
	void BeginWriteTransfer(uint32 bytes);
	void CompleteCommand();
	void AbortCommand(uint8 cmd);
	bool ReadLBA(uint32& lba);
	void WriteLBA(uint32 lba);
	DecodedCHS DecodeCHS(uint32 lba);

	union {
		uint8	mRegisters[8];
		struct {
			uint8 mData;
			uint8 mErrors;
			uint8 mSectorCount;
			uint8 mSectorNumber;
			uint8 mCylinderLow;
			uint8 mCylinderHigh;
			uint8 mHead;
			uint8 mStatus;
		} mRFile;
	};

	uint8 mFeatures;

	ATScheduler *mpScheduler;
	IATUIRenderer *mpUIRenderer;

	uint8 mMaxSectorTransferCount;
	uint32 mSectorCount;
	uint32 mSectorsPerTrack;
	uint32 mHeadCount;
	uint32 mCylinderCount;
	uint32 mIODelaySetting;
	uint32 mTransferIndex;
	uint32 mTransferLength;
	uint32 mTransferSectorCount;
	uint32 mTransferLBA;
	uint32 mActiveCommandNextTime;
	uint8 mActiveCommand;
	uint8 mActiveCommandState;
	bool mbTransferAsWrites;
	bool mbTransfer16Bit;
	bool mbWriteEnabled;
	bool mbFastDevice;

	vdfastvector<uint8> mTransferBuffer;

	VDFile mFile;
	VDStringW mPath;
};

#endif
