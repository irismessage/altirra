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

#include <stdafx.h>
#include <windows.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/math.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/services.h>
#include <at/atnativeui/dialog.h>
#include "resource.h"
#include "gtia.h"
#include "simulator.h"

extern ATSimulator g_sim;

class ATAdjustArtifactingDialog final : public VDDialogFrameW32 {
public:
	ATAdjustArtifactingDialog();

	bool OnLoaded() override;
	void OnDataExchange(bool write) override;
	void OnHScroll(uint32 id, int code) override;

protected:
	struct Mapping {
		uint32 mId;
		sint32 mTickMin;
		sint32 mTickMax;
		vdfunction<sint32()> mpGet;
		vdfunction<void(sint32)> mpSet;
	};

	void UpdateLabel(const Mapping& mapping);
	void AddMapping(uint32 id, sint32 tickMin, sint32 tickMax, const vdfunction<sint32()>& get, const vdfunction<void(sint32)>& set);

	ATArtifactingParams mParams;

	vdvector<Mapping> mMappings;
};

ATAdjustArtifactingDialog::ATAdjustArtifactingDialog()
	: VDDialogFrameW32(IDD_ADJUST_ARTIFACTING)
{
}

bool ATAdjustArtifactingDialog::OnLoaded() {
	AddMapping(IDC_NTSC_LUMASHARP, 0, 100, [this]() { return VDRoundToInt32(mParams.mNTSCLumaSharpness * 100.0f); }, [this](sint32 v) { mParams.mNTSCLumaSharpness = (float)v / 100.0f; });
	AddMapping(IDC_NTSC_CHROMASHARP, 0, 100, [this]() { return VDRoundToInt32(mParams.mNTSCChromaSharpness * 100.0f); }, [this](sint32 v) { mParams.mNTSCChromaSharpness = (float)v / 100.0f; });
	AddMapping(IDC_NTSC_LUMANOTCHQ, 1, 100, [this]() { return VDRoundToInt32(mParams.mNTSCLumaNotchQ * 10.0f); }, [this](sint32 v) { mParams.mNTSCLumaNotchQ = (float)v / 10.0f; });

	for(const Mapping& mapping : mMappings) {
		TBSetRange(mapping.mId, mapping.mTickMin, mapping.mTickMax);
	}

	OnDataExchange(false);
	return false;
}

void ATAdjustArtifactingDialog::OnDataExchange(bool write) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	if (write) {
		gtia.SetArtifactingParams(mParams);
	} else {
		mParams = gtia.GetArtifactingParams();

		for(const Mapping& mapping : mMappings) {
			TBSetValue(mapping.mId, mapping.mpGet());
			UpdateLabel(mapping);
		}
	}
}

void ATAdjustArtifactingDialog::OnHScroll(uint32 id, int code) {
	for(const Mapping& mapping : mMappings) {
		if (mapping.mId == id) {
			sint32 v = TBGetValue(id);

			if (v != mapping.mpGet()) {
				mapping.mpSet(v);
				UpdateLabel(mapping);
				OnDataExchange(true);
			}
			break;
		}
	}
}

void ATAdjustArtifactingDialog::UpdateLabel(const Mapping& mapping) {
	switch(mapping.mId) {
		case IDC_NTSC_LUMASHARP:
			SetControlTextF(IDC_STATIC_NTSC_LUMASHARP, L"%.2f", mParams.mNTSCLumaSharpness);
			break;
		case IDC_NTSC_CHROMASHARP:
			SetControlTextF(IDC_STATIC_NTSC_CHROMASHARP, L"%.2f", mParams.mNTSCChromaSharpness);
			break;
		case IDC_NTSC_LUMANOTCHQ:
			SetControlTextF(IDC_STATIC_NTSC_LUMANOTCHQ, L"%.1f", mParams.mNTSCLumaNotchQ);
			break;
	}
}

void ATAdjustArtifactingDialog::AddMapping(uint32 id, sint32 tickMin, sint32 tickMax, const vdfunction<sint32()>& get, const vdfunction<void(sint32)>& set) {
	mMappings.push_back({id, tickMin, tickMax, get, set});
}

///////////////////////////////////////////////////////////////////////////

class ATAdjustColorsDialog final : public VDDialogFrameW32 {
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
	void ExportPalette(const wchar_t *s);
	void OnLumaRampChanged(VDUIProxyComboBoxControl *sender, int sel);

	ATColorSettings mSettings;
	ATColorParams *mpParams;
	ATColorParams *mpOtherParams;
	HMENU mPresetsPopupMenu;

	VDUIProxyComboBoxControl mLumaRampCombo;
	VDDelegate mDelLumaRampChanged;

	ATAdjustArtifactingDialog mArtDialog;
};

ATAdjustColorsDialog g_adjustColorsDialog;

ATAdjustColorsDialog::ATAdjustColorsDialog()
	: VDDialogFrameW32(IDD_ADJUST_COLORS)
	, mPresetsPopupMenu(NULL)
{
	mLumaRampCombo.OnSelectionChanged() += mDelLumaRampChanged.Bind(this, &ATAdjustColorsDialog::OnLumaRampChanged);
}

bool ATAdjustColorsDialog::OnLoaded() {
	mPresetsPopupMenu = LoadMenu(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDR_COLOR_PRESETS_MENU));

	AddProxy(&mLumaRampCombo, IDC_LUMA_RAMP);
	mLumaRampCombo.AddItem(L"Linear");
	mLumaRampCombo.AddItem(L"XL/XE");

	TBSetRange(IDC_HUESTART, -120, 360);
	TBSetRange(IDC_HUERANGE, 0, 540);
	TBSetRange(IDC_BRIGHTNESS, -50, 50);
	TBSetRange(IDC_CONTRAST, 0, 200);
	TBSetRange(IDC_SATURATION, 0, 100);
	TBSetRange(IDC_GAMMACORRECT, 50, 260);
	TBSetRange(IDC_ARTPHASE, -60, 360);
	TBSetRange(IDC_ARTSAT, 0, 400);
	TBSetRange(IDC_ARTBRI, -50, 50);

	EnableControl(IDC_PALQUIRKS, g_sim.GetGTIA().IsPALMode());

#if 0
	mArtDialog.Create(this);

	const vdrect32 rc = GetClientArea();
	mArtDialog.SetPosition(vdpoint32(rc.left, rc.bottom));

	vdsize32 sz = GetArea().size();
	sz.h += mArtDialog.GetSize().h;
	SetSize(sz, true);
#endif

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
		if (!mSettings.mbUsePALParams)
			*mpOtherParams = *mpParams;

		g_sim.GetGTIA().SetColorSettings(mSettings);
	} else {
		mSettings = gtia.GetColorSettings();

		if (gtia.IsPALMode()) {
			mpParams = &mSettings.mPALParams;
			mpOtherParams = &mSettings.mNTSCParams;
		} else {
			mpParams = &mSettings.mNTSCParams;
			mpOtherParams = &mSettings.mPALParams;
		}

		CheckButton(IDC_SHARED, !mSettings.mbUsePALParams);
		CheckButton(IDC_PALQUIRKS, mpParams->mbUsePALQuirks);

		TBSetValue(IDC_HUESTART, VDRoundToInt(mpParams->mHueStart));
		TBSetValue(IDC_HUERANGE, VDRoundToInt(mpParams->mHueRange));
		TBSetValue(IDC_BRIGHTNESS, VDRoundToInt(mpParams->mBrightness * 100.0f));
		TBSetValue(IDC_CONTRAST, VDRoundToInt(mpParams->mContrast * 100.0f));
		TBSetValue(IDC_SATURATION, VDRoundToInt(mpParams->mSaturation * 100.0f));
		TBSetValue(IDC_GAMMACORRECT, VDRoundToInt(mpParams->mGammaCorrect * 100.0f));

		sint32 adjustedHue = VDRoundToInt(mpParams->mArtifactHue);
		if (adjustedHue < -60)
			adjustedHue += 360;
		else if (adjustedHue > 360)
			adjustedHue -= 360;
		TBSetValue(IDC_ARTPHASE, adjustedHue);

		TBSetValue(IDC_ARTSAT, VDRoundToInt(mpParams->mArtifactSat * 100.0f));
		TBSetValue(IDC_ARTBRI, VDRoundToInt(mpParams->mArtifactBias * 100.0f));

		mLumaRampCombo.SetSelection(mpParams->mLumaRampMode);

		UpdateLabel(IDC_HUESTART);
		UpdateLabel(IDC_HUERANGE);
		UpdateLabel(IDC_BRIGHTNESS);
		UpdateLabel(IDC_CONTRAST);
		UpdateLabel(IDC_SATURATION);
		UpdateLabel(IDC_GAMMACORRECT);
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
	} else if (id == IDC_SHARED) {
		bool split = !IsButtonChecked(IDC_SHARED);

		if (split != mSettings.mbUsePALParams) {
			if (!split) {
				if (!Confirm(L"Enabling palette sharing will overwrite the other profile with the current colors. Proceed?", L"Altirra Warning")) {
					CheckButton(IDC_SHARED, false);
					return true;
				}
			}

			mSettings.mbUsePALParams = split;
			OnDataExchange(true);
		}

		return true;
	} else if (id == IDC_PALQUIRKS) {
		bool quirks = IsButtonChecked(IDC_PALQUIRKS);

		if (mpParams->mbUsePALQuirks != quirks) {
			mpParams->mbUsePALQuirks = quirks;

			OnDataExchange(true);
			UpdateColorImage();
		}		
	} else if (id == IDC_EXPORT) {
		const VDStringW& fn = VDGetSaveFileName('pal ', (VDGUIHandle)mhdlg, L"Export palette", L"Atari800 palette (*.pal)\0*.pal", L"pal");

		if (!fn.empty()) {
			ExportPalette(fn.c_str());
		}
	} else if (id == ID_COLORS_DEFAULTNTSC_XL) {
		mpParams->mHueStart = -36.0f;
		mpParams->mHueRange = 25.5f * 15.0f;
		mpParams->mBrightness = -0.08f;
		mpParams->mContrast = 1.08f;
		mpParams->mSaturation = 0.33f;
		mpParams->mGammaCorrect = 1.0f;
		mpParams->mArtifactHue = 279.0f;
		mpParams->mArtifactSat = 0.68f;
		mpParams->mArtifactBias = 0.25f;
		mpParams->mbUsePALQuirks = false;
		OnDataExchange(true);
		OnDataExchange(false);
	} else if (id == ID_COLORS_DEFAULTNTSC_XE) {
		mpParams->mHueStart = -36.0f;
		mpParams->mHueRange = 25.5f * 15.0f;
		mpParams->mBrightness = -0.08f;
		mpParams->mContrast = 1.08f;
		mpParams->mSaturation = 0.33f;
		mpParams->mGammaCorrect = 1.0f;
		mpParams->mArtifactHue = 220.0f;
		mpParams->mArtifactSat = 0.68f;
		mpParams->mArtifactBias = 0.25f;
		mpParams->mbUsePALQuirks = false;
		OnDataExchange(true);
		OnDataExchange(false);
	} else if (id == ID_COLORS_DEFAULTPAL) {
		mpParams->mHueStart = -23.0f;
		mpParams->mHueRange = 23.5f * 15.0f;
		mpParams->mBrightness = 0.0f;
		mpParams->mContrast = 1.0f;
		mpParams->mSaturation = 0.29f;
		mpParams->mGammaCorrect = 1.0f;
		mpParams->mArtifactHue = -96.0f;
		mpParams->mArtifactSat = 2.76f;
		mpParams->mArtifactBias = 0.35f;
		mpParams->mbUsePALQuirks = true;
		OnDataExchange(true);
		OnDataExchange(false);
	} else if (id == ID_COLORS_1XNTSC) {
		mpParams->mHueStart = -15.0f;
		mpParams->mHueRange = 360.0f * 15.0f / 14.4f;
		mpParams->mBrightness = 0.0f;
		mpParams->mContrast = 1.0f;
		mpParams->mSaturation = 75.0f / 255.0f;
		mpParams->mGammaCorrect = 1.0f;
		mpParams->mArtifactHue = -96.0f;
		mpParams->mArtifactSat = 2.76f;
		mpParams->mArtifactBias = 0.35f;
		mpParams->mbUsePALQuirks = false;
		OnDataExchange(true);
		OnDataExchange(false);
	} else if (id == ID_COLORS_25NTSC) {
		mpParams->mHueStart = -51.0f;
		mpParams->mHueRange = 27.9f * 15.0f;
		mpParams->mBrightness = 0.0f;
		mpParams->mContrast = 1.0f;
		mpParams->mSaturation = 75.0f / 255.0f;
		mpParams->mGammaCorrect = 1.0f;
		mpParams->mArtifactHue = -96.0f;
		mpParams->mArtifactSat = 2.76f;
		mpParams->mArtifactBias = 0.35f;
		mpParams->mbUsePALQuirks = false;
		OnDataExchange(true);
		OnDataExchange(false);
	} else if (id == ID_COLORS_ALTERNATE) {
		mpParams->mHueStart = -29.0f;
		mpParams->mHueRange = 26.1f * 15.0f;
		mpParams->mBrightness = -0.03f;
		mpParams->mContrast = 0.88f;
		mpParams->mSaturation = 0.23f;
		mpParams->mGammaCorrect = 1.0f;
		mpParams->mArtifactHue = -96.0f;
		mpParams->mArtifactSat = 2.76f;
		mpParams->mArtifactBias = 0.35f;
		mpParams->mbUsePALQuirks = false;
		OnDataExchange(true);
		OnDataExchange(false);
	} else if (id == ID_COLORS_AUTHENTICNTSC) {
		mpParams->mHueStart = -51.0f;
		mpParams->mHueRange = 418.0f;
		mpParams->mBrightness = -0.11f;
		mpParams->mContrast = 1.04f;
		mpParams->mSaturation = 0.34f;
		mpParams->mGammaCorrect = 1.0f;
		mpParams->mArtifactHue = -96.0f;
		mpParams->mArtifactSat = 2.76f;
		mpParams->mArtifactBias = 0.35f;
		mpParams->mbUsePALQuirks = false;
		OnDataExchange(true);
		OnDataExchange(false);
	} else if (id == ID_COLORS_G2F) {
		mpParams->mHueStart = -9.36754f;
		mpParams->mHueRange = 361.019f;
		mpParams->mBrightness = +0.174505f;
		mpParams->mContrast = 0.82371f;
		mpParams->mSaturation = 0.21993f;
		mpParams->mGammaCorrect = 1.0f;
		mpParams->mArtifactHue = -96.0f;
		mpParams->mArtifactSat = 2.76f;
		mpParams->mArtifactBias = 0.35f;
		mpParams->mbUsePALQuirks = false;
		OnDataExchange(true);
		OnDataExchange(false);
	} else if (id == ID_COLORS_OLIVIERPAL) {
		mpParams->mHueStart = -14.7889f;
		mpParams->mHueRange = 385.155f;
		mpParams->mBrightness = +0.057038f;
		mpParams->mContrast = 0.941149f;
		mpParams->mSaturation = 0.195861f;
		mpParams->mGammaCorrect = 1.0f;
		mpParams->mArtifactHue = -96.0f;
		mpParams->mArtifactSat = 2.76f;
		mpParams->mArtifactBias = 0.35f;
		mpParams->mbUsePALQuirks = false;
		OnDataExchange(true);
		OnDataExchange(false);
	}

	return false;
}

void ATAdjustColorsDialog::OnHScroll(uint32 id, int code) {
	if (id == IDC_HUESTART) {
		float v = (float)TBGetValue(IDC_HUESTART);

		if (mpParams->mHueStart != v) {
			mpParams->mHueStart = v;

			OnDataExchange(true);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_HUERANGE) {
		float v = (float)TBGetValue(IDC_HUERANGE);

		if (mpParams->mHueRange != v) {
			mpParams->mHueRange = v;

			OnDataExchange(true);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_BRIGHTNESS) {
		float v = (float)TBGetValue(IDC_BRIGHTNESS) / 100.0f;

		if (mpParams->mBrightness != v) {
			mpParams->mBrightness = v;

			OnDataExchange(true);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_CONTRAST) {
		float v = (float)TBGetValue(IDC_CONTRAST) / 100.0f;

		if (mpParams->mContrast != v) {
			mpParams->mContrast = v;

			OnDataExchange(true);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_SATURATION) {
		float v = (float)TBGetValue(IDC_SATURATION) / 100.0f;

		if (mpParams->mSaturation != v) {
			mpParams->mSaturation = v;

			OnDataExchange(true);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_GAMMACORRECT) {
		float v = (float)TBGetValue(IDC_GAMMACORRECT) / 100.0f;

		if (mpParams->mGammaCorrect != v) {
			mpParams->mGammaCorrect = v;

			OnDataExchange(true);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_ARTPHASE) {
		float v = (float)TBGetValue(IDC_ARTPHASE);

		if (mpParams->mArtifactHue != v) {
			mpParams->mArtifactHue = v;

			OnDataExchange(true);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_ARTSAT) {
		float v = (float)TBGetValue(IDC_ARTSAT) / 100.0f;

		if (mpParams->mArtifactSat != v) {
			mpParams->mArtifactSat = v;

			OnDataExchange(true);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_ARTBRI) {
		float v = (float)TBGetValue(IDC_ARTBRI) / 100.0f;

		if (mpParams->mArtifactBias != v) {
			mpParams->mArtifactBias = v;

			OnDataExchange(true);
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
					19,
					1,
					32,
					BI_RGB,
					16*19*4,
					0,
					0,
					0,
					0
				}
			};

			uint32 pal[256 + 48] = {0};
			g_sim.GetGTIA().GetPalette(pal + 48);

			// last three rows are used for 'text' screen
			pal[0x10] = pal[0x30];
			for(int i=0; i<14; ++i) {
				pal[0x11 + i] = pal[0x30 + 0x94];
				pal[0x01 + i] = pal[0x30 + 0x94];
			}
			pal[0x10+2] = pal[0x30 + 0x9A];
			pal[0x10+15] = pal[0x30];

			// flip palette section
			for(int i=0; i<128; i += 16)
				VDSwapMemory(&pal[i+48], &pal[240+48-i], 16*sizeof(uint32));

			StretchDIBits(drawInfo.hDC,
				drawInfo.rcItem.left,
				drawInfo.rcItem.top,
				drawInfo.rcItem.right - drawInfo.rcItem.left,
				drawInfo.rcItem.bottom - drawInfo.rcItem.top,
				0, 0, 16, 19, pal, &bi, DIB_RGB_COLORS, SRCCOPY);

			SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}
	}

	return VDDialogFrameW32::DlgProc(msg, wParam, lParam);
}

void ATAdjustColorsDialog::UpdateLabel(uint32 id) {
	switch(id) {
		case IDC_HUESTART:
			SetControlTextF(IDC_STATIC_HUESTART, L"%.0f\u00B0", mpParams->mHueStart);
			break;
		case IDC_HUERANGE:
			SetControlTextF(IDC_STATIC_HUERANGE, L"%.1f\u00B0", mpParams->mHueRange / 15.0f);
			break;
		case IDC_BRIGHTNESS:
			SetControlTextF(IDC_STATIC_BRIGHTNESS, L"%+.0f%%", mpParams->mBrightness * 100.0f);
			break;
		case IDC_CONTRAST:
			SetControlTextF(IDC_STATIC_CONTRAST, L"%.0f%%", mpParams->mContrast * 100.0f);
			break;
		case IDC_SATURATION:
			SetControlTextF(IDC_STATIC_SATURATION, L"%.0f%%", mpParams->mSaturation * 100.0f);
			break;
		case IDC_GAMMACORRECT:
			SetControlTextF(IDC_STATIC_GAMMACORRECT, L"%.2f", mpParams->mGammaCorrect);
			break;
		case IDC_ARTPHASE:
			SetControlTextF(IDC_STATIC_ARTPHASE, L"%.0f\u00B0", mpParams->mArtifactHue);
			break;
		case IDC_ARTSAT:
			SetControlTextF(IDC_STATIC_ARTSAT, L"%.0f%%", mpParams->mArtifactSat * 100.0f);
			break;
		case IDC_ARTBRI:
			SetControlTextF(IDC_STATIC_ARTBRI, L"%+.0f%%", mpParams->mArtifactBias * 100.0f);
			break;
	}
}

void ATAdjustColorsDialog::UpdateColorImage() {
	// update image
	HWND hwndColors = GetDlgItem(mhdlg, IDC_COLORS);
	InvalidateRect(hwndColors, NULL, FALSE);
}

void ATAdjustColorsDialog::ExportPalette(const wchar_t *s) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	uint32 pal[256];
	gtia.GetPalette(pal);

	uint8 pal8[768];
	for(int i=0; i<256; ++i) {
		const uint32 c = pal[i];

		pal8[i*3+0] = (uint8)(c >> 16);
		pal8[i*3+1] = (uint8)(c >>  8);
		pal8[i*3+2] = (uint8)(c >>  0);
	}

	VDFile f(s, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	f.write(pal8, sizeof pal8);
}

void ATAdjustColorsDialog::OnLumaRampChanged(VDUIProxyComboBoxControl *sender, int sel) {
	ATLumaRampMode newMode = (ATLumaRampMode)sel;

	if (mpParams->mLumaRampMode != newMode) {
		mpParams->mLumaRampMode = newMode;

		OnDataExchange(true);
		UpdateColorImage();
	}
}

void ATUIOpenAdjustColorsDialog(VDGUIHandle hParent) {
	g_adjustColorsDialog.Create(hParent);
}

void ATUICloseAdjustColorsDialog() {
	g_adjustColorsDialog.Destroy();
}
