//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
//	Beginning with 1.6.0, the VirtualDub system library is licensed
//	differently than the remainder of VirtualDub.  This particular file is
//	thus licensed as follows (the "zlib" license):
//
//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.

#include <stdafx.h>
#include <windows.h>

#include <vd2/system/error.h>
#include <vd2/system/date.h>
#include <vd2/system/filesys.h>
#include <vd2/system/VDString.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>

namespace {
	bool IsHardDrivePath(const wchar_t *path) {
		const VDStringW rootPath(VDFileGetRootPath(path));

		UINT type = GetDriveTypeW(rootPath.c_str());

		return type == DRIVE_FIXED || type == DRIVE_UNKNOWN || type == DRIVE_REMOVABLE;
	}
};

uint32 VDFileGetAttributesFromNativeW32(uint32 nativeAttrs);

///////////////////////////////////////////////////////////////////////////////
//
//	VDFile
//
///////////////////////////////////////////////////////////////////////////////

using namespace nsVDFile;

VDFile::VDFile(const char *pszFileName, uint32 flags)
	: mhFile(NULL)
{
	open(pszFileName, flags);
}

VDFile::VDFile(const wchar_t *pwszFileName, uint32 flags)
	: mhFile(NULL)
{
	open(pwszFileName, flags);
}

VDFile::VDFile(HANDLE h)
	: mhFile(h)
{
	LONG lo, hi = 0;

	lo = SetFilePointer(h, 0, &hi, FILE_CURRENT);

	mFilePosition = (uint32)lo + ((uint64)(uint32)hi << 32);
}

vdnothrow VDFile::VDFile(VDFile&& other) vdnoexcept
	: mhFile(other.mhFile)
	, mpFilename(std::move(other.mpFilename))
	, mFilePosition(other.mFilePosition)
{
	other.mhFile = nullptr;
}

VDFile::~VDFile() {
	closeNT();
}

vdnothrow VDFile& VDFile::operator=(VDFile&& other) vdnoexcept {
	std::swap(mhFile, other.mhFile);
	std::swap(mpFilename, other.mpFilename);
	std::swap(mFilePosition, other.mFilePosition);

	return *this;
}

void VDFile::open(const char *pszFilename, uint32 flags) {
	uint32 err = open_internal(pszFilename, NULL, flags);

	if (err)
		throw VDWin32Exception(L"Cannot open file \"%s\":\n%%s", err, pszFilename);
}

void VDFile::open(const wchar_t *pwszFilename, uint32 flags) {
	uint32 err = open_internal(NULL, pwszFilename, flags);

	if (err)
		throw VDWin32Exception(L"Cannot open file \"%ls\":\n%%s", err, pwszFilename);
}

bool VDFile::openNT(const wchar_t *pwszFilename, uint32 flags) {
	return open_internal(NULL, pwszFilename, flags) == 0;
}

bool VDFile::tryOpen(const wchar_t *pwszFilename, uint32 flags) {
	uint32 err = open_internal(NULL, pwszFilename, flags);

	if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
		return false;

	if (err)
		throw VDWin32Exception(L"Cannot open file \"%ls\":\n%%s", err, pwszFilename);

	return true;
}

bool VDFile::openAlways(const wchar_t *pwszFilename, uint32 flags) {
	uint32 err = open_internal(NULL, pwszFilename, flags);

	if (err == ERROR_ALREADY_EXISTS)
		return true;

	if (err)
		throw VDWin32Exception(L"Cannot open file \"%ls\":\n%%s", err, pwszFilename);

	return true;
}

uint32 VDFile::open_internal(const char *pszFilename, const wchar_t *pwszFilename, uint32 flags) {
	close();

	mpFilename = _wcsdup(VDFileSplitPath(pszFilename ? VDTextAToW(pszFilename).c_str() : pwszFilename));
	if (!mpFilename)
		return ERROR_OUTOFMEMORY;

	// At least one of the read/write flags must be set.
	VDASSERT(flags & (kRead | kWrite));

	DWORD dwDesiredAccess = 0;

	if (flags & kRead)  dwDesiredAccess  = GENERIC_READ;
	if (flags & kWrite) dwDesiredAccess |= GENERIC_WRITE;

	// Win32 docs are screwed here -- FILE_SHARE_xxx is the inverse of a deny flag.

	DWORD dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
	if (flags & kDenyRead)	dwShareMode = FILE_SHARE_WRITE;
	if (flags & kDenyWrite) dwShareMode &= ~FILE_SHARE_WRITE;

	// One of the creation flags must be set.
	VDASSERT(flags & kCreationMask);

	DWORD dwCreationDisposition;

	uint32 creationType = flags & kCreationMask;

	switch(creationType) {
	case kOpenExisting:		dwCreationDisposition = OPEN_EXISTING; break;
	case kOpenAlways:		dwCreationDisposition = OPEN_ALWAYS; break;
	case kCreateAlways:		dwCreationDisposition = CREATE_ALWAYS; break;
	case kCreateNew:		dwCreationDisposition = CREATE_NEW; break;
	case kTruncateExisting:	dwCreationDisposition = TRUNCATE_EXISTING; break;
	default:
		VDNEVERHERE;
		return ERROR_INVALID_PARAMETER;
	}

	VDASSERT((flags & (kSequential | kRandomAccess)) != (kSequential | kRandomAccess));

	DWORD dwAttributes = FILE_ATTRIBUTE_NORMAL;

	if (flags & kSequential)	dwAttributes |= FILE_FLAG_SEQUENTIAL_SCAN;
	if (flags & kRandomAccess)	dwAttributes |= FILE_FLAG_RANDOM_ACCESS;
	if (flags & kWriteThrough)	dwAttributes |= FILE_FLAG_WRITE_THROUGH;
	if (flags & kUnbuffered)	dwAttributes |= FILE_FLAG_NO_BUFFERING;

	VDStringW tempFilenameW;

	if (pszFilename) {
		tempFilenameW = VDTextAToW(pszFilename);
		pwszFilename = tempFilenameW.c_str();
		pszFilename = NULL;
	}

	if ((flags & FILE_FLAG_NO_BUFFERING) && !IsHardDrivePath(pwszFilename))
		flags &= ~FILE_FLAG_NO_BUFFERING;

	mhFile = CreateFileW(pwszFilename, dwDesiredAccess, dwShareMode, NULL, dwCreationDisposition, dwAttributes, NULL);

	DWORD err = GetLastError();

	// If we failed and FILE_FLAG_NO_BUFFERING was set, strip it and try again.
	// VPC and Novell shares sometimes do this....
	if (mhFile == INVALID_HANDLE_VALUE && err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) {
		if (dwAttributes & FILE_FLAG_NO_BUFFERING) {
			dwAttributes &= ~FILE_FLAG_NO_BUFFERING;
			dwAttributes |= FILE_FLAG_WRITE_THROUGH;

			mhFile = CreateFileW(pwszFilename, dwDesiredAccess, dwShareMode, NULL, dwCreationDisposition, dwAttributes, NULL);

			err = GetLastError();
		}
	}

	// INVALID_HANDLE_VALUE isn't NULL.  *sigh*

	if (mhFile == INVALID_HANDLE_VALUE) {
		mhFile = NULL;

		return err;
	}

	mFilePosition = 0;
	return 0;
}

bool VDFile::closeNT() {
	if (mhFile) {
		HANDLE h = mhFile;
		mhFile = NULL;
		if (!CloseHandle(h))
			return false;
	}

	return true;
}

void VDFile::close() {
	if (!closeNT())
		throw VDWin32Exception(L"Cannot complete file \"%ls\": %%s", GetLastError(), mpFilename.get());
}

bool VDFile::truncateNT() {
	return 0 != SetEndOfFile(mhFile);
}

void VDFile::truncate() {
	if (!truncateNT())
		throw VDWin32Exception(L"Cannot truncate file \"%ls\": %%s", GetLastError(), mpFilename.get());
}

bool VDFile::extendValidNT(sint64 pos) {
	// The SetFileValidData() API is only available on XP and Server 2003.

	typedef BOOL (APIENTRY *tpSetFileValidData)(HANDLE hFile, LONGLONG ValidDataLength);		// Windows XP, Server 2003
	static tpSetFileValidData pSetFileValidData = (tpSetFileValidData)GetProcAddress(GetModuleHandleW(L"kernel32"), "SetFileValidData");

	if (!pSetFileValidData) {
		SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
		return false;
	}

	return 0 != pSetFileValidData(mhFile, pos);
}

void VDFile::extendValid(sint64 pos) {
	if (!extendValidNT(pos))
		throw VDWin32Exception(L"Cannot extend file \"%ls\": %%s", GetLastError(), mpFilename.get());
}

bool VDFile::enableExtendValid() {
	// SetFileValidData() requires the SE_MANAGE_VOLUME_NAME privilege, so we must enable it
	// on the process token. We don't attempt to strip the privilege afterward as that would
	// introduce race conditions.
	bool bSuccessful = false;
	DWORD err = 0;

	SetLastError(0);

	HANDLE h;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &h)) {
		LUID luid;

		if (LookupPrivilegeValue(NULL, SE_MANAGE_VOLUME_NAME, &luid)) {
			TOKEN_PRIVILEGES tp;
			tp.PrivilegeCount = 1;
			tp.Privileges[0].Luid = luid;
			tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

			if (AdjustTokenPrivileges(h, FALSE, &tp, 0, NULL, NULL))
				bSuccessful = true;
			else
				err = GetLastError();
		}

		CloseHandle(h);
	}

	if (!bSuccessful && err)
		SetLastError(err);

	return bSuccessful;
}

long VDFile::readData(void *buffer, long length) {
	DWORD dwActual;

	if (!ReadFile(mhFile, buffer, (DWORD)length, &dwActual, NULL))
		throw VDWin32Exception(L"Cannot read from file \"%ls\": %%s", GetLastError(), mpFilename.get());

	mFilePosition += dwActual;

	return dwActual;
}

void VDFile::read(void *buffer, long length) {
	if (length != readData(buffer, length))
		throw VDWin32Exception(L"Cannot read from file \"%ls\": Premature end of file.", GetLastError(), mpFilename.get());
}

long VDFile::writeData(const void *buffer, long length) {
	DWORD dwActual;

	if (!WriteFile(mhFile, buffer, (DWORD)length, &dwActual, NULL) || dwActual != (DWORD)length)
		goto found_error;

	mFilePosition += dwActual;

	return dwActual;

found_error:
	throw VDWin32Exception(L"Cannot write to file \"%ls\": %%s", GetLastError(), mpFilename.get());
}

void VDFile::write(const void *buffer, long length) {
	if (length != writeData(buffer, length))
		throw VDWin32Exception(L"Cannot write to file \"%ls\": Unable to write all data.", GetLastError(), mpFilename.get());
}

bool VDFile::seekNT(sint64 newPos, eSeekMode mode) {
	DWORD dwMode;

	switch(mode) {
	case kSeekStart:
		dwMode = FILE_BEGIN;
		break;
	case kSeekCur:
		dwMode = FILE_CURRENT;
		break;
	case kSeekEnd:
		dwMode = FILE_END;
		break;
	default:
		VDNEVERHERE;
		return false;
	}

	union {
		sint64 pos;
		LONG l[2];
	} u = { newPos };

	u.l[0] = SetFilePointer(mhFile, u.l[0], &u.l[1], dwMode);

	if (u.l[0] == -1 && GetLastError() != NO_ERROR)
		return false;

	mFilePosition = u.pos;
	return true;
}

void VDFile::seek(sint64 newPos, eSeekMode mode) {
	if (!seekNT(newPos, mode))
		throw VDWin32Exception(L"Cannot seek within file \"%ls\": %%s", GetLastError(), mpFilename.get());
}

bool VDFile::skipNT(sint64 delta) {
	if (!delta)
		return true;

	char buf[1024];

	if (delta <= sizeof buf) {
		return (long)delta == readData(buf, (long)delta);
	} else
		return seekNT(delta, kSeekCur);
}

void VDFile::skip(sint64 delta) {
	if (!delta)
		return;

	char buf[1024];

	if (delta > 0 && delta <= sizeof buf) {
		if ((long)delta != readData(buf, (long)delta))
			throw VDWin32Exception(L"Cannot seek within file \"%ls\": %%s", GetLastError(), mpFilename.get());
	} else
		seek(delta, kSeekCur);
}

sint64 VDFile::size() const {
	union {
		uint64 siz;
		DWORD l[2];
	} u;

	u.l[0] = GetFileSize(mhFile, &u.l[1]);

	DWORD err;

	if (u.l[0] == (DWORD)-1L && (err = GetLastError()) != NO_ERROR)
		throw VDWin32Exception(L"Cannot retrieve size of file \"%ls\": %%s", GetLastError(), mpFilename.get());

	return (sint64)u.siz;
}

sint64 VDFile::tell() const {
	return mFilePosition;
}

bool VDFile::flushNT() {
	return 0 != FlushFileBuffers(mhFile);
}

void VDFile::flush() {
	if (!flushNT())
		throw VDWin32Exception(L"Cannot flush file \"%ls\": %%s", GetLastError(), mpFilename.get());
}

bool VDFile::isOpen() const {
	return mhFile != 0;
}

VDFileHandle VDFile::getRawHandle() const {
	return mhFile;
}

uint32 VDFile::getAttributes() const {
	if (!mhFile)
		return 0;

	if (::GetFileType(mhFile) == FILE_TYPE_PIPE)
		return 0;

	BY_HANDLE_FILE_INFORMATION info = {};
	if (!::GetFileInformationByHandle(mhFile, &info))
		return 0;

	return VDFileGetAttributesFromNativeW32(info.dwFileAttributes);
}

VDDate VDFile::getCreationTime() const {
	FILETIME creationTime {};

	if (!mhFile || !GetFileTime(mhFile, &creationTime, nullptr, nullptr))
		return VDDate {};

	const uint64 ticks = ULARGE_INTEGER { { creationTime.dwLowDateTime, creationTime.dwHighDateTime } }.QuadPart;
	return VDDate { ticks };
}

void VDFile::setCreationTime(const VDDate& date) {
	if (mhFile) {
		FILETIME ft;
		ft.dwLowDateTime = (DWORD)date.mTicks;
		ft.dwHighDateTime = (DWORD)(date.mTicks >> 32);

		SetFileTime(mhFile, &ft, nullptr, nullptr);
	}
}

VDDate VDFile::getLastWriteTime() const {
	FILETIME lastWriteTime {};

	if (!mhFile || !GetFileTime(mhFile, nullptr, nullptr, &lastWriteTime))
		return VDDate {};

	const uint64 ticks = ULARGE_INTEGER { { lastWriteTime.dwLowDateTime, lastWriteTime.dwHighDateTime } }.QuadPart;
	return VDDate { ticks };
}

void VDFile::setLastWriteTime(const VDDate& date) {
	if (mhFile) {
		FILETIME ft;
		ft.dwLowDateTime = (DWORD)date.mTicks;
		ft.dwHighDateTime = (DWORD)(date.mTicks >> 32);

		SetFileTime(mhFile, nullptr, nullptr, &ft);
	}
}

void *VDFile::AllocUnbuffer(size_t nBytes) {
	return VirtualAlloc(NULL, nBytes, MEM_COMMIT, PAGE_READWRITE);
}

void VDFile::FreeUnbuffer(void *p) {
	VirtualFree(p, 0, MEM_RELEASE);
}
