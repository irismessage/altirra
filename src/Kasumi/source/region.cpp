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

#include <stdafx.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/region.h>
#include <vd2/system/math.h>
#include <vd2/system/vdstl.h>

void VDPixmapRegion::swap(VDPixmapRegion& x) {
	mSpans.swap(x.mSpans);
	std::swap(mBounds, x.mBounds);
}

VDPixmapPathRasterizer::VDPixmapPathRasterizer()
	: mpEdgeBlocks(NULL)
	, mpFreeEdgeBlocks(NULL)
	, mEdgeBlockIdx(kEdgeBlockMax)
	, mpScanBuffer(NULL)
{
	ClearScanBuffer();
}

VDPixmapPathRasterizer::~VDPixmapPathRasterizer() {
	Clear();
	FreeEdgeLists();
}

void VDPixmapPathRasterizer::Clear() {
	ClearEdgeList();
	ClearScanBuffer();
}

void VDPixmapPathRasterizer::QuadraticBezierX(const vdint2 *pts) {
	int x0 = pts[0].x;
	int x1 = pts[1].x;
	int x2 = pts[2].x;
	int y0 = pts[0].y;
	int y1 = pts[1].y;
	int y2 = pts[2].y;

	// P = (1-t)^2*P0 + 2t(1-t)*P1 + t^2*P2
	// P = (1-2t+t^2)P0 + 2(t-t^2)P1 + t^2*P2
	// P = (P0-2P1+P2)t^2 + 2(P1-P0)t + P0

	int cx2 =    x0-2*x1+x2;
	int cx1 = -2*x0+2*x1;
	int cx0 =    x0;

	int cy2 =    y0-2*y1+y2;
	int cy1 = -2*y0+2*y1;
	int cy0 =    y0;

	// This equation is from Graphics Gems I.
	//
	// The idea is that since we're approximating a cubic curve with lines,
	// any error we incur is due to the curvature of the line, which we can
	// estimate by calculating the maximum acceleration of the curve.  For
	// a cubic, the acceleration (second derivative) is a line, meaning that
	// the absolute maximum acceleration must occur at either the beginning
	// (|c2|) or the end (|c2+c3|).  Our bounds here are a little more
	// conservative than that, but that's okay.
	//
	// If the acceleration of the parametric formula is zero (c2 = c3 = 0),
	// that component of the curve is linear and does not incur any error.
	// If a=0 for both X and Y, the curve is a line segment and we can
	// use a step size of 1.

	int maxaccel1 = abs(cy2);
	int maxaccel2 = abs(cx2);

	int maxaccel = maxaccel1 > maxaccel2 ? maxaccel1 : maxaccel2;
	int h = 1;

	while(maxaccel > 8 && h < 1024) {
		maxaccel >>= 2;
		h += h;
	}

	int lastx = x0;
	int lasty = y0;

	// compute forward differences
	sint64 h1 = (sint64)(0x40000000 / h) << 2;
	sint64 h2 = h1/h;

	sint64 ax0 = (sint64)cx0 << 32;
	sint64 ax1 =   h1*(sint64)cx1 +   h2*(sint64)cx2;
	sint64 ax2 = 2*h2*(sint64)cx2;

	sint64 ay0 = (sint64)cy0 << 32;
	sint64 ay1 =   h1*(sint64)cy1 +   h2*(sint64)cy2;
	sint64 ay2 = 2*h2*(sint64)cy2;

	// round, not truncate
	ax0 += 0x80000000;
	ay0 += 0x80000000;

	do {
		ax0 += ax1;
		ax1 += ax2;
		ay0 += ay1;
		ay1 += ay2;

		int xi = (int)((uint64)ax0 >> 32);
		int yi = (int)((uint64)ay0 >> 32);

		FastLineX(lastx, lasty, xi, yi);
		lastx = xi;
		lasty = yi;
	} while(--h);
}

void VDPixmapPathRasterizer::CubicBezierX(const vdint2 *pts) {
	int x0 = pts[0].x;
	int x1 = pts[1].x;
	int x2 = pts[2].x;
	int x3 = pts[3].x;
	int y0 = pts[0].y;
	int y1 = pts[1].y;
	int y2 = pts[2].y;
	int y3 = pts[3].y;

	int cx3 = -  x0+3*x1-3*x2+x3;
	int cx2 =  3*x0-6*x1+3*x2;
	int cx1 = -3*x0+3*x1;
	int cx0 =    x0;

	int cy3 = -  y0+3*y1-3*y2+y3;
	int cy2 =  3*y0-6*y1+3*y2;
	int cy1 = -3*y0+3*y1;
	int cy0 =    y0;

	// This equation is from Graphics Gems I.
	//
	// The idea is that since we're approximating a cubic curve with lines,
	// any error we incur is due to the curvature of the line, which we can
	// estimate by calculating the maximum acceleration of the curve.  For
	// a cubic, the acceleration (second derivative) is a line, meaning that
	// the absolute maximum acceleration must occur at either the beginning
	// (|c2|) or the end (|c2+c3|).  Our bounds here are a little more
	// conservative than that, but that's okay.
	//
	// If the acceleration of the parametric formula is zero (c2 = c3 = 0),
	// that component of the curve is linear and does not incur any error.
	// If a=0 for both X and Y, the curve is a line segment and we can
	// use a step size of 1.

	int maxaccel1 = abs(2*cy2) + abs(6*cy3);
	int maxaccel2 = abs(2*cx2) + abs(6*cx3);

	int maxaccel = maxaccel1 > maxaccel2 ? maxaccel1 : maxaccel2;
	int h = 1;

	while(maxaccel > 8 && h < 1024) {
		maxaccel >>= 2;
		h += h;
	}

	int lastx = x0;
	int lasty = y0;

	// compute forward differences
	sint64 h1 = (sint64)(0x40000000 / h) << 2;
	sint64 h2 = h1/h;
	sint64 h3 = h2/h;

	sint64 ax0 = (sint64)cx0 << 32;
	sint64 ax1 =   h1*(sint64)cx1 +   h2*(sint64)cx2 + h3*(sint64)cx3;
	sint64 ax2 = 2*h2*(sint64)cx2 + 6*h3*(sint64)cx3;
	sint64 ax3 = 6*h3*(sint64)cx3;

	sint64 ay0 = (sint64)cy0 << 32;
	sint64 ay1 =   h1*(sint64)cy1 +   h2*(sint64)cy2 + h3*(sint64)cy3;
	sint64 ay2 = 2*h2*(sint64)cy2 + 6*h3*(sint64)cy3;
	sint64 ay3 = 6*h3*(sint64)cy3;

	// round, not truncate
	ax0 += 0x80000000;
	ay0 += 0x80000000;

	do {
		ax0 += ax1;
		ax1 += ax2;
		ax2 += ax3;
		ay0 += ay1;
		ay1 += ay2;
		ay2 += ay3;

		int xi = (int)((uint64)ax0 >> 32);
		int yi = (int)((uint64)ay0 >> 32);

		FastLineX(lastx, lasty, xi, yi);
		lastx = xi;
		lasty = yi;
	} while(--h);
}

void VDPixmapPathRasterizer::CubicBezierF(const vdfloat2 pts[4]) {
	vdint2 ptx[4] {
		vdint2{VDRoundToInt(pts[0].x * 8.0f), VDRoundToInt(pts[0].y * 8.0f)},
		vdint2{VDRoundToInt(pts[1].x * 8.0f), VDRoundToInt(pts[1].y * 8.0f)},
		vdint2{VDRoundToInt(pts[2].x * 8.0f), VDRoundToInt(pts[2].y * 8.0f)},
		vdint2{VDRoundToInt(pts[3].x * 8.0f), VDRoundToInt(pts[3].y * 8.0f)},
	};

	CubicBezierX(ptx);
}

void VDPixmapPathRasterizer::LineX(const vdint2& pt1, const vdint2& pt2) {
	FastLineX(pt1.x, pt1.y, pt2.x, pt2.y);
}

void VDPixmapPathRasterizer::LineF(const vdfloat2& pt1, const vdfloat2& pt2) {
	FastLineX(
		VDRoundToInt(pt1.x * 8.0f),
		VDRoundToInt(pt1.y * 8.0f),
		VDRoundToInt(pt2.x * 8.0f),
		VDRoundToInt(pt2.y * 8.0f)
	);
}

void VDPixmapPathRasterizer::FastLineX(int x0, int y0, int x1, int y1) {
	int flag = 1;

	if (y1 == y0)
		return;

	if (y1 < y0) {
		int t;

		t=x0; x0=x1; x1=t;
		t=y0; y0=y1; y1=t;
		flag = 0;
	}

	int dy = y1-y0;
	int xacc = x0<<13;

	// prestep y0 down
	int iy0 = (y0+3) >> 3;
	int iy1 = (y1+3) >> 3;

	if (iy0 < iy1) {
		int invslope = (x1-x0)*65536/dy;

		int prestep = (4-y0) & 7;
		xacc += (invslope * prestep)>>3;

		if (iy0 < mScanYMin || iy1 > mScanYMax) {
			ReallocateScanBuffer(iy0, iy1);
			VDASSERT(iy0 >= mScanYMin && iy1 <= mScanYMax);
		}
		
		VDASSERT(iy0 >= mScanYMin && iy1 <= mScanYMax);

		const auto ymin = mScanYMin;
		while(iy0 < iy1) {
			int ix = (xacc+32767)>>16;

			if (mEdgeBlockIdx >= kEdgeBlockMax) {
				if (mpFreeEdgeBlocks) {
					EdgeBlock *newBlock = mpFreeEdgeBlocks;
					mpFreeEdgeBlocks = mpFreeEdgeBlocks->next;
					newBlock->next = mpEdgeBlocks;
					mpEdgeBlocks = newBlock;
				} else {
					mpEdgeBlocks = new EdgeBlock(mpEdgeBlocks);
				}

				mEdgeBlockIdx = 0;
			}
			
			Edge& e = mpEdgeBlocks->edges[mEdgeBlockIdx];
			Scan& s = mpScanBuffer[iy0 - ymin];
			++mEdgeBlockIdx;

			e.posandflag = ix*2+flag;
			e.next = s.chain;
			s.chain = &e;
			++s.count;

			++iy0;
			xacc += invslope;
		}
	}
}

void VDPixmapPathRasterizer::RectangleX(const vdint2& p1, const vdint2& p2, bool cw) {
	if (cw) {
		FastLineX(p1.x, p1.y, p2.x, p1.y);
		FastLineX(p2.x, p1.y, p2.x, p2.y);
		FastLineX(p2.x, p2.y, p1.x, p2.y);
		FastLineX(p1.x, p2.y, p1.x, p1.y);
	} else {
		FastLineX(p1.x, p1.y, p1.x, p2.y);
		FastLineX(p1.x, p2.y, p2.x, p2.y);
		FastLineX(p2.x, p2.y, p2.x, p1.y);
		FastLineX(p2.x, p1.y, p1.x, p1.y);
	}
}

void VDPixmapPathRasterizer::RectangleF(const vdfloat2& p1, const vdfloat2& p2, bool cw) {
	const int x1 = VDRoundToInt(p1.x * 8.0f);
	const int y1 = VDRoundToInt(p1.y * 8.0f);
	const int x2 = VDRoundToInt(p2.x * 8.0f);
	const int y2 = VDRoundToInt(p2.y * 8.0f);

	if (cw) {
		FastLineX(x1, y1, x2, y1);
		FastLineX(x2, y1, x2, y2);
		FastLineX(x2, y2, x1, y2);
		FastLineX(x1, y2, x1, y1);
	} else {
		FastLineX(x1, y1, x1, y2);
		FastLineX(x1, y2, x2, y2);
		FastLineX(x2, y2, x2, y1);
		FastLineX(x2, y1, x1, y1);
	}
}

void VDPixmapPathRasterizer::CircleF(const vdfloat2& center, float r, bool cw) {
	// 4/3*(sqrt(2)-1)
	const float r2 = r * 0.5522847498307933984022516322796f;

	const float cx = center.x;
	const float cy = center.y;
	vdfloat2 pts[] {
		vdfloat2{ cx + r, cy },
		vdfloat2{ cx + r, cy - r2 },
		vdfloat2{ cx + r2, cy - r },
		vdfloat2{ cx, cy - r },
		vdfloat2{ cx - r2, cy - r },
		vdfloat2{ cx - r, cy - r2 },
		vdfloat2{ cx - r, cy },
		vdfloat2{ cx - r, cy + r2 },
		vdfloat2{ cx - r2, cy + r },
		vdfloat2{ cx, cy + r },
		vdfloat2{ cx + r2, cy + r },
		vdfloat2{ cx + r, cy + r2 },
		vdfloat2{ cx + r, cy },
	};

	if (cw)
		std::reverse(std::begin(pts), std::end(pts));

	CubicBezierF(pts + 0);
	CubicBezierF(pts + 3);
	CubicBezierF(pts + 6);
	CubicBezierF(pts + 9);
}

void VDPixmapPathRasterizer::StrokeF(const vdfloat2 *pts, size_t n, float thickness) {
	using namespace nsVDMath;

	if (!n)
		return;

	vdfastvector<vdfloat2> filteredPts;
	filteredPts.reserve(n);

	if (n > 1) {
		vdfloat2 lastPt = pts[n-1];
		for(vdfloat2 pt : vdspan<const vdfloat2>(pts, n)) {
			if ((pt - lastPt).lensq() > 1e-5f)
				filteredPts.push_back(pt);

			lastPt = pt;
		}
	} else
		filteredPts.push_back(*pts);

	if (filteredPts.size() == 1) {
		RectangleF(filteredPts.front() - vdfloat2{thickness, thickness}, filteredPts.front() + vdfloat2{thickness, thickness}, false);
		return;
	}

	const float halfThick = thickness * 0.5f;
	vdfloat2 pt0 = filteredPts[0];
	vdfloat2 pt1 = filteredPts[1];
	vdfloat2 n01 = rot(normalize(pt1 - pt0));

	vdfloat2 h0 = pt0 - n01 * halfThick;
	vdfloat2 l0 = pt0 + n01 * halfThick;

	LineF(l0, h0);

	for(vdfloat2 pt2 : vdspan<vdfloat2>(filteredPts.begin()+2, filteredPts.end())) {
		vdfloat2 n12 = rot(normalize(pt2 - pt1));

		vdfloat2 h1a = pt1 - n01*halfThick;
		vdfloat2 l1a = pt1 + n01*halfThick;
		vdfloat2 h1b = pt1 - n12*halfThick;
		vdfloat2 l1b = pt1 + n12*halfThick;

		LineF(h0, h1a);
		LineF(h1a, h1b);
		LineF(l1b, l1a);
		LineF(l1a, l0);

		h0 = h1b;
		l0 = l1b;
		
		pt1 = pt2;
		n01 = n12;
	}

	vdfloat2 h2 = pt1 - n01*halfThick;
	vdfloat2 l2 = pt1 + n01*halfThick;

	LineF(h0, h2);
	LineF(h2, l2);
	LineF(l2, l0);
}

void VDPixmapPathRasterizer::ScanConvert(VDPixmapRegion& region) {
	// Convert the edges to spans.  We couldn't do this before because some of
	// the regions may have winding numbers >+1 and it would have been a pain
	// to try to adjust the spans on the fly.  We use one heap to detangle
	// a scanline's worth of edges from the singly-linked lists, and another
	// to collect the actual scans.
	vdfastvector<int> heap;

	region.mSpans.clear();
	int xmin = INT_MAX;
	int xmax = INT_MIN;
	int ymin = INT_MAX;
	int ymax = INT_MIN;

	for(int y=mScanYMin; y<mScanYMax; ++y) {
		uint32 flipcount = mpScanBuffer[y - mScanYMin].count;

		if (!flipcount)
			continue;

		// Keep the edge heap from doing lots of stupid little reallocates.
		if (heap.capacity() < flipcount)
			heap.resize((flipcount + 63)&~63);

		// Detangle scanline into edge heap.
		int *heap0 = heap.data();
		int *heap1 = heap0;
		for(const Edge *ptr = mpScanBuffer[y - mScanYMin].chain; ptr; ptr = ptr->next)
			*heap1++ = ptr->posandflag;

		VDASSERT(heap1 - heap0 == flipcount);

		// Sort edge heap.  Note that we conveniently made the opening edges
		// one more than closing edges at the same spot, so we won't have any
		// problems with abutting spans.

		std::sort(heap0, heap1);

#if 0
		while(heap0 != heap1) {
			int x = *heap0++ >> 1;
			region.mSpans.push_back((y<<16) + x + 0x80008000);
			region.mSpans.push_back((y<<16) + x + 0x80008001);
		}
		continue;
#endif

		// Trim any odd edges off, since we can never close on one.
		if (flipcount & 1)
			--heap1;

		// Process edges and add spans.  Since we only check for a non-zero
		// winding number, it doesn't matter which way the outlines go. Also, since
		// the parity always flips after each edge regardless of direction, we can
		// process the edges in pairs.

		size_t spanstart = region.mSpans.size();

		int x_left;
		int count = 0;
		while(heap0 != heap1) {
			int x = *heap0++;

			if (!count)
				x_left = (x>>1);

			count += (x&1);

			x = *heap0++;

			count += (x&1);

			if (!--count) {
				int x_right = (x>>1);

				if (x_right > x_left) {
					region.mSpans.push_back((y<<16) + x_left  + 0x80008000);
					region.mSpans.push_back((y<<16) + x_right + 0x80008000);

				}
			}
		}

		size_t spanend = region.mSpans.size();

		if (spanend > spanstart) {
			if (ymin > y)
				ymin = y;

			if (ymax < y)
				ymax = y;

			int x1 = (region.mSpans[spanstart] & 0xffff) - 0x8000;
			int x2 = (region.mSpans[spanend-1] & 0xffff) - 0x8000;

			if (xmin > x1)
				xmin = x1;

			if (xmax < x2)
				xmax = x2;
		}
	}

	if (xmax > xmin) {
		region.mBounds.set(xmin, ymin, xmax, ymax);
	} else {
		region.mBounds.set(0, 0, 0, 0);
	}

	// Dump the edge and scan buffers, since we no longer need them.
	ClearEdgeList();
	ClearScanBuffer();
}

void VDPixmapPathRasterizer::ClearEdgeList() {
	if (mpEdgeBlocks) {
		EdgeBlock *block = mpEdgeBlocks;
		
		while(EdgeBlock *next = block->next)
			block = next;

		block->next = mpFreeEdgeBlocks;
		mpFreeEdgeBlocks = mpEdgeBlocks;
		mpEdgeBlocks = NULL;
	}

	mEdgeBlockIdx = kEdgeBlockMax;
}

void VDPixmapPathRasterizer::FreeEdgeLists() {
	ClearEdgeList();

	while(EdgeBlock *block = mpFreeEdgeBlocks) {
		mpFreeEdgeBlocks = block->next;

		delete block;
	}
}

void VDPixmapPathRasterizer::ClearScanBuffer() {
	delete[] mpScanBuffer;
	mpScanBuffer = nullptr;
	mScanYMin = 0;
	mScanYMax = 0;
}

void VDPixmapPathRasterizer::ReallocateScanBuffer(int ymin, int ymax) {
	// 
	// check if there actually is a scan buffer to avoid unintentionally pinning at zero
	if (mpScanBuffer) {
		int nicedelta = (mScanYMax - mScanYMin);

		if (ymin < mScanYMin) {
			int yminnice = mScanYMin - nicedelta;
			if (ymin > yminnice)
				ymin = yminnice;

			ymin &= ~31;
		} else
			ymin = mScanYMin;

		if (ymax > mScanYMax) {
			int ymaxnice = mScanYMax + nicedelta;
			if (ymax < ymaxnice)
				ymax = ymaxnice;

			ymax = (ymax + 31) & ~31;
		} else
			ymax = mScanYMax;

		VDASSERT(ymin <= mScanYMin && ymax >= mScanYMax);
	}

	// reallocate scan buffer
	Scan *pNewBuffer = new Scan[ymax - ymin];

	if (mpScanBuffer) {
		memcpy(pNewBuffer + (mScanYMin - ymin), mpScanBuffer, (mScanYMax - mScanYMin) * sizeof(Scan));
		delete[] mpScanBuffer;

		// zero new areas of scan buffer
		for(int y=ymin; y<mScanYMin; ++y) {
			pNewBuffer[y - ymin] = Scan();
		}

		for(int y=mScanYMax; y<ymax; ++y) {
			pNewBuffer[y - ymin] = Scan();
		}
	} else {
		for(int y=ymin; y<ymax; ++y) {
			pNewBuffer[y - ymin] = Scan();
		}
	}

	mpScanBuffer = pNewBuffer;
	mScanYMin = ymin;
	mScanYMax = ymax;
}

bool VDPixmapFillRegion(const VDPixmap& dst, const VDPixmapRegion& region, int x, int y, uint32 color) {
	if (dst.format != nsVDPixmap::kPixFormat_XRGB8888)
		return false;

	// fast out
	if (region.mSpans.empty())
		return true;

	// check if vertical clipping is required
	const size_t n = region.mSpans.size();
	uint32 start = 0;
	uint32 end = n;

	uint32 spanmin = (-x) + ((-y) << 16) + 0x80008000;

	if (region.mSpans.front() < spanmin) {
		uint32 lo = 0, hi = n;

		// compute top clip
		while(lo < hi) {
			int mid = ((lo + hi) >> 1) & ~1;

			if (region.mSpans[mid + 1] < spanmin)
				lo = mid + 2;
			else
				hi = mid;
		}

		start = lo;

		// check for total top clip
		if (start >= n)
			return true;
	}

	uint32 spanlimit = (dst.w - x) + ((dst.h - y - 1) << 16) + 0x80008000;

	if (region.mSpans.back() > spanlimit) {
		// compute bottom clip
		int lo = start;
		int hi = n;

		while(lo < hi) {
			int mid = ((lo + hi) >> 1) & ~1;

			if (region.mSpans[mid] >= spanlimit)
				hi = mid;
			else
				lo = mid+2;
		}

		end = lo;

		// check for total bottom clip
		if (start >= end)
			return true;
	}

	// fill region
	const uint32 *pSpan = &region.mSpans[start];
	const uint32 *pEnd  = &region.mSpans[0] + end;
	int lasty = -1;
	uint32 *dstp = nullptr;

	for(; pSpan != pEnd; pSpan += 2) {
		uint32 span0 = pSpan[0];
		uint32 span1 = pSpan[1];

		uint32 py = (span0 >> 16) - 0x8000 + y;
		uint32 px = (span0 & 0xffff) - 0x8000 + x;
		uint32 w = span1-span0;

		VDASSERT(py < (uint32)dst.h);
		VDASSERT(px < (uint32)dst.w);
		VDASSERT(dst.w - (int)px >= (int)w);

		if (lasty != py)
			dstp = (uint32 *)vdptroffset(dst.data, dst.pitch * py);

		uint32 *p = dstp + px;
		do {
			*p++ = color;
		} while(--w);
	}

	return true;
}

namespace {
	void RenderABuffer32(const VDPixmap& dst, int y, const uint8 *alpha, uint32 w, uint32 color) {
		if (!w)
			return;

		// update dest pointer
		uint32 *dstp = (uint32 *)vdptroffset(dst.data, dst.pitch * y);

		const uint32 color_rb = color & 0x00FF00FF;
		const uint32 color_g  = color & 0x0000FF00;
		do {
			const uint32 px = *dstp;
			const uint32 px_rb = px & 0x00FF00FF;
			const uint32 px_g  = px & 0x0000FF00;
			const sint32 a     = *alpha++;

			const uint32 result_rb = (((px_rb << 6) + ((sint32)(color_rb - px_rb)*a + 0x00200020)) & 0x3FC03FC0);
			const uint32 result_g  = (((px_g  << 6) + ((sint32)(color_g  - px_g )*a + 0x00002000)) & 0x003FC000);

			*dstp++ = (result_rb + result_g) >> 6;
		} while(--w);
	}

	void RenderABuffer8(const VDPixmap& dst, int y, const uint8 *alpha, uint32 w, uint32 color) {
		if (!w)
			return;

		// update dest pointer
		uint8 *dstp = (uint8 *)vdptroffset(dst.data, dst.pitch * y);

		do {
			const uint8 px = *dstp;
			const sint8 a = *alpha++;

			*dstp++ = px + (((sint32)(color - px) * a + 32) >> 6);
		} while(--w);
	}

	void RenderABuffer8_128(const VDPixmap& dst, int y, const uint8 *alpha, uint32 w, uint32 color) {
		if (!w)
			return;

		// update dest pointer
		uint8 *dstp = (uint8 *)vdptroffset(dst.data, dst.pitch * y);

		do {
			const uint8 px = *dstp;
			const sint16 a = *alpha++;

			*dstp++ = px + (((sint32)(color - px) * a + 64) >> 7);
		} while(--w);
	}

	void RenderABuffer8_256(const VDPixmap& dst, int y, const uint16 *alpha, uint32 w, uint32 color) {
		if (!w)
			return;

		// update dest pointer
		uint8 *dstp = (uint8 *)vdptroffset(dst.data, dst.pitch * y);

		do {
			const uint8 px = *dstp;
			const sint32 a = *alpha++;

			*dstp++ = px + (((sint32)(color - px) * a + 128) >> 8);
		} while(--w);
	}

	void RenderABuffer8_1024(const VDPixmap& dst, int y, const uint16 *alpha, uint32 w, uint32 color) {
		if (!w)
			return;

		// update dest pointer
		uint8 *dstp = (uint8 *)vdptroffset(dst.data, dst.pitch * y);

		do {
			const uint8 px = *dstp;
			const sint32 a = *alpha++;

			*dstp++ = px + (((sint32)(color - px) * a + 512) >> 10);
		} while(--w);
	}
}

bool VDPixmapFillRegionAntialiased_32x_32x(const VDPixmap& dst, const VDPixmapRegion& region, int x, int y, uint32 color) {
	if (dst.format != nsVDPixmap::kPixFormat_Y8)
		return false;

	// fast out
	if (region.mSpans.empty())
		return true;

	// check if vertical clipping is required
	const size_t n = region.mSpans.size();
	uint32 start = 0;
	uint32 end = n;

	uint32 spanmin = -x + (-y << 16) + 0x80008000;

	if (region.mSpans.front() < spanmin) {
		// find first span : x2 > spanmin
		start = std::upper_bound(region.mSpans.begin(), region.mSpans.end(), spanmin) - region.mSpans.begin();
		start &= ~1;

		// check for total top clip
		if (start >= n)
			return true;
	}

	uint32 spanlimit = (dst.w*32 - x) + (((dst.h*32 - y) - 1) << 16) + 0x80008000;

	if (region.mSpans.back() > spanlimit) {
		// find last span : x1 < spanlimit
		end = std::lower_bound(region.mSpans.begin(), region.mSpans.end(), spanlimit) - region.mSpans.begin();

		end = (end + 1) & ~1;

		// check for total bottom clip
		if (start >= end)
			return true;
	}

	// allocate A-buffer
	vdfastvector<uint16> abuffer(dst.w, 0);

	// fill region
	const uint32 *pSpan = &region.mSpans[start];
	const uint32 *pEnd  = &region.mSpans[0] + end;
	int lasty = -1;

	sint32 dstw32 = dst.w * 32;
	sint32 dsth32 = dst.h * 32;

	for(; pSpan != pEnd; pSpan += 2) {
		uint32 span0 = pSpan[0];
		uint32 span1 = pSpan[1];

		sint32 py = (span0 >> 16) - 0x8000 + y;

		if ((uint32)py >= (uint32)dsth32)
			continue;

		sint32 px1 = (span0 & 0xffff) - 0x8000 + x;
		sint32 px2 = (span1 & 0xffff) - 0x8000 + x;

		if (lasty != py) {
			if (((lasty ^ py) & 0xFFFFFFE0)) {
				if (lasty >= 0) {
					// flush scanline

					RenderABuffer8_1024(dst, lasty >> 5, abuffer.data(), dst.w, color);
				}

				memset(abuffer.data(), 0, abuffer.size() * sizeof(abuffer[0]));
			}
			lasty = py;
		}

		if (px1 < 0)
			px1 = 0;
		if (px2 > dstw32)
			px2 = dstw32;

		if (px1 >= px2)
			continue;

		uint32 ix1 = px1 >> 5;
		uint32 ix2 = px2 >> 5;
		uint16 *p1 = abuffer.data() + ix1;
		uint16 *p2 = abuffer.data() + ix2;

		if (p1 == p2) {
			p1[0] += (px2 - px1);
		} else {
			if (px1 & 31) {
				p1[0] += 32 - (px1 & 31);
				++p1;
			}

			while(p1 != p2) {
				p1[0] += 32;
				++p1;
			}

			if (px2 & 31)
				p1[0] += px2 & 32;
		}
	}

	if (lasty >= 0)
		RenderABuffer8_1024(dst, lasty >> 5, abuffer.data(), dst.w, color);

	return true;
}

bool VDPixmapFillRegionAntialiased_16x_16x(const VDPixmap& dst, const VDPixmapRegion& region, int x, int y, uint32 color) {
	if (dst.format != nsVDPixmap::kPixFormat_Y8)
		return false;

	// fast out
	if (region.mSpans.empty())
		return true;

	// check if vertical clipping is required
	const size_t n = region.mSpans.size();
	uint32 start = 0;
	uint32 end = n;

	uint32 spanmin = -x + (-y << 16) + 0x80008000;

	if (region.mSpans.front() < spanmin) {
		// find first span : x2 > spanmin
		start = std::upper_bound(region.mSpans.begin(), region.mSpans.end(), spanmin) - region.mSpans.begin();
		start &= ~1;

		// check for total top clip
		if (start >= n)
			return true;
	}

	uint32 spanlimit = (dst.w*16 - x) + (((dst.h*16 - y) - 1) << 16) + 0x80008000;

	if (region.mSpans.back() > spanlimit) {
		// find last span : x1 < spanlimit
		end = std::lower_bound(region.mSpans.begin(), region.mSpans.end(), spanlimit) - region.mSpans.begin();

		end = (end + 1) & ~1;

		// check for total bottom clip
		if (start >= end)
			return true;
	}

	// allocate A-buffer
	vdfastvector<uint16> abuffer(dst.w, 0);

	// fill region
	const uint32 *pSpan = &region.mSpans[start];
	const uint32 *pEnd  = &region.mSpans[0] + end;
	int lasty = -1;

	sint32 dstw16 = dst.w * 16;
	sint32 dsth16 = dst.h * 16;

	for(; pSpan != pEnd; pSpan += 2) {
		uint32 span0 = pSpan[0];
		uint32 span1 = pSpan[1];

		sint32 py = (span0 >> 16) - 0x8000 + y;

		if ((uint32)py >= (uint32)dsth16)
			continue;

		sint32 px1 = (span0 & 0xffff) - 0x8000 + x;
		sint32 px2 = (span1 & 0xffff) - 0x8000 + x;

		if (lasty != py) {
			if (((lasty ^ py) & 0xFFFFFFF0)) {
				if (lasty >= 0) {
					// flush scanline

					RenderABuffer8_256(dst, lasty >> 4, abuffer.data(), dst.w, color);
				}

				memset(abuffer.data(), 0, abuffer.size() * sizeof(abuffer[0]));
			}
			lasty = py;
		}

		if (px1 < 0)
			px1 = 0;
		if (px2 > dstw16)
			px2 = dstw16;

		if (px1 >= px2)
			continue;

		uint32 ix1 = px1 >> 4;
		uint32 ix2 = px2 >> 4;
		uint16 *p1 = abuffer.data() + ix1;
		uint16 *p2 = abuffer.data() + ix2;

		if (p1 == p2) {
			p1[0] += (px2 - px1);
		} else {
			if (px1 & 15) {
				p1[0] += 16 - (px1 & 15);
				++p1;
			}

			while(p1 != p2) {
				p1[0] += 16;
				++p1;
			}

			if (px2 & 15)
				p1[0] += px2 & 15;
		}
	}

	if (lasty >= 0)
		RenderABuffer8_256(dst, lasty >> 4, abuffer.data(), dst.w, color);

	return true;
}

bool VDPixmapFillRegionAntialiased_16x_8x(const VDPixmap& dst, const VDPixmapRegion& region, int x, int y, uint32 color) {
	if (dst.format != nsVDPixmap::kPixFormat_XRGB8888 && dst.format != nsVDPixmap::kPixFormat_Y8)
		return false;

	// fast out
	if (region.mSpans.empty())
		return true;

	// check if vertical clipping is required
	const size_t n = region.mSpans.size();
	uint32 start = 0;
	uint32 end = n;

	uint32 spanmin = -x + (-y << 16) + 0x80008000;

	if (region.mSpans.front() < spanmin) {
		// find first span : x2 > spanmin
		start = std::upper_bound(region.mSpans.begin(), region.mSpans.end(), spanmin) - region.mSpans.begin();
		start &= ~1;

		// check for total top clip
		if (start >= n)
			return true;
	}

	uint32 spanlimit = (dst.w*16 - x) + (((dst.h*8 - y) - 1) << 16) + 0x80008000;

	if (region.mSpans.back() > spanlimit) {
		// find last span : x1 < spanlimit
		end = std::lower_bound(region.mSpans.begin(), region.mSpans.end(), spanlimit) - region.mSpans.begin();

		end = (end + 1) & ~1;

		// check for total bottom clip
		if (start >= end)
			return true;
	}

	// allocate A-buffer
	vdfastvector<uint8> abuffer(dst.w, 0);

	// fill region
	const uint32 *pSpan = &region.mSpans[start];
	const uint32 *pEnd  = &region.mSpans[0] + end;
	int lasty = -1;

	sint32 dstw16 = dst.w * 16;
	sint32 dsth8 = dst.h * 8;

	for(; pSpan != pEnd; pSpan += 2) {
		uint32 span0 = pSpan[0];
		uint32 span1 = pSpan[1];

		sint32 py = (span0 >> 16) - 0x8000 + y;

		if ((uint32)py >= (uint32)dsth8)
			continue;

		sint32 px1 = (span0 & 0xffff) - 0x8000 + x;
		sint32 px2 = (span1 & 0xffff) - 0x8000 + x;

		if (lasty != py) {
			if (((lasty ^ py) & 0xFFFFFFF8)) {
				if (lasty >= 0) {
					// flush scanline

					RenderABuffer8_128(dst, lasty >> 3, abuffer.data(), dst.w, color);
				}

				memset(abuffer.data(), 0, abuffer.size());
			}
			lasty = py;
		}

		if (px1 < 0)
			px1 = 0;
		if (px2 > dstw16)
			px2 = dstw16;

		if (px1 >= px2)
			continue;

		uint32 ix1 = px1 >> 4;
		uint32 ix2 = px2 >> 4;
		uint8 *p1 = abuffer.data() + ix1;
		uint8 *p2 = abuffer.data() + ix2;

		if (p1 == p2) {
			p1[0] += (px2 - px1);
		} else {
			if (px1 & 15) {
				p1[0] += 16 - (px1 & 15);
				++p1;
			}

			while(p1 != p2) {
				p1[0] += 16;
				++p1;
			}

			if (px2 & 15)
				p1[0] += px2 & 15;
		}
	}

	if (lasty >= 0)
		RenderABuffer8_128(dst, lasty >> 3, abuffer.data(), dst.w, color);

	return true;
}

bool VDPixmapFillRegionAntialiased8x(const VDPixmap& dst, const VDPixmapRegion& region, int x, int y, uint32 color) {
	switch(dst.format) {
	case nsVDPixmap::kPixFormat_YUV444_Planar:
	case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
	case nsVDPixmap::kPixFormat_YUV444_Planar_709:
	case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
	case nsVDPixmap::kPixFormat_YUV422_Planar:
	case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
	case nsVDPixmap::kPixFormat_YUV422_Planar_709:
	case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
	case nsVDPixmap::kPixFormat_YUV420_Planar:
	case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
	case nsVDPixmap::kPixFormat_YUV420_Planar_709:
	case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
		{
			VDPixmap pxY;
			VDPixmap pxCb;
			VDPixmap pxCr;

			pxY.format = nsVDPixmap::kPixFormat_Y8;
			pxY.data = dst.data;
			pxY.pitch = dst.pitch;
			pxY.w = dst.w;
			pxY.h = dst.h;

			pxCb.format = nsVDPixmap::kPixFormat_Y8;
			pxCb.data = dst.data2;
			pxCb.pitch = dst.pitch2;
			pxCb.w = dst.w;
			pxCb.h = dst.h;

			pxCr.format = nsVDPixmap::kPixFormat_Y8;
			pxCr.data = dst.data3;
			pxCr.pitch = dst.pitch3;
			pxCr.w = dst.w;
			pxCr.h = dst.h;

			uint32 colorY = (color >> 8) & 0xff;
			uint32 colorCb = (color >> 0) & 0xff;
			uint32 colorCr = (color >> 16) & 0xff;

			VDPixmapFillRegionAntialiased8x(pxY, region, x, y, colorY);

			switch(dst.format) {
			case nsVDPixmap::kPixFormat_YUV420_Planar:
			case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV420_Planar_709:
			case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
				pxCr.w = pxCb.w = dst.w >> 1;
				pxCr.h = pxCb.h = dst.h >> 1;
				x >>= 1;
				y >>= 1;
				x += 2;
				VDPixmapFillRegionAntialiased_16x_16x(pxCb, region, x, y, colorCb);
				VDPixmapFillRegionAntialiased_16x_16x(pxCr, region, x, y, colorCr);
				return true;
			case nsVDPixmap::kPixFormat_YUV422_Planar:
			case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV422_Planar_709:
			case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
				pxCr.w = pxCb.w = dst.w >> 1;
				x >>= 1;
				x += 2;
				VDPixmapFillRegionAntialiased_16x_8x(pxCb, region, x, y, colorCb);
				VDPixmapFillRegionAntialiased_16x_8x(pxCr, region, x, y, colorCr);
				return true;
			case nsVDPixmap::kPixFormat_YUV444_Planar:
			case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV444_Planar_709:
			case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
				VDPixmapFillRegionAntialiased8x(pxCb, region, x, y, colorCb);
				VDPixmapFillRegionAntialiased8x(pxCr, region, x, y, colorCr);
				return true;
			}
		}
	}

	if (dst.format != nsVDPixmap::kPixFormat_XRGB8888 && dst.format != nsVDPixmap::kPixFormat_Y8)
		return false;

	// fast out
	if (region.mSpans.empty())
		return true;

	// check if vertical clipping is required
	const size_t n = region.mSpans.size();
	uint32 start = 0;
	uint32 end = n;

	uint32 spanmin = -x + (-y << 16) + 0x80008000;

	if (region.mSpans.front() < spanmin) {
		// find first span : x2 > spanmin
		start = std::upper_bound(region.mSpans.begin(), region.mSpans.end(), spanmin) - region.mSpans.begin();
		start &= ~1;

		// check for total top clip
		if (start >= n)
			return true;
	}

	uint32 spanlimit = (dst.w*8 - x) + (((dst.h*8 - y) - 1) << 16) + 0x80008000;

	if (region.mSpans.back() > spanlimit) {
		// find last span : x1 < spanlimit
		end = std::lower_bound(region.mSpans.begin(), region.mSpans.end(), spanlimit) - region.mSpans.begin();

		end = (end + 1) & ~1;

		// check for total bottom clip
		if (start >= end)
			return true;
	}

	// allocate A-buffer
	vdfastvector<uint8> abuffer(dst.w, 0);

	// fill region
	const uint32 *pSpan = &region.mSpans[start];
	const uint32 *pEnd  = &region.mSpans[0] + end;
	int lasty = -1;

	sint32 dstw8 = dst.w * 8;
	sint32 dsth8 = dst.h * 8;

	for(; pSpan != pEnd; pSpan += 2) {
		uint32 span0 = pSpan[0];
		uint32 span1 = pSpan[1];

		sint32 py = (span0 >> 16) - 0x8000 + y;

		if ((uint32)py >= (uint32)dsth8)
			continue;

		sint32 px1 = (span0 & 0xffff) - 0x8000 + x;
		sint32 px2 = (span1 & 0xffff) - 0x8000 + x;

		if (lasty != py) {
			if (((lasty ^ py) & 0xFFFFFFF8)) {
				if (lasty >= 0) {
					// flush scanline

					if (dst.format == nsVDPixmap::kPixFormat_XRGB8888)
						RenderABuffer32(dst, lasty >> 3, abuffer.data(), dst.w, color);
					else
						RenderABuffer8(dst, lasty >> 3, abuffer.data(), dst.w, color);
				}

				memset(abuffer.data(), 0, abuffer.size());
			}
			lasty = py;
		}

		if (px1 < 0)
			px1 = 0;
		if (px2 > dstw8)
			px2 = dstw8;

		if (px1 >= px2)
			continue;

		uint32 ix1 = px1 >> 3;
		uint32 ix2 = px2 >> 3;
		uint8 *p1 = abuffer.data() + ix1;
		uint8 *p2 = abuffer.data() + ix2;

		if (p1 == p2) {
			p1[0] += (px2 - px1);
		} else {
			if (px1 & 7) {
				p1[0] += 8 - (px1 & 7);
				++p1;
			}

			while(p1 != p2) {
				p1[0] += 8;
				++p1;
			}

			if (px2 & 7)
				p1[0] += px2 & 7;
		}
	}

	if (lasty >= 0) {
		if (dst.format == nsVDPixmap::kPixFormat_XRGB8888)
			RenderABuffer32(dst, lasty >> 3, abuffer.data(), dst.w, color);
		else
			RenderABuffer8(dst, lasty >> 3, abuffer.data(), dst.w, color);
	}

	return true;
}

void VDPixmapCreateRoundRegion(VDPixmapRegion& dst, float r) {
	int ir = VDCeilToInt(r);
	float r2 = r*r;

	dst.mSpans.clear();
	dst.mBounds.set(-ir, 0, ir+1, 0);

	for(int y = -ir; y <= ir; ++y) {
		int dx = VDCeilToInt(sqrtf(r2 - y*y));

		if (dx > 0) {
			dst.mSpans.push_back(0x80008000 + (y << 16) - dx);
			dst.mSpans.push_back(0x80008001 + (y << 16) + dx);
			if (dst.mBounds.top > y)
				dst.mBounds.top = y;
			if (dst.mBounds.bottom < y)
				dst.mBounds.bottom = y;
		}
	}
}

void VDPixmapConvolveRegion(VDPixmapRegion& dst, const VDPixmapRegion& r1, const VDPixmapRegion& r2, int dx1, int dx2, int dy) {
	dst.mSpans.clear();
	dst.mSpans.resize(r1.mSpans.size()+r2.mSpans.size());

	const uint32 *itA	= r1.mSpans.data();
	const uint32 *itAE	= itA + r1.mSpans.size();
	const uint32 *itB	= r2.mSpans.data();
	const uint32 *itBE	= itB + r2.mSpans.size();
	uint32 *dstp = dst.mSpans.data();

	uint32 offset1 = (dy<<16) + dx1;
	uint32 offset2 = (dy<<16) + dx2;

	while(itA != itAE && itB != itBE) {
		uint32 x1;
		uint32 x2;

		if (itB[0] + offset1 < itA[0]) {
			// B span is earlier.  Use it.
			x1 = itB[0] + offset1;
			x2 = itB[1] + offset2;
			itB += 2;

			// B spans *can* overlap, due to the widening.
			while(itB != itBE && itB[0]+offset1 <= x2) {
				uint32 bx2 = itB[1] + offset2;
				if (x2 < bx2)
					x2 = bx2;

				itB += 2;
			}

			goto a_start;
		} else {
			// A span is earlier.  Use it.
			x1 = itA[0];
			x2 = itA[1];
			itA += 2;

			// A spans don't overlap, so begin merge loop with B first.
		}

		for(;;) {
			// If we run out of B spans or the B span doesn't overlap,
			// then the next A span can't either (because A spans don't
			// overlap) and we exit.

			if (itB == itBE || itB[0]+offset1 > x2)
				break;

			do {
				uint32 bx2 = itB[1] + offset2;
				if (x2 < bx2)
					x2 = bx2;

				itB += 2;
			} while(itB != itBE && itB[0]+offset1 <= x2);

			// If we run out of A spans or the A span doesn't overlap,
			// then the next B span can't either, because we would have
			// consumed all overlapping B spans in the above loop.
a_start:
			if (itA == itAE || itA[0] > x2)
				break;

			do {
				uint32 ax2 = itA[1];
				if (x2 < ax2)
					x2 = ax2;

				itA += 2;
			} while(itA != itAE && itA[0] <= x2);
		}

		// Flush span.
		dstp[0] = x1;
		dstp[1] = x2;
		dstp += 2;
	}

	// Copy over leftover spans.
	memcpy(dstp, itA, sizeof(uint32)*(itAE - itA));
	dstp += itAE - itA;

	while(itB != itBE) {
		// B span is earlier.  Use it.
		uint32 x1 = itB[0] + offset1;
		uint32 x2 = itB[1] + offset2;
		itB += 2;

		// B spans *can* overlap, due to the widening.
		while(itB != itBE && itB[0]+offset1 <= x2) {
			uint32 bx2 = itB[1] + offset2;
			if (x2 < bx2)
				x2 = bx2;

			itB += 2;
		}

		dstp[0] = x1;
		dstp[1] = x2;
		dstp += 2;
	}

	dst.mSpans.resize(dstp - dst.mSpans.data());
}

void VDPixmapConvolveRegion(VDPixmapRegion& dst, const VDPixmapRegion& r1, const VDPixmapRegion& r2) {
	VDPixmapRegion temp;

	const uint32 *src1 = r2.mSpans.data();
	const uint32 *src2 = src1 + r2.mSpans.size();

	dst.mSpans.clear();
	while(src1 != src2) {
		uint32 p1 = src1[0];
		uint32 p2 = src1[1];
		src1 += 2;

		temp.mSpans.swap(dst.mSpans);
		VDPixmapConvolveRegion(dst, temp, r1, (p1 & 0xffff) - 0x8000, (p2 & 0xffff) - 0x8000, (p1 >> 16) - 0x8000);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////

void VDPixmapResolve4x_SSE2(void *dst, ptrdiff_t dstpitch, const void *src, ptrdiff_t srcpitch, uint32 w, uint32 h);
void VDPixmapResolve4x_NEON(void *dst, ptrdiff_t dstpitch, const void *src, ptrdiff_t srcpitch, uint32 w, uint32 h);

void VDPixmapResolve4x_Scalar(void *dst, ptrdiff_t dstpitch, const void *src, ptrdiff_t srcpitch, uint32 w, uint32 h) {
	do {
		uint8 *__restrict dst2 = (uint8 *)dst;
		const uint8 *__restrict src2 = (const uint8 *)src;

		for(uint32 x = 0; x < w; ++x) {
			sint32 sumb = 0;
			sint32 sumg = 0;
			sint32 sumr = 0;
			sint32 suma = 0;

			const uint8 *__restrict src3 = src2;
			for(int y2 = 0; y2 < 4; ++y2) {
				sumb += src3[0]*src3[0] + src3[4]*src3[4] + src3[ 8]*src3[ 8] + src3[12]*src3[12];
				sumg += src3[1]*src3[1] + src3[5]*src3[5] + src3[ 9]*src3[ 9] + src3[13]*src3[13];
				sumr += src3[2]*src3[2] + src3[6]*src3[6] + src3[10]*src3[10] + src3[14]*src3[14];
				suma += src3[3]*src3[3] + src3[7]*src3[7] + src3[11]*src3[11] + src3[15]*src3[15];

				src3 += srcpitch;
			}

			float b = sqrtf((float)sumb * (1.0f / 16.0f));
			float g = sqrtf((float)sumg * (1.0f / 16.0f));
			float r = sqrtf((float)sumr * (1.0f / 16.0f));
			float a = (float)suma * (1.0f / 16.0f);

			dst2[0] = (uint8)(b + 0.5f);
			dst2[1] = (uint8)(g + 0.5f);
			dst2[2] = (uint8)(r + 0.5f);
			dst2[3] = (uint8)(a + 0.5f);

			dst2 += 4;
			src2 += 16;
		}

		dst = (char *)dst + dstpitch;
		src = (const char *)src + srcpitch * 4;
	} while(--h);
}

void VDPixmapResolve4x(VDPixmap& dst, const VDPixmap& src4x) {
	sint32 w = std::min<sint32>(dst.w, src4x.w >> 2);
	sint32 h = std::min<sint32>(dst.h, src4x.h >> 2);

	if (w <= 0 || h <= 0)
		return;

#if VD_CPU_X86 || VD_CPU_X64
	VDPixmapResolve4x_SSE2(dst.data, dst.pitch, src4x.data, src4x.pitch, w, h);
#elif VD_CPU_ARM64
	VDPixmapResolve4x_NEON(dst.data, dst.pitch, src4x.data, src4x.pitch, w, h);
#else
	VDPixmapResolve4x_Scalar(dst.data, dst.pitch, src4x.data, src4x.pitch, w, h);
#endif
}
