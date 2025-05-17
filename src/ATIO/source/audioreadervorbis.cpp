//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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
#include <vd2/system/file.h>
#include <at/atio/audioreader.h>
#include <at/atio/vorbisdecoder.h>

class ATAudioReaderVorbis final : public IATAudioReader {
public:
	ATAudioReaderVorbis(IVDRandomAccessStream& stream);

	uint64 GetDataSize() const override;
	uint64 GetDataPos() const override;

	ATAudioReadFormatInfo GetFormatInfo() const override;
	uint32 ReadStereo16(sint16 *dst, uint32 n) override;

private:
	IVDRandomAccessStream& mStream;
	uint64 mPos = 0;
	uint64 mLength = 0;
	bool mbStreamEnded = false;

	ATVorbisDecoder mDecoder;
};

IATAudioReader *ATCreateAudioReaderVorbis(IVDRandomAccessStream& inputStream) {
	return new ATAudioReaderVorbis(inputStream);
}

ATAudioReaderVorbis::ATAudioReaderVorbis(IVDRandomAccessStream& stream)
	: mStream(stream)
{
	mLength = (uint64)stream.Length();

	mDecoder.Init(
		[this](void *dst, size_t len) -> size_t {
			uint64 left = mLength - mPos;
			if (len > left)
				len = left;

			if (len > 0x7FFFFFFF)
				len = 0x7FFFFFFF;

			mPos += len;

			mStream.Read(dst, len);
			return len;
		}
	);

	mDecoder.ReadHeaders();

	mbStreamEnded = !mDecoder.ReadAudioPacket();
}

uint64 ATAudioReaderVorbis::GetDataSize() const {
	return mLength;
}

uint64 ATAudioReaderVorbis::GetDataPos() const {
	return mPos;
}

ATAudioReadFormatInfo ATAudioReaderVorbis::GetFormatInfo() const {
	ATAudioReadFormatInfo info {};
	info.mChannels = mDecoder.GetChannelCount();
	info.mSamplesPerSec = mDecoder.GetSampleRate();

	return info;
}

uint32 ATAudioReaderVorbis::ReadStereo16(sint16 *dst, uint32 n) {
	uint32 total = 0;

	while(n && !mbStreamEnded) {
		const uint32 actual = mDecoder.ReadInterleavedSamplesStereoS16(dst, n);
		if (!actual) {
			if (!mDecoder.ReadAudioPacket()) {
				mbStreamEnded = true;
				break;
			}

			continue;
		}

		total += actual;
		dst += 2*actual;
		n -= actual;
	}

	return total;
}
