//	Altirra - Atari 800/800XL/5200 emulator
//	I/O library - save state common definitions
//	Copyright (C) 2008-2016 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/error.h>
#include <at/atcore/serializable.h>
#include <at/atio/savestate.h>

extern const uint8 kATSaveStateHeader[12]={'A', 'l', 't', 'S', 'a', 'v', 'e', 0x0D, 0x0A, 0x1A, 0x00, 0x80 };

///////////////////////////////////////////////////////////////////////////

class ATSaveStateImage2 final : public vdrefcounted<IATSaveStateImage2> {
public:
	ATSaveStateImage2(IATSerializable *rootObj);

	void *AsInterface(uint32 id);

	ATImageType GetImageType() const override { return kATImageType_SaveState2; }
	std::optional<uint32> GetImageFileCRC() const { return {}; }
	std::optional<ATChecksumSHA256> GetImageFileSHA256() const { return {}; }

	const IATSerializable *GetRoot() const override { return mpRoot; }

private:
	vdrefptr<IATSerializable> mpRoot;
};

ATSaveStateImage2::ATSaveStateImage2(IATSerializable *rootObj)
	: mpRoot(rootObj)
{
}

void *ATSaveStateImage2::AsInterface(uint32 id) {
	switch(id) {
		case IATSaveStateImage2::kTypeID:
			return static_cast<IATSaveStateImage2 *>(this);
	}

	return nullptr;
}

///////////////////////////////////////////////////////////////////////////

vdfunction<void(VDZipArchive&, const wchar_t *, IATSerializable **)> g_ATSaveState2Reader;

void ATSetSaveState2Reader(vdfunction<void(VDZipArchive&, const wchar_t *, IATSerializable **)> fn) {
	g_ATSaveState2Reader = std::move(fn);
}

void ATReadSaveState2(VDZipArchive& zip, const wchar_t *rootFileName, IATSaveStateImage2 **saveState) {
	if (!g_ATSaveState2Reader)
		throw MyError("Save states are not supported.");

	vdrefptr<IATSerializable> rootObj;
	g_ATSaveState2Reader(zip, rootFileName, ~rootObj);

	vdrefptr<ATSaveStateImage2> saveStateObj(new ATSaveStateImage2(rootObj));
	*saveState = saveStateObj.release();
}
