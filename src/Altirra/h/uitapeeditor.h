//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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

#ifndef f_AT_UITAPEEDITOR_H
#define f_AT_UITAPEEDITOR_H

#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <at/atnativeui/canvas_win32.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/messagedispatcher.h>
#include <at/atnativeui/uinativewindow.h>
#include <at/atcore/fft.h>

class IATCassetteImage;
class ATCassetteEmulator;

class ATUITapeViewControl final : public ATUINativeWindow, public ATUINativeMouseMessages {
public:
	enum class DrawMode : uint8 {
		Scroll,
		Select,
		Draw,
		Insert,
		Analyze
	};

	enum class Decoder : uint8 {
		FSK_Sync,
		FSK_PLL,
		T2000,
	};

	enum class FilterMode : uint8 {
		FSKDirectSample2000Baud,
		FSKDirectSample1000Baud
	};

	enum class WaveformMode : uint8 {
		None,
		Waveform,
		Spectrogram
	};

	ATUITapeViewControl();
	~ATUITapeViewControl();

	void SetCassetteEmulator(ATCassetteEmulator *emu);

	IATCassetteImage *GetImage() const;
	void SetImage(IATCassetteImage *image);

	void LockViewReset();
	void UnlockViewReset();

	void OnTapeModified();

	void SetOnDrawModeChanged(vdfunction<void()> fn);
	void SetOnAnalysisEncodingChanged(vdfunction<void()> fn);
	void SetOnSelectionChanged(vdfunction<void()> fn);

	DrawMode GetDrawMode() const { return mDrawMode; }
	void SetDrawMode(DrawMode drawMode);

	Decoder GetAnalysisDecoder() const;
	void SetAnalysisDecoder(Decoder encoding);

	bool GetSIOMonitorEnabled() const;
	void SetSIOMonitorEnabled(bool enabled);

	bool GetShowTurboData() const;
	void SetShowTurboData(bool enabled);

	WaveformMode GetWaveformMode() const;
	void SetWaveformMode(WaveformMode mode);

	bool GetFrequencyGuidelinesEnabled() const;
	void SetFrequencyGuidelinesEnabled(bool enabled);

	bool GetStoreWaveformOnLoad() const;
	void SetStoreWaveformOnLoad(bool enabled);

	bool HasSelection() const;
	bool HasNonEmptySelection() const;
	uint32 GetSelectionSortedStart() const { return mSelSortedStartSample; }
	uint32 GetSelectionSortedEnd() const { return mSelSortedEndSample; }

	void ClearSelection();
	void SetSelection(uint32 startSample, uint32 endSample);

	void EnsureSelectionVisible();
	void EnsureRangeVisible(uint32 startSample, uint32 endSample);

	void GoToLocation(uint32 sample, float pixelsPerSample);

	void Insert();
	void Delete();
	void ReAnalyze();
	void ReAnalyzeFlip();
	void Filter(FilterMode filterMode);

	bool HasClip() const;
	void SelectAll();
	void Deselect();
	void Cut();
	void Copy();
	void Paste();
	void ConvertToStdBlock();
	void ConvertToRawBlock();
	void ExtractSelectionAsCFile(vdfastvector<uint8>& data) const;

	bool HasDecodedData() const;
	void CopyDecodedData() const;

	bool CanUndo() const;
	bool CanRedo() const;
	void Undo();
	void Redo();
	void ClearUndoRedo();

	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
	static constexpr uint32 kUndoLimit = 50;

	enum class UndoSelectionMode : uint8 {
		None,					// selection is not changed
		SelectionIsRange,		// current selection is range, new selection is new range
		SelectionToEnd,			// current selection is range, new selection is end of new range
		EndToSelection,			// current selection is end of range, new selection is new range
	};

	struct UndoEntry {
		uint32 mStart = 0;
		uint32 mLength = 0;
		vdrefptr<IATTapeImageClip> mpData;
		UndoSelectionMode mSelectionMode {};
	};

	struct AnalysisChannel;

	void OnMouseMove(sint32 x, sint32 y) override;
	void OnMouseDownL(sint32 x, sint32 y) override;
	void OnMouseUpL(sint32 x, sint32 y) override;
	void OnMouseDownR(sint32 x, sint32 y) override;
	void OnMouseUpR(sint32 x, sint32 y) override;
	void OnMouseWheel(sint32 x, sint32 y, float delta) override;
	void OnMouseLeave() override;
	void OnHScroll(uint32 code);
	void OnPaint();
	void PaintAnalysisChannel(IVDDisplayRendererGDI *r, const AnalysisChannel& ch, uint32 posStart, uint32 posEnd, sint32 x1, sint32 x2, sint32 y);
	void PaintSpectrogram(IVDDisplayRendererGDI& r, uint32 pos, uint32 posinc, sint32 x, sint32 xinc, sint32 n, sint32 ywfhi, sint32 ywflo);

	void SetHaveMouse();

	void SetScrollX(sint64 x);
	void ScrollDeltaX(sint64 dx, bool deferred);
	void SetZoom(sint32 zoom, sint32 centerClientX);
	
	uint32 PreModify();
	void PostModify(uint32 newPos);

	void UpdateScrollLimit();
	void UpdateHorizScroll();
	void UpdatePalettes();
	void UpdateFontMetrics();
	void UpdateDivisionSpacing();
	void UpdateHeadState();
	void UpdateHeadPosition();

	void InvalidateHeadArea();
	void InvalidateHeadAreaAtGlobalX(sint64 x);
	void InvalidateRange(uint32 start, uint32 end, sint32 dx1 = 0, sint32 dx2 = 0);
	void InvalidateRangeDeferred(uint32 start, uint32 end);

	uint32 ClientXToSampleEdge(sint32 x, bool clampToLength) const;
	uint32 ClientXToSample(sint32 x) const;
	sint64 SampleEdgeToClientXFloorRaw(uint32 sample) const;
	sint32 SampleEdgeToClientXFloor(uint32 sample) const;
	sint64 SampleEdgeToClientXCeilRaw(uint32 sample) const;
	sint32 SampleEdgeToClientXCeil(uint32 sample) const;
	sint64 SampleToGlobalX(uint32 sample) const;
	sint64 SampleToClientXRaw(uint32 sample) const;
	sint32 SampleToClientXPoint(uint32 sample) const;

	// Push an undo entry for an operation that replaces [start, start+len) with
	// a new range [start, start+newLen).
	void PushUndo(uint32 start, uint32 len, uint32 newLen, UndoSelectionMode selMode);
	void PushUndo(UndoEntry&& ue);
	void ExecuteUndoRedo(UndoEntry& ue);

	void Analyze(uint32 start, uint32 end);

	void OnByteDecoded(uint32 startPos, uint32 endPos, uint8 data, bool framingError, uint32 cyclesPerHalfBit);

	struct DecodedBlock {
		uint32 mSampleStart {};
		uint32 mSampleEnd {};
		uint32 mSampleValidEnd {};
		float mBaudRate {};
		uint32 mStartByte {};
		uint32 mByteCount {};
		uint32 mChecksumPos {};
		bool mbValidFrame {};
		uint8 mSuspiciousBit {};
		bool mbSuspiciousBitPolarity {};
	};

	enum class DecodedByteFlags : uint8 {
		None = 0x00,
		FramingError = 0x01
	};

	friend DecodedByteFlags operator&(DecodedByteFlags x, DecodedByteFlags y) { return (DecodedByteFlags)((uint8)x & (uint8)y); }
	friend DecodedByteFlags operator|(DecodedByteFlags x, DecodedByteFlags y) { return (DecodedByteFlags)((uint8)x | (uint8)y); }
	friend DecodedByteFlags& operator|=(DecodedByteFlags& x, DecodedByteFlags y) { x = (DecodedByteFlags)((uint8)x | (uint8)y); return x; }

	struct DecodedByte {
		uint32 mStartSample {};
		uint16 mBitSampleOffsets[10] {};
		uint8 mData {};
		DecodedByteFlags mFlags {};
	};

	struct DecodedByteStartPred {
		bool operator()(const DecodedByte& dbyte, uint32 start) const {
			return dbyte.mStartSample < start;
		}

		bool operator()(uint32 start, const DecodedByte& dbyte) const {
			return start < dbyte.mStartSample;
		}
	};

	struct DecodedBlocks {
		vdfastvector<DecodedBlock> mBlocks;
		vdfastvector<DecodedByte> mByteData;

		void Clear() {
			mBlocks.clear();
			mByteData.clear();
		}
	};

	void DecodeFSK(uint32 start, uint32 end, bool stopOnFramingError, DecodedBlocks& output) const;
	void DecodeFSK2(uint32 start, uint32 end, bool stopOnFramingError, DecodedBlocks& output) const;
	void DecodeT2000(uint32 start, uint32 end, DecodedBlocks& output) const;
	static bool TryIdentifySuspiciousBit(const DecodedBlocks& dblocks, DecodedBlock& dblock, uint32 forcedSyncBytes, uint32 checksumPos, uint8 receivedSum);

	vdrefptr<IATCassetteImage> mpImage;
	uint32 mTapeChangedLock = 0;
	vdrefptr<IATTapeImageClip> mpImageClip;
	sint64 mScrollX = 0;
	sint64 mScrollMax = 0;
	sint32 mWidth = 0;
	sint32 mHeight = 0;
	sint32 mCenterX = 0;
	sint32 mDeferredScrollX = 0;
	bool mbDeferredScrollPending = false;
	int mScrollShift = 0;
	int mPaletteShift = 0;
	sint32 mZoom = 0;
	float mZoomAccum = 0;
	uint32 mSampleCount = 0;
	uint32 mViewResetLock = 0;

	// deferred invalidate range, to make lots of itty bitty invalidations cheap
	uint32 mDeferredInvStart = 0;
	uint32 mDeferredInvEnd = 0;

	DrawMode mDrawMode = DrawMode::Scroll;
	DrawMode mActiveDragMode = DrawMode::Scroll;
	Decoder mAnalysisDecoder = Decoder::FSK_Sync;

	bool mbDragging = false;
	sint32 mDragOriginX = 0;

	bool mbHaveMouse = false;

	HFONT mhfont = nullptr;

	sint64 mTargetPixelsPerTimeMarker = 100;
	double mCurrentPixelsPerTimeMarker = 100;
	bool mbTimeMarkerShowMS = false;

	bool mbShowTurboData = false;

	WaveformMode mWaveformMode = WaveformMode::Waveform;
	bool mbShowFrequencyGuidelines = false;

	bool mbStoreWaveformOnLoad = false;

	bool mbSelectionValid = false;
	uint32 mSelStartSample = 0;				// selection bound, drag start point
	uint32 mSelEndSample = 0;				// selection bound, drag end point
	uint32 mSelSortedStartSample = 0;		// selection bound, earliest of two points -> [sorted start, sorted end)
	uint32 mSelSortedEndSample = 0;			// selection bound, latest of two points -> [sorted start, sorted end)

	bool mbDrawValid = false;
	bool mbDrawPolarity = false;
	uint32 mDrawStartSample = 0;
	uint32 mDrawEndSample = 0;

	uint32 mHeadPosition = 0;
	bool mbHeadPlay = false;
	bool mbHeadRecord = false;

	int mPointRadius = 1;

	bool mbSIOMonitorEnabled = false;
	uint8 mSIOMonChecksum = 0;

	static constexpr uint32 kInvalidChecksumPos = ~UINT32_C(0);
	uint32 mSIOMonChecksumPos = kInvalidChecksumPos;
	uint32 mSIOMonFramingErrors = 0;

	vdfunction<void()> mFnOnDrawModeChanged;
	vdfunction<void()> mFnOnAnalysisEncodingChanged;
	vdfunction<void()> mFnOnSelectionChanged;

	ATCassetteEmulator *mpCasEmu = nullptr;
	vdfunction<void()> mFnOnPositionChanged;
	vdfunction<void()> mFnOnPlayStateChanged;
	vdfunction<void(uint32, uint32, uint8, bool, uint32)> mFnByteDecoded;

	vdvector<UndoEntry> mUndoQueue;
	vdvector<UndoEntry> mRedoQueue;

	ATUICanvasW32 mCanvas;
	VDPixmapBuffer mBltImage;
	VDDisplayImageView mBltImageView;

	VDPixmapBuffer mSpectrogramImage;
	VDDisplayImageView mSpectrogramImageView;

	sint32 mAnalysisTextMinWidth = 0;
	sint32 mAnalysisHeight = 0;

	struct AnalysisChannel {
		DecodedBlocks mDecodedBlocks;
		uint32 mSampleStart = 0;
		uint32 mSampleEnd = 0;
	} mAnalysisChannels[2];

	uint32 mPalette[257] {};

	float mFFTWindow[128] {};
	uint32 mSpectrogramPalette[256] {};

	struct SpecSample {
		uint32 mBinOffset;
		float mCoeffs[4];
	};

	vdfastvector<SpecSample> mSpectrogramInterp; 

	vdautoptr<ATFFT<128>> mpFFT;
};

class ATUITapeEditorDialog final : public VDDialogFrameW32 {
public:
	ATUITapeEditorDialog();

	void GoToLocation(uint32 sample, float pixelsPerSample);

public:
	bool PreNCDestroy() override;
	bool OnLoaded() override;
	void OnDestroy() override;
	bool OnCancel() override;
	void OnSize() override;
	bool OnCommand(uint32 id, uint32 extcode) override;
	void OnInitMenu(VDZHMENU hmenu) override;

private:
	void OnTapeStateChanged();

	void OnToolbarCommand(uint32 id);

	void New();
	void Open();
	void Reload();
	void Load(const wchar_t *path);
	void SaveAsCAS();
	void SaveAsWAV();
	void SelectAll();
	void Deselect();
	void Cut();
	void Copy();
	void CopyDecodedData();
	void Paste();
	void Delete();
	void Undo();
	void Redo();
	void ConvertToStdBlock();
	void ConvertToRawBlock();
	void ExtractCFile();

	bool OKToDiscard();

	enum : uint32 {
		kCmdId_ModeScroll = 100,
		kCmdId_ModeSelect,
		kCmdId_ModeDraw,
		kCmdId_ModeInsert,
		kCmdId_ModeAnalyze,
		kCmdId_Delete,
		kCmdId_Filter
	};

	void DeferUpdateModeButtons();
	void DeferUpdateStatusMessage();

	void UpdateModeButtons();
	void UpdateAnalyzeModeButton();
	void UpdateStatusMessage();

	bool mbPendingUpdateModeButtons = false;
	bool mbPendingUpdateModeButtonsCall = false;
	bool mbPendingUpdateStatusMessage = false;
	bool mbPendingUpdateStatusMessageCall = false;

	VDStringW mBaseCaption;

	VDUIProxyToolbarControl mToolbar;
	VDUIProxyStatusBarControl mStatusBar;
	vdrefptr<ATUITapeViewControl> mpTapeView;
	vdfunction<void()> mFnOnTapeDirtyStateChanged;
	vdfunction<void()> mFnOnTapeChanged;
};


#endif
