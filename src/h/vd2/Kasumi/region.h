//	VirtualDub - Video processing and capture application
//	Graphics support library
//	Copyright (C) 1998-2009 Avery Lee
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

#ifndef f_VD2_KASUMI_REGION_H
#define f_VD2_KASUMI_REGION_H

struct VDPixmap;

#include <vd2/system/vectors.h>
#include <vd2/system/vdstl.h>

class VDPixmapRegion {
public:
	void swap(VDPixmapRegion& x);

public:
	vdfastvector<uint32> mSpans;
	vdrect32	mBounds;
};

// Path rasterizer
//
// Integer coordinates are in 13.3 signed fixed point, while floating point coordinates
// are in pixels. Pixel centers are at half integers (+4 fixed or +0.5 float).
//
// The path rasterizer is winding order agnostic since it uses a nonzero rule. Paths
// must be closed, however, or they will leak.
//
class VDPixmapPathRasterizer {
	VDPixmapPathRasterizer(const VDPixmapPathRasterizer&) = delete;
	VDPixmapPathRasterizer& operator=(const VDPixmapPathRasterizer&) = delete;
public:
	VDPixmapPathRasterizer();
	~VDPixmapPathRasterizer();

	void Clear();
	void QuadraticBezierX(const vdint2 pts[3]);
	void CubicBezierX(const vdint2 pts[4]);
	void CubicBezierF(const vdfloat2 pts[4]);
	void LineX(const vdint2& pt1, const vdint2& pt2);
	void LineF(const vdfloat2& pt1, const vdfloat2& pt2);
	void FastLineX(int x0, int y0, int x1, int y1);

	void RectangleX(const vdint2& p1, const vdint2& p2, bool cw);
	void RectangleF(const vdfloat2& p1, const vdfloat2& p2, bool cw);

	void CircleF(const vdfloat2& center, float r, bool cw);

	void StrokeF(const vdfloat2 *pts, size_t n, float thickness);

	void ScanConvert(VDPixmapRegion& region);

protected:
	void ClearEdgeList();
	void FreeEdgeLists();
	void ClearScanBuffer();
	void ReallocateScanBuffer(int ymin, int ymax);

	struct Edge {
		Edge *next;
		int posandflag;
	};

	enum { kEdgeBlockMax = 1024 };

	struct EdgeBlock {
		EdgeBlock *next;
		Edge edges[1024];

		EdgeBlock(EdgeBlock *p) : next(p) {}
	};

	struct Scan {
		Edge *chain;
		uint32 count;
	};

	EdgeBlock *mpEdgeBlocks;
	EdgeBlock *mpFreeEdgeBlocks;
	int mEdgeBlockIdx;
	Scan *mpScanBuffer;
	int mScanYMin;
	int mScanYMax;
};

bool VDPixmapFillRegion(const VDPixmap& dst, const VDPixmapRegion& region, int x, int y, uint32 color);
bool VDPixmapFillRegionAntialiased8x(const VDPixmap& dst, const VDPixmapRegion& region, int x, int y, uint32 color);

void VDPixmapCreateRoundRegion(VDPixmapRegion& dst, float r);
void VDPixmapConvolveRegion(VDPixmapRegion& dst, const VDPixmapRegion& r1, const VDPixmapRegion& r2);

// Resolve a 4x render to 1x using a box filter with gamma correction, specifically intended
// for antialiased path rendering.
void VDPixmapResolve4x(VDPixmap& dst, const VDPixmap& src4x);

#endif
