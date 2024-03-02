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
#include "iderawimage.h"

ATIDERawImage::ATIDERawImage() {
}

ATIDERawImage::~ATIDERawImage() {
	Shutdown();
}

uint32 ATIDERawImage::GetSectorCount() const {
	return 0;
}

void ATIDERawImage::Init(const wchar_t *path, bool write) {
	Shutdown();

	mFile.open(path, write ? nsVDFile::kReadWrite | nsVDFile::kDenyAll | nsVDFile::kOpenAlways : nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting);
}

void ATIDERawImage::Shutdown() {
	mFile.closeNT();
}

void ATIDERawImage::Flush() {
}

void ATIDERawImage::RequestUpdate() {
}

void ATIDERawImage::ReadSectors(void *data, uint32 lba, uint32 n) {
	mFile.seek((sint64)lba << 9);

	uint32 requested = n << 9;
	uint32 actual = mFile.readData(data, requested);

	if (requested < actual)
		memset((char *)data + actual, 0, requested - actual);
}

void ATIDERawImage::WriteSectors(const void *data, uint32 lba, uint32 n) {
	mFile.seek((sint64)lba << 9);
	mFile.write(data, 512 * n);
}
