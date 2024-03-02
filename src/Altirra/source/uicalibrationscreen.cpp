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

#include "stdafx.h"
#include <vd2/VDDisplay/textrenderer.h>
#include <at/atui/uimanager.h>
#include <at/atuicontrols/uibutton.h>
#include <at/atuicontrols/uilabel.h>
#include <at/atuicontrols/uilistview.h>
#include "uiaccessors.h"
#include "uicalibrationscreen.h"

///////////////////////////////////////////////////////////////////////////

class ATUICalibrationLevelsStrip final : public ATUIContainer {
public:
	ATUICalibrationLevelsStrip(bool white)
		: mBaseColor(white ? 0xEFEFEF : 0)
		, mInnerColor(white ? 0xFFFFFF : 0)
		, mLevel(white ? 239 : 1)
	{
	}

	void OnCreate() override;
	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) override;

private:
	uint32 mBaseColor = 0;
	uint32 mInnerColor = 0;
	int mLevel = 0;
	bool mbDrawInner = false;
};

void ATUICalibrationLevelsStrip::OnCreate() {
	StartTimer(0.5f, 0.5f,
		[this] {
			mbDrawInner = !mbDrawInner;
			Invalidate();
		}
	);
}

void ATUICalibrationLevelsStrip::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	sint32 blockSpacing = 20;
	sint32 blockSize = std::max<sint32>(1, std::min<sint32>(h - 30, (w - blockSpacing * 15) / 16));
	sint32 inset = blockSize / 4;

	auto* tr = rdr.GetTextRenderer();

	tr->SetAlignment(tr->kAlignCenter, tr->kVertAlignTop);
	tr->SetColorRGB(0x808080);
	
	VDStringW s;
	for(int i=0; i<16; ++i) {
		s.sprintf(L"%u", mLevel + i);
		tr->DrawTextLine((blockSize + blockSpacing) * i + (blockSize >> 1), blockSize + 5, s.c_str());
	}

	for(int i=0; i<16; ++i) {
		sint32 x = (blockSize + blockSpacing) * i;
		rdr.SetColorRGB(mBaseColor + 0x010101 * (i + 1));
		rdr.FillRect(x, 0, blockSize, blockSize);

		if (mbDrawInner == (i & 1)) {
			rdr.SetColorRGB(mInnerColor);
			rdr.FillRect(x + inset, inset, blockSize - inset*2, blockSize - inset*2);
		}

		x += blockSize + blockSpacing;
	}
}

class ATUICalibrationPanelBlackWhiteLevels final : public ATUIContainer {
public:
	void OnCreate() override;
};

void ATUICalibrationPanelBlackWhiteLevels::OnCreate() {
	vdrefptr topStrip(new ATUICalibrationLevelsStrip(false));
	topStrip->SetPlacement(vdrect32f(0, 0, 1, 0.25f), {}, {});
	topStrip->SetSizeOffset(vdsize32(0, 0));
	AddChild(topStrip);

	vdrefptr bottomStrip(new ATUICalibrationLevelsStrip(true));
	bottomStrip->SetPlacement(vdrect32f(0, 0.75f, 1, 1), {}, {});
	bottomStrip->SetSizeOffset(vdsize32(0, 0));
	AddChild(bottomStrip);

	vdrefptr text(new ATUILabel);
	AddChild(text);

	text->SetPlacement(vdrect32f(0, 0.3f, 1, 0.6f), vdpoint32(0, 50), {});
	text->SetSizeOffset(vdsize32(0, 0));
	text->SetFont(mpManager->GetThemeFont(kATUIThemeFont_Header));
	text->SetWordWrapEnabled(true);
	text->SetText(
		L"Each panel shows if the display is correctly showing near-black and near-white levels without clipping. "
		L"Adjust the display's black level and contrast settings so that the squares are as dark or bright as possible "
		L"while the blinking inner squares are still visible. The difference will be very faint for 1 and 255."
	);
	text->SetAlphaFillColor(0);
	text->SetTextColor(0xFFFFFF);
}

///////////////////////////////////////////////////////////////////////////

class ATUICalibrationPanelHDR final : public ATUIContainer {
public:
	void OnCreate() override;
	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) override;

	VDDisplayImageView mPatView;

};

void ATUICalibrationPanelHDR::OnCreate() {
	vdrefptr text(new ATUILabel);
	AddChild(text);

	text->SetPlacement(vdrect32f(0, 0, 1, 1), vdpoint32(0, 50), vdfloat2(0.5f, 0.3f));
	text->SetSizeOffset(vdsize32(0, -480));
	text->SetFont(mpManager->GetThemeFont(kATUIThemeFont_Header));
	text->SetWordWrapEnabled(true);
	text->SetText(
		L"This screen is only pertinent when displaying HDR.\n"
		L"\n"
		L"The top gradient shows intensity in nits with the notched middle flattening out when the display limit is reached (typically 400-600 nits). Ideally, "
		L"this would happen abruptly. If the falloff is gradual or extends beyond than "
		L"display is rated for (e.g. 1000 nits for a 400 nit display), then the display is applying tone mapping. "
		L"This is undesirable as it distorts colors and makes the Windows desktop muddy.\n"
		L"\n"
		L"The bottom pattern measures input linearity, with the solid and pattern gradients "
		L"matching at linear gamma (1.0). Ideally, the display input is linear up to the brightness limit of "
		L"the display; it will curve upward as the display can no longer display pixels bright enough. "
		L"When the display is applying tone mapping, this will occur before the brightness limit is reached.\n"
		L"\n"
		L"Depending on the display, switching the HDR mode settings or changing from HDMI to DisplayPort can "
		L"bypass or reduce tone mapping and improve accuracy.");
	text->SetAlphaFillColor(0);
	text->SetTextColor(0xFFFFFF);

	static constexpr uint32 kPixels[4] {
		0,
		0,
		0xFFFFFF,
		0xFFFFFF,
	};

	VDPixmap px {};
	px.data = (void *)kPixels;
	px.pitch = 8;
	px.w = 2;
	px.h = 2;
	px.format = nsVDPixmap::kPixFormat_XRGB8888;
	mPatView.SetImage(px, false);
}

void ATUICalibrationPanelHDR::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	if (rdr.GetCaps().mbSupportsHDR) {
		const sint32 bandh8 = 15;
		const sint32 bandh4 = bandh8 * 2;
		const sint32 bandh2 = bandh4 * 2;
		const sint32 bandh = bandh2 * 2;

		const int N = 40;
		vdfloat2 pts[(N+1)*2];
		vdfloat4 cols[(N+1)*2];

		const float wf = (float)w;
		const float xstep = wf / (float)N;
		for(int pass = 0; pass < 2; ++pass) {
			const float y1 = pass ? (float)(bandh2 - bandh8) : 0;
			const float y2 = pass ? (float)(bandh2 + bandh8) : (float)bandh;
			const float brscale = 12.5f * (pass ? 1.1f : 1.0f);

			for(int i = 0; i <= N; ++i) {
				pts[i*2+0] = vdfloat2(xstep * (float)i, y1);
				pts[i*2+1] = vdfloat2(xstep * (float)i, y2);

				float nb = (float)i / (float)N;

				nb = powf(nb, 3.0f);
				
				const float br = brscale * nb;
				cols[i*2+0] = cols[i*2+1] = vdfloat4(br, br, br, 1.0f);
			}

			rdr.FillTriStripHDR(pts, cols, (N+1)*2, false);
		}

		rdr.SetColorRGB(0xFFFFFF);
		for(int i=0; i<=10; ++i) {
			sint32 x = (sint32)(0.5f + (wf - 1.0f) * powf((float)i / 10.0f, 1.0f / 3.0f));

			rdr.FillRect(x, bandh + 10, 1, 20);
		}

		auto* tr = rdr.GetTextRenderer();
		tr->SetFont(mpManager->GetThemeFont(kATUIThemeFont_Default));
		tr->SetColorRGB(0xFFFFFF);

		VDStringW s;
		for(int i=0; i<=10; ++i) {
			sint32 x = (sint32)(0.5f + (wf - 1.0f) * powf((float)i / 10.0f, 1.0f / 3.0f));
		
			tr->SetAlignment(i == 0 ? tr->kAlignLeft : i < 10 ? tr->kAlignCenter : tr->kAlignRight, tr->kVertAlignTop);

			s.sprintf(L"%d", i*100);
			tr->DrawTextLine(x, bandh + 35, s.c_str());
		}
	
		vdfloat2 uvs[(N+1)*2];

		for(int band = 0; band <= 10; ++band) {
			const float y1 = h - 15*(21-band*2);
			const float y2 = h - 15*(20-band*2);

			for(int i = 0; i <= N; ++i) {
				pts[i*2+0] = vdfloat2(xstep * (float)i, y1);
				pts[i*2+1] = vdfloat2(xstep * (float)i, y2);
				uvs[i*2+0] = pts[i*2+0]*0.5f;
				uvs[i*2+1] = pts[i*2+1]*0.5f;

				float nb = (float)i / (float)N;

				nb = powf(nb, 3.0f);

				const float br = 12.5f * nb;
				cols[i*2+0] = cols[i*2+1] = vdfloat4(br, br, br, 1.0f);
			}

			rdr.FillTriStripHDR(pts, cols, uvs, (N+1)*2, false, false, mPatView);
		}

		for(int band = 0; band <= 10; ++band) {
			const float y1 = h - 15*(22-band*2);
			const float y2 = h - 15*(21-band*2);

			const float brscale = powf(1.2f, band - 5) * 12.5f * 0.5f;
			for(int i = 0; i <= N; ++i) {
				pts[i*2+0] = vdfloat2(xstep * (float)i, y1);
				pts[i*2+1] = vdfloat2(xstep * (float)i, y2);

				float nb = (float)i / (float)N;

				nb = powf(nb, 3.0f);

				const float br = brscale * nb;
				cols[i*2+0] = cols[i*2+1] = vdfloat4(br, br, br, 1.0f);
			}

			rdr.FillTriStripHDR(pts, cols, (N+1)*2, false);
		}
	}

	ATUIContainer::Paint(rdr, w, h);
}

///////////////////////////////////////////////////////////////////////////

class ATUICalibrationPanelHDRToneMap final : public ATUIContainer {
public:
	void OnCreate() override;
	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) override;

	VDDisplayImageView mPatView;

};

void ATUICalibrationPanelHDRToneMap::OnCreate() {
	vdrefptr text(new ATUILabel);
	AddChild(text);

	text->SetPlacement(vdrect32f(0, 0.75f, 1, 1), vdpoint32(0, 50), vdfloat2(0, 0));
	text->SetSizeOffset(vdsize32(0, -50));
	text->SetFont(mpManager->GetThemeFont(kATUIThemeFont_Header));
	text->SetWordWrapEnabled(true);
	text->SetText(
		L"The rays show the display's tone mapping response to increasing intensities. Markers are spaced every 100 nits. "
		L"A display which is doing minimal or tone mapping will hard clip quickly to yellow. More aggressive tone mapping will "
		L"show a more gradual falloff, which accepts higher intensities than the display can support but distorts lower intensities. "
		L"(This test is inspired by the VESA DisplayHDR tone mapping test.)"
	);
	text->SetAlphaFillColor(0);
	text->SetTextColor(0xFFFFFF);
}

void ATUICalibrationPanelHDRToneMap::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	if (!rdr.PushViewport(vdrect32(0, 0, w, h*3/4), 0, 0))
		return;

	if (rdr.GetCaps().mbSupportsHDR) {
		const int kIntensitySteps = 100;

		vdfloat2 pts[kIntensitySteps*2+1];
		vdfloat4 cols[kIntensitySteps*2+1];

		float radius = std::max<float>(w, h * 0.75f) * 0.5f;
		float xc = radius;
		float yc = (float)(h * 0.375);

		pts[kIntensitySteps*2].x = xc;
		pts[kIntensitySteps*2].y = yc;
		cols[kIntensitySteps*2] = vdfloat4(125,62.5f,0,1);

		// Perceptual quantizer (PQ) constants
		static constexpr float pq_m1 = 1305.0f / 8192.0f;
		static constexpr float pq_m2 = 2523.0f / 32.0f;
		static constexpr float pq_c1 = 107.0f / 128.0f;
		static constexpr float pq_c2 = 2413.0f / 128.0f;
		static constexpr float pq_c3 = 2392.0f / 128.0f;

		static constexpr float kSunScale = 1.125f;

		const float angstep = nsVDMath::kfTwoPi / 60.0f;
		for(int i=0; i<60; ++i) {
			float angle1 = angstep * ((float)i - 0.5f);
			float angle2 = angle1 + angstep;
			float c1 = radius*cosf(angle1);
			float s1 = radius*sinf(angle1);
			float c2 = radius*cosf(angle2);
			float s2 = radius*sinf(angle2);

			const int offset = -(i & 1) * 4;
			for(int j=0; j<kIntensitySteps*2; j+=2) {
				float f = (float)(kIntensitySteps*2 - j) / (float)(kIntensitySteps*2);

				pts[j+0].x = xc + f*c1;
				pts[j+0].y = yc - f*s1;
				pts[j+1].x = xc + f*c2;
				pts[j+1].y = yc - f*s2;

				// convert fraction to nits through perceptual quantizer (PQ) EOTF, then
				// divide by 80 for scRGB encoding
				float e = std::clamp<float>(kSunScale * (float)(j + offset) / (float)(kIntensitySteps*2), 0.0f, 1.0f);
				float eim2 = powf(e, 1.0f / pq_m2);
				float v = 125.0f * powf(std::max<float>(eim2 - pq_c1, 0) / (pq_c2 - pq_c3 * eim2), 1.0f / pq_m1);

				cols[j+0] = cols[j+1] = vdfloat4(v, v*0.5f, 0.0f, 1.0f);
			}

			rdr.FillTriStripHDR(pts, cols, kIntensitySteps*2+1, false);
		}

		for(int i=1; i<=10; ++i) {
			float lum = (100.0f / 10000.0f) * (float)i;

			// encode with PQ
			float lm1 = powf(lum, pq_m1);
			float e = powf((pq_c1 + pq_c2*lm1) / (1.0f + pq_c3*lm1), pq_m2);

			// draw marker
			const int x = (int)(0.5f + radius * e / kSunScale);
			const int markerh = h / 90;

			rdr.SetColorRGB(0x00FF00);
			rdr.FillRect(x, (int)yc - markerh, 1, markerh*2 + 1);
		}
	}

	rdr.PopViewport();

	ATUIContainer::Paint(rdr, w, h);
}

///////////////////////////////////////////////////////////////////////////

class ATUICalibrationPanelGradient final : public ATUIContainer {
public:
	void OnCreate() override;
	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) override;
};

void ATUICalibrationPanelGradient::OnCreate() {
	vdrefptr text(new ATUILabel);
	AddChild(text);

	text->SetPlacement(vdrect32f(0, 0.5f, 1, 1), vdpoint32(0, 50), {});
	text->SetSizeOffset(vdsize32(0, 0));
	text->SetFont(mpManager->GetThemeFont(kATUIThemeFont_Header));
	text->SetWordWrapEnabled(true);
	text->SetText(
		L"All gradients above should appear with uniform stepping on a display panel with at least 8 bit per channel capability (true or FRC). Otherwise, "
		L"the display may need to be adjusted or the graphics driver's desktop color settings reset. 6-bit panels or displays with HDR "
		L"will show less uniform stepping."
	);
	text->SetAlphaFillColor(0);
	text->SetTextColor(0xFFFFFF);

}

void ATUICalibrationPanelGradient::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	const sint32 xstep = std::max<sint32>(1, w / 256);

	for(int band=0; band<4; ++band) {
		static constexpr uint32 kBaseColors[] {
			0x010000,
			0x000100,
			0x000001,
			0x010101,
		};

		const uint32 baseColor = kBaseColors[band];
		const sint32 bandy = (h * band + 4) / 8;
		const sint32 bandh = (h * (band + 1) + 4) / 8 - bandy;

		for(int i=0; i<256; ++i) {
			rdr.SetColorRGB(baseColor * i);

			if (i & 1)
				rdr.FillRect(i * xstep, bandy + bandh / 2, xstep * 2, bandh - bandh / 2);
			else
				rdr.FillRect(i * xstep, bandy, xstep * 2, bandh / 2);
		}
	}

	ATUIContainer::Paint(rdr, w, h);
}

///////////////////////////////////////////////////////////////////////////

ATUICalibrationScreen::ATUICalibrationScreen() {
	SetFillColor(0);
}

ATUICalibrationScreen::~ATUICalibrationScreen() {
}

void ATUICalibrationScreen::ShowDialog() {
	vdrefptr p { new ATUICalibrationScreen };
	auto& m = ATUIGetManager();

	m.GetMainWindow()->AddChild(p);
	p->SetPlacementFill();

	m.BeginModal(p);
}

void ATUICalibrationScreen::OnCreate() {
	BindAction(kATUIVK_Escape, 1);
	BindAction(kATUIVK_UIReject, 1);

	const vdsize32 buttonSize = mpManager->ScaleThemeSize(vdfloat2(80, 24));
	const float bottomSize = mpManager->ScaleThemeSize(vdfloat2(0, 20)).h;

	mpOKButton = new ATUIButton;
	AddChild(mpOKButton);

	mpOKButton->SetText(L"OK");
	mpOKButton->SetPlacement(vdrect32f(0.98f, 0.98f, 0.98f, 0.98f), vdpoint32(0, 0), vdfloat2(1, 1));
	mpOKButton->SetSize(buttonSize);
	mpOKButton->OnActivatedEvent() = [this] { Close(); };

	mpListView = new ATUIListView;
	AddChild(mpListView);

	mpListView->SetPlacement(vdrect32f(0.02, 0.02f, 0.1f, 0.98f), {}, {});
	mpListView->SetSizeOffset(vdsize32(0, -bottomSize));
	mpListView->AddItem(L"Black/white levels");
	mpListView->AddItem(L"Gradient");
	mpListView->AddItem(L"HDR");
	mpListView->AddItem(L"HDR tone map");
	mpListView->SetSelectedItem(0);
	mpListView->OnItemSelectedEvent() = [this](sint32) { UpdatePanel(); };

	UpdatePanel();
}

void ATUICalibrationScreen::OnActionStop(uint32 id) {
	if (id == 1) {
		mpManager->EndModal();
		Destroy();
	}
}

void ATUICalibrationScreen::UpdatePanel() {
	if (mpPanel) {
		RemoveChild(mpPanel);
		mpPanel = nullptr;
	}

	switch(mpListView->GetSelectedItem()) {
		case 0:
			mpPanel = new ATUICalibrationPanelBlackWhiteLevels;
			break;

		case 1:
			mpPanel = new ATUICalibrationPanelGradient;
			break;

		case 2:
			mpPanel = new ATUICalibrationPanelHDR;
			break;

		case 3:
			mpPanel = new ATUICalibrationPanelHDRToneMap;
			break;

		default:
			break;
	}

	if (mpPanel) {
		AddChild(mpPanel);
		mpPanel->SetPlacement(vdrect32f(0.15f, 0.05f, 0.95f, 0.95f), {}, {});
		mpPanel->SetSizeOffset(mpListView->GetSizeOffset());
	}
}
