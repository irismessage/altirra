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
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/vdalloc.h>
#include <at/atio/audioreaderwav.h>
#include <at/atio/wav.h>

ATAudioReaderWAV::ATAudioReaderWAV(IVDRandomAccessStream& stream)
	: mStream(stream)
{
}

void ATAudioReaderWAV::Init() {
	using namespace nsVDWinFormats;

	WaveFormatEx wf = {0};
	sint64 limit = mStream.Length();
	sint64 datapos = -1;
	uint32 datalen = 0;

	for(;;) {
		uint32 hdr[2];

		if (mStream.Pos() >= limit)
			break;

		if (8 != mStream.ReadData(hdr, 8))
			break;

		uint32 fcc = hdr[0];
		uint32 len = VDFromLE32(hdr[1]);

		switch(fcc) {
		case VDMAKEFOURCC('R', 'I', 'F', 'F'):
			limit = mStream.Pos() + len;
			if (len < 4)
				throw MyError("'%ls' is an invalid WAV file.", mStream.GetNameForError());

			mStream.Read(hdr, 4);
			if (hdr[0] != VDMAKEFOURCC('W', 'A', 'V', 'E'))
				throw MyError("'%ls' is not a WAV file.", mStream.GetNameForError());

			len = 0;
			break;

		case VDMAKEFOURCC('f', 'm', 't', ' '):
			{
				uint32 toread = std::min<uint32>(sizeof(wf), len);

				mStream.Read(&wf, toread);
				len -= toread;

				// validate format
				if (wf.mFormatTag != kWAVE_FORMAT_PCM
					|| (wf.mBitsPerSample != 8 && wf.mBitsPerSample != 16)
					|| (wf.mChannels != 1 && wf.mChannels != 2)
					|| (wf.mBlockAlign != wf.mBitsPerSample * wf.mChannels / 8)
					|| wf.GetSamplesPerSec() < 8000)
				{
					throw MyError("'%ls' uses an unsupported WAV format.", mStream.GetNameForError());
				}

				mFormatInfo.mSamplesPerSec = wf.GetSamplesPerSec();
				mFormatInfo.mChannels = wf.mChannels;
				mBitsPerSample = wf.mBitsPerSample;
				mBytesPerBlock = wf.mBlockAlign;
			}
			break;

		case VDMAKEFOURCC('d', 'a', 't', 'a'):
			datapos = mStream.Pos();
			datalen = len;
			break;
		}

		if (len)
			mStream.Seek(mStream.Pos() + len + (len & 1));
	}

	if (!wf.mBlockAlign || datapos < 0)
		throw MyError("'%ls' is not a valid WAV file.", mStream.GetNameForError());

	mStream.Seek(datapos);

	mFrameCount = datalen / mBytesPerBlock;
	mFramesLeft = mFrameCount;

	if (mBitsPerSample == 8) {
		if (mFormatInfo.mChannels == 1)
			mpReadFn = &ATAudioReaderWAV::ReadM8AsS16;
		else
			mpReadFn = &ATAudioReaderWAV::ReadS8AsS16;
	} else {
		if (mFormatInfo.mChannels == 1)
			mpReadFn = &ATAudioReaderWAV::ReadM16AsS16;
		else
			mpReadFn = &ATAudioReaderWAV::ReadS16AsS16;
	}

	mDataSize = datalen;
	mDataPos = 0;
}

uint64 ATAudioReaderWAV::GetDataSize() const {
	return mDataSize;
}

uint64 ATAudioReaderWAV::GetDataPos() const {
	return mDataPos;
}

uint64 ATAudioReaderWAV::GetFrameCount() const {
	return mFrameCount;
}

ATAudioReadFormatInfo ATAudioReaderWAV::GetFormatInfo() const {
	return mFormatInfo;
}

uint32 ATAudioReaderWAV::ReadStereo16(sint16 *dst, uint32 n) {
	n = std::min<uint32>(mFramesLeft, n);
	mFramesLeft -= n;
	mDataPos += n * mBytesPerBlock;

	(this->*mpReadFn)(dst, n);
	return n;
}

void ATAudioReaderWAV::ReadM8AsS16(sint16 *dst, uint32 count) {
	uint8 buf[1024];

	while(count) {
		uint32 tc = count > 1024 ? 1024 : count;
		count -= tc;

		mStream.Read(buf, tc);

		for(uint32 i=0; i<tc; ++i) {
			dst[0] = dst[1] = (buf[i] << 8) - 0x8000;
			dst += 2;
		}
	}
}

void ATAudioReaderWAV::ReadM16AsS16(sint16 *dst, uint32 count) {
	sint16 buf[1024];

	while(count) {
		uint32 tc = count > 1024 ? 1024 : count;
		count -= tc;

		mStream.Read(buf, tc*2);

		for(uint32 i=0; i<tc; ++i) {
			dst[0] = dst[1] = buf[i];
			dst += 2;
		}
	}
}

void ATAudioReaderWAV::ReadS8AsS16(sint16 *dst, uint32 count) {
	uint8 buf[1024][2];

	while(count) {
		uint32 tc = count > 1024 ? 1024 : count;
		count -= tc;

		mStream.Read(buf, tc*2);

		for(uint32 i=0; i<tc; ++i) {
			dst[0] = (buf[i][0] << 8) - 0x8000;
			dst[1] = (buf[i][1] << 8) - 0x8000;
			dst += 2;
		}
	}
}

void ATAudioReaderWAV::ReadS16AsS16(sint16 *dst, uint32 count) {
	mStream.Read(dst, count*4);
}

////////////////////////////////////////////////////////////////////////////////

IATAudioReader *ATCreateAudioReaderWAV(IVDRandomAccessStream& inputStream) {
	vdautoptr<ATAudioReaderWAV> reader(new ATAudioReaderWAV(inputStream));

	reader->Init();
	return reader.release();
}
