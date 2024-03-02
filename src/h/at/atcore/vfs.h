//	Altirra - Atari 800/800XL/5200 emulator
//	Core library - virtualized file system support
//	Copyright (C) 2009-2016 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

//=========================================================================
// Virtualized file system support
//
// VFS support allows subsystems to access internal resources within
// bundles using filesystem-like paths. In particular, this allows
// referencing files inside of .zip files. Allowable syntaxes:
//
//	c:\foo.bin
//	file://c:/foo.bin
//	zip://c:/foo/bar.zip!foobar
//	gz://c:/foo/bar.gz
//	atfs://c:/foo/file.atr!foo.dcm

#ifndef f_AT_ATCORE_VFS_H
#define f_AT_ATCORE_VFS_H

#include <vd2/system/error.h>
#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/VDString.h>

class IVDRandomAccessStream;

class ATInvalidVFSPathException : public MyError {
public:
	ATInvalidVFSPathException(const wchar_t *badPath);
};

class ATUnsupportedVFSPathException : public MyError {
public:
	ATUnsupportedVFSPathException(const wchar_t *badPath);
};

class ATVFSException : public MyError {
public:
	template<typename... Args>
	ATVFSException(bool hasPath, Args&&... args)
		: mbHasPath(hasPath), MyError(std::forward<Args>(args)...)
	{
	}

	bool HasPath() const { return mbHasPath; }

	[[noreturn]]
	virtual void RethrowWithPath(const wchar_t *path) const = 0;

protected:
	bool mbHasPath = false;
};

template<typename T>
class ATVFSExceptionT : public ATVFSException {
public:
	using ATVFSException::ATVFSException;

	[[noreturn]]
	void RethrowWithPath(const wchar_t *path) const override {
		throw T(path);
	}
};

class ATVFSNotFoundException : public ATVFSExceptionT<ATVFSNotFoundException> {
public:
	ATVFSNotFoundException();
	ATVFSNotFoundException(const wchar_t *path);
};

class ATVFSReadOnlyException : public ATVFSExceptionT<ATVFSReadOnlyException> {
public:
	ATVFSReadOnlyException();
	ATVFSReadOnlyException(const wchar_t *path);
};

class ATVFSNotAvailableException : public ATVFSExceptionT<ATVFSNotAvailableException> {
public:
	ATVFSNotAvailableException();
	ATVFSNotAvailableException(const wchar_t *path);
};

enum ATVFSProtocol : uint8 {
	kATVFSProtocol_None,
	kATVFSProtocol_File,
	kATVFSProtocol_Zip,
	kATVFSProtocol_GZip,
	kATVFSProtocol_Atfs,
	kATVFSProtocol_Blob,
	kATVFSProtocol_Special
};

bool ATDecodeVFSPath(VDStringW& dst, const VDStringSpanW& src);
void ATEncodeVFSPath(VDStringW& dst, const VDStringSpanW& src, bool filepath);

ATVFSProtocol ATParseVFSPath(const wchar_t *s, VDStringW& basePath, VDStringW& subPath);
VDStringW ATMakeVFSPath(ATVFSProtocol protocol, const wchar_t *basePath, const wchar_t *subPath);
VDStringW ATMakeVFSPath(const wchar_t *protocolAndBasePath, const wchar_t *subPath);

// Given a path, return the path to the parent. Examples:
//
//	C:			-> C:
//	C:\			-> C:\
//	C:\x		-> C:\
//	C:\x\		-> C:\
//	file://x/y	-> file://x/
//	zip://a/b!	-> zip://a/b!
//	zip://a/b!c	-> zip://a/b!
//	zip://a/b!c/d -> zip://a/b!c/

const wchar_t *ATVFSSplitPathFile(const wchar_t *path);

bool ATVFSIsFilePath(const wchar_t *s);

VDStringW ATMakeVFSPathForGZipFile(const wchar_t *path);
VDStringW ATMakeVFSPathForZipFile(const wchar_t *path, const wchar_t *fileName);

class ATVFSFileView : public vdrefcount {
	ATVFSFileView(const ATVFSFileView&) = delete;
	ATVFSFileView& operator=(const ATVFSFileView&) = delete;

public:
	ATVFSFileView() = default;
	virtual ~ATVFSFileView() = default;

	IVDRandomAccessStream& GetStream() { return *mpStream; }
	const wchar_t *GetFileName() const { return mFileName.c_str(); }
	bool IsReadOnly() const { return mbReadOnly; }

protected:
	IVDRandomAccessStream *mpStream;
	VDStringW mFileName;
	bool mbReadOnly;
};

void ATVFSOpenFileView(const wchar_t *vfsPath, bool write, ATVFSFileView **viewOut);
void ATVFSOpenFileView(const wchar_t *vfsPath, bool write, bool update, ATVFSFileView **viewOut);

void ATVFSSetAtfsProtocolHandler(vdfunction<void(ATVFSFileView *, const wchar_t *, ATVFSFileView **)> handler);
void ATVFSSetBlobProtocolHandler(vdfunction<void(const wchar_t *, bool, bool, ATVFSFileView **)> handler);
void ATVFSSetSpecialProtocolHandler(vdfunction<void(const wchar_t *, bool, bool, ATVFSFileView **)> handler);

#endif
