//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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
#include <vd2/system/registry.h>
#include <at/atnativeui/dialog.h>
#include "resource.h"
#include "uipageddialog.h"
#include "uitypes.h"
#include "videowriter.h"

class ATUIDialogVideoRecording final : public ATUIContextualHelpDialog {
public:
	ATUIDialogVideoRecording(bool pal);

	ATVideoEncoding GetEncoding() const { return mEncoding; }
	ATVideoRecordingFrameRate GetFrameRate() const { return mFrameRate; }
	ATVideoRecordingAspectRatioMode GetAspectRatioMode() const { return mAspectRatioMode; }
	ATVideoRecordingResamplingMode GetResamplingMode() const { return mResamplingMode; }
	ATVideoRecordingScalingMode GetScalingMode() const { return mScalingMode; }
	uint32 GetVideoBitRate() const { return mVideoBitRate; }
	uint32 GetAudioBitRate() const { return mAudioBitRate; }
	bool GetHalfRate() const { return mbHalfRate; }
	bool GetEncodeAllFrames() const { return mbEncodeAllFrames; }

protected:
	VDZHWND GetHelpBaseWindow() const override final;
	ATUIContextualHelpProvider *GetHelpProvider() override final;

private:
	bool OnLoaded() override;
	void OnDataExchange(bool write) override;
	bool OnCommand(uint32 id, uint32 extcode) override;
	bool OnOK() override;
	void OnHScroll(uint32 id, int code) override;

	void UpdateFrameRateControls();
	void UpdateEnables();
	void UpdateVideoBitrateLabel();
	void UpdateAudioBitrateLabel();

	ATUIContextualHelpProvider::HelpEntry& OnGetDynamicHelp(const ATUIContextualHelpProvider::HelpEntry& he);

	bool mbPAL;
	bool mbHalfRate = false;
	bool mbEncodeAllFrames = false;
	uint32 mVideoBitRate = 1000000;
	uint32 mAudioBitRate = 128000;
	ATVideoEncoding mEncoding = kATVideoEncoding_ZMBV;
	ATVideoRecordingFrameRate mFrameRate = kATVideoRecordingFrameRate_Normal;
	ATVideoRecordingResamplingMode mResamplingMode;
	ATVideoRecordingAspectRatioMode mAspectRatioMode;
	ATVideoRecordingScalingMode mScalingMode;

	VDUIProxyComboBoxControl mVideoCodecView;
	VDUIProxyComboBoxControl mResamplingModeView;
	VDUIProxyComboBoxControl mAspectRatioModeView;
	VDUIProxyComboBoxControl mScalingModeView;

	VDStringW mBaseFrameRateLabels[3];

	ATUIContextualHelpProvider mHelpProvider;
	ATUIContextualHelpProvider::HelpEntry mTempHelpEntry;

	static const uint32 kFrameRateIds[];
};

const uint32 ATUIDialogVideoRecording::kFrameRateIds[]={
	IDC_FRAMERATE_NORMAL,
	IDC_FRAMERATE_NTSCRATIO,
	IDC_FRAMERATE_INTEGRAL,
};

ATUIDialogVideoRecording::ATUIDialogVideoRecording(bool pal)
	: ATUIContextualHelpDialog(IDD_VIDEO_RECORDING)
	, mbPAL(pal)
{
	mHelpProvider.LinkParent([this](uint32 id) { return GetControlPos(id); });
	mHelpProvider.SetDynamicHelpProvider([this](const ATUIContextualHelpProvider::HelpEntry& he) -> ATUIContextualHelpProvider::HelpEntry& { return OnGetDynamicHelp(he); });

	mVideoCodecView.SetOnSelectionChanged([this](int) { UpdateEnables(); RefreshHelp(IDC_VC); });
}

VDZHWND ATUIDialogVideoRecording::GetHelpBaseWindow() const {
	return mhdlg;
}

ATUIContextualHelpProvider *ATUIDialogVideoRecording::GetHelpProvider() {
	return &mHelpProvider;
}

bool ATUIDialogVideoRecording::OnLoaded() {
	AddProxy(&mVideoCodecView, IDC_VC);
	AddProxy(&mResamplingModeView, IDC_RESAMPLING);
	AddProxy(&mAspectRatioModeView, IDC_ASPECTRATIO);
	AddProxy(&mScalingModeView, IDC_FRAMESIZEMODE);

	TBSetRange(IDC_VIDEOBITRATE, 5, 80);
	TBSetRange(IDC_AUDIOBITRATE, 0, 3);

	mVideoCodecView.AddItem(L"Uncompressed (AVI)");
	mVideoCodecView.AddItem(L"Run-Length Encoding (AVI)");
	mVideoCodecView.AddItem(L"Zipped Motion Block Vector (AVI)");
	mVideoCodecView.AddItem(L"Windows Media Video 7 + WMAv8 (WMV)");
	mVideoCodecView.AddItem(L"Windows Media Video 9 + WMAv8 (WMV)");
	mVideoCodecView.AddItem(L"H.264 + MP3 (MP4)");
	mVideoCodecView.AddItem(L"H.264 + AAC (MP4)");

	mResamplingModeView.AddItem(L"Bilinear - smooth resampling");
	mResamplingModeView.AddItem(L"Sharp Bilinear - sharper resampling");
	mResamplingModeView.AddItem(L"Nearest - sharpest resampling");

	mAspectRatioModeView.AddItem(L"Full - use correct aspect ratio");
	mAspectRatioModeView.AddItem(L"Pixel double - 1x/2x only");
	mAspectRatioModeView.AddItem(L"None - record raw pixels");

	mScalingModeView.AddItem(L"No scaling");
	mScalingModeView.AddItem(L"Scale to 640x480 (4:3)");
	mScalingModeView.AddItem(L"Scale to 854x480 (16:9)");
	mScalingModeView.AddItem(L"Scale to 960x720 (4:3)");
	mScalingModeView.AddItem(L"Scale to 1280x720 (16:9)");

	mHelpProvider.AddHelpEntry(IDC_VC, L"", L"");
	mHelpProvider.AddHelpEntry(IDC_ENCODE_ALL_FRAMES, L"Encode duplicate frames as full frames",
		L"Disable duplicate frame optimizations. This is required when uploading AVI files to YouTube, which does not handle null frames correctly. It does not apply to WMV or MP4 output.");
	mHelpProvider.AddHelpEntry(IDC_HALF_RATE, L"Record at half frame rate",
		L"Record only every other frame, to reduce file size and CPU load.");
	mHelpProvider.AddHelpEntry(IDC_FRAMERATE_NORMAL, L"Frame rate: Accurate",
		L"Record at the actual frame rate of the original computer. This is the most correct, but results in a slightly non-standard frame rate that may occasionally jerk during playback.");
	mHelpProvider.AddHelpEntry(IDC_FRAMERATE_NTSCRATIO, L"Frame rate: Broadcast",
		L"Record slightly faster at the same rate as broadcast TV. This uses a standard frame rate that other programs and services are most likely to support without problems.");
	mHelpProvider.AddHelpEntry(IDC_FRAMERATE_INTEGRAL, L"Frame rate: Integral",
		L"Record slightly faster at an even 50/60Hz. This uses a frame rate likely to match computer display refresh rates for smoothest playback.");
	mHelpProvider.AddHelpEntry(IDC_FRAMESIZEMODE, L"Scaling mode",
		L"Scale the video up to a standard frame size used for 4:3 or 16:9 video, particularly those recommended by YouTube.");
	mHelpProvider.AddHelpEntry(IDC_ASPECTRATIO, L"Aspect ratio mode",
		L"Scale the video to correct the pixel aspect ratio. Full correction uses the aspect ratio, but results in uneven scaling. Pixel doubling mode applies 2x scaling to get close with even scaling. None just uses the raw pixels from the emulation.");
	mHelpProvider.AddHelpEntry(IDC_RESAMPLING, L"Resampling mode",
		L"Controls how the video is scaled. Bilinear is smoothest but can be blurry. Nearest is sharpest but looks bad with uneven ratios and can cause video compression artifacts. Sharp Bilinear is in between.");

	for(int i=0; i<3; ++i)
		GetControlText(kFrameRateIds[i], mBaseFrameRateLabels[i]);

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogVideoRecording::OnDataExchange(bool write) {
	VDRegistryAppKey key("Settings");

	if (write) {
		switch(mVideoCodecView.GetSelection()) {
			case 0:	mEncoding = kATVideoEncoding_Raw; break;
			case 1:	mEncoding = kATVideoEncoding_RLE; break;
			default:
			case 2: mEncoding = kATVideoEncoding_ZMBV; break;
			case 3: mEncoding = kATVideoEncoding_WMV7; break;
			case 4: mEncoding = kATVideoEncoding_WMV9; break;
			case 5: mEncoding = kATVideoEncoding_H264_MP3; break;
			case 6: mEncoding = kATVideoEncoding_H264_AAC; break;
		}

		if (IsButtonChecked(IDC_FRAMERATE_NORMAL))
			mFrameRate = kATVideoRecordingFrameRate_Normal;
		else if (IsButtonChecked(IDC_FRAMERATE_NTSCRATIO))
			mFrameRate = kATVideoRecordingFrameRate_NTSCRatio;
		else if (IsButtonChecked(IDC_FRAMERATE_INTEGRAL))
			mFrameRate = kATVideoRecordingFrameRate_Integral;

		mAspectRatioMode = (ATVideoRecordingAspectRatioMode)std::clamp(((int)ATVideoRecordingAspectRatioMode::Count - 1) - mAspectRatioModeView.GetSelection(), 0, (int)ATVideoRecordingAspectRatioMode::Count - 1);
		mResamplingMode = (ATVideoRecordingResamplingMode)std::clamp(((int)ATVideoRecordingResamplingMode::Count - 1) - mResamplingModeView.GetSelection(), 0, (int)ATVideoRecordingResamplingMode::Count - 1);
		mScalingMode = (ATVideoRecordingScalingMode)std::clamp(mScalingModeView.GetSelection(), 0, (int)ATVideoRecordingScalingMode::Count - 1);

		mbHalfRate = IsButtonChecked(IDC_HALF_RATE);
		mbEncodeAllFrames = IsButtonChecked(IDC_ENCODE_ALL_FRAMES);

		mVideoBitRate = TBGetValue(IDC_VIDEOBITRATE) * 100000;
		mAudioBitRate = TBGetValue(IDC_AUDIOBITRATE) * 32000 + 96000;

		key.setInt("Video Recording: Compression Mode", mEncoding);
		key.setInt("Video Recording: Frame Rate", mFrameRate);
		key.setBool("Video Recording: Half Rate", mbHalfRate);
		key.setBool("Video Recording: Encode All Frames", mbEncodeAllFrames);
		key.setInt("Video Recording: Aspect Ratio Mode", (int)mAspectRatioMode);
		key.setInt("Video Recording: Resampling Mode", (int)mResamplingMode);
		key.setInt("Video Recording: Frame Size Mode", (int)mScalingMode);
		key.setInt("Video Recording: Video Bit Rate", (int)mVideoBitRate);
		key.setInt("Video Recording: Audio Bit Rate", (int)mAudioBitRate);
	} else {
		mEncoding = (ATVideoEncoding)key.getEnumInt("Video Recording: Compression Mode", kATVideoEncodingCount, kATVideoEncoding_ZMBV);
		mFrameRate = (ATVideoRecordingFrameRate)key.getEnumInt("Video Recording: Frame Rate", kATVideoRecordingFrameRateCount, kATVideoRecordingFrameRate_Normal);
		mbHalfRate = key.getBool("Video Recording: Half Rate", false);
		mbEncodeAllFrames = key.getBool("Video Recording: Encode All Frames", mbEncodeAllFrames);
		
		mAspectRatioMode = (ATVideoRecordingAspectRatioMode)key.getEnumInt("Video Recording: Aspect Ratio Mode", (int)ATVideoRecordingAspectRatioMode::Count, (int)ATVideoRecordingAspectRatioMode::IntegerOnly);
		mResamplingMode = (ATVideoRecordingResamplingMode)key.getEnumInt("Video Recording: Resampling Mode", (int)ATVideoRecordingResamplingMode::Count, (int)ATVideoRecordingResamplingMode::Nearest);
		mScalingMode = (ATVideoRecordingScalingMode)key.getEnumInt("Video Recording: Frame Size Mode", (int)ATVideoRecordingScalingMode::Count, (int)ATVideoRecordingScalingMode::None);

		CheckButton(IDC_HALF_RATE, mbHalfRate);
		CheckButton(IDC_ENCODE_ALL_FRAMES, mbEncodeAllFrames);

		mResamplingModeView.SetSelection(((int)ATVideoRecordingResamplingMode::Count - 1) - (int)mResamplingMode);
		mAspectRatioModeView.SetSelection(((int)ATVideoRecordingAspectRatioMode::Count - 1) - (int)mAspectRatioMode);
		mScalingModeView.SetSelection((int)mScalingMode);

		switch(mEncoding) {
			case kATVideoEncoding_Raw:	mVideoCodecView.SetSelection(0); break;
			case kATVideoEncoding_RLE:	mVideoCodecView.SetSelection(1); break;
			default:
			case kATVideoEncoding_ZMBV:	mVideoCodecView.SetSelection(2); break;
			case kATVideoEncoding_WMV7:	mVideoCodecView.SetSelection(3); break;
			case kATVideoEncoding_WMV9:	mVideoCodecView.SetSelection(4); break;
			case kATVideoEncoding_H264_MP3:	mVideoCodecView.SetSelection(5); break;
			case kATVideoEncoding_H264_AAC:	mVideoCodecView.SetSelection(6); break;
				break;
		}

		switch(mFrameRate) {
			case kATVideoRecordingFrameRate_Normal:
				CheckButton(IDC_FRAMERATE_NORMAL, true);
				break;

			case kATVideoRecordingFrameRate_NTSCRatio:
				CheckButton(IDC_FRAMERATE_NTSCRATIO, true);
				break;

			case kATVideoRecordingFrameRate_Integral:
				CheckButton(IDC_FRAMERATE_INTEGRAL, true);
				break;
		}

		mVideoBitRate = std::clamp<uint32>(key.getInt("Video Recording: Video Bit Rate", 1000000), 500000, 8000000);
		mAudioBitRate = std::clamp<uint32>(key.getInt("Video Recording: Audio Bit Rate", 128000), 96000, 256000);

		TBSetValue(IDC_VIDEOBITRATE, (mVideoBitRate + 50000) / 100000);
		TBSetValue(IDC_AUDIOBITRATE, (mAudioBitRate - 96000 + 16000) / 32000);

		UpdateFrameRateControls();
		UpdateVideoBitrateLabel();
		UpdateAudioBitrateLabel();
		UpdateEnables();
	}
}

bool ATUIDialogVideoRecording::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_HALF_RATE) {
		bool halfRate = IsButtonChecked(IDC_HALF_RATE);

		if (mbHalfRate != halfRate) {
			mbHalfRate = halfRate;

			UpdateFrameRateControls();
		}
	}

	return false;
}

bool ATUIDialogVideoRecording::OnOK() {
	if (VDDialogFrameW32::OnOK())
		return true;

	if (mEncoding == kATVideoEncoding_H264_AAC) {
		if (!Confirm2("MFAACEncodingBug",
			L"AAC encoding produces the best quality but may produce audio glitches on some systems due to a bug in the Media Foundation AAC Encoder. H.264+MP3 encoding is recommended instead when this occurs.",
			L"AAC Bug Warning"))
			return true;
	}

	return false;
}

void ATUIDialogVideoRecording::OnHScroll(uint32 id, int code) {
	if (id == IDC_VIDEOBITRATE)
		UpdateVideoBitrateLabel();
	else if (id == IDC_AUDIOBITRATE)
		UpdateAudioBitrateLabel();
}

void ATUIDialogVideoRecording::UpdateFrameRateControls() {
	VDStringW s;

	static const double kFrameRates[][2]={
		{ 3579545.0 / (2.0*114.0*262.0), 1773447.0 / (114.0*312.0) },
		{ 60000.0/1001.0, 50000.0/1001.0 },
		{ 60.0, 50.0 },
	};

	for(int i=0; i<3; ++i) {
		VDStringRefW label(mBaseFrameRateLabels[i]);
		VDStringW::size_type pos = label.find(L'|');

		if (pos != VDStringW::npos) {
			if (mbPAL)
				label = label.subspan(pos+1, VDStringW::npos);
			else
				label = label.subspan(0, pos);
		}

		VDStringW t;

		t.sprintf(L"%.3f fps (%.*ls)", kFrameRates[i][mbPAL] * (mbHalfRate ? 0.5 : 1.0), label.size(), label.data());

		SetControlText(kFrameRateIds[i], t.c_str());
	}
}

void ATUIDialogVideoRecording::UpdateEnables() {
	bool hasVideoBitrate = false;
	bool hasAudioBitrate = false;
	bool hasFullFrameOption = true;

	switch(mVideoCodecView.GetSelection()) {
		case 3:		// WMV7
		case 4:		// WMV9
		case 5:		// H.264
			hasVideoBitrate = true;
			hasAudioBitrate = true;
			hasFullFrameOption = false;
			break;

		default:
			hasVideoBitrate = false;
			hasAudioBitrate = false;
			break;
	}

	EnableControl(IDC_ENCODE_ALL_FRAMES, hasFullFrameOption);

	EnableControl(IDC_STATIC_VIDEOBITRATE, hasVideoBitrate);
	EnableControl(IDC_VIDEOBITRATE, hasVideoBitrate);
	EnableControl(IDC_VIDEOBITRATE_VALUE, hasVideoBitrate);

	EnableControl(IDC_STATIC_AUDIOBITRATE, hasVideoBitrate);
	EnableControl(IDC_AUDIOBITRATE, hasVideoBitrate);
	EnableControl(IDC_AUDIOBITRATE_VALUE, hasVideoBitrate);
}

void ATUIDialogVideoRecording::UpdateVideoBitrateLabel() {
	int v = TBGetValue(IDC_VIDEOBITRATE);

	SetControlTextF(IDC_VIDEOBITRATE_VALUE, L"%.1f Mbps", (float)v / 10.0f);
}

void ATUIDialogVideoRecording::UpdateAudioBitrateLabel() {
	int v = TBGetValue(IDC_AUDIOBITRATE);

	SetControlTextF(IDC_AUDIOBITRATE_VALUE, L"%d Kbps", 96 + 32*v);
}

ATUIContextualHelpProvider::HelpEntry& ATUIDialogVideoRecording::OnGetDynamicHelp(const ATUIContextualHelpProvider::HelpEntry& he) {
	mTempHelpEntry = he;

	switch(he.mId) {
		case IDC_VC:
			switch(mVideoCodecView.GetSelection()) {
				case 0:
					mTempHelpEntry.mLabel = L"Video encoding: Uncompressed (AVI)";
					mTempHelpEntry.mText = L"Records video in .AVI format as uncompressed RGB. This takes the most space but is the most compatible.";
					break;

				case 1:
					mTempHelpEntry.mLabel = L"Video encoding: Run-Length Encoding (AVI)";
					mTempHelpEntry.mText = L"Records video in .AVI format with lossless RLE video compression. This takes less space than uncompressed RGB but cannot encode 24-bit video.";
					break;

				case 2:
					mTempHelpEntry.mLabel = L"Video encoding: Zipped Motion Block Vector (AVI)";
					mTempHelpEntry.mText = L"Records video in .AVI format using the lossless ZMBV codec, originally created by the DOSBox project. This provides very good compression for retro-computer video. A ZMBV-capable decoder such as ffmpeg/ffdshow is needed to play these files.";
					break;

				case 3:
				case 4:
					mTempHelpEntry.mLabel = L"Video encoding: Windows Media Video 7/9 + WMAv8 (WMV)";
					mTempHelpEntry.mText = L"Records video in .WMV format using Windows Media Video and Audio. This requires at least Windows 7.";
					break;

				case 5:
					mTempHelpEntry.mLabel = L"Video encoding: H.264/MP3 (MP4)";
					mTempHelpEntry.mText = L"Records video in .MP4 format with H.264 video encoding and MP3 audio encoding. This requires at least Windows 8.";
					break;

				case 6:
					mTempHelpEntry.mLabel = L"Video encoding: H.264/AAC (MP4)";
					mTempHelpEntry.mText = L"Records video in .MP4 format with H.264 video encoding and AAC audio encoding. This requires at least Windows 7.";
					break;
			}
			break;
	}

	return mTempHelpEntry;
}

///////////////////////////////////////////////////////////////////////////

bool ATUIShowDialogVideoEncoding(VDGUIHandle parent, bool hz50, ATVideoEncoding& encoding,
	uint32& videoBitRate,
	uint32& audioBitRate,
	ATVideoRecordingFrameRate& frameRate,
	ATVideoRecordingAspectRatioMode& aspectRatioMode,
	ATVideoRecordingResamplingMode& resamplingMode,
	ATVideoRecordingScalingMode& scalingMode,
	bool& halfRate, bool& encodeAll)
{
	ATUIDialogVideoRecording dlg(hz50);

	if (!dlg.ShowDialog(parent))
		return false;

	encoding = dlg.GetEncoding();
	frameRate = dlg.GetFrameRate();
	aspectRatioMode = dlg.GetAspectRatioMode();
	resamplingMode = dlg.GetResamplingMode();
	scalingMode = dlg.GetScalingMode();
	videoBitRate = dlg.GetVideoBitRate();
	audioBitRate = dlg.GetAudioBitRate();
	halfRate = dlg.GetHalfRate();
	encodeAll = dlg.GetEncodeAllFrames();
	return true;
}
