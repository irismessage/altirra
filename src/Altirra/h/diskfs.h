#ifndef f_AT_DISKFS_H
#define f_AT_DISKFS_H

#include <vd2/system/date.h>
#include <vd2/system/error.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>

class IATDiskImage;

struct ATDiskFSInfo {
	VDStringA mFSType;
	uint32	mFreeBlocks;
	uint32	mBlockSize;
};

struct ATDiskFSEntryInfo {
	VDStringW mFileName;
	uint32	mSectors;
	uint32	mBytes;
	uintptr	mKey;
	bool	mbIsDirectory;
	bool	mbDateValid;
	VDExpandedDate	mDate;
};

struct ATDiskFSValidationReport {
	bool mbBitmapIncorrect;
	bool mbBrokenFiles;
};

enum ATDiskFSError {
	kATDiskFSError_InvalidFileName,
	kATDiskFSError_DiskFull,
	kATDiskFSError_DirectoryFull,
	kATDiskFSError_CorruptedFileSystem,
	kATDiskFSError_FileExists,
	kATDiskFSError_ReadOnly,
	kATDiskFSError_FileTooLarge,
	kATDiskFSError_ReadError,
	kATDiskFSError_WriteError,
	kATDiskFSError_CannotReadSparseFile,
	kATDiskFSError_DirectoryNotEmpty,
	kATDiskFSError_UnsupportedCompressionMode,
	kATDiskFSError_DecompressionError,
	kATDiskFSError_CRCError
};

class ATDiskFSException : public MyError {
public:
	ATDiskFSException(ATDiskFSError error);

	ATDiskFSError GetErrorCode() const { return mErrorCode; }

protected:
	const ATDiskFSError mErrorCode;
};

class VDINTERFACE IATDiskFS {
public:
	virtual ~IATDiskFS() {}

	virtual void GetInfo(ATDiskFSInfo& info) = 0;

	virtual bool IsReadOnly() = 0;
	virtual void SetReadOnly(bool readOnly) = 0;

	virtual bool Validate(ATDiskFSValidationReport& report) = 0;
	virtual void Flush() = 0;

	virtual uintptr FindFirst(uintptr key, ATDiskFSEntryInfo& info) = 0;
	virtual bool FindNext(uintptr searchKey, ATDiskFSEntryInfo& info) = 0;
	virtual void FindEnd(uintptr searchKey) = 0;

	virtual void GetFileInfo(uintptr key, ATDiskFSEntryInfo& info) = 0;
	virtual uintptr GetParentDirectory(uintptr dirKey) = 0;

	virtual uintptr LookupFile(uintptr parentKey, const char *filename) = 0;

	virtual void DeleteFile(uintptr key) = 0;
	virtual void ReadFile(uintptr key, vdfastvector<uint8>& dst) = 0;
	virtual void WriteFile(uintptr parentKey, const char *filename, const void *src, uint32 len) = 0;
	virtual void RenameFile(uintptr key, const char *newFileName) = 0;
};

IATDiskFS *ATDiskFormatImageDOS2(IATDiskImage *image);

IATDiskFS *ATDiskMountImage(IATDiskImage *image, bool readOnly);
IATDiskFS *ATDiskMountImageARC(const wchar_t *path);
IATDiskFS *ATDiskMountImageSDX2(IATDiskImage *image, bool readOnly);

#endif	// f_AT_DISKFS_H
