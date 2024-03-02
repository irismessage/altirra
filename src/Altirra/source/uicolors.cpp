//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
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

#include "stdafx.h"
#include <windows.h>
#include <vd2/system/error.h>
#include <vd2/system/math.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/services.h>
#include "Dialog.h"
#include "resource.h"
#include "gtia.h"
#include "simulator.h"

extern ATSimulator g_sim;

class ATAdjustColorsDialog : public VDDialogFrameW32 {
public:
	ATAdjustColorsDialog();

protected:
	bool OnLoaded();
	void OnDestroy();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void OnHScroll(uint32 id, int code);
	VDZINT_PTR DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam);
	void UpdateLabel(uint32 id);
	void UpdateColorImage();

	ATColorParams mParams;
	HMENU mPresetsPopupMenu;
};

ATAdjustColorsDialog g_adjustColorsDialog;

ATAdjustColorsDialog::ATAdjustColorsDialog()
	: VDDialogFrameW32(IDD_ADJUST_COLORS)
	, mPresetsPopupMenu(NULL)
{
}

bool ATAdjustColorsDialog::OnLoaded() {
	mPresetsPopupMenu = LoadMenu(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDR_COLOR_PRESETS_MENU));

	TBSetRange(IDC_HUESTART, -60, 360);
	TBSetRange(IDC_HUERANGE, 0, 540);
	TBSetRange(IDC_BRIGHTNESS, -50, 50);
	TBSetRange(IDC_CONTRAST, 0, 200);
	TBSetRange(IDC_SATURATION, 0, 100);
	TBSetRange(IDC_ARTPHASE, -60, 360);
	TBSetRange(IDC_ARTSAT, 0, 400);
	TBSetRange(IDC_ARTBRI, -50, 50);

	OnDataExchange(false);
	SetFocusToControl(IDC_HUESTART);
	return true;
}

void ATAdjustColorsDialog::OnDestroy() {
	if (mPresetsPopupMenu) {
		DestroyMenu(mPresetsPopupMenu);
		mPresetsPopupMenu = NULL;
	}

	VDDialogFrameW32::OnDestroy();
}

void ATAdjustColorsDialog::OnDataExchange(bool write) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	if (write) {
	} else {
		mParams = gtia.GetColorParams();
		TBSetValue(IDC_HUESTART, VDRoundToInt(mParams.mHueStart));
		TBSetValue(IDC_HUERANGE, VDRoundToInt(mParams.mHueRange));
		TBSetValue(IDC_BRIGHTNESS, VDRoundToInt(mParams.mBrightness * 100.0f));
		TBSetValue(IDC_CONTRAST, VDRoundToInt(mParams.mContrast * 100.0f));
		TBSetValue(IDC_SATURATION, VDRoundToInt(mParams.mSaturation * 100.0f));
		TBSetValue(IDC_ARTPHASE, VDRoundToInt(mParams.mArtifactHue));
		TBSetValue(IDC_ARTSAT, VDRoundToInt(mParams.mArtifactSat * 100.0f));
		TBSetValue(IDC_ARTBRI, VDRoundToInt(mParams.mArtifactBias * 100.0f));

		UpdateLabel(IDC_HUESTART);
		UpdateLabel(IDC_HUERANGE);
		UpdateLabel(IDC_BRIGHTNESS);
		UpdateLabel(IDC_CONTRAST);
		UpdateLabel(IDC_SATURATION);
		UpdateLabel(IDC_ARTPHASE);
		UpdateLabel(IDC_ARTSAT);
		UpdateLabel(IDC_ARTBRI);
		UpdateColorImage();
	}
}

bool ATAdjustColorsDialog::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_LOADPRESET) {
		TPMPARAMS tpp = { sizeof(TPMPARAMS) };

		GetWindowRect(GetDlgItem(mhdlg, IDC_LOADPRESET), &tpp.rcExclude);

		TrackPopupMenuEx(GetSubMenu(mPresetsPopupMenu, 0), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, tpp.rcExclude.right, tpp.rcExclude.top, mhdlg, &tpp);
		return true;
	} else if (id == ID_COLORS_ORIGINAL) {
		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		gtia.ResetColors();
		OnDataExchange(false);
	} else if (id == ID_COLORS_ALTERNATE) {
		ATColorParams params;
		params.mHueStart = -29.0f;
		params.mHueRange = 26.1f * 15.0f;
		params.mBrightness = -0.03f;
		params.mContrast = 0.88f;
		params.mSaturation = 0.23f;
		params.mArtifactHue = 96.0f;
		params.mArtifactSat = 2.76f;
		params.mArtifactBias = 0.35f;

		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		gtia.SetColorParams(params);
		OnDataExchange(false);
	} else if (id == ID_COLORS_AUTHENTICNTSC) {
		ATColorParams params;
		params.mHueStart = -51.0f;
		params.mHueRange = 418.0f;
		params.mBrightness = -0.11f;
		params.mContrast = 1.04f;
		params.mSaturation = 0.34f;
		params.mArtifactHue = 96.0f;
		params.mArtifactSat = 2.76f;
		params.mArtifactBias = 0.35f;

		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		gtia.SetColorParams(params);
		OnDataExchange(false);
	} else if (id == ID_COLORS_G2F) {
		ATColorParams params;

		params.mHueStart = -9.36754f;
		params.mHueRange = 361.019f;
		params.mBrightness = +0.174505f;
		params.mContrast = 0.82371f;
		params.mSaturation = 0.21993f;
		params.mArtifactHue = 96.0f;
		params.mArtifactSat = 2.76f;
		params.mArtifactBias = 0.35f;

		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		gtia.SetColorParams(params);
		OnDataExchange(false);
	} else if (id == ID_COLORS_OLIVIERPAL) {
		ATColorParams params;

		params.mHueStart = -14.7889f;
		params.mHueRange = 385.155f;
		params.mBrightness = +0.057038f;
		params.mContrast = 0.941149f;
		params.mSaturation = 0.195861f;
		params.mArtifactHue = 96.0f;
		params.mArtifactSat = 2.76f;
		params.mArtifactBias = 0.35f;

		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		gtia.SetColorParams(params);
		OnDataExchange(false);
	}

	return false;
}

void ATAdjustColorsDialog::OnHScroll(uint32 id, int code) {
	if (id == IDC_HUESTART) {
		float v = (float)TBGetValue(IDC_HUESTART);

		if (mParams.mHueStart != v) {
			mParams.mHueStart = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_HUERANGE) {
		float v = (float)TBGetValue(IDC_HUERANGE);

		if (mParams.mHueRange != v) {
			mParams.mHueRange = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_BRIGHTNESS) {
		float v = (float)TBGetValue(IDC_BRIGHTNESS) / 100.0f;

		if (mParams.mBrightness != v) {
			mParams.mBrightness = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_CONTRAST) {
		float v = (float)TBGetValue(IDC_CONTRAST) / 100.0f;

		if (mParams.mContrast != v) {
			mParams.mContrast = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_SATURATION) {
		float v = (float)TBGetValue(IDC_SATURATION) / 100.0f;

		if (mParams.mSaturation != v) {
			mParams.mSaturation = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_ARTPHASE) {
		float v = (float)TBGetValue(IDC_ARTPHASE);

		if (mParams.mArtifactHue != v) {
			mParams.mArtifactHue = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_ARTSAT) {
		float v = (float)TBGetValue(IDC_ARTSAT) / 100.0f;

		if (mParams.mArtifactSat != v) {
			mParams.mArtifactSat = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_ARTBRI) {
		float v = (float)TBGetValue(IDC_ARTBRI) / 100.0f;

		if (mParams.mArtifactBias != v) {
			mParams.mArtifactBias = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	}
}

VDZINT_PTR ATAdjustColorsDialog::DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	if (msg == WM_DRAWITEM) {
		if (wParam == IDC_COLORS) {
			const DRAWITEMSTRUCT& drawInfo = *(const DRAWITEMSTRUCT *)lParam;

			BITMAPINFO bi = {
				{
					sizeof(BITMAPINFOHEADER),
					16,
					16,
					1,
					32,
					BI_RGB,
					16*16*4,
					0,
					0,
					0,
					0
				}
			};

			uint32 pal[256];
			g_sim.GetGTIA().GetPalette(pal);

			for(int i=0; i<128; i += 16)
				VDSwapMemory(&pal[i], &pal[240-i], 16*sizeof(uint32));

			StretchDIBits(drawInfo.hDC,
				drawInfo.rcItem.left,
				drawInfo.rcItem.top,
				drawInfo.rcItem.right - drawInfo.rcItem.left,
				drawInfo.rcItem.bottom - drawInfo.rcItem.top,
				0, 0, 16, 16, pal, &bi, DIB_RGB_COLORS, SRCCOPY);

			SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}
	}

	return VDDialogFrameW32::DlgProc(msg, wParam, lParam);
}

void ATAdjustColorsDialog::UpdateLabel(uint32 id) {
	switch(id) {
		case IDC_HUESTART:
			SetControlTextF(IDC_STATIC_HUESTART, L"%.0f\u00B0", mParams.mHueStart);
			break;
		case IDC_HUERANGE:
			SetControlTextF(IDC_STATIC_HUERANGE, L"%.1f\u00B0", mParams.mHueRange / 15.0f);
			break;
		case IDC_BRIGHTNESS:
			SetControlTextF(IDC_STATIC_BRIGHTNESS, L"%+.0f%%", mParams.mBrightness * 100.0f);
			break;
		case IDC_CONTRAST:
			SetControlTextF(IDC_STATIC_CONTRAST, L"%.0f%%", mParams.mContrast * 100.0f);
			break;
		case IDC_SATURATION:
			SetControlTextF(IDC_STATIC_SATURATION, L"%.0f%%", mParams.mSaturation * 100.0f);
			break;
		case IDC_ARTPHASE:
			SetControlTextF(IDC_STATIC_ARTPHASE, L"%.0f\u00B0", mParams.mArtifactHue);
			break;
		case IDC_ARTSAT:
			SetControlTextF(IDC_STATIC_ARTSAT, L"%.0f%%", mParams.mArtifactSat * 100.0f);
			break;
		case IDC_ARTBRI:
			SetControlTextF(IDC_STATIC_ARTBRI, L"%+.0f%%", mParams.mArtifactBias * 100.0f);
			break;
	}
}

void ATAdjustColorsDialog::UpdateColorImage() {
	// update image
	HWND hwndColors = GetDlgItem(mhdlg, IDC_COLORS);
	InvalidateRect(hwndColors, NULL, FALSE);
}

void ATUIOpenAdjustColorsDialog(VDGUIHandle hParent) {
	g_adjustColorsDialog.Create(hParent);
}

void ATUICloseAdjustColorsDialog() {
	g_adjustColorsDialog.Destroy();
}
