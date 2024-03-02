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
#include <vd2/system/math.h>
#include <at/atui/dialog.h>
#include "resource.h"
#include "audiooutput.h"
#include "simulator.h"
#include "audiosyncmixer.h"

extern ATSimulator g_sim;

class ATAudioOptionsDialog : public VDDialogFrameW32 {
public:
	ATAudioOptionsDialog();
	~ATAudioOptionsDialog();

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	void OnHScroll(uint32 id, int code);
	void UpdateVolumeLabel();
	void UpdateDriveVolumeLabel();
	void UpdateLatencyLabel();
	void UpdateExtraBufferLabel();

	int mVolumeTick;
	int mDriveVolTick;
	int mLatencyTick;
	int mExtraBufferTick;
};

ATAudioOptionsDialog::ATAudioOptionsDialog()
	: VDDialogFrameW32(IDD_AUDIO_OPTIONS)
{
}

ATAudioOptionsDialog::~ATAudioOptionsDialog() {
}

bool ATAudioOptionsDialog::OnLoaded() {
	TBSetRange(IDC_VOLUME, 0, 200);
	TBSetRange(IDC_DRIVEVOL, 0, 200);
	TBSetRange(IDC_LATENCY, 1, 50);
	TBSetRange(IDC_EXTRABUFFER, 2, 50);

	OnDataExchange(false);
	SetFocusToControl(IDC_VOLUME);
	return true;
}

void ATAudioOptionsDialog::OnDataExchange(bool write) {
	IATAudioOutput *audioOut = g_sim.GetAudioOutput();
	ATAudioSyncMixer *syncMix = g_sim.GetAudioSyncMixer();

	if (write) {
		audioOut->SetVolume(powf(10.0f, (mVolumeTick - 200) * 0.01f));
		syncMix->SetMixLevel(kATAudioMix_Drive, powf(10.0f, (mDriveVolTick - 200) * 0.01f));
		audioOut->SetLatency(mLatencyTick * 10);
		audioOut->SetExtraBuffer(mExtraBufferTick * 10);
		audioOut->SetApi(IsButtonChecked(IDC_API_DIRECTSOUND) ? kATAudioApi_DirectSound : kATAudioApi_WaveOut);

		if (IsButtonChecked(IDC_DEBUG))
			audioOut->SetStatusRenderer(g_sim.GetUIRenderer());
		else
			audioOut->SetStatusRenderer(NULL);
	} else {
		const float volume = audioOut->GetVolume();
		mVolumeTick = 200 + VDRoundToInt(100.0f * log10(volume));

		const float drivevol = syncMix->GetMixLevel(kATAudioMix_Drive);
		mDriveVolTick = 200 + VDRoundToInt(100.0f * log10(drivevol));
		mLatencyTick = (audioOut->GetLatency() + 5) / 10;
		mExtraBufferTick = (audioOut->GetExtraBuffer() + 5) / 10;

		CheckButton(audioOut->GetApi() == kATAudioApi_DirectSound ? IDC_API_DIRECTSOUND : IDC_API_WAVEOUT, true);
		CheckButton(IDC_DEBUG, audioOut->GetStatusRenderer() != NULL);

		TBSetValue(IDC_VOLUME, mVolumeTick);
		TBSetValue(IDC_DRIVEVOL, mDriveVolTick);
		TBSetValue(IDC_LATENCY, mLatencyTick);
		TBSetValue(IDC_EXTRABUFFER, mExtraBufferTick);
		UpdateVolumeLabel();
		UpdateDriveVolumeLabel();
		UpdateLatencyLabel();
		UpdateExtraBufferLabel();
	}
}

void ATAudioOptionsDialog::OnHScroll(uint32 id, int code) {
	if (id == IDC_VOLUME) {
		int tick = TBGetValue(IDC_VOLUME);

		if (tick != mVolumeTick) {
			mVolumeTick = tick;
			UpdateVolumeLabel();
		}
	} else if (id == IDC_DRIVEVOL) {
		int tick = TBGetValue(IDC_DRIVEVOL);

		if (tick != mDriveVolTick) {
			mDriveVolTick = tick;
			UpdateDriveVolumeLabel();
		}
	} else if (id == IDC_LATENCY) {
		int tick = TBGetValue(IDC_LATENCY);

		if (tick != mLatencyTick) {
			mLatencyTick = tick;
			UpdateLatencyLabel();
		}
	} else if (id == IDC_EXTRABUFFER) {
		int tick = TBGetValue(IDC_EXTRABUFFER);

		if (tick != mExtraBufferTick) {
			mExtraBufferTick = tick;
			UpdateExtraBufferLabel();
		}
	}
}

void ATAudioOptionsDialog::UpdateVolumeLabel() {
	SetControlTextF(IDC_STATIC_VOLUME, L"%.1fdB", 0.1f * (mVolumeTick - 200));
}

void ATAudioOptionsDialog::UpdateDriveVolumeLabel() {
	SetControlTextF(IDC_STATIC_DRIVEVOL, L"%.1fdB", 0.1f * (mDriveVolTick - 200));
}

void ATAudioOptionsDialog::UpdateLatencyLabel() {
	SetControlTextF(IDC_STATIC_LATENCY, L"%d ms", mLatencyTick * 10);
}

void ATAudioOptionsDialog::UpdateExtraBufferLabel() {
	SetControlTextF(IDC_STATIC_EXTRABUFFER, L"%d ms", mExtraBufferTick * 10);
}

void ATUIShowAudioOptionsDialog(VDGUIHandle hParent) {
	ATAudioOptionsDialog dlg;
	dlg.ShowDialog(hParent);
}
