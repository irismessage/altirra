#include "stdafx.h"
#include <windows.h>
#include <vd2/system/error.h>
#include <vd2/system/w32assist.h>

bool ATLoadKernelResource(int id, void *dst, uint32 size) {
	HMODULE hmod = VDGetLocalModuleHandleW32();

	HRSRC hrsrc = FindResourceA(hmod, MAKEINTRESOURCE(id), "KERNEL");
	if (!hrsrc)
		return false;

	DWORD rsize = SizeofResource(hmod, hrsrc);
	if (size != rsize)
		return false;

	HGLOBAL hg = LoadResource(hmod, hrsrc);

	const void *p = LockResource(hg);

	if (!p)
		return false;

	memcpy(dst, p, size);

	return true;
}

void ATFileSetReadOnlyAttribute(const wchar_t *path, bool readOnly) {
	VDStringA s;
	DWORD attrs;

	if (!VDIsWindowsNT()) {
		s = VDTextWToA(path);
		attrs = GetFileAttributesA(s.c_str());
	} else {
		attrs = GetFileAttributesW(path);
	}

	if (attrs == INVALID_FILE_ATTRIBUTES)
		throw MyWin32Error("Unable to change read-only flag on file: %s", GetLastError());


	BOOL success;
	if (!VDIsWindowsNT())
		success = SetFileAttributesA(s.c_str(), attrs);
	else
		success = SetFileAttributesW(path, attrs);

	if (!success)
		throw MyWin32Error("Unable to change read-only flag on file: %s", GetLastError());
}
