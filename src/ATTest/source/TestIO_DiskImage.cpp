//	Altirra - Atari 800/800XL/5200 emulator
//	Test module
//	Copyright (C) 2009-2020 Avery Lee
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
#include <vd2/system/binary.h>
#include <at/atcore/configvar.h>
#include <at/atio/diskimage.h>
#include "test.h"
#include "blob.h"

void ATTestDiskImageType(const wchar_t *filename, ATDiskImageFormat format, int subFormat) {
	VDStringW blobName = VDStringW(L"blob://") + filename;

	uint8 testbuf[4096];
	uint8 checkbuf[4096];
	for(int i = 0; i < 4096; ++i)
		testbuf[i] = (uint8)(i * i);

	vdrefptr<IATDiskImage> diskImage;
	
	// SD basic write
	{
		ATCreateDiskImage(720, 3, 128, ~diskImage);
		diskImage->WriteVirtualSector(0, testbuf, 128);
		diskImage->WriteVirtualSector(1, testbuf + 256, 128);
		diskImage->Save(blobName.c_str(), format);

		[[maybe_unused]] auto [data, minRange, maxRange] = ATTestGetBlob(filename);

		if (format == kATDiskImageFormat_ATR) {
			TEST_ASSERT(data.size() == 720*128 + 16);
			TEST_ASSERT(VDReadUnalignedLEU16(&data[0]) == 0x0296);
			TEST_ASSERT(VDReadUnalignedLEU16(&data[2]) == ((720 * 128) >> 4));
			TEST_ASSERT(VDReadUnalignedLEU16(&data[4]) == 128);
			TEST_ASSERT(!memcmp(&data[16], testbuf, 128));
			TEST_ASSERT(!memcmp(&data[16 + 128], testbuf + 256, 128));
		} else if (format == kATDiskImageFormat_XFD) {
			TEST_ASSERT(data.size() == 720*128);
			TEST_ASSERT(!memcmp(&data[0], testbuf, 128));
			TEST_ASSERT(!memcmp(&data[128], testbuf + 256, 128));
		} else if (format == kATDiskImageFormat_ATX) {
			// check density
			TEST_ASSERT(data[18] == 0);

			// check that track 0 is FM
			TEST_ASSERT(!(data[64] & 2));

			// check that track 0 has 128 bytes as default size
			TEST_ASSERT(data[62] == 0);

			// check that all sectors on track 0 have correct status and ascending
			// positions
			uint16 lastPos = 0;

			for(int i=0; i<18; ++i) {
				TEST_ASSERT(data[88 + 8*i + 1] == 0);

				uint16 pos = data[88 + 8*i + 2] + 256*data[88 + 8*i + 3];

				TEST_ASSERT(pos >= lastPos);
				lastPos = pos;
			}
		}
	}

	// SD basic read
	{
		ATLoadDiskImage(blobName.c_str(), ~diskImage);

		TEST_ASSERT(128 == diskImage->ReadVirtualSector(0, checkbuf, 128));
		TEST_ASSERT(!memcmp(checkbuf, testbuf, 128));

		TEST_ASSERT(128 == diskImage->ReadVirtualSector(1, checkbuf, 128));
		TEST_ASSERT(!memcmp(checkbuf, testbuf + 256, 128));

		const ATDiskGeometryInfo& geo = diskImage->GetGeometry();
		TEST_ASSERT(geo.mBootSectorCount == 3);
		TEST_ASSERT(geo.mSectorSize == 128);
		TEST_ASSERT(geo.mTotalSectorCount == 720);
		TEST_ASSERT(geo.mbMFM == false);
		TEST_ASSERT(geo.mbHighDensity == false);

		for(int i=0; i<16; ++i) {
			ATDiskVirtualSectorInfo vsi;
			diskImage->GetVirtualSectorInfo(i, vsi);

			TEST_ASSERT(vsi.mNumPhysSectors == 1);
			ATDiskPhysicalSectorInfo psi;
			diskImage->GetPhysicalSectorInfo(vsi.mStartPhysSector, psi);

			TEST_ASSERT(psi.mFDCStatus == 0xFF);
			TEST_ASSERT(psi.mPhysicalSize == 128);
		}
	}

	// SD basic update
	{
		diskImage->Flush();
		[[maybe_unused]] auto [data, minRange, maxRange] = ATTestGetBlob(filename);
		TEST_ASSERT(minRange == maxRange);

		// do two sector writes and verify that they are coalesced
		diskImage->WriteVirtualSector(0, testbuf + 1024, 128);
		diskImage->WriteVirtualSector(1, testbuf + 2048, 128);
		diskImage->Flush();

		[[maybe_unused]] auto [data2, minRange2, maxRange2] = ATTestGetBlob(filename);
		if (format == kATDiskImageFormat_ATR) {
			TEST_ASSERT(minRange2 == 16);
			TEST_ASSERT(maxRange2 == 16 + 256);
			TEST_ASSERT(!memcmp(&data2[16], testbuf + 1024, 128));
			TEST_ASSERT(!memcmp(&data2[16 + 128], testbuf + 2048, 128));
		} else if (format == kATDiskImageFormat_XFD) {
			TEST_ASSERT(minRange2 == 0);
			TEST_ASSERT(maxRange2 == 256);
			TEST_ASSERT(!memcmp(&data2[0], testbuf + 1024, 128));
			TEST_ASSERT(!memcmp(&data2[128], testbuf + 2048, 128));
		}

		// flush again, check that there were no writes
		diskImage->Flush();
		[[maybe_unused]] auto [data3, minRange3, maxRange3] = ATTestGetBlob(filename);
		TEST_ASSERT(minRange3 == maxRange3);

		// write discontinuous sectors and verify
		diskImage->WriteVirtualSector(0, testbuf + 2048, 128);
		diskImage->WriteVirtualSector(2, testbuf + 3072, 128);
		diskImage->Flush();

		[[maybe_unused]] auto [data4, minRange4, maxRange4] = ATTestGetBlob(filename);

		if (format == kATDiskImageFormat_ATR) {
			TEST_ASSERT(minRange4 == 16);
			TEST_ASSERT(maxRange4 == 16 + 384);
			TEST_ASSERT(!memcmp(&data4[16], testbuf + 2048, 128));
			TEST_ASSERT(!memcmp(&data4[16 + 128], testbuf + 2048, 128));
			TEST_ASSERT(!memcmp(&data4[16 + 256], testbuf + 3072, 128));
		} else if (format == kATDiskImageFormat_XFD) {
			TEST_ASSERT(minRange4 == 0);
			TEST_ASSERT(maxRange4 == 384);
			TEST_ASSERT(!memcmp(&data4[0], testbuf + 2048, 128));
			TEST_ASSERT(!memcmp(&data4[128], testbuf + 2048, 128));
			TEST_ASSERT(!memcmp(&data4[256], testbuf + 3072, 128));
		}
	}

	// ED basic write
	{
		ATCreateDiskImage(1040, 3, 128, ~diskImage);
		diskImage->WriteVirtualSector(0, testbuf, 128);
		diskImage->WriteVirtualSector(1, testbuf + 256, 128);
		diskImage->Save(blobName.c_str(), format);

		[[maybe_unused]] auto [data, minRange, maxRange] = ATTestGetBlob(filename);

		if (format == kATDiskImageFormat_ATR) {
			TEST_ASSERT(data.size() == 1040*128 + 16);
			TEST_ASSERT(VDReadUnalignedLEU16(&data[0]) == 0x0296);
			TEST_ASSERT(VDReadUnalignedLEU16(&data[2]) == ((1040 * 128) >> 4));
			TEST_ASSERT(VDReadUnalignedLEU16(&data[4]) == 128);
			TEST_ASSERT(!memcmp(&data[16], testbuf, 128));
			TEST_ASSERT(!memcmp(&data[16 + 128], testbuf + 256, 128));
		} else if (format == kATDiskImageFormat_XFD) {
			TEST_ASSERT(data.size() == 1040*128);
			TEST_ASSERT(!memcmp(&data[0], testbuf, 128));
			TEST_ASSERT(!memcmp(&data[128], testbuf + 256, 128));
		} else if (format == kATDiskImageFormat_ATX) {
			// check density
			TEST_ASSERT(data[18] == 1);

			// check that track 0 has 128 bytes as default size
			TEST_ASSERT(data[62] == 0);

			// check that track 0 is MFM
			TEST_ASSERT(data[64] & 2);

			// check that all sectors on track 0 have correct status
			for(int i=0; i<26; ++i) {
				TEST_ASSERT(data[88 + 8*i + 1] == 0);
			}
		}
	}

	// ED basic read
	{
		ATLoadDiskImage(blobName.c_str(), ~diskImage);

		TEST_ASSERT(128 == diskImage->ReadVirtualSector(0, checkbuf, 128));
		TEST_ASSERT(!memcmp(checkbuf, testbuf, 128));

		TEST_ASSERT(128 == diskImage->ReadVirtualSector(1, checkbuf, 128));
		TEST_ASSERT(!memcmp(checkbuf, testbuf + 256, 128));

		const ATDiskGeometryInfo& geo = diskImage->GetGeometry();
		TEST_ASSERT(geo.mBootSectorCount == 3);
		TEST_ASSERT(geo.mSectorSize == 128);
		TEST_ASSERT(geo.mTotalSectorCount == 1040);
		TEST_ASSERT(geo.mbMFM == true);
		TEST_ASSERT(geo.mbHighDensity == false);

		for(int i=0; i<16; ++i) {
			ATDiskVirtualSectorInfo vsi;
			diskImage->GetVirtualSectorInfo(i, vsi);

			TEST_ASSERT(vsi.mNumPhysSectors == 1);
			ATDiskPhysicalSectorInfo psi;
			diskImage->GetPhysicalSectorInfo(vsi.mStartPhysSector, psi);

			TEST_ASSERT(psi.mFDCStatus == 0xFF);
			TEST_ASSERT(psi.mPhysicalSize == 128);
		}
	}

	// ED basic update
	{
		diskImage->Flush();
		[[maybe_unused]] auto [data, minRange, maxRange] = ATTestGetBlob(filename);
		TEST_ASSERT(minRange == maxRange);

		// do two sector writes and verify that they are coalesced
		diskImage->WriteVirtualSector(0, testbuf + 1024, 128);
		diskImage->WriteVirtualSector(1, testbuf + 2048, 128);
		diskImage->Flush();

		[[maybe_unused]] auto [data2, minRange2, maxRange2] = ATTestGetBlob(filename);
		if (format == kATDiskImageFormat_ATR) {
			TEST_ASSERT(minRange2 == 16);
			TEST_ASSERT(maxRange2 == 16 + 256);
			TEST_ASSERT(!memcmp(&data2[16], testbuf + 1024, 128));
			TEST_ASSERT(!memcmp(&data2[16 + 128], testbuf + 2048, 128));
		} else if (format == kATDiskImageFormat_XFD) {
			TEST_ASSERT(minRange2 == 0);
			TEST_ASSERT(maxRange2 == 256);
			TEST_ASSERT(!memcmp(&data2[0], testbuf + 1024, 128));
			TEST_ASSERT(!memcmp(&data2[128], testbuf + 2048, 128));
		}

		// flush again, check that there were no writes
		diskImage->Flush();
		[[maybe_unused]] auto [data3, minRange3, maxRange3] = ATTestGetBlob(filename);
		TEST_ASSERT(minRange3 == maxRange3);

		// write discontinuous sectors and verify
		diskImage->WriteVirtualSector(0, testbuf + 2048, 128);
		diskImage->WriteVirtualSector(2, testbuf + 3072, 128);
		diskImage->Flush();

		[[maybe_unused]] auto [data4, minRange4, maxRange4] = ATTestGetBlob(filename);

		if (format == kATDiskImageFormat_ATR) {
			TEST_ASSERT(minRange4 == 16);
			TEST_ASSERT(maxRange4 == 16 + 384);
			TEST_ASSERT(!memcmp(&data4[16], testbuf + 2048, 128));
			TEST_ASSERT(!memcmp(&data4[16 + 128], testbuf + 2048, 128));
			TEST_ASSERT(!memcmp(&data4[16 + 256], testbuf + 3072, 128));
		} else if (format == kATDiskImageFormat_XFD) {
			TEST_ASSERT(minRange4 == 0);
			TEST_ASSERT(maxRange4 == 384);
			TEST_ASSERT(!memcmp(&data4[0], testbuf + 2048, 128));
			TEST_ASSERT(!memcmp(&data4[128], testbuf + 2048, 128));
			TEST_ASSERT(!memcmp(&data4[256], testbuf + 3072, 128));
		}
	}

	// skip double density tests for ATX w/o full sector flag
	if (format == kATDiskImageFormat_ATX && subFormat == 0)
		return;

	// DD basic write
	{
		ATCreateDiskImage(720, 3, 256, ~diskImage);
		diskImage->WriteVirtualSector(0, testbuf, 128);
		diskImage->WriteVirtualSector(1, testbuf + 256, 128);
		diskImage->WriteVirtualSector(2, testbuf + 512, 128);
		diskImage->WriteVirtualSector(3, testbuf + 768, 256);
		diskImage->Save(blobName.c_str(), format);

		[[maybe_unused]] auto [data, minRange, maxRange] = ATTestGetBlob(filename);

		if (format == kATDiskImageFormat_ATR) {
			TEST_ASSERT(data.size() == 720*256 + 16 - 3 * 128);
			TEST_ASSERT(VDReadUnalignedLEU16(&data[0]) == 0x0296);
			TEST_ASSERT(VDReadUnalignedLEU16(&data[2]) == ((720 * 256 - 3 * 128) >> 4));
			TEST_ASSERT(VDReadUnalignedLEU16(&data[4]) == 256);
			TEST_ASSERT(!memcmp(&data[16], testbuf, 128));
			TEST_ASSERT(!memcmp(&data[16 + 128*1], testbuf + 256, 128));
			TEST_ASSERT(!memcmp(&data[16 + 128*2], testbuf + 512, 128));
			TEST_ASSERT(!memcmp(&data[16 + 128*3], testbuf + 768, 256));
		} else if (format == kATDiskImageFormat_XFD) {
			TEST_ASSERT(data.size() == 720*256);
			TEST_ASSERT(!memcmp(&data[128*0], testbuf, 128));
			TEST_ASSERT(!memcmp(&data[128*2], testbuf + 256, 128));
			TEST_ASSERT(!memcmp(&data[128*4], testbuf + 512, 128));
			TEST_ASSERT(!memcmp(&data[128*6], testbuf + 768, 256));
		} else if (format == kATDiskImageFormat_ATX) {
			// check density
			TEST_ASSERT(data[18] == 2);

			// check that track 0 has 256 bytes as default size
			TEST_ASSERT(data[62] == 1);

			// check that track 0 is MFM
			TEST_ASSERT(data[64] & 2);

			// check that all sectors on track 0 have correct status
			for(int i=0; i<18; ++i) {
				TEST_ASSERT(data[88 + 8*i + 1] == 0);
			}
		}
	}

	// DD basic read
	{
		ATLoadDiskImage(blobName.c_str(), ~diskImage);

		TEST_ASSERT(128 == diskImage->ReadVirtualSector(0, checkbuf, 128));
		TEST_ASSERT(!memcmp(checkbuf, testbuf, 128));

		TEST_ASSERT(128 == diskImage->ReadVirtualSector(1, checkbuf, 128));
		TEST_ASSERT(!memcmp(checkbuf, testbuf + 256, 128));

		TEST_ASSERT(128 == diskImage->ReadVirtualSector(2, checkbuf, 128));
		TEST_ASSERT(!memcmp(checkbuf, testbuf + 512, 128));

		TEST_ASSERT(256 == diskImage->ReadVirtualSector(3, checkbuf, 256));
		TEST_ASSERT(!memcmp(checkbuf, testbuf + 768, 256));

		for(int i=0; i<16; ++i) {
			ATDiskVirtualSectorInfo vsi;
			diskImage->GetVirtualSectorInfo(i, vsi);

			TEST_ASSERT(vsi.mNumPhysSectors == 1);
			ATDiskPhysicalSectorInfo psi;
			diskImage->GetPhysicalSectorInfo(vsi.mStartPhysSector, psi);

			TEST_ASSERT(psi.mFDCStatus == 0xFF);
			TEST_ASSERT(psi.mPhysicalSize == 256);
		}

		const ATDiskGeometryInfo& geo = diskImage->GetGeometry();
		TEST_ASSERT(geo.mBootSectorCount == 3);
		TEST_ASSERT(geo.mSectorSize == 256);
		TEST_ASSERT(geo.mTotalSectorCount == 720);
		TEST_ASSERT(geo.mbMFM == true);
		TEST_ASSERT(geo.mbHighDensity == false);
	}

	// DD full boot sector test
	{
		ATCreateDiskImage(720, 3, 256, ~diskImage);
		vdfastvector<ATDiskVirtualSectorInfo> vsecs(18);
		vdfastvector<ATDiskPhysicalSectorInfo> psecs(18);

		for(int i=0; i<18; ++i) {
			diskImage->GetVirtualSectorInfo(i, vsecs[i]);
			vsecs[i].mStartPhysSector = i;

			diskImage->GetPhysicalSectorInfo(i, psecs[i]);
			psecs[i].mOffset = 0;
			psecs[i].mPhysicalSize = 256;
			psecs[i].mImageSize = 256;
		}

		// reformat track 0 with full 256 byte sectors in the image
		diskImage->FormatTrack(0, 18, vsecs.data(), 18, psecs.data(), testbuf);

		// verify that sector 1 kept full data (we need to use the physical sector)
		ATDiskVirtualSectorInfo vsi;
		diskImage->GetVirtualSectorInfo(0, vsi);
		diskImage->ReadPhysicalSector(vsi.mStartPhysSector, checkbuf, 256);
		TEST_ASSERT(!memcmp(testbuf, checkbuf, 256));

		// save the image
		diskImage->Save(blobName.c_str(), format);

		[[maybe_unused]] auto [data, minRange, maxRange] = ATTestGetBlob(filename);
		if (format == kATDiskImageFormat_ATR) {
			// ATR -- boot sectors trimmed to 128 bytes
			TEST_ASSERT(data.size() == 720*256 + 16 - 3 * 128);
			TEST_ASSERT(!memcmp(&data[16], testbuf, 128));
			TEST_ASSERT(!memcmp(&data[16 + 128*1], testbuf + 256, 128));
			TEST_ASSERT(!memcmp(&data[16 + 128*2], testbuf + 512, 128));
			TEST_ASSERT(!memcmp(&data[16 + 128*3], testbuf + 768, 256));
		} else if (format == kATDiskImageFormat_XFD) {
			// XFD -- full boot sectors kept
			TEST_ASSERT(data.size() == 720*256);
			TEST_ASSERT(!memcmp(&data[256*0], testbuf, 256));
			TEST_ASSERT(!memcmp(&data[256*2], testbuf + 256, 256));
			TEST_ASSERT(!memcmp(&data[256*4], testbuf + 512, 256));
			TEST_ASSERT(!memcmp(&data[256*6], testbuf + 768, 256));
		}

		// rewrite sector 2 and flush, make sure sector 3 is not corrupted
		diskImage->WriteVirtualSector(1, testbuf + 1024, 256);
		diskImage->Flush();

		[[maybe_unused]] auto [data2, minRange2, maxRange2] = ATTestGetBlob(filename);
		if (format == kATDiskImageFormat_ATR) {
			// ATR -- boot sectors trimmed to 128 bytes
			TEST_ASSERT(data2.size() == 720*256 + 16 - 3 * 128);
			TEST_ASSERT(minRange2 == 16 + 128);
			TEST_ASSERT(maxRange2 == 16 + 256);
			TEST_ASSERT(!memcmp(&data2[16], testbuf, 128));
			TEST_ASSERT(!memcmp(&data2[16 + 128*1], testbuf + 1024, 128));
			TEST_ASSERT(!memcmp(&data2[16 + 128*2], testbuf + 512, 128));
			TEST_ASSERT(!memcmp(&data2[16 + 128*3], testbuf + 768, 256));
		} else if (format == kATDiskImageFormat_XFD) {
			// XFD -- full boot sectors kept
			TEST_ASSERT(data2.size() == 720*256);
			TEST_ASSERT(minRange2 == 256);
			TEST_ASSERT(maxRange2 == 512);
			TEST_ASSERT(!memcmp(&data2[256*0], testbuf, 256));
			TEST_ASSERT(!memcmp(&data2[256*2], testbuf + 1024, 256));
			TEST_ASSERT(!memcmp(&data2[256*4], testbuf + 512, 256));
			TEST_ASSERT(!memcmp(&data2[256*6], testbuf + 768, 256));
		}

		// re-read the disk and check statuses
		ATLoadDiskImage(blobName.c_str(), ~diskImage);
		for(int i=0; i<3; ++i) {
			ATDiskVirtualSectorInfo vsi;
			diskImage->GetVirtualSectorInfo(i, vsi);

			TEST_ASSERT(vsi.mNumPhysSectors == 1);
			ATDiskPhysicalSectorInfo psi;
			diskImage->GetPhysicalSectorInfo(vsi.mStartPhysSector, psi);

			TEST_ASSERT(psi.mFDCStatus == 0xFF);
			TEST_ASSERT(psi.mPhysicalSize == 256);
		}
	}
}

DEFINE_TEST(IO_DiskImage) {
	ATTestDiskImageType(L"test.atr", kATDiskImageFormat_ATR, 0);
	ATTestDiskImageType(L"test.xfd", kATDiskImageFormat_XFD, 0);
	ATTestDiskImageType(L"test.atx", kATDiskImageFormat_ATX, 0);

	// enable ATX full sector storage
	ATConfigVar **cvars = nullptr;
	size_t ncvars = 0;
	ATGetConfigVars(cvars, ncvars);

	for(size_t i = 0; i < ncvars; ++i) {
		if (!strcmp(cvars[i]->mpVarName, "image.disk.atx.full_sector_support")) {
			cvars[i]->FromString("true");
			break;
		}
	}

	ATTestDiskImageType(L"test.atx", kATDiskImageFormat_ATX, 1);

	return 0;
}
