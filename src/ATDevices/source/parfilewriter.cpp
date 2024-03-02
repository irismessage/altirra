//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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

#include <stdafx.h>
#include <at/atcore/propertyset.h>
#include <at/atdevices/parfilewriter.h>

void ATCreateDeviceParallelFileWriter(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr p(new ATDeviceParallelFileWriter);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefParallelFileWriter = { "parfilewriter", "parfilewriter", L"Parallel Port File Writer", ATCreateDeviceParallelFileWriter };

int ATDeviceParallelFileWriter::AddRef() {
	return ATDevice::AddRef();
}

int ATDeviceParallelFileWriter::Release() {
	return ATDevice::Release();
}

void *ATDeviceParallelFileWriter::AsInterface(uint32 iid) {
	switch(iid) {
		case IATPrinterOutput::kTypeID: return static_cast<IATPrinterOutput *>(this);
		default:
			return ATDevice::AsInterface(iid);
	}
}

void ATDeviceParallelFileWriter::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefParallelFileWriter;
}

void ATDeviceParallelFileWriter::GetSettingsBlurb(VDStringW& buf) {
	buf += mPath;

	buf += mbTextTranslation ? L"; text" : L"; binary";
}

void ATDeviceParallelFileWriter::GetSettings(ATPropertySet& pset) {
	pset.SetString("path", mPath.c_str());
	pset.SetBool("text_mode", mbTextTranslation);
}

bool ATDeviceParallelFileWriter::SetSettings(const ATPropertySet& pset) {
	const wchar_t *path = pset.GetString("path", L"");

	if (mPath != path) {
		mPath = path;

		mFile.closeNT();

		TryOpenOutput();
	}

	mbTextTranslation = pset.GetBool("text_mode", false);

	return true;
}

void ATDeviceParallelFileWriter::Init() {
	mbInited = true;
	TryOpenOutput();
}

void ATDeviceParallelFileWriter::Shutdown() {
	mFile.closeNT();
	mbInited = false;
}

void ATDeviceParallelFileWriter::ColdReset() {
	TryOpenOutput();
}

bool ATDeviceParallelFileWriter::GetErrorStatus(uint32 idx, VDStringW& error) {
	if (idx == 0 && !mCurrentError.empty()) {
		error = VDTextAToW(mCurrentError.c_str());
		return true;
	}

	return false;
}

void ATDeviceParallelFileWriter::WriteASCII(const void *buf, size_t len) {
	if (!mbTextTranslation) {
		Write(buf, len);
		Flush();
		return;
	}

	const uint8 *src = (const uint8 *)buf;
	while(len--) {
		uint8 c = *src++;

		if (c == 0x0D) {
			WriteByte(0x0D);
			c = 0x0A;
			mbTextIgnoreNextLF = true;
		} else if (c == 0x0A) {
			if (mbTextIgnoreNextLF) {
				mbTextIgnoreNextLF = false;
				continue;
			}

			WriteByte(0x0D);
			c = 0x0A;
		} else {
			mbTextIgnoreNextLF = false;
		}

		WriteByte(c);
	}

	Flush();
}

void ATDeviceParallelFileWriter::WriteATASCII(const void *buf, size_t len) {
	if (!mbTextTranslation) {
		Write(buf, len);
		Flush();
		return;
	}

	const uint8 *src = (const uint8 *)buf;

	while(len--) {
		uint8 c = *src++;

		if (c == 0x9B) {
			WriteByte(0x0D);
			c = 0x0A;
		}

		WriteByte(c);
	}

	Flush();
}

void ATDeviceParallelFileWriter::WriteByte(uint8 c) {
	if (mBufferLevel >= kBufferSize)
		Flush();

	mBuffer[mBufferLevel++] = c;
}

void ATDeviceParallelFileWriter::Write(const void *buf, size_t len) {
	while(len) {
		if (mBufferLevel == 0) {
			WriteRaw(buf, len);
			break;
		}

		const size_t tc = std::min<size_t>(len, kBufferSize - mBufferLevel);

		if (tc) {
			memcpy(mBuffer + mBufferLevel, buf, tc);
			mBufferLevel += tc;

			buf = (const char *)buf + tc;
			len -= tc;
		}

		Flush();
	}
}

void ATDeviceParallelFileWriter::Flush() {
	if (mBufferLevel) {
		WriteRaw(mBuffer, mBufferLevel);
		mBufferLevel = 0;
	}
}

void ATDeviceParallelFileWriter::WriteRaw(const void *buf, size_t len) {
	if (!len || !mFile.isOpen())
		return;

	try {
		mFile.write(buf, len);
	} catch(MyError& e) {
		mCurrentError.TransferFrom(e);
		mFile.closeNT();
	}
}

void ATDeviceParallelFileWriter::TryOpenOutput() {
	if (mbInited && !mPath.empty() && !mFile.isOpen()) {
		try {
			mFile.open(mPath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyNone | nsVDFile::kOpenAlways);
			mFile.seek(0, nsVDFile::kSeekEnd);
			mCurrentError.clear();
		} catch(MyError& e) {
			mCurrentError.TransferFrom(e);
			mFile.closeNT();
		}
	}
}
