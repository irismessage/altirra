//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_DEVICEVIDEOSOURCE_H
#define f_AT_ATCORE_DEVICEVIDEOSOURCE_H

#include <vd2/system/unknown.h>

class IATDeviceVideoSource {
public:
	static constexpr uint32 kTypeID = "IATDeviceVideoSource"_vdtypeid;

	// Read a pixel from the current video frame. The video frame
	// coordinates are in terms of the CCIR 601 interlaced active
	// frame area -- 13.5MHz 720x486, with the first sample at
	// x=0.5 and the last sample at x=719.5. The return is 24-bit sRGB.
	virtual uint32 ReadVideoSample(float x, int y) const = 0;
};

#endif
