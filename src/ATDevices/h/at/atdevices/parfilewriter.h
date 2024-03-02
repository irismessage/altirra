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

#ifndef f_AT_ATDEVICES_PARFILEWRITER_H
#define f_AT_ATDEVICES_PARFILEWRITER_H

#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceprinter.h>

class ATDeviceParallelFileWriter final : public ATDevice, IATPrinterOutput {
public:
	int AddRef() override;
	int Release() override;
	void *AsInterface(uint32 iid) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void GetSettings(ATPropertySet& pset) override;
	bool SetSettings(const ATPropertySet& pset) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;
	bool GetErrorStatus(uint32 idx, VDStringW& error) override;

	// IATPrinterOutput
	void WriteASCII(const void *buf, size_t len) override;
	void WriteATASCII(const void *buf, size_t len) override;

private:
	void WriteByte(uint8 c);
	void Write(const void *buf, size_t len);
	void Flush();
	void WriteRaw(const void *buf, size_t len);
	void TryOpenOutput();

	VDStringW mPath;
	VDFile mFile;
	MyError mCurrentError;
	bool mbInited = false;
	bool mbTextTranslation = false;
	bool mbTextIgnoreNextLF = false;
	uint32 mBufferLevel = 0;

	static constexpr uint32 kBufferSize = 256;
	uint8 mBuffer[kBufferSize];
};

#endif
