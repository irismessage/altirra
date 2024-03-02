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
#include <at/atnativeui/messageloop.h>
#include "resource.h"
#include "gtia.h"
#include "simulator.h"

extern ATSimulator g_sim;

///////////////////////////////////////////////////////////////////////////

class ATAdjustScreenEffectsDialog final : public VDDialogFrameW32 {
public:
	ATAdjustScreenEffectsDialog();

private:
	bool PreNCDestroy() override;
	bool OnLoaded() override;
	void OnDestroy() override;
	void OnDataExchange(bool write) override;
	void OnEnable(bool enable) override;
	void OnHScroll(uint32 id, int code) override;
	void UpdateLabel(uint32 id);
	void UpdateEnables();
	void ResetToDefaults();
	void OnBloomEnableChanged();
	void OnBloomScanlineCompensationChanged();
	void OnHDREnableChanged();

	ATGTIAEmulator::HDRAvailability mLastHDRAvailability = ATGTIAEmulator::HDRAvailability::Available;
	VDUIProxyButtonControl mBloomEnableView;
	VDUIProxyButtonControl mBloomScanlineCompensationView;
	VDUIProxyButtonControl mHDREnableView;
	VDUIProxyButtonControl mResetToDefaultsView;
};

ATAdjustScreenEffectsDialog *g_pATAdjustScreenEffectsDialog;

ATAdjustScreenEffectsDialog::ATAdjustScreenEffectsDialog()
	: VDDialogFrameW32(IDD_ADJUST_SCREENFX)
{
	mBloomEnableView.SetOnClicked(
		[this] { OnBloomEnableChanged(); }
	);

	mBloomScanlineCompensationView.SetOnClicked(
		[this] { OnBloomScanlineCompensationChanged(); }
	);

	mHDREnableView.SetOnClicked(
		[this] { OnHDREnableChanged(); }
	);

	mResetToDefaultsView.SetOnClicked(
		[this] { ResetToDefaults(); }
	);
}

bool ATAdjustScreenEffectsDialog::PreNCDestroy() {
	g_pATAdjustScreenEffectsDialog = nullptr;
	return true;
}

bool ATAdjustScreenEffectsDialog::OnLoaded() {
	ATUIRegisterModelessDialog(mhwnd);

	AddProxy(&mBloomEnableView, IDC_ENABLE_BLOOM);
	AddProxy(&mBloomScanlineCompensationView, IDC_SCANLINECOMPENSATION);
	AddProxy(&mHDREnableView, IDC_ENABLEHDR);
	AddProxy(&mResetToDefaultsView, IDC_RESET);

	TBSetRange(IDC_SCANLINE_INTENSITY, 1, 7);
	TBSetRange(IDC_DISTORTION_X, 0, 180);
	TBSetRange(IDC_DISTORTION_Y, 0, 100);
	TBSetRange(IDC_BLOOM_THRESHOLD, 0, 100);
	TBSetRange(IDC_BLOOM_RADIUS, 0, 100);
	TBSetRange(IDC_BLOOM_DIRECT_INTENSITY, 0, 200);
	TBSetRange(IDC_BLOOM_INDIRECT_INTENSITY, 0, 200);

	TBSetRange(IDC_SDRBRIGHTNESS, 0, 400);
	TBSetRange(IDC_HDRBRIGHTNESS, 0, 400);

	OnDataExchange(false);
	SetFocusToControl(IDC_SCANLINE_INTENSITY);
	return true;
}

void ATAdjustScreenEffectsDialog::OnDestroy() {
	ATUIUnregisterModelessDialog(mhwnd);

	VDDialogFrameW32::OnDestroy();
}

void ATAdjustScreenEffectsDialog::OnDataExchange(bool write) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	if (!write) {
		const auto& params = gtia.GetArtifactingParams();

		mBloomEnableView.SetChecked(params.mbEnableBloom);
		mBloomScanlineCompensationView.SetChecked(params.mbBloomScanlineCompensation);
		mHDREnableView.SetChecked(params.mbEnableHDR);

		TBSetValue(IDC_SCANLINE_INTENSITY, VDRoundToInt(params.mScanlineIntensity * 8.0f));
		TBSetValue(IDC_DISTORTION_X, VDRoundToInt(params.mDistortionViewAngleX));
		TBSetValue(IDC_DISTORTION_Y, VDRoundToInt(params.mDistortionYRatio * 100.0f));
		TBSetValue(IDC_BLOOM_THRESHOLD, VDRoundToInt(params.mBloomThreshold * 100.0f));
		TBSetValue(IDC_BLOOM_RADIUS, VDRoundToInt(params.mBloomRadius * 5.0f));
		TBSetValue(IDC_BLOOM_DIRECT_INTENSITY, VDRoundToInt(params.mBloomDirectIntensity * 100.0f));
		TBSetValue(IDC_BLOOM_INDIRECT_INTENSITY, VDRoundToInt(params.mBloomIndirectIntensity * 100.0f));
		TBSetValue(IDC_SDRBRIGHTNESS, VDRoundToInt(logf(params.mSDRIntensity / 80.0f) / logf(2.0f) * 100.0f));
		TBSetValue(IDC_HDRBRIGHTNESS, VDRoundToInt(logf(params.mHDRIntensity / 80.0f) / logf(2.0f) * 100.0f));

		UpdateLabel(IDC_SCANLINE_INTENSITY);
		UpdateLabel(IDC_DISTORTION_X);
		UpdateLabel(IDC_DISTORTION_Y);
		UpdateLabel(IDC_BLOOM_THRESHOLD);
		UpdateLabel(IDC_BLOOM_RADIUS);
		UpdateLabel(IDC_BLOOM_DIRECT_INTENSITY);
		UpdateLabel(IDC_BLOOM_INDIRECT_INTENSITY);
		UpdateLabel(IDC_SDRBRIGHTNESS);
		UpdateLabel(IDC_HDRBRIGHTNESS);
		UpdateEnables();
	}
}

void ATAdjustScreenEffectsDialog::OnEnable(bool enable) {
	ATUISetGlobalEnableState(enable);
}

void ATAdjustScreenEffectsDialog::OnHScroll(uint32 id, int code) {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetArtifactingParams();
	bool update = false;

	if (id == IDC_SCANLINE_INTENSITY) {
		float v = (float)TBGetValue(IDC_SCANLINE_INTENSITY) / 8.0f;

		if (fabsf(params.mScanlineIntensity - v) > 1e-5f) {
			params.mScanlineIntensity = v;
			update = true;
		}
	} else if (id == IDC_DISTORTION_X) {
		float v = (float)TBGetValue(IDC_DISTORTION_X);

		if (fabsf(params.mDistortionViewAngleX - v) > 1e-5f) {
			params.mDistortionViewAngleX = v;
			update = true;
		}
	} else if (id == IDC_DISTORTION_Y) {
		float v = (float)TBGetValue(IDC_DISTORTION_Y) / 100.0f;

		if (fabsf(params.mDistortionYRatio - v) > 1e-5f) {
			params.mDistortionYRatio = v;
			update = true;
		}
	} else if (id == IDC_BLOOM_THRESHOLD) {
		float v = (float)TBGetValue(IDC_BLOOM_THRESHOLD) / 100.0f;

		if (fabsf(params.mBloomThreshold - v) > 1e-5f) {
			params.mBloomThreshold = v;
			update = true;
		}
	} else if (id == IDC_BLOOM_RADIUS) {
		float v = (float)TBGetValue(IDC_BLOOM_RADIUS) / 5.0f;

		if (fabsf(params.mBloomRadius - v) > 1e-5f) {
			params.mBloomRadius = v;
			update = true;
		}
	} else if (id == IDC_BLOOM_DIRECT_INTENSITY) {
		float v = (float)TBGetValue(IDC_BLOOM_DIRECT_INTENSITY) / 100.0f;

		if (fabsf(params.mBloomDirectIntensity - v) > 1e-5f) {
			params.mBloomDirectIntensity = v;
			update = true;
		}
	} else if (id == IDC_BLOOM_INDIRECT_INTENSITY) {
		float v = (float)TBGetValue(IDC_BLOOM_INDIRECT_INTENSITY) / 100.0f;

		if (fabsf(params.mBloomIndirectIntensity - v) > 1e-5f) {
			params.mBloomIndirectIntensity = v;
			update = true;
		}
	} else if (id == IDC_SDRBRIGHTNESS) {
		float v = 80.0f * powf(2.0f, (float)TBGetValue(id) / 100.0f);

		if (fabsf(params.mSDRIntensity - v) > 1e-5f) {
			params.mSDRIntensity = v;
			update = true;
		}
	} else if (id == IDC_HDRBRIGHTNESS) {
		float v = 80.0f * powf(2.0f, (float)TBGetValue(id) / 100.0f);

		if (fabsf(params.mHDRIntensity - v) > 1e-5f) {
			params.mHDRIntensity = v;
			update = true;
		}
	}

	if (update) {
		gtia.SetArtifactingParams(params);
		UpdateLabel(id);
	}
}

void ATAdjustScreenEffectsDialog::UpdateLabel(uint32 id) {
	const auto& params = g_sim.GetGTIA().GetArtifactingParams();

	switch(id) {
		case IDC_SCANLINE_INTENSITY:
			SetControlTextF(IDC_STATIC_SCANLINE_INTENSITY, L"%d%%", (int)(params.mScanlineIntensity * 100.0f + 0.5f));
			break;
		case IDC_DISTORTION_X:
			SetControlTextF(IDC_STATIC_DISTORTION_X, L"%.0f\u00B0", params.mDistortionViewAngleX);
			break;
		case IDC_DISTORTION_Y:
			SetControlTextF(IDC_STATIC_DISTORTION_Y, L"%.0f%%", params.mDistortionYRatio * 100.0f);
			break;
		case IDC_BLOOM_THRESHOLD:
			SetControlTextF(IDC_STATIC_BLOOM_THRESHOLD, L"%.2f", params.mBloomThreshold);
			break;
		case IDC_BLOOM_RADIUS:
			SetControlTextF(IDC_STATIC_BLOOM_RADIUS, L"%.1f cclk", params.mBloomRadius * 0.5f);
			break;
		case IDC_BLOOM_DIRECT_INTENSITY:
			SetControlTextF(IDC_STATIC_BLOOM_DIRECT_INTENSITY, L"%.2f", params.mBloomDirectIntensity);
			break;
		case IDC_BLOOM_INDIRECT_INTENSITY:
			SetControlTextF(IDC_STATIC_BLOOM_INDIRECT_INTENSITY, L"%.2f", params.mBloomIndirectIntensity);
			break;
		case IDC_SDRBRIGHTNESS:
			SetControlTextF(IDC_SDRBRIGHTNESS_VALUE, L"%.0f nits", params.mSDRIntensity);
			break;
		case IDC_HDRBRIGHTNESS:
			SetControlTextF(IDC_HDRBRIGHTNESS_VALUE, L"%.0f nits", params.mHDRIntensity);
			break;
	}
}

void ATAdjustScreenEffectsDialog::UpdateEnables() {
	const bool hwSupport = g_sim.GetGTIA().AreAcceleratedEffectsAvailable();
	const ATGTIAEmulator::HDRAvailability hdrAvailability = g_sim.GetGTIA().IsHDRRenderingAvailable();
	const bool hwHdrSupport = hdrAvailability == ATGTIAEmulator::HDRAvailability::Available;

	if (mLastHDRAvailability != hdrAvailability) {
		mLastHDRAvailability = hdrAvailability;

		switch(hdrAvailability) {
			case ATGTIAEmulator::HDRAvailability::NoMinidriverSupport:
				SetControlText(IDC_HDRWARNING, L"HDR display is not available with the currently selected graphics API. DirectX 11 is required for HDR support.");
				break;

			case ATGTIAEmulator::HDRAvailability::NoSystemSupport:
				SetControlText(IDC_HDRWARNING, L"HDR display is not available with the current operating system. DXGI 1.6 (Windows 10 1703+) is required for HDR support.");
				break;

			case ATGTIAEmulator::HDRAvailability::NoHardwareSupport:
				SetControlText(IDC_HDRWARNING, L"HDR display is not available with the current graphics driver.");
				break;

			case ATGTIAEmulator::HDRAvailability::NoDisplaySupport:
				SetControlText(IDC_HDRWARNING, L"HDR is not available with the current display. HDR must be supported and enabled in Windows display settings.");
				break;

			case ATGTIAEmulator::HDRAvailability::AccelNotEnabled:
				SetControlText(IDC_HDRWARNING, L"HDR display is not available because hardware accelerated display effects are not enabled in Configure System, Display.");
				break;

			case ATGTIAEmulator::HDRAvailability::Available:
				break;
		}
	}

	const bool bloomEnabled = hwSupport && mBloomEnableView.GetChecked();
	const bool hdrEnabled = hwHdrSupport && mHDREnableView.GetChecked();

	ShowControl(IDC_DISTORTION_X, hwSupport);
	ShowControl(IDC_DISTORTION_Y, hwSupport);
	ShowControl(IDC_LABEL_DISTORTION_X, hwSupport);
	ShowControl(IDC_LABEL_DISTORTION_Y, hwSupport);
	ShowControl(IDC_STATIC_DISTORTION_X, hwSupport);
	ShowControl(IDC_STATIC_DISTORTION_Y, hwSupport);
	mBloomEnableView.SetVisible(hwSupport);
	mBloomScanlineCompensationView.SetVisible(hwSupport);
	ShowControl(IDC_BLOOM_THRESHOLD, hwSupport);
	ShowControl(IDC_BLOOM_RADIUS, hwSupport);
	ShowControl(IDC_BLOOM_DIRECT_INTENSITY, hwSupport);
	ShowControl(IDC_BLOOM_INDIRECT_INTENSITY, hwSupport);
	ShowControl(IDC_STATIC_BLOOM_THRESHOLD, hwSupport);
	ShowControl(IDC_STATIC_BLOOM_RADIUS, hwSupport);
	ShowControl(IDC_STATIC_BLOOM_DIRECT_INTENSITY, hwSupport);
	ShowControl(IDC_STATIC_BLOOM_INDIRECT_INTENSITY, hwSupport);
	ShowControl(IDC_LABEL_BLOOM_THRESHOLD, hwSupport);
	ShowControl(IDC_LABEL_BLOOM_RADIUS, hwSupport);
	ShowControl(IDC_LABEL_BLOOM_DIRECT_INTENSITY, hwSupport);
	ShowControl(IDC_LABEL_BLOOM_INDIRECT_INTENSITY, hwSupport);

	mBloomScanlineCompensationView.SetEnabled(bloomEnabled);
	EnableControl(IDC_BLOOM_THRESHOLD, bloomEnabled);
	EnableControl(IDC_BLOOM_RADIUS, bloomEnabled);
	EnableControl(IDC_BLOOM_DIRECT_INTENSITY, bloomEnabled);
	EnableControl(IDC_BLOOM_INDIRECT_INTENSITY, bloomEnabled);
	ShowControl(IDC_WARNING, !hwSupport);

	ShowControl(IDC_HDRWARNING, !hwHdrSupport);
	ShowControl(IDC_ENABLEHDR, hwHdrSupport);
	ShowControl(IDC_STATIC_SDRBRIGHTNESS, hwHdrSupport);
	ShowControl(IDC_STATIC_HDRBRIGHTNESS, hwHdrSupport);
	ShowControl(IDC_SDRBRIGHTNESS, hwHdrSupport);
	ShowControl(IDC_HDRBRIGHTNESS, hwHdrSupport);
	ShowControl(IDC_SDRBRIGHTNESS_VALUE, hwHdrSupport);
	ShowControl(IDC_HDRBRIGHTNESS_VALUE, hwHdrSupport);

	EnableControl(IDC_STATIC_SDRBRIGHTNESS, hdrEnabled);
	EnableControl(IDC_STATIC_HDRBRIGHTNESS, hdrEnabled);
	EnableControl(IDC_SDRBRIGHTNESS, hdrEnabled);
	EnableControl(IDC_HDRBRIGHTNESS, hdrEnabled);
	EnableControl(IDC_SDRBRIGHTNESS_VALUE, hdrEnabled);
	EnableControl(IDC_HDRBRIGHTNESS_VALUE, hdrEnabled);
}

void ATAdjustScreenEffectsDialog::ResetToDefaults() {
	auto& gtia = g_sim.GetGTIA();

	gtia.SetArtifactingParams(ATArtifactingParams::GetDefault());

	OnDataExchange(false);
}

void ATAdjustScreenEffectsDialog::OnBloomEnableChanged() {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetArtifactingParams();
	bool enable = mBloomEnableView.GetChecked();

	if (params.mbEnableBloom != enable) {
		params.mbEnableBloom = enable;

		gtia.SetArtifactingParams(params);

		UpdateEnables();
	}
}

void ATAdjustScreenEffectsDialog::OnBloomScanlineCompensationChanged() {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetArtifactingParams();
	bool enable = mBloomScanlineCompensationView.GetChecked();

	if (params.mbBloomScanlineCompensation != enable) {
		params.mbBloomScanlineCompensation = enable;

		gtia.SetArtifactingParams(params);
	}
}

void ATAdjustScreenEffectsDialog::OnHDREnableChanged() {
	auto& gtia = g_sim.GetGTIA();
	auto params = gtia.GetArtifactingParams();
	bool enable = mHDREnableView.GetChecked();

	if (params.mbEnableHDR != enable) {
		params.mbEnableHDR = enable;

		gtia.SetArtifactingParams(params);

		UpdateEnables();
	}
}

void ATUIOpenAdjustScreenEffectsDialog(VDGUIHandle hParent) {
	if (g_pATAdjustScreenEffectsDialog) {
		g_pATAdjustScreenEffectsDialog->Activate();
	} else {
		g_pATAdjustScreenEffectsDialog = new ATAdjustScreenEffectsDialog;
		if (!g_pATAdjustScreenEffectsDialog->Create(hParent)) {
			delete g_pATAdjustScreenEffectsDialog;
			g_pATAdjustScreenEffectsDialog = nullptr;
		}
	}
}

void ATUICloseAdjustScreenEffectsDialog() {
	if (g_pATAdjustScreenEffectsDialog)
		g_pATAdjustScreenEffectsDialog->Destroy();
}
