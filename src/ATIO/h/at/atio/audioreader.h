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

#ifndef f_AT_ATIO_AUDIOREADER_H
#define f_AT_ATIO_AUDIOREADER_H

#include <vd2/system/vdtypes.h>

class IVDRandomAccessStream;

struct ATAudioReadFormatInfo {
	uint32 mChannels;
	uint32 mSamplesPerSec;
};

class IATAudioReader {
public:
	virtual ~IATAudioReader() = default;

	virtual uint64 GetDataSize() const = 0;
	virtual uint64 GetDataPos() const = 0;
	virtual uint64 GetFrameCount() const = 0;

	virtual ATAudioReadFormatInfo GetFormatInfo() const = 0;
	virtual uint32 ReadStereo16(sint16 *dst, uint32 n) = 0;
};

IATAudioReader *ATCreateAudioReaderWAV(IVDRandomAccessStream& inputStream);
IATAudioReader *ATCreateAudioReaderFLAC(IVDRandomAccessStream& inputStream, bool verify);

#endif
