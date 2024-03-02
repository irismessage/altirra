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

#ifndef f_AT_ATIO_AUDIOREADERWAV_H
#define f_AT_ATIO_AUDIOREADERWAV_H

#include <at/atio/audioreader.h>

class ATAudioReaderWAV final : public IATAudioReader {
public:
	ATAudioReaderWAV(IVDRandomAccessStream& stream);

	void Init();

	uint64 GetDataSize() const override;
	uint64 GetDataPos() const override;
	uint64 GetFrameCount() const override;

	ATAudioReadFormatInfo GetFormatInfo() const override;
	uint32 ReadStereo16(sint16 *dst, uint32 n) override;

private:
	void ReadM8AsS16(sint16 *dst, uint32 count);
	void ReadM16AsS16(sint16 *dst, uint32 count);
	void ReadS8AsS16(sint16 *dst, uint32 count);
	void ReadS16AsS16(sint16 *dst, uint32 count);

	IVDRandomAccessStream& mStream;
	ATAudioReadFormatInfo mFormatInfo {};
	uint32 mFrameCount = 0;
	uint32 mFramesLeft = 0;
	uint32 mBitsPerSample = 0;
	uint32 mBytesPerBlock = 0;
	uint64 mDataSize = 0;
	uint64 mDataPos = 0;

	void (ATAudioReaderWAV::*mpReadFn)(sint16 *dst, uint32 count) = nullptr;
};

#endif
