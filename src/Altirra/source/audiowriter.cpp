//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2009 Avery Lee
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
#include "audiowriter.h"

ATAudioWriter::ATAudioWriter(const wchar_t *filename)
	: mbErrorState(false)
	, mFile(filename, nsVDFile::kWrite | nsVDFile::kDenyRead | nsVDFile::kCreateAlways | nsVDFile::kSequential)
{
}

ATAudioWriter::~ATAudioWriter() {
}

void ATAudioWriter::CheckExceptions() {
	if (!mbErrorState)
		return;

	if (!mError.empty()) {
		MyError e;

		e.TransferFrom(mError);
		throw e;
	}
}

void ATAudioWriter::WriteRawAudio(const float *left, const float *right, uint32 count, uint32 timestamp) {
	if (mbErrorState)
		return;

	try {
		if (right) {
			WriteInterleaved(left, right, count);
		} else {
			mFile.writeData(left, sizeof(float)*count);
		}
	} catch(MyError& e) {
		mError.TransferFrom(e);
		mbErrorState = true;
	}
}

void ATAudioWriter::WriteInterleaved(const float *left, const float *right, uint32 count) {
	float buf[512][2];

	while(count) {
		uint32 tc = count > 512 ? 512 : count;

		for(uint32 i=0; i<tc; ++i) {
			buf[i][0] = left[i];
			buf[i][1] = right[i];
		}

		mFile.writeData(buf, tc*sizeof(buf[0]));
		left += tc;
		right += tc;
		count -= tc;
	}
}
