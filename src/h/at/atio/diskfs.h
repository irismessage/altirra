//	Altirra - Atari 800/800XL/5200 emulator
//	I/O library - filesystem handler defines
//	Copyright (C) 2009-2015 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_ATIO_DISKFS_H
#define f_AT_ATIO_DISKFS_H

#include <vd2/system/date.h>
#include <vd2/system/error.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>

class IATDiskImage;
class IVDRandomAccessStream;

struct ATDiskFSInfo {
	VDStringA mFSType;
	uint32	mFreeBlocks;
	uint32	mBlockSize;
};

struct ATDiskFSEntryInfo {
	VDStringA mFileName;
	uint32	mSectors;
	uint32	mBytes;
	uint32	mKey;
	bool	mbIsDirectory;
	bool	mbDateValid;
	VDExpandedDate	mDate;
};

struct ATDiskFSValidationReport {
	bool mbBitmapIncorrect = false;
	bool mbBitmapIncorrectLostSectorsOnly = false;
	bool mbBrokenFiles = false;
	bool mbOpenWriteFiles = false;
	bool mbMetadataCorruption = false;

	bool IsSerious() const {
		return mbBitmapIncorrect || mbBrokenFiles || mbOpenWriteFiles || mbMetadataCorruption;
	}
};

enum ATDiskFSError {
	kATDiskFSError_InvalidFileName,
	kATDiskFSError_DiskFull,
	kATDiskFSError_DiskFullFragmented,
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
	kATDiskFSError_CRCError,
	kATDiskFSError_NotSupported,
	kATDiskFSError_MediaNotSupported,
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
	virtual void SetAllowExtend(bool allow) = 0;

	virtual bool Validate(ATDiskFSValidationReport& report) = 0;
	virtual void Flush() = 0;

	virtual uintptr FindFirst(uint32 key, ATDiskFSEntryInfo& info) = 0;
	virtual bool FindNext(uintptr searchKey, ATDiskFSEntryInfo& info) = 0;
	virtual void FindEnd(uintptr searchKey) = 0;

	virtual void GetFileInfo(uint32 key, ATDiskFSEntryInfo& info) = 0;
	virtual uint32 GetParentDirectory(uint32 dirKey) = 0;

	virtual uint32 LookupFile(uint32 parentKey, const char *filename) = 0;

	virtual void DeleteFile(uint32 key) = 0;
	virtual void ReadFile(uint32 key, vdfastvector<uint8>& dst) = 0;
	virtual uint32 WriteFile(uint32 parentKey, const char *filename, const void *src, uint32 len) = 0;
	virtual void RenameFile(uint32 key, const char *newFileName) = 0;
	virtual void SetFileTimestamp(uint32 key, const VDExpandedDate& date) = 0;

	virtual uint32 CreateDir(uint32 parentKey, const char *filename) = 0;
};

IATDiskFS *ATDiskFormatImageDOS1(IATDiskImage *image);
IATDiskFS *ATDiskFormatImageDOS2(IATDiskImage *image);
IATDiskFS *ATDiskFormatImageDOS3(IATDiskImage *image);
IATDiskFS *ATDiskFormatImageMyDOS(IATDiskImage *image);
IATDiskFS *ATDiskFormatImageSDX2(IATDiskImage *image, const char *volNameHint = 0);

IATDiskFS *ATDiskMountImage(IATDiskImage *image, bool readOnly);
IATDiskFS *ATDiskMountImageARC(const wchar_t *path);
IATDiskFS *ATDiskMountImageARC(IVDRandomAccessStream& stream, const wchar_t *path);
IATDiskFS *ATDiskMountImageSDX2(IATDiskImage *image, bool readOnly);

#endif	// f_AT_DISKFS_H
