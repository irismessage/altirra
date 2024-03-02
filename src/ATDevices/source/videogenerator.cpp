//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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
#include <vd2/system/math.h>
#include <vd2/system/vectors.h>
#include <at/atcore/devicevideosource.h>
#include <at/atcore/propertyset.h>
#include <at/atdevices/videogenerator.h>

extern const ATDeviceDefinition g_ATDeviceDefVideoGenerator {
	"videogenerator",
	nullptr,
	L"Video generator",
	[](const ATPropertySet& pset, IATDevice** dev) {
		vdrefptr<ATDeviceVideoGenerator> devp(new ATDeviceVideoGenerator);
		
		*dev = devp.release();
	}
};

void *ATDeviceVideoGenerator::AsInterface(uint32 id) {
	if (id == IATDeviceVideoSource::kTypeID)
		return static_cast<IATDeviceVideoSource *>(this);

	return ATDevice::AsInterface(id);
}

void ATDeviceVideoGenerator::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefVideoGenerator;
}

void ATDeviceVideoGenerator::Init() {
	using namespace nsVDMath;

	for(int i=1; i<16; ++i) {
		vdfloat2 pts[3];

		float r = 200.0f * (float)(63 - i) / 64.0f;
		for(int j=0; j<3; ++j) {
			float ang = ((float)j + (float)i / 15.0f) / 3.0f * 2.0f * nsVDMath::kfPi;

			pts[j].x = 320.0f + r * cosf(ang);
			pts[j].y = 240.0f - r * sinf(ang);
		}

		float xminf = std::max(0.0f, std::min(std::min(pts[0].x, pts[1].x), pts[2].x));
		float yminf = std::max(0.0f, std::min(std::min(pts[0].y, pts[1].y), pts[2].y));
		float xmaxf = std::min(640.0f, std::max(std::max(pts[0].x, pts[1].x), pts[2].x));
		float ymaxf = std::min(480.0f, std::max(std::max(pts[0].y, pts[1].y), pts[2].y));

		int xmin = (int)ceilf(xminf + 0.5f);
		int ymin = (int)ceilf(yminf + 0.5f);
		int xmax = (int)ceilf(xmaxf + 0.5f);
		int ymax = (int)ceilf(ymaxf + 0.5f);

		float triv[3];
		float tridx[3];
		float tridy[3];
		vdfloat2 pos0((float)xmin + 0.5f, (float)ymin + 0.5f);

		for(int j=0; j<3; ++j) {
			vdfloat2 edge = pts[j ? j-1 : 2] - pts[j];
			vdfloat2 normal { edge.y, -edge.x };

			normal = -normal;
			triv[j] = dot(normal, pos0 - pts[j]);
			tridx[j] = normal.x;
			tridy[j] = normal.y;
		}

		const uint8 pv = (uint8)(i * 17);
		for(int y=ymin; y<ymax; ++y) {
			float v0 = triv[0];
			float v1 = triv[1];
			float v2 = triv[2];

			uint8 *row = mImage[y];
			for(int x=xmin; x<xmax; ++x) {
				if (v0 >= 0 && v1 >= 0 && v2 >= 0)
					row[x] = pv;

				v0 += tridx[0];
				v1 += tridx[1];
				v2 += tridx[2];
			}

			triv[0] += tridy[0];
			triv[1] += tridy[1];
			triv[2] += tridy[2];
		}
	}
}

uint32 ATDeviceVideoGenerator::ReadVideoSample(float x, int y) const {
	if (y < 0 || y >= 480)
		return 0;

	// Our image is generated with square pixels, so convert from CCIR 601
	// (13.5MHz) to square pixels (12 + 27/99 MHz). 640 square pixels
	// correspond to the center 704 CCIR 601 pixels.
	int xf = (int)((x - 8.0f) * (640.0f / 704.0f) * 256.0f - 128.0f + 0.5f);
	int xi = xf >> 8;

	// return blanking if completely outside of active region
	if (xi < -1 || xi >= 640)
		return 0;

	// interpolate pixel
	const uint8 (&row)[640] = mImage[y];
	int px1;
	int px2;

	if (xi < 0) {
		px1 = 0;
		px2 = row[0];
	} else if (xi < 639) {
		px1 = row[xi];
		px2 = row[xi + 1];
	} else {
		px1 = row[639];
		px2 = 0;
	}

	int px = px1 + (((px2 - px1) * (xf & 0xff) + 0x80) >> 8);

	return px * 0x010101;
}
