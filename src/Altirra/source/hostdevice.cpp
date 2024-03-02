//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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

#include "stdafx.h"
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include "hostdevice.h"
#include "hostdeviceutils.h"
#include "kerneldb.h"
#include "oshelper.h"
#include "cio.h"
#include "uirender.h"

using namespace ATCIOSymbols;

uint8 ATTranslateWin32ErrorToSIOError(uint32 err);

///////////////////////////////////////////////////////////////////////////

void ATHostDeviceMergeWildPath(VDStringW& dst, const wchar_t *s, const wchar_t *pat) {
	int charsLeft = 8;

	dst.clear();
	while(wchar_t patchar = *pat++) {
		wchar_t d = *s;

		if (d == L'.')
			d = 0;
		else if (d)
			++s;

		if (patchar == L'?') {
			if (d && charsLeft) {
				--charsLeft;
				dst += d;
			}

			continue;
		}

		if (patchar == L'*') {
			if (d) {
				if (charsLeft) {
					--charsLeft;
					dst += d;
				}

				--pat;
			}

			continue;
		}

		if (patchar == L'.') {
			if (d) {
				--s;
				continue;
			}

			if (*s == L'.')
				++s;

			dst.push_back(L'.');
			charsLeft = 3;
			continue;
		}

		if (charsLeft) {
			--charsLeft;
			dst.push_back(patchar);
		}
	}

	if (!dst.empty() && dst.back() == L'.')
		dst.pop_back();
}

#ifdef _DEBUG
namespace {
	struct ATTest_HostDeviceMergeWildPath {
		ATTest_HostDeviceMergeWildPath() {
			VDStringW resultPath;

			ATHostDeviceMergeWildPath(resultPath, L"foo.txt", L"bar.txt"); VDASSERT(resultPath == L"bar.txt");
			ATHostDeviceMergeWildPath(resultPath, L"foo.txt", L"*.*"); VDASSERT(resultPath == L"foo.txt");
			ATHostDeviceMergeWildPath(resultPath, L"foo.txt", L"*.bin"); VDASSERT(resultPath == L"foo.bin");
			ATHostDeviceMergeWildPath(resultPath, L"foo.txt", L"bar.*"); VDASSERT(resultPath == L"bar.txt");
			ATHostDeviceMergeWildPath(resultPath, L"foo.txt", L"f?x.txt"); VDASSERT(resultPath == L"fox.txt");
			ATHostDeviceMergeWildPath(resultPath, L"foo", L"b*.*"); VDASSERT(resultPath == L"boo");

		}
	} g_ATTest_HostDeviceMergeWildPath;
}
#endif

bool ATHostDeviceParseFilename(const char *s, bool allowDir, bool allowWild, bool allowPath, VDStringW& nativeRelPath) {
	bool inext = false;
	bool wild = false;
	int fnchars = 0;
	int extchars = 0;
	uint32 componentStart = nativeRelPath.size();

	while(uint8 c = *s++) {
		if (c == '>' || c == '\\') {
			if (wild)
				return false;

			if (!allowPath)
				return false;

			if (inext && !extchars)
				return false;

			if (ATHostDeviceIsDevice(nativeRelPath.c_str() + componentStart))
				nativeRelPath.insert(nativeRelPath.begin() + componentStart, L'$');

			inext = false;
			fnchars = 0;
			extchars = 0;
			continue;
		}

		if (c == '.') {
			if (!fnchars) {
				if (s[0] == '.') {
					if (s[1] == 0 || s[1] == '>' || s[1] == '\\') {
						if (!allowPath)
							return false;

						// remove a component
						if (!nativeRelPath.empty() && nativeRelPath.back() == '\\')
							nativeRelPath.pop_back();

						while(!nativeRelPath.empty()) {
							wchar_t c = nativeRelPath.back();

							nativeRelPath.pop_back();

							if (c == '\\')
								break;
						}

						++s;

						if (!*s)
							break;

						++s;
						continue;
					}
				} else if (s[0] == '>' || s[0] == '\\' || s[0] == 0) {
					if (!allowPath)
						return false;

					continue;
				}
			}

			if (inext)
				return false;
		}

		if (!fnchars) {
			if (!nativeRelPath.empty())
				nativeRelPath += '\\';

			componentStart = nativeRelPath.size();
		}

		if (c == '.') {
			nativeRelPath += '.';
			inext = true;
			continue;
		}

		if ((uint8)(c - 'a') < 26)
			c &= ~0x20;

		if (c == '*' || c == '?')
			wild = true;
		else if (!ATHostDeviceIsValidPathChar(c))
			return false;

		if (inext) {
			if (++extchars > 3)
				return false;
		} else {
			if (++fnchars > 8)
				return false;
		}

		nativeRelPath += c;
	}

	if (!allowDir) {
		if (fnchars + extchars == 0)
			return false;
	}

	if (wild && !allowWild)
		return false;

	if (!wild && ATHostDeviceIsDevice(nativeRelPath.c_str() + componentStart))
		nativeRelPath.insert(nativeRelPath.begin() + componentStart, L'$');

	// strip off trailing separator if present
	if (!nativeRelPath.empty() && nativeRelPath.back() == '\\')
		nativeRelPath.pop_back();

	return true;
}

#ifdef _DEBUG
namespace {
	struct ATTest_HostDeviceParseFilename {
		ATTest_HostDeviceParseFilename() {
			VDStringW nativeRelPath;

			nativeRelPath = L""; VDASSERT( ATHostDeviceParseFilename("TEST.TXT", false, false, true, nativeRelPath) && nativeRelPath == L"TEST.TXT");
			nativeRelPath = L""; VDASSERT(!ATHostDeviceParseFilename("*.TXT", false, false, true, nativeRelPath));
			nativeRelPath = L""; VDASSERT( ATHostDeviceParseFilename("*.TXT", false, true, true, nativeRelPath) && nativeRelPath == L"*.TXT");
			nativeRelPath = L""; VDASSERT(!ATHostDeviceParseFilename("*>*.TXT", false, false, true, nativeRelPath));
			nativeRelPath = L""; VDASSERT(!ATHostDeviceParseFilename("*>*.TXT", false, true, true, nativeRelPath));
			nativeRelPath = L""; VDASSERT(!ATHostDeviceParseFilename("*>FOO.TXT", false, true, true, nativeRelPath));
			nativeRelPath = L""; VDASSERT(!ATHostDeviceParseFilename("", false, false, true, nativeRelPath));
			nativeRelPath = L""; VDASSERT( ATHostDeviceParseFilename("", true, false, true, nativeRelPath) && nativeRelPath == L"");
			nativeRelPath = L""; VDASSERT( ATHostDeviceParseFilename("FOO>", true, false, true, nativeRelPath) && nativeRelPath == L"FOO");
			nativeRelPath = L""; VDASSERT( ATHostDeviceParseFilename("FOO>BAR", true, false, true, nativeRelPath) && nativeRelPath == L"FOO\\BAR");
			nativeRelPath = L""; VDASSERT( ATHostDeviceParseFilename("FOO>BAR>", true, false, true, nativeRelPath) && nativeRelPath == L"FOO\\BAR");
			nativeRelPath = L""; VDASSERT( ATHostDeviceParseFilename("FOO>BAR>.", true, false, true, nativeRelPath) && nativeRelPath == L"FOO\\BAR");
			nativeRelPath = L""; VDASSERT( ATHostDeviceParseFilename("FOO>BAR>..", true, false, true, nativeRelPath) && nativeRelPath == L"FOO");
			nativeRelPath = L""; VDASSERT( ATHostDeviceParseFilename("CON", false, false, true, nativeRelPath) && nativeRelPath == L"$CON");
			nativeRelPath = L""; VDASSERT( ATHostDeviceParseFilename("CON.TXT", false, false, true, nativeRelPath) && nativeRelPath == L"$CON.TXT");
			nativeRelPath = L""; VDASSERT( ATHostDeviceParseFilename("CONX.TXT", false, false, true, nativeRelPath) && nativeRelPath == L"CONX.TXT");
			nativeRelPath = L""; VDASSERT( ATHostDeviceParseFilename("TEST.TXT", false, false, true, nativeRelPath) && nativeRelPath == L"TEST.TXT");
			nativeRelPath = L""; VDASSERT( ATHostDeviceParseFilename("TEST.TXT", false, false, true, nativeRelPath) && nativeRelPath == L"TEST.TXT");
			nativeRelPath = L"FOO"; VDASSERT(ATHostDeviceParseFilename("TEST.TXT", false, false, true, nativeRelPath) && nativeRelPath == L"FOO\\TEST.TXT");
			nativeRelPath = L"FOO\\BAR"; VDASSERT(ATHostDeviceParseFilename("TEST.TXT", false, false, true, nativeRelPath) && nativeRelPath == L"FOO\\BAR\\TEST.TXT");
			nativeRelPath = L"FOO\\BAR"; VDASSERT(ATHostDeviceParseFilename("BAZ>TEST.TXT", false, false, true, nativeRelPath) && nativeRelPath == L"FOO\\BAR\\BAZ\\TEST.TXT");
			nativeRelPath = L"FOO\\BAR"; VDASSERT(ATHostDeviceParseFilename("..\\BAZ>TEST.TXT", false, false, true, nativeRelPath) && nativeRelPath == L"FOO\\BAZ\\TEST.TXT");
			nativeRelPath = L"FOO\\BAR\\BLAH"; VDASSERT(ATHostDeviceParseFilename("..\\..\\BAZ>TEST.TXT", false, false, true, nativeRelPath) && nativeRelPath == L"FOO\\BAZ\\TEST.TXT");
			nativeRelPath = L"FOO\\BAR\\BLAH"; VDASSERT(ATHostDeviceParseFilename("..\\..\\..\\..\\BAZ>TEST.TXT", false, false, true, nativeRelPath) && nativeRelPath == L"BAZ\\TEST.TXT");
		}
	} g_ATTest_HostDeviceParseFilename;
}
#endif

///////////////////////////////////////////////////////////////////////////
class ATHostDeviceChannel {
public:
	ATHostDeviceChannel();

	void Close();

	bool GetLength(uint32& len);
	uint8 Seek(uint32 pos);
	uint8 Read(void *dst, uint32 len, uint32& actual);
	uint8 Write(const void *src, uint32 len);

public:
	VDFile	mFile;
	vdfastvector<uint8>	mData;
	uint32	mOffset;
	uint32	mLength;
	bool	mbWriteBackData;
	bool	mbUsingRawData;
	bool	mbTranslateEOL;
	bool	mbOpen;
	bool	mbReadEnabled;
	bool	mbWriteEnabled;
};

ATHostDeviceChannel::ATHostDeviceChannel()
	: mOffset(0)
	, mLength(0)
	, mbWriteBackData(false)
	, mbUsingRawData(false)
	, mbTranslateEOL(false)
	, mbOpen(false)
	, mbReadEnabled(false)
	, mbWriteEnabled(false)
{
}

void ATHostDeviceChannel::Close() {
	if (!mbOpen)
		return;

	if (mbWriteBackData && mFile.isOpen()) {
		vdfastvector<uint8> tmp;
		tmp.reserve(mData.size());

		vdfastvector<uint8>::const_iterator it(mData.begin()), itEnd(mData.end());
		for(; it != itEnd; ++it) {
			uint8 c = *it;

			if (c == 0x9B) {
				tmp.push_back(0x0D);
				c = 0x0A;
			}

			tmp.push_back(c);
		}

		if (mFile.seekNT(0)) {
			mFile.writeData(tmp.data(), (uint32)tmp.size());
			mFile.truncateNT();
			mFile.closeNT();
		}
	}

	mbOpen = false;
	mbWriteBackData = false;
	mbUsingRawData = false;
	mbReadEnabled = false;
	mbWriteEnabled = false;
	mFile.closeNT();

	vdfastvector<uint8> tmp;
	tmp.swap(mData);
}

bool ATHostDeviceChannel::GetLength(uint32& len) {
	try {
		if (mbUsingRawData)
			len = (uint32)mData.size();
		else
			len = (uint32)mFile.size();
	} catch(const MyError&) {
		return false;
	}

	return true;
}

uint8 ATHostDeviceChannel::Seek(uint32 pos) {
	if (!mbWriteEnabled) {
		if (pos > mLength)
			return CIOStatInvPoint;
	}

	mOffset = pos;

	return CIOStatSuccess;
}

uint8 ATHostDeviceChannel::Read(void *dst, uint32 len, uint32& actual) {
	actual = 0;

	uint8 status = CIOStatSuccess;
	try {
		if (mbUsingRawData) {
			uint32 fileSize = (uint32)mData.size();

			if (mOffset < fileSize) {
				uint32 tc = fileSize - mOffset;

				if (tc > len)
					tc = len;

				memcpy(dst, mData.data() + mOffset, tc);
				actual = tc;
			}
		} else {
			mFile.seek(mOffset);
			actual = mFile.readData(dst, len);
		}

		mOffset += actual;

		if (!actual)
			status = CIOStatEndOfFile;
		else if (mOffset >= mLength)
			status = CIOStatSuccessEOF;

	} catch(const MyError&) {
		return CIOStatFatalDiskIO;
	}

	return status;
}

uint8 ATHostDeviceChannel::Write(const void *buf, uint32 tc) {
	if (0xFFFFFF - mOffset < tc)
		return CIOStatDiskFull;

	if (mbUsingRawData) {
		uint32 end = mOffset + tc;

		if (end > mLength)
			mData.resize(end, 0);

		memcpy(mData.data() + mOffset, buf, tc);
	} else {
		try {
			mFile.seek(mOffset);
			mFile.write(buf, tc);
		} catch(const MyError&) {
			return CIOStatFatalDiskIO;
		}
	}

	mOffset += tc;

	if (mLength < mOffset)
		mLength = mOffset;

	return CIOStatSuccess;
}

///////////////////////////////////////////////////////////////////////////
class ATHostDeviceEmulator : public IATHostDeviceEmulator {
	ATHostDeviceEmulator(const ATHostDeviceEmulator&);
	ATHostDeviceEmulator& operator=(const ATHostDeviceEmulator&);
public:
	ATHostDeviceEmulator();
	~ATHostDeviceEmulator();

	void SetUIRenderer(IATUIRenderer *uir);

	bool IsEnabled() const;
	void SetEnabled(bool enabled);

	bool IsReadOnly() const;
	void SetReadOnly(bool enabled);

	bool IsBurstIOEnabled() const;
	void SetBurstIOEnabled(bool enabled);

	bool IsLongNameEncodingEnabled() const { return mbLongNameEncoding; }
	void SetLongNameEncodingEnabled(bool enabled) { mbLongNameEncoding = enabled; }

	const wchar_t *GetBasePath(int index) const;
	void SetBasePath(int index, const wchar_t *s);

	void WarmReset();
	void ColdReset();

	void OnCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset);

protected:
	void DoOpen(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem);
	void DoClose(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem);
	void DoGetByte(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem);
	void DoPutByte(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem);
	void DoGetRecord(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem);
	void DoPutRecord(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem);
	void DoGetStatus(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem);
	void DoSpecial(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem);

	bool ReadFilename(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, bool allowDir, bool allowWild);
	bool GetNextMatch(VDDirectoryIterator& it, bool allowDirs = false, VDStringA *encodedName = 0);

	typedef ATHostDeviceChannel Channel;

	Channel		mChannels[8];
	VDStringW	mNativeBasePath[4];
	VDStringW	mNativeCurDir[4];
	VDStringW	mNativeSearchPath;
	VDStringA	mFilePattern;
	VDStringW	mNativeRelPath;
	VDStringW	mNativeDirPath;
	int			mPathIndex;
	bool		mbPathTranslate;
	bool		mbEnabled;
	bool		mbReadOnly;
	bool		mbBurstIOEnabled;
	bool		mbLongNameEncoding;

	IATUIRenderer	*mpUIRenderer;

	char		mFilename[128];
	uint32		mFilenameEnd;
};

IATHostDeviceEmulator *ATCreateHostDeviceEmulator() {
	return new ATHostDeviceEmulator;
}

ATHostDeviceEmulator::ATHostDeviceEmulator()
	: mPathIndex(0)
	, mbPathTranslate(false)
	, mbEnabled(false)
	, mbReadOnly(false)
	, mbBurstIOEnabled(true)
	, mbLongNameEncoding(false)
	, mpUIRenderer(NULL)
	, mFilenameEnd(0)
{
	ColdReset();
}

ATHostDeviceEmulator::~ATHostDeviceEmulator() {
	ColdReset();
}

void ATHostDeviceEmulator::SetUIRenderer(IATUIRenderer *uir) {
	mpUIRenderer = uir;
}

bool ATHostDeviceEmulator::IsEnabled() const {
	return mbEnabled;
}

void ATHostDeviceEmulator::SetEnabled(bool enable) {
	if (mbEnabled == enable)
		return;

	mbEnabled = enable;

	ColdReset();
}

bool ATHostDeviceEmulator::IsReadOnly() const {
	return mbReadOnly;
}

void ATHostDeviceEmulator::SetReadOnly(bool enabled) {
	mbReadOnly = enabled;
}

bool ATHostDeviceEmulator::IsBurstIOEnabled() const {
	return mbBurstIOEnabled;
}

void ATHostDeviceEmulator::SetBurstIOEnabled(bool enabled) {
	mbBurstIOEnabled = enabled;
}

const wchar_t *ATHostDeviceEmulator::GetBasePath(int index) const {
	return (unsigned)index < 4 ? mNativeBasePath[index].c_str() : L"";
}

void ATHostDeviceEmulator::SetBasePath(int index, const wchar_t *basePath) {
	if ((unsigned)index >= 4)
		return;

	VDStringW& nbpath = mNativeBasePath[index];
	nbpath = basePath;

	if (!nbpath.empty()) {
		if (!VDIsPathSeparator(nbpath.back()))
			nbpath += L'\\';
	}
}

void ATHostDeviceEmulator::WarmReset() {
	ColdReset();
}

void ATHostDeviceEmulator::ColdReset() {
	for(int i=0; i<8; ++i) {
		Channel& ch = mChannels[i];

		ch.Close();
	}
}

void ATHostDeviceEmulator::OnCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset) {
	if (!mbEnabled)
		return;

	switch(offset) {
		case 0:
			DoOpen(cpu, mem);
			break;

		case 2:
			DoClose(cpu, mem);
			break;

		case 4:
			DoGetByte(cpu, mem);
			break;

		case 6:
			DoPutByte(cpu, mem);
			break;

		case 8:
			DoGetStatus(cpu, mem);
			break;

		case 10:
			DoSpecial(cpu, mem);
			break;
	}
}

void ATHostDeviceEmulator::DoOpen(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem) {
	ATKernelDatabase kdb(mem);
	const int idx = (cpu->GetX() >> 4) & 7;

	Channel& ch = mChannels[idx];
	if (ch.mbOpen) {
		cpu->Ldy(CIOStatIOCBInUse);
		return;
	}

	uint8 mode = kdb.ICAX1Z;
	bool append = false;
	bool create = false;
	bool write = false;
	uint32 flags;

	switch(mode) {
		case 0x04:
			flags = nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting;
			break;

		case 0x08:
			flags = nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways;
			create = true;
			write = true;
			break;

		case 0x09:
			flags = nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kOpenAlways;
			create = true;
			append = true;
			write = true;
			break;

		case 0x0C:
			flags = nsVDFile::kReadWrite | nsVDFile::kDenyAll | nsVDFile::kOpenExisting;
			write = true;
			break;

		case 0x06:	// open directory
		case 0x07:	// open directory, showing extended (Atari DOS 2.5)
			break;

		default:
			cpu->Ldy(CIOStatInvalidCmd);
			return;
	}

	if (write && mbReadOnly) {
		cpu->Ldy(CIOStatReadOnly);
		return;
	}

	ch.mbReadEnabled = (mode & 0x04) != 0;
	ch.mbWriteEnabled = (mode & 0x08) != 0;

	if (!ReadFilename(cpu, mem, true, true))
		return;

	if (mpUIRenderer)
		mpUIRenderer->SetHActivity(false);

	if (mode == 0x06 || mode == 0x07) {
		ch.mbOpen = true;
		ch.mbUsingRawData = true;
		ch.mData.clear();
		ch.mOffset = 0;

		const bool useSpartaDOSFormat = false;
		VDStringA line;

		try {
			VDDirectoryIterator it(mNativeSearchPath.c_str());

			// <---------------> 17 bytes
			//   DOS     SYS 037   (Normal)
			// * DOS     SYS 037   (Locked)
			//  <DOS     SYS>037   (DOS 2.5 extended)
			//  :BLAH    X   0008  (MyDOS 4.53 subdirectory)
			//   ZHAND    COM    857 11-01-85 10:51a  (SpartaDOS 3.2g file)
			//   X        BIN  1386k 25-01-06 20:16   (SpartaDOS X large file)
			//   FOO          <DIR>   6-06-94  3:48p  (SpartaDOS 3.2g dir)

			if (useSpartaDOSFormat) {
				const char kHeader1[]="Volume: ";
				const char kHeader2[]="Directory: ";

				ch.mData.push_back(0x9B);
				memcpy(ch.mData.alloc(sizeof(kHeader1)-1), kHeader1, sizeof(kHeader1)-1);
				ch.mData.push_back(0x9B);
				memcpy(ch.mData.alloc(sizeof(kHeader2)-1), kHeader2, sizeof(kHeader2)-1);

				VDStringW::const_iterator it(mNativeCurDir[mPathIndex].begin()), itEnd(mNativeCurDir[mPathIndex].end());

				if (it == itEnd) {
					const uint8 kMain[]={'M', 'A', 'I', 'N'};

					memcpy(ch.mData.alloc(4), kMain, 4);
				} else {
					ch.mData.push_back('>');

					for(; it != itEnd; ++it) {
						wchar_t c = *it;

						if (c == L'$')
							continue;

						if (c == '\\')
							c = L'>';

						ch.mData.push_back((uint8)c);
					}
				}

				ch.mData.push_back(0x9B);
				ch.mData.push_back(0x9B);
			}

			VDStringA translatedName;
			while(GetNextMatch(it, true, &translatedName)) {
				const char *fn = translatedName.c_str();
				const char *ext = VDFileSplitExt(fn);

				int flen = ext - fn;
				if (flen > 8)
					flen = 8;

				if (*ext == '.')
					++ext;

				int elen = strlen(ext);
				if (elen > 3)
					elen = 3;

				if (useSpartaDOSFormat) {
					line.clear();

					for(int i=0; i<flen; ++i)
						line.push_back(toupper((unsigned char)fn[i]));

					for(int i=flen; i<9; ++i)
						line.push_back(' ');

					for(int i=0; i<elen; ++i)
						line.push_back(toupper((unsigned char)ext[i]));

					for(int i=elen; i<4; ++i)
						line.push_back(' ');

					sint64 len = it.GetSize();

					if (len < 1000000)
						line.append_sprintf("%6u", (unsigned)len);
					else if (len < 1000000ull * 1024)
						line.append_sprintf("%5uk", (unsigned)(len >> 10));
					else if (len < 1000000ull * 1024 * 1024)
						line.append_sprintf("%5um", (unsigned)(len >> 20));
					else if (len < 1000000ull * 1024 * 1024 * 1024)
						line.append_sprintf("%5ug", (unsigned)(len >> 30));
					else
						line.append_sprintf("%5ut", (unsigned)(len >> 40));

					VDDate date = it.GetLastWriteDate();
					const VDExpandedDate& xdate = VDGetLocalDate(date);

					line.append_sprintf(" %02u-%02u-%02u %02u:%02u"
						, xdate.mDay
						, xdate.mMonth
						, xdate.mYear % 100
						, xdate.mHour
						, xdate.mSecond
						);

					size_t n = line.size();
					memcpy(ch.mData.alloc(n + 1), line.data(), n);
					ch.mData.back() = 0x9B;
				} else {
					uint8 *s = ch.mData.alloc(18);

					memset(s, ' ', 18);

					if (it.IsDirectory())
						s[1] = ':';
					else if (it.GetAttributes() & kVDFileAttr_ReadOnly)
						s[0] = '*';

					for(int i=0; i<flen; ++i)
						s[i+2] = toupper((unsigned char)fn[i]);

					for(int i=0; i<elen; ++i)
						s[i+10] = toupper((unsigned char)ext[i]);

					sint64 byteSize = it.GetSize();

					if (byteSize > 999 * 125)
						byteSize = 999 * 125;

					int sectors = ((int)byteSize + 124) / 125;

					s[14] = '0' + (sectors / 100);
					s[15] = '0' + ((sectors / 10) % 10);
					s[16] = '0' + (sectors % 10);
					s[17] = 0x9B;
				}
			}
		} catch(const MyError&) {
		}

		if (useSpartaDOSFormat) {
			static const char kSizeHeader[]=" 65521 FREE SECTORS";

			memcpy(ch.mData.alloc(sizeof(kSizeHeader)), kSizeHeader, sizeof(kSizeHeader)-1);
			ch.mData.back() = 0x9B;
		} else {
			uint8 *t = ch.mData.alloc(17);
			t[ 0] = '9';
			t[ 1] = '9';
			t[ 2] = '9';
			t[ 3] = ' ';
			t[ 4] = 'F';
			t[ 5] = 'R';
			t[ 6] = 'E';
			t[ 7] = 'E';
			t[ 8] = ' ';
			t[ 9] = 'S';
			t[10] = 'E';
			t[11] = 'C';
			t[12] = 'T';
			t[13] = 'O';
			t[14] = 'R';
			t[15] = 'S';
			t[16] = 0x9B;
		}

		ch.mLength = (uint32)ch.mData.size();

		cpu->Ldy(1);
	} else {
		// attempt to open file
		ch.mbUsingRawData = false;

		try {
			VDDirectoryIterator it(mNativeSearchPath.c_str());

			if (!GetNextMatch(it)) {
				if (create)
					ch.mFile.open(VDMakePath(mNativeBasePath[mPathIndex].c_str(), mNativeRelPath.c_str()).c_str(), flags);
				else {
					cpu->Ldy(CIOStatFileNotFound);
					return;
				}
			} else {
				ch.mFile.open(it.GetFullPath().c_str(), flags);
			}

			ch.mbTranslateEOL = mbPathTranslate;

			if (mbPathTranslate) {
				ch.mbUsingRawData = true;
				ch.mbWriteBackData = ch.mbWriteEnabled;

				if (ch.mbReadEnabled) {
					sint64 len = ch.mFile.size();

					if (len > 0xFFFFFF)
						throw MyError("file too large");

					uint32 len32 = (uint32)len;
					vdfastvector<uint8> tmp(len32);

					ch.mFile.read(tmp.data(), len32);

					ch.mData.reserve(len32);

					uint8 skipNext = 0;
					for(vdfastvector<uint8>::const_iterator it(tmp.begin()), itEnd(tmp.end());
						it != itEnd;
						++it)
					{
						uint8 c = *it;

						if (skipNext) {
							uint8 d = skipNext;
							skipNext = 0;

							if (c == d)
								continue;
						}

						if (c == '\r' || c == '\n') {
							skipNext = c ^ ('\r' ^ '\n');
							c = 0x9B;
						}

						ch.mData.push_back(c);
					}
				}

				ch.mOffset = 0;
				ch.mLength = (uint32)ch.mData.size();

				if (append)
					ch.mOffset = ch.mLength;
			} else {
				sint64 size64 = ch.mFile.size();

				ch.mLength = size64 > 0xFFFFFF ? 0xFFFFFF : (uint32)size64;

				if (append) {
					ch.mFile.seek(ch.mLength);
					ch.mOffset = ch.mLength;
				} else
					ch.mOffset = 0;
			}
		} catch(const MyWin32Error& e) {
			ch.mFile.closeNT();
			cpu->Ldy(ATTranslateWin32ErrorToSIOError(e.GetWin32Error()));
			return;
		} catch(const MyError&) {
			ch.mFile.closeNT();
			cpu->Ldy(CIOStatFileNotFound);
			return;
		}

		// all good
		ch.mbOpen = true;
		cpu->Ldy(1);
	}
}

void ATHostDeviceEmulator::DoClose(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem) {
	const int idx = (cpu->GetX() >> 4) & 7;
	Channel& ch = mChannels[idx];

	ch.Close();

	cpu->Ldy(1);
}

void ATHostDeviceEmulator::DoGetByte(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem) {
	ATKernelDatabase kdb(mem);

	const int idx = (cpu->GetX() >> 4) & 7;
	Channel& ch = mChannels[idx];

	if (!ch.mbOpen) {
		cpu->Ldy(CIOStatNotOpen);
		return;
	}

	if (!ch.mbReadEnabled) {
		cpu->Ldy(CIOStatWriteOnly);
		return;
	}

	if (mpUIRenderer)
		mpUIRenderer->SetHActivity(false);

	// check if we can do a burst read
	uint8 status = CIOStatSuccess;

	if (mbBurstIOEnabled && kdb.ICCOMZ == 0x07) {
		uint16 len = kdb.ICBLZ;

		// zero bytes is a special case meaning to return one byte in the A register
		if (len) {
			uint16 addr = kdb.ICBAZ;

			uint32 tc = len;

			if (tc > 1024)
				tc = 1024;

			uint8 buf[1024];

			uint32 actual;

			status = ch.Read(buf, tc, actual);

			if (actual) {
				int actualm1 = actual - 1;

				for(int i=0; i<actualm1; ++i)
					mem->WriteByte(addr + i, buf[i]);

				kdb.ICBAZ = (uint16)(addr + actualm1);
				kdb.ICBLZ = (uint16)(len - actualm1);

				cpu->SetA(buf[actualm1]);
			} else
				cpu->SetA(0);

			cpu->Ldy(status);
			return;
		}
	}

	uint8 buf = 0;
	uint32 actual;
	status = ch.Read(&buf, 1, actual);

	cpu->SetA(buf);
	cpu->Ldy(status);
}

void ATHostDeviceEmulator::DoPutByte(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem) {
	const int idx = (cpu->GetX() >> 4) & 7;
	Channel& ch = mChannels[idx];

	if (!ch.mbOpen) {
		cpu->Ldy(CIOStatNotOpen);
		return;
	}

	if (!ch.mbWriteEnabled) {
		cpu->Ldy(CIOStatWriteOnly);
		return;
	}

	if (mpUIRenderer)
		mpUIRenderer->SetHActivity(true);

	// check if we can do a burst write
	ATKernelDatabase kdb(mem);
	uint8 status = CIOStatSuccess;

	if (mbBurstIOEnabled && kdb.ICCOMZ == 0x0B) {
		uint16 len = kdb.ICBLZ;

		// zero bytes is a special case meaning to write one byte from the A register
		if (len) {
			uint16 addr = kdb.ICBAZ;

			int tc = len;

			if (tc > 1024)
				tc = 1024;

			uint8 buf[1024];

			buf[0] = cpu->GetA();

			for(int i=1; i<tc; ++i)
				buf[i] = mem->ReadByte(addr + i);

			status = ch.Write(buf, tc);
			if (status < 0x80) {
				int tcm1 = tc - 1;
				kdb.ICBAZ = (uint16)(addr + tcm1);
				kdb.ICBLZ = (uint16)(len - tcm1);
			}
		}
	} else {
		uint8 buf = cpu->GetA();

		status = ch.Write(&buf, 1);
	}

	cpu->Ldy(status);
}

void ATHostDeviceEmulator::DoGetStatus(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem) {
	ATKernelDatabase kdb(mem);

	cpu->Ldy(CIOStatSuccess);
}

void ATHostDeviceEmulator::DoSpecial(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem) {
	ATKernelDatabase kdb(mem);
	const uint8 command = kdb.ICCOMZ;

	try {
		// The Atari OS manual has incorrect command IDs for the NOTE and
		// POINT commands: it says that NOTE is $25 and POINT is $26, but
		// it's the other way around.

		if (command == 0x26) {			// note
			const int idx = (cpu->GetX() >> 4) & 7;
			Channel& ch = mChannels[idx];

			if (!ch.mbOpen) {
				cpu->Ldy(CIOStatNotOpen);
				return;
			}

			int offset = ch.mbUsingRawData ? ch.mOffset : (int)ch.mFile.tell();
			int sector = offset / 125;

			// Note that we must write directly to the originating IOCB as
			// AUX3-5 are not copied into zero page.

			const uint16 aux3 = ATKernelSymbols::ICAX1 + 2 + (idx << 4);
			mem->WriteByte(aux3 + 0, (uint8)sector);
			mem->WriteByte(aux3 + 1, (uint8)(sector >> 8));
			mem->WriteByte(aux3 + 2, (uint8)(offset % 125));

			cpu->Ldy(CIOStatSuccess);
		} else if (command == 0x25) {	// point
			const int idx = (cpu->GetX() >> 4) & 7;
			Channel& ch = mChannels[idx];

			if (!ch.mbOpen) {
				cpu->Ldy(CIOStatNotOpen);
				return;
			}

			const uint16 aux3 = ATKernelSymbols::ICAX1 + 2 + (idx << 4);
			uint8 rawpos[3];
			for(int i=0; i<3; ++i)
				rawpos[i] = mem->ReadByte(aux3 + i);

			if (rawpos[2] >= 125) {
				cpu->Ldy(CIOStatInvPoint);
				return;
			}

			uint32 pos = 125*(rawpos[0] + 256*(int)rawpos[1]) + rawpos[2];

			cpu->Ldy(ch.Seek(pos));

		} else if (command == 0x23) {	// lock
			// DOS 2.0S lock behavior:
			// - A file can be locked while open for write.
			// - A file can't be locked on creation until it has been closed (file not found).
			// - If no file is found, file not found is returned, even with wildcards.

			if (mbReadOnly) {
				cpu->Ldy(CIOStatReadOnly);
				return;
			}

			if (!ReadFilename(cpu, mem, true, true))
				return;

			VDDirectoryIterator it(mNativeSearchPath.c_str());
			bool found = false;

			while(GetNextMatch(it)) {
				ATFileSetReadOnlyAttribute(it.GetFullPath().c_str(), true);
				found = true;
			}

			if (!found)
				cpu->Ldy(CIOStatFileNotFound);
			else
				cpu->Ldy(CIOStatSuccess);
		} else if (command == 0x24) {	// unlock
			// DOS 2.0S unlock behavior:
			// - A file can be unlocked while open for write.
			// - A file can't be unlocked on creation until it has been closed (file not found).
			// - If no file is found, file not found is returned, even with wildcards.

			if (mbReadOnly) {
				cpu->Ldy(CIOStatReadOnly);
				return;
			}

			if (!ReadFilename(cpu, mem, true, true))
				return;

			VDDirectoryIterator it(mNativeSearchPath.c_str());
			bool found = false;

			while(GetNextMatch(it)) {
				ATFileSetReadOnlyAttribute(it.GetFullPath().c_str(), false);
				found = true;
			}

			if (!found)
				cpu->Ldy(CIOStatFileNotFound);
			else
				cpu->Ldy(CIOStatSuccess);
		} else if (command == 0x21) {	// delete
			if (mbReadOnly) {
				cpu->Ldy(CIOStatReadOnly);
				return;
			}

			if (!ReadFilename(cpu, mem, true, true))
				return;

			VDDirectoryIterator it(mNativeSearchPath.c_str());
			bool found = false;

			while(GetNextMatch(it)) {
				VDRemoveFileEx(it.GetFullPath().c_str());
				found = true;
			}

			if (!found)
				cpu->Ldy(CIOStatFileNotFound);
			else
				cpu->Ldy(CIOStatSuccess);
		} else if (command == 0x20) {	// rename
			if (!ReadFilename(cpu, mem, false, true))
				return;

			// look for second filename
			ATKernelDatabase kdb(mem);
			uint16 bufadr = kdb.ICBAZ;

			uint8 c = mem->ReadByte(bufadr + mFilenameEnd);

			if (c != ',' && c != ' ') {
				cpu->Ldy(CIOStatFileNameErr);
				return;
			}

			uint32 idx2 = mFilenameEnd + 1;
			while(mem->ReadByte(bufadr + idx2) == ' ') {
				++idx2;

				if (idx2 >= 256) {
					cpu->Ldy(CIOStatFileNameErr);
					return;
				}
			}

			// parse out second filename
			VDStringA fn2;
			for(;;) {
				uint8 c = mem->ReadByte(bufadr + (idx2++));

				if (c == 0x9B || c == 0x20 || c == ',' || c == 0)
					break;

				// check for excessively long or unterminated filename
				if (idx2 == 256)  {
					cpu->Ldy(CIOStatFileNameErr);
					return;
				}

				// reject non-ASCII characters
				if (c < 0x20 || c > 0x7f) {
					cpu->Ldy(CIOStatFileNameErr);
					return;
				}

				// convert to lowercase
				if (c >= 0x61 && c <= 0x7A)
					c -= 0x20;

				fn2.push_back((char)c);
			}

			VDStringW nativePath2;
			if (!ATHostDeviceParseFilename(fn2.c_str(), false, true, false, nativePath2)) {
				cpu->Ldy(CIOStatFileNameErr);
				return;
			}

			try {
				VDDirectoryIterator it(mNativeSearchPath.c_str());
				const wchar_t *const destName = nativePath2.c_str();
				const bool wildDest = ATHostDeviceIsPathWild(destName);

				VDStringW destFileBuf;
				bool matched = false;

				while(GetNextMatch(it, true)) {
					if (VDFileGetAttributes(it.GetFullPath().c_str()) & kVDFileAttr_ReadOnly) {
						cpu->Ldy(CIOStatFileLocked);
						return;
					}

					if (wildDest) {
						const wchar_t *srcName = it.GetName();
						if (*srcName == L'$')
							++srcName;

						destFileBuf.clear();
						ATHostDeviceMergeWildPath(destFileBuf, srcName, destName);

						if (ATHostDeviceIsPathWild(destFileBuf.c_str()))
							destFileBuf.insert(0, L'$');

						const VDStringW& destFile = VDMakePath(mNativeDirPath.c_str(), destFileBuf.c_str());
						VDMoveFile(it.GetFullPath().c_str(), destFile.c_str());
					} else {
						const VDStringW& destFile = VDMakePath(mNativeDirPath.c_str(), destName);
						VDMoveFile(it.GetFullPath().c_str(), destFile.c_str());
					}

					matched = true;
				}

				cpu->Ldy(matched ? CIOStatSuccess : CIOStatFileNotFound);
			} catch(const MyWin32Error& e) {
				cpu->Ldy(ATTranslateWin32ErrorToSIOError(e.GetWin32Error()));
			} catch(const MyError&) {
				cpu->Ldy(CIOStatFatalDiskIO);
			}
		} else if (command == 0x27) {	// SDX: Get File Length
			const int idx = (cpu->GetX() >> 4) & 7;
			Channel& ch = mChannels[idx];

			if (!ch.mbOpen) {
				cpu->Ldy(CIOStatNotOpen);
				return;
			}

			uint32 len;
			if (ch.GetLength(len)) {
				kdb.ICAX3Z = (uint8)len;
				kdb.ICAX4Z = (uint8)(len >> 8);
				kdb.ICAX5Z = (uint8)(len >> 16);
				cpu->Ldy(CIOStatSuccess);
			} else {
				cpu->Ldy(CIOStatFatalDiskIO);
			}
		} else if (command == 0x2C || command == 0x29) {	// SDX: Set Current Directory / MyDOS: Change Directory
			if (!ReadFilename(cpu, mem, true, true))
				return;

			if (mFilePattern.empty()) {
				VDDirectoryIterator it(mNativeSearchPath.c_str());

				if (GetNextMatch(it)) {
					const VDStringW& newPath = VDMakePath(mNativeBasePath[mPathIndex].c_str(), VDFileSplitPathLeft(mNativeRelPath).c_str());

					mNativeCurDir[mPathIndex] = VDMakePath(newPath.c_str(), it.GetName());
					cpu->Ldy(CIOStatSuccess);
				} else {
					cpu->Ldy(CIOStatPathNotFound);
				}
			} else {
				const VDStringW& newPath = VDMakePath(mNativeBasePath[mPathIndex].c_str(), mNativeRelPath.c_str());

				if (VDDoesPathExist(newPath.c_str())) {
					mNativeCurDir[mPathIndex] = mNativeRelPath;
					cpu->Ldy(CIOStatSuccess);
				} else {
					cpu->Ldy(CIOStatPathNotFound);
				}
			}
		} else if (command == 0x30) {	// SDX: Get Current Directory
			uint16 pathdst = kdb.ICBLL_ICBLH;

			VDStringW::const_iterator it(mNativeCurDir[mPathIndex].begin()), itEnd(mNativeCurDir[mPathIndex].end());

			if (it != itEnd) {
				mem->WriteByte(pathdst++, '>');

				for(; it != itEnd; ++it) {
					wchar_t c = *it;

					if (c == L'$')
						continue;

					if (c == '\\')
						c = L'>';

					mem->WriteByte(pathdst++, (uint8)c);
				}
			}

			mem->WriteByte(pathdst, 0);
			cpu->Ldy(CIOStatSuccess);
		} else if (command == 0x2A) {	// SDX: Create Directory
			if (mbReadOnly) {
				cpu->Ldy(CIOStatReadOnly);
				return;
			}

			if (!ReadFilename(cpu, mem, false, false))
				return;

			const VDStringW& newPath = VDMakePath(mNativeBasePath[mPathIndex].c_str(), mNativeRelPath.c_str());
			uint8 status = CIOStatSuccess;

			try {
				VDCreateDirectory(newPath.c_str());
			} catch(const MyWin32Error& e) {
				status = ATTranslateWin32ErrorToSIOError(e.GetWin32Error());
			} catch(const MyError&) {
				status = CIOStatFatalDiskIO;
			}

			cpu->Ldy(status);
		} else if (command == 0x2B) {	// SDX: Remove Directory
			if (mbReadOnly) {
				cpu->Ldy(CIOStatReadOnly);
				return;
			}

			if (!ReadFilename(cpu, mem, false, false))
				return;

			uint8 status = CIOStatSuccess;

			try {
				if (mFilePattern.empty()) {
					VDDirectoryIterator it(mNativeSearchPath.c_str());

					if (GetNextMatch(it)) {
						const VDStringW& newPath = VDMakePath(mNativeDirPath.c_str(), it.GetName());

						VDRemoveDirectory(newPath.c_str());
					} else {
						status = CIOStatPathNotFound;
					}
				} else {
					const VDStringW& newPath = VDMakePath(mNativeBasePath[mPathIndex].c_str(), mNativeRelPath.c_str());

					VDRemoveDirectory(newPath.c_str());
				}
			} catch(const MyWin32Error& e) {
				status = ATTranslateWin32ErrorToSIOError(e.GetWin32Error());
			} catch(const MyError&) {
				status = CIOStatFatalDiskIO;
			}

			cpu->Ldy(status);
		}
	} catch(const MyWin32Error& e) {
		cpu->Ldy(ATTranslateWin32ErrorToSIOError(e.GetWin32Error()));
	} catch(const MyError&) {
		cpu->Ldy(CIOStatFatalDiskIO);
	}
}

bool ATHostDeviceEmulator::ReadFilename(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, bool allowDir, bool allowWild) {
	ATKernelDatabase kdb(mem);
	uint16 bufadr = kdb.ICBAZ;

	for(int i=0; i<128; ++i) {
		uint8 c = mem->ReadByte(bufadr + i);

		if (c == 0x9B || c == 0x20 || c == ',' || c == 0) {
			mFilenameEnd = i;
			mFilename[i] = 0;
			break;
		}

		// check for excessively long or unterminated filename
		if (i == 127)  {
			cpu->Ldy(CIOStatFileNameErr);
			return false;
		}

		// reject non-ASCII characters
		if (c < 0x20 || c > 0x7f) {
			cpu->Ldy(CIOStatFileNameErr);
			return false;
		}

		// convert to lowercase
		if (c >= 0x61 && c <= 0x7A)
			c -= 0x20;

		mFilename[i] = (char)c;
	}

	// parse path prefix
	int index = 1;

	// check for H device specifier
	const char *s = mFilename;
	char c = *s++;
	if (c != 'H') {
		cpu->Ldy(CIOStatFileNameErr);
		return false;
	}

	// check for drive number
	c = *s++;

	index = 1;
	if (c != ':') {
		if (c < '1' || c > '9' || c == '5') {
			cpu->Ldy(CIOStatFileNameErr);
			return false;
		}

		index = c - '0';

		c = *s++;
		if (c != ':') {
			cpu->Ldy(CIOStatFileNameErr);
			return false;
		}
	}

	VDStringW parsedPath;

	// check for parent specifiers
	if (*s == '>' || *s == '\\') {
		++s;
	} else
		parsedPath = mNativeCurDir[mPathIndex];

	// check for back-up specifiers
	while(*s == L'<') {
		++s;

		while(!parsedPath.empty()) {
			wchar_t c = parsedPath.back();

			parsedPath.pop_back();

			if (c == L'\\')
				break;
		}
	}

	if (index >= 6) {
		mPathIndex = index - 6;
		mbPathTranslate = true;
	} else {
		mPathIndex = index - 1;
		mbPathTranslate = false;
	}

	if (mNativeBasePath[mPathIndex].empty()) {
		cpu->Ldy(CIOStatPathNotFound);
		return false;
	}

	// validate filename format
	if (!ATHostDeviceParseFilename(s, allowDir, allowWild, true, parsedPath)) {
		cpu->Ldy(CIOStatFileNameErr);
		return false;
	}

	const wchar_t *nativeRelPath = parsedPath.c_str();
	const wchar_t *nativeFile = VDFileSplitPath(nativeRelPath);

	mNativeRelPath = nativeRelPath;
	mNativeDirPath = mNativeBasePath[mPathIndex];
	mNativeDirPath.append(nativeRelPath, nativeFile);
	mNativeSearchPath = mNativeDirPath;
	mNativeSearchPath += L"*.*";
	mFilePattern = VDTextWToA(nativeFile);

	if (mFilePattern.find('.') == VDStringW::npos)
		mFilePattern += '.';

	return true;
}

bool ATHostDeviceEmulator::GetNextMatch(VDDirectoryIterator& it, bool allowDirs, VDStringA *encodedName) {
	char xlName[13];

	for(;;) {
		if (!it.Next())
			return false;

		if (it.IsDotDirectory())
			continue;

		if (!allowDirs && it.IsDirectory())
			continue;

		ATHostDeviceEncodeName(xlName, it.GetName(), mbLongNameEncoding);

		if (VDFileWildMatch(mFilePattern.c_str(), xlName)) {
			if (encodedName)
				encodedName->assign(xlName);

			return true;
		}
	}
}
