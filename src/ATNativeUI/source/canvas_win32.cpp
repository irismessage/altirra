//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
#include <windows.h>
#include <uxtheme.h>
#include <at/atnativeui/canvas_win32.h>
#include <vd2/VDDisplay/renderergdi.h>

class IVDDisplayRenderer;

ATUICanvasW32::ATUICanvasW32()
	: mpRenderer(VDDisplayCreateRendererGDI())
	, mhwnd(nullptr)
	, mhdc(nullptr)
	, mhPaintBuffer(nullptr)
{
}

ATUICanvasW32::~ATUICanvasW32() {
	delete mpRenderer;
}

void ATUICanvasW32::Init(VDZHWND hwnd) {
	mhwnd = hwnd;
}

IVDDisplayRenderer *ATUICanvasW32::Begin(PAINTSTRUCT& ps, bool doubleBuffer) {
	HDC hdc = BeginDirect(ps, doubleBuffer);

	if (!hdc)
		return nullptr;

	if (!mpRenderer->Begin(hdc, ps.rcPaint.right, ps.rcPaint.bottom)) {
		EndDirect(ps);
		return nullptr;
	}

	return mpRenderer;
}

void ATUICanvasW32::End(PAINTSTRUCT& ps) {
	if (mhdc) {
		mpRenderer->End();

		EndDirect(ps);
	}
}

VDZHDC ATUICanvasW32::BeginDirect(VDZPAINTSTRUCT& ps, bool enableDoubleBuffering) {
	HDC hdc = BeginPaint(mhwnd, &ps);

	if (!hdc)
		return nullptr;

	if (enableDoubleBuffering) {
		BP_PAINTPARAMS bufParams { sizeof(bufParams), BPPF_ERASE, nullptr, nullptr };
		HDC newHdc = nullptr;

		mhPaintBuffer = BeginBufferedPaint(hdc, &ps.rcPaint, BPBF_COMPATIBLEBITMAP, &bufParams, &newHdc);
		if (mhPaintBuffer)
			hdc = newHdc;
	}

	mhdc = hdc;
	return hdc;
}

void ATUICanvasW32::EndDirect(VDZPAINTSTRUCT& ps) {
	if (mhdc) {
		if (mhPaintBuffer) {
			EndBufferedPaint(mhPaintBuffer, TRUE);
			mhPaintBuffer = nullptr;
		}

		EndPaint(mhwnd, &ps);
		mhdc = nullptr;
	}
}

void ATUICanvasW32::Scroll(sint32 dx, sint32 dy) {
	ScrollWindow(mhwnd, dx, dy, nullptr, nullptr);
}
