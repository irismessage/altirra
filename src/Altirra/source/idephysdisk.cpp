#include "stdafx.h"
#include <windows.h>
#include <winioctl.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include "idephysdisk.h"

bool ATIDEIsPhysicalDiskPath(const wchar_t *path) {
	return wcsncmp(path, L"\\\\?\\", 4) == 0;
}

ATIDEPhysicalDisk::ATIDEPhysicalDisk()
	: mhDisk(INVALID_HANDLE_VALUE)
	, mpBuffer(NULL)
{
}

ATIDEPhysicalDisk::~ATIDEPhysicalDisk() {
	Shutdown();
}

void ATIDEPhysicalDisk::Init(const wchar_t *path) {
	Shutdown();

	mhDisk = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);

	if (mhDisk == INVALID_HANDLE_VALUE)
		throw MyWin32Error("Cannot open physical disk: %%s", GetLastError());

	mpBuffer = VDFile::AllocUnbuffer(512 * 32);
	if (!mpBuffer) {
		Shutdown();
		throw MyMemoryError();
	}

	DISK_GEOMETRY info = {0};
	DWORD actual;
	if (!DeviceIoControl(mhDisk, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &info, sizeof info, &actual, NULL))
		throw MyWin32Error("Cannot get size of physical disk: %%s", GetLastError());

	mSectorCount = (uint32)info.Cylinders.QuadPart * info.TracksPerCylinder * info.SectorsPerTrack;
}

void ATIDEPhysicalDisk::Shutdown() {
	if (mpBuffer) {
		VDFile::FreeUnbuffer(mpBuffer);
		mpBuffer = NULL;
	}

	if (mhDisk != INVALID_HANDLE_VALUE) {
		CloseHandle(mhDisk);
		mhDisk = INVALID_HANDLE_VALUE;
	}
}

void ATIDEPhysicalDisk::RequestUpdate() {
	if (mhDisk != INVALID_HANDLE_VALUE)
		FlushFileBuffers(mhDisk);
}

void ATIDEPhysicalDisk::ReadSectors(void *data, uint32 lba, uint32 n) {
	const uint64 offset = (uint64)lba << 9;
	LONG offsetLo = (LONG)offset;
	LONG offsetHi = (LONG)(offset >> 32);

	if (INVALID_SET_FILE_POINTER == SetFilePointer(mhDisk, offsetLo, &offsetHi, FILE_BEGIN)) {
		DWORD err = GetLastError();

		if (err != NO_ERROR)
			throw MyWin32Error("Error reading from physical disk: %%s.", err);
	}

	uint32 bytes = n * 512;
	while(bytes) {
		uint32 toread = bytes > 512*32 ? 512*32 : bytes;

		DWORD actual;
		if (!ReadFile(mhDisk, mpBuffer, toread, &actual, NULL))
			throw MyWin32Error("Error reading from physical disk: %%s.", GetLastError());

		bytes -= toread;
		memcpy(data, mpBuffer, toread);
		data = (char *)data + toread;
	}
}

void ATIDEPhysicalDisk::WriteSectors(void *data, uint32 lba, uint32 n) {
}
