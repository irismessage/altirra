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

#ifndef f_AT_AUDIOWRITER_H
#define f_AT_AUDIOWRITER_H

#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include "audiooutput.h"

class ATAudioWriter : public IATAudioTap {
	ATAudioWriter(const ATAudioWriter&);
	ATAudioWriter& operator=(const ATAudioWriter&);
public:
	ATAudioWriter(const wchar_t *filename);
	~ATAudioWriter();

	void CheckExceptions();
	void WriteRawAudio(const float *left, const float *right, uint32 count, uint32 timestamp);
	
protected:
	void WriteInterleaved(const float *left, const float *right, uint32 count);

	bool mbErrorState;
	VDFile mFile;
	MyError mError;
};

#endif	// f_AT_AUDIOWRITER_H
