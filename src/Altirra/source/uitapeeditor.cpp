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

#include <stdafx.h>
#include <vd2/system/color.h>
#include <vd2/system/file.h>
#include <vd2/system/vdstl_vectorview.h>
#include <vd2/system/w32assist.h>
#include <vd2/VDDisplay/renderergdi.h>
#include <vd2/Dita/services.h>
#include <at/atcore/sioutils.h>
#include <at/atcore/vfs.h>
#include <at/atio/cassetteimage.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/messagedispatcher.h>
#include <at/atnativeui/messageloop.h>
#include <at/atnativeui/uiframe.h>
#include "cassette.h"
#include "resource.h"
#include "oshelper.h"
#include "simulator.h"
#include "uiaccessors.h"
#include "uifilefilters.h"
#include "uitapeeditor.h"

extern ATSimulator g_sim;

ATUITapeEditorDialog *g_pATUITapeEditorDialog;

ATUITapeViewControl::ATUITapeViewControl() {
	mFnOnSelectionChanged = [] {};
	mFnOnPositionChanged = [this] { UpdateHeadPosition(); };
	mFnOnPlayStateChanged = [this] { UpdateHeadState(); };
	mFnByteDecoded = [this](auto... args) { OnByteDecoded(args...); };

	mBltImage.init(256, 3, nsVDPixmap::kPixFormat_ARGB8888);
	mBltImageView.SetImage(mBltImage, true);
}

ATUITapeViewControl::~ATUITapeViewControl() {
	SetCassetteEmulator(nullptr);
}

void ATUITapeViewControl::SetCassetteEmulator(ATCassetteEmulator *emu) {
	if (mpCasEmu != emu) {
		if (mpCasEmu) {
			mpCasEmu->PositionChanged -= &mFnOnPositionChanged;
			mpCasEmu->PlayStateChanged -= &mFnOnPlayStateChanged;

			if (mbSIOMonitorEnabled)
				mpCasEmu->ByteDecoded.Remove(&mFnByteDecoded);
		}

		mpCasEmu = emu;

		if (mpCasEmu) {
			mpCasEmu->PositionChanged += &mFnOnPositionChanged;
			mpCasEmu->PlayStateChanged += &mFnOnPlayStateChanged;

			if (mbSIOMonitorEnabled)
				mpCasEmu->ByteDecoded.Add(&mFnByteDecoded);

			UpdateHeadPosition();
			UpdateHeadState();
		}
	}
}

IATCassetteImage *ATUITapeViewControl::GetImage() const {
	return mpImage;
}

void ATUITapeViewControl::SetImage(IATCassetteImage *image) {
	if (mpImage != image) {
		mpImage = image;

		OnTapeModified();

		if (!mViewResetLock) {
			SetZoom(-12, 0);
			SetScrollX(mSampleCount >> (12 + 1));
		}
	}
}

void ATUITapeViewControl::LockViewReset() {
	++mViewResetLock;
}

void ATUITapeViewControl::UnlockViewReset() {
	--mViewResetLock;
}

void ATUITapeViewControl::OnTapeModified() {
	if (!mTapeChangedLock) {
		ClearUndoRedo();

		if (mpImage) {
			mSampleCount = mpImage->GetDataLength();
		} else {
			mSampleCount = 0;
		}

		UpdateScrollLimit();
		UpdateHorizScroll();
		UpdatePalettes();
		Invalidate();
	}
}

void ATUITapeViewControl::SetOnDrawModeChanged(vdfunction<void()> fn) {
	mFnOnDrawModeChanged = std::move(fn);
}

void ATUITapeViewControl::SetOnAnalysisEncodingChanged(vdfunction<void()> fn) {
	mFnOnAnalysisEncodingChanged = std::move(fn);
}

void ATUITapeViewControl::SetOnSelectionChanged(vdfunction<void()> fn) {
	mFnOnSelectionChanged = std::move(fn);
}

void ATUITapeViewControl::SetDrawMode(DrawMode drawMode) {
	if (mDrawMode != drawMode) {
		mDrawMode = drawMode;

		mFnOnDrawModeChanged();
	}
}

ATUITapeViewControl::Encoding ATUITapeViewControl::GetAnalysisEncoding() const {
	return mAnalysisEncoding;
}

void ATUITapeViewControl::SetAnalysisEncoding(Encoding encoding) {
	if (mAnalysisEncoding != encoding) {
		mAnalysisEncoding = encoding;

		if (mFnOnAnalysisEncodingChanged)
			mFnOnAnalysisEncodingChanged();
	}
}

bool ATUITapeViewControl::GetSIOMonitorEnabled() const {
	return mbSIOMonitorEnabled;
}

void ATUITapeViewControl::SetSIOMonitorEnabled(bool enabled) {
	if (mbSIOMonitorEnabled != enabled) {
		mbSIOMonitorEnabled = enabled;

		if (enabled) {
			if (mpCasEmu)
				mpCasEmu->ByteDecoded.Add(&mFnByteDecoded);
		} else {
			if (mpCasEmu)
				mpCasEmu->ByteDecoded.Remove(&mFnByteDecoded);
		}
	}
}

bool ATUITapeViewControl::GetShowTurboData() const {
	return mbShowTurboData;
}

void ATUITapeViewControl::SetShowTurboData(bool enabled) {
	if (mbShowTurboData != enabled) {
		mbShowTurboData = enabled;

		Invalidate();
	}
}

bool ATUITapeViewControl::GetShowWaveform() const {
	return mbShowWaveform;
}

void ATUITapeViewControl::SetShowWaveform(bool enabled) {
	if (mbShowWaveform != enabled) {
		mbShowWaveform = enabled;

		Invalidate();
	}
}

bool ATUITapeViewControl::GetStoreWaveformOnLoad() const {
	return mbStoreWaveformOnLoad;
}

void ATUITapeViewControl::SetStoreWaveformOnLoad(bool enabled) {
	mbStoreWaveformOnLoad = enabled;
}

bool ATUITapeViewControl::HasSelection() const {
	return mbSelectionValid;
}

bool ATUITapeViewControl::HasNonEmptySelection() const {
	return mbSelectionValid && mSelStartSample != mSelEndSample;
}

void ATUITapeViewControl::ClearSelection() {
	if (mbSelectionValid) {
		mbSelectionValid = false;

		Invalidate();
		mFnOnSelectionChanged();
	}
}

void ATUITapeViewControl::SetSelection(uint32 startSample, uint32 endSample) {
	if (mbSelectionValid && mSelStartSample == startSample && mSelEndSample == endSample)
		return;

	const uint32 sortedStartSample = std::min(startSample, endSample);
	const uint32 sortedEndSample = std::max(startSample, endSample);

	const sint32 x1 = SampleEdgeToClientXFloor(sortedStartSample);
	const sint32 x3 = (startSample == endSample) ? x1 + 1 : SampleEdgeToClientXCeil(sortedEndSample);

	if (!mbSelectionValid) {
		mbSelectionValid = true;

		InvalidateArea(vdrect32(x1, 0, x3, mHeight));
	} else {
		const sint32 x2 = SampleEdgeToClientXFloor(mSelSortedStartSample);
		const sint32 x4 = (mSelEndSample == mSelStartSample) ? x2 + 1 : SampleEdgeToClientXCeil(mSelSortedEndSample);

		if (mSelStartSample == mSelEndSample || startSample == endSample) {
			// either the old or new selection was a sliver -- invalidate both ranges
			// even if they overlap
			InvalidateArea(vdrect32(x1, 0, x3, mHeight));
			InvalidateArea(vdrect32(x2, 0, x4, mHeight));
		} else {
			// neither are slivers -- invalidate the XOR of the two ranges
			if (x1 != x2)
				InvalidateArea(vdrect32(std::min(x1, x2), 0, std::max(x1, x2), mHeight));

			if (x3 != x4)
				InvalidateArea(vdrect32(std::min(x3, x4), 0, std::max(x3, x4), mHeight));
		}
	}

	mSelStartSample = startSample;
	mSelEndSample = endSample;
	mSelSortedStartSample = sortedStartSample;
	mSelSortedEndSample = sortedEndSample;

	mFnOnSelectionChanged();
}

void ATUITapeViewControl::Insert() {
	if (mbSelectionValid && mSelSortedEndSample > mSelSortedStartSample) {
		if (mpImage) {
			uint32 deckPos = PreModify();

			PushUndo(mSelSortedStartSample, 0, mSelSortedEndSample - mSelSortedStartSample, UndoSelectionMode::EndToSelection);

			ATCassetteWriteCursor cursor {};
			cursor.mPosition = mSelSortedStartSample;

			mpImage->WriteBlankData(cursor, mSelSortedEndSample - mSelSortedStartSample, true);

			if (deckPos >= mSelSortedStartSample)
				deckPos += cursor.mPosition - mSelSortedStartSample;

			PostModify(deckPos);
		}

		SetSelection(mSelSortedEndSample, mSelSortedEndSample);
		Invalidate();
	}
}

void ATUITapeViewControl::Delete() {
	if (mbSelectionValid && mSelSortedEndSample > mSelSortedStartSample && mpImage) {
		uint32 deckPos = PreModify();

		PushUndo(mSelSortedStartSample, mSelSortedEndSample - mSelSortedStartSample, 0, UndoSelectionMode::SelectionIsRange);

		mpImage->DeleteRange(mSelSortedStartSample, mSelSortedEndSample);

		PostModify(deckPos >= mSelSortedEndSample ? mSelSortedStartSample + (deckPos - mSelSortedEndSample)
			: deckPos >= mSelSortedStartSample ? mSelSortedStartSample
			: deckPos);

		SetSelection(mSelSortedStartSample, mSelSortedStartSample);
		Invalidate();
	}
}

bool ATUITapeViewControl::HasClip() const {
	return mpImageClip != nullptr;
}

void ATUITapeViewControl::Cut() {
	if (mbSelectionValid && mSelSortedEndSample > mSelSortedStartSample && mpImage) {
		mpImageClip = mpImage->CopyRange(mSelSortedStartSample, mSelSortedEndSample);

		uint32 deckPos = PreModify();

		PushUndo(mSelSortedStartSample, mSelSortedEndSample - mSelSortedStartSample, 0, UndoSelectionMode::SelectionIsRange);

		mpImage->DeleteRange(mSelSortedStartSample, mSelSortedEndSample);

		PostModify(deckPos >= mSelSortedEndSample ? mSelSortedStartSample + (deckPos - mSelSortedEndSample)
			: deckPos >= mSelSortedStartSample ? mSelSortedStartSample
			: deckPos);

		SetSelection(mSelSortedStartSample, mSelSortedStartSample);
		Invalidate();
	}
}

void ATUITapeViewControl::Copy() {
	if (mbSelectionValid && mSelSortedEndSample > mSelSortedStartSample && mpImage)
		mpImageClip = mpImage->CopyRange(mSelSortedStartSample, mSelSortedEndSample);
}

void ATUITapeViewControl::Paste() {
	if (mbSelectionValid && mpImage && mpImageClip) {
		uint32 deckPos = PreModify();

		PushUndo(mSelSortedStartSample, mSelSortedEndSample - mSelSortedStartSample, mpImageClip->GetLength(), UndoSelectionMode::SelectionIsRange);

		if (mSelSortedEndSample > mSelSortedStartSample) {
			mpImage->DeleteRange(mSelSortedStartSample, mSelSortedEndSample);

			if (deckPos >= mSelSortedEndSample)
				deckPos = mSelSortedStartSample + (deckPos - mSelSortedEndSample);
			else if (deckPos >= mSelSortedStartSample)
				deckPos = mSelSortedStartSample;

			SetSelection(mSelSortedStartSample, mSelSortedStartSample);
		}

		const uint32 newPos = mpImage->InsertRange(mSelSortedStartSample, *mpImageClip);

		if (deckPos > mSelSortedStartSample)
			deckPos = newPos + (deckPos - mSelSortedStartSample);

		PostModify(deckPos);

		SetSelection(newPos, newPos);
		Invalidate();
	}
}

void ATUITapeViewControl::ConvertToStdBlock() {
	if (!mbSelectionValid || !mpImage)
		return;

	// capture range is 450-900 baud
	static constexpr uint32 kMinBitLen = (uint32)(kATCassetteDataSampleRate / 900.0f);
	static constexpr uint32 kMaxBitLen = (uint32)(kATCassetteDataSampleRate / 450.0f);

	const uint32 start = mSelSortedStartSample;
	const uint32 end = mSelSortedEndSample;
	if (start >= end)
		return;

	const uint32 deckPos = PreModify();
	PushUndo(start, end - start, end - start, UndoSelectionMode::SelectionIsRange);

	DecodedBlocks dblocks;
	DecodeFSK(start, end, true, dblocks);

	ATCassetteWriteCursor writeCursor;
	writeCursor.mPosition = start;

	for(const DecodedBlock& dblock : dblocks.mBlocks) {
		// blank tape to block start
		if (writeCursor.mPosition <= dblock.mSampleStart) {
			mpImage->WriteBlankData(writeCursor, dblock.mSampleStart - writeCursor.mPosition, false);
		} else {
			VDFAIL("Write cursor is beyond block start.");
		}

		// write standard block
		for(const uint8 v : vdvector_view(dblocks.mByteData.data() + dblock.mStartByte, dblock.mByteCount))
			mpImage->WriteStdData(writeCursor, v, dblock.mBaudRate, false);

		// if we stopped short of the original block, clear to that point
		if (writeCursor.mPosition < dblock.mSampleEnd)
			mpImage->WriteBlankData(writeCursor, dblock.mSampleEnd - writeCursor.mPosition, false);
	}

	PostModify(deckPos);

	Invalidate();
}

void ATUITapeViewControl::ConvertToRawBlock() {
	vdfastvector<uint16> rleData;

	if (!mbSelectionValid || !mpImage)
		return;

	const uint32 start = mSelSortedStartSample;
	const uint32 end = std::min(mSelSortedEndSample, mSampleCount);
	if (start >= end)
		return;
	
	bool nextPolarity = false;
	for(uint32 pos = start; pos < end; ) {
		auto nextTransition = mpImage->FindNextBit(pos, end - 1, nextPolarity, mbShowTurboData);
		uint32 pulseEnd = std::min<uint32>(nextTransition.mPos, mSampleCount);
		uint32 pulseLen = pulseEnd - pos;

		if (pulseLen) {
			while(pulseLen > 0xFFFF) {
				rleData.push_back(0xFFFF);
				rleData.push_back(0);
			}

			rleData.push_back(pulseLen);
		}

		nextPolarity = !nextPolarity;
		pos = pulseEnd;
	}

	const uint32 deckPos = PreModify();
	PushUndo(start, end - start, end - start, UndoSelectionMode::SelectionIsRange);

	ATCassetteWriteCursor cursor;
	cursor.mPosition = start;

	nextPolarity = true;
	for(uint16 pulseLen : rleData) {
		mpImage->WritePulse(cursor, nextPolarity, pulseLen, false, true);
		nextPolarity = !nextPolarity;
	}

	PostModify(deckPos);
}

void ATUITapeViewControl::ExtractSelectionAsCFile(vdfastvector<uint8>& data) const {
	data.clear();

	if (!mbSelectionValid || !mpImage)
		return;

	DecodedBlocks dblocks;
	DecodeFSK(mSelSortedStartSample, mSelSortedEndSample, true, dblocks);

	if (dblocks.mBlocks.empty())
		return;

	int blockNo = 1;
	for(const DecodedBlock& dblock : dblocks.mBlocks) {
		const uint8 *blockData = dblocks.mByteData.data() + dblock.mStartByte;
		const bool *errors = dblocks.mByteFramingErrors.data() + dblock.mStartByte;

		if (dblock.mByteCount >= 3) {
			if (blockData[0] != 0x55 || blockData[1] != 0x55)
				throw MyError("Sync error on block #%d", blockNo);
		}

		if (dblock.mByteCount < 132)
			throw MyError("Block #%d is too short", blockNo);

		if (std::find(errors, errors + dblock.mByteCount, true) != errors + dblock.mByteCount)
			throw MyError("Block #%d has a framing error.", blockNo);

		uint32 blockLen = 128;
		if (blockData[2] == 0xFE)
			break;
		else if (blockData[2] == 0xFA) {
			blockLen = blockData[130];

			if (blockLen >= 128)
				throw MyError("Block #%d has invalid length for a partial block.", blockNo);
		} else if (blockData[2] != 0xFC)
			throw MyError("Block #%d has unrecognized control byte.", blockNo);

		uint8 chksum = ATComputeSIOChecksum(blockData, 131);
		if (chksum != blockData[131])
			throw MyError("Block #%d has a checksum error.", blockNo);

		data.insert(data.end(), blockData + 3, blockData + 3 + blockLen);

		++blockNo;
	}
}

bool ATUITapeViewControl::CanUndo() const {
	return !mUndoQueue.empty();
}

bool ATUITapeViewControl::CanRedo() const {
	return !mRedoQueue.empty();
}

void ATUITapeViewControl::Undo() {
	if (mUndoQueue.empty())
		return;

	UndoEntry ue = std::move(mUndoQueue.back());
	mUndoQueue.pop_back();

	ExecuteUndoRedo(ue);

	mRedoQueue.emplace_back(std::move(ue));
}

void ATUITapeViewControl::Redo() {
	if (mRedoQueue.empty())
		return;

	UndoEntry ue = std::move(mRedoQueue.back());
	mRedoQueue.pop_back();

	ExecuteUndoRedo(ue);

	mUndoQueue.emplace_back(std::move(ue));
}

void ATUITapeViewControl::ClearUndoRedo() {
	mUndoQueue.clear();
	mRedoQueue.clear();
}

LRESULT ATUITapeViewControl::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	auto [result, handled] = ATUIDispatchWndProcMessage<ATUINativeMouseMessages>(mhdlg, msg, wParam, lParam, *this);
	if (handled)
		return result;

	switch(msg) {
		case WM_NCCREATE:
			mCanvas.Init(mhwnd);
			break;

		case WM_CREATE:
			UpdateFontMetrics();
			break;

		case WM_DESTROY:
			if (mhfont) {
				DeleteObject(mhfont);
				mhfont = nullptr;
			}
			break;

		case WM_SIZE:
			{
				auto sz = GetClientArea().size();
				mWidth = sz.w;
				mHeight = sz.h;
				mCenterX = mWidth >> 1;

				Invalidate();
			}
			break;

		case WM_ERASEBKGND:
			return TRUE;

		case WM_PAINT:
			OnPaint();
			return 0;

		case WM_HSCROLL:
			OnHScroll(LOWORD(wParam));
			return 0;

		case WM_CAPTURECHANGED:
			if (lParam != (LPARAM)mhwnd)
				mbDragging = false;
			break;

		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT) {
				LPCWSTR id = IDC_ARROW;

				if (mbDragging) {
					switch(mActiveDragMode) {
						case DrawMode::Scroll:
						default:
							id = IDC_HAND;
							break;

						case DrawMode::Draw:
							id = IDC_CROSS;
							break;

						case DrawMode::Select:
						case DrawMode::Insert:
						case DrawMode::Analyze:
							id = IDC_IBEAM;
							break;
					}
				} else if (GetKeyState(VK_LCONTROL) < 0)
					id = IDC_CROSS;
				else {
					switch(mDrawMode) {
						case DrawMode::Scroll:
						default:
							id = IDC_ARROW;
							break;

						case DrawMode::Draw:
							id = IDC_CROSS;
							break;

						case DrawMode::Select:
						case DrawMode::Insert:
						case DrawMode::Analyze:
							id = IDC_IBEAM;
							break;
					}
				}

				SetCursor(LoadCursor(nullptr, id));
				return TRUE;
			}
			break;

		case WM_USER + 100:
			if (mDeferredInvStart < mDeferredInvEnd) {
				InvalidateRange(mDeferredInvStart, mDeferredInvEnd);
				mDeferredInvStart = 0;
				mDeferredInvEnd = 0;
			}
			return 0;

		case WM_TIMER:
			if (wParam == 100) {
				KillTimer(mhwnd, 100);

				if (mbDeferredScrollPending) {
					mbDeferredScrollPending = false;

					if (mDeferredScrollX) {
						sint32 dx = mDeferredScrollX;
						mDeferredScrollX = 0;

						ScrollDeltaX(dx, false);
					}
				}
				return 0;
			}

			break;
	}

	return ATUINativeWindow::WndProc(msg, wParam, lParam);
}

void ATUITapeViewControl::OnMouseMove(sint32 x, sint32 y) {
	SetHaveMouse();

	if (mbDragging) {
		switch(mActiveDragMode) {
			case DrawMode::Scroll: {
				sint32 dx = x - mDragOriginX;
				mDragOriginX = x;

				ScrollDeltaX(-dx, true);

				SetCursor(LoadCursor(nullptr, IDC_HAND));
				break;
			}

			case DrawMode::Select:
			case DrawMode::Analyze:
				SetSelection(mSelStartSample, ClientXToSampleEdge(x, true));
				break;

			case DrawMode::Draw: {
				uint32 samp = ClientXToSample(x);

				if (mDrawEndSample != samp) {
					mDrawEndSample = samp;
					Invalidate();
				}
				break;
			}

			case DrawMode::Insert:
				SetSelection(mSelStartSample, std::max(mSelStartSample, ClientXToSampleEdge(x, false)));
				break;

			default:
				break;
		}
	}
}

void ATUITapeViewControl::OnMouseDownL(sint32 x, sint32 y) {
	SetHaveMouse();

	if (mbDragging)
		return;

	if (GetKeyState(VK_CONTROL) < 0) {
	} else {
		mDragOriginX = x;
		mbDragging = true;

		SetCapture(mhwnd);

		mActiveDragMode = mDrawMode;

		switch(mDrawMode) {
			case DrawMode::Scroll:
				SetCursor(LoadCursor(nullptr, IDC_HAND));
				break;

			case DrawMode::Select:
			case DrawMode::Insert:
			case DrawMode::Analyze:
				{
					uint32 samp = ClientXToSampleEdge(x, true);
					SetSelection(samp, samp);
				}
				break;

			case DrawMode::Draw:
				mbDrawValid = true;
				mbDrawPolarity = y < (mHeight >> 1);
				mDrawStartSample = mDrawEndSample = ClientXToSample(x);
				Invalidate();
				break;

			default:
				break;
		}
	}
}

void ATUITapeViewControl::OnMouseUpL(sint32 x, sint32 y) {
	if (mbDragging) {
		switch(mActiveDragMode) {
			case DrawMode::Draw:
				if (mbDrawValid) {
					mbDrawValid = false;

					if (mpImage) {
						const uint32 start = std::min<uint32>(mDrawStartSample, mDrawEndSample);
						const uint32 end = std::max<uint32>(mDrawStartSample, mDrawEndSample);

						if (start < end) {
							ATCassetteWriteCursor cursor { start };

							const uint32 deckPos = PreModify();

							PushUndo(start, end-start, end-start, UndoSelectionMode::None);

							mpImage->WritePulse(cursor, mbDrawPolarity, end - start, false, !mbShowTurboData);

							PostModify(deckPos);
						}
					}

					Invalidate();
				}
				break;

			case DrawMode::Insert:
				Insert();
				break;

			case DrawMode::Analyze:
				Analyze(mSelSortedStartSample, mSelSortedEndSample);
				ClearSelection();
				break;
		}

		ReleaseCapture();
	}
}

void ATUITapeViewControl::OnMouseDownR(sint32 x, sint32 y) {
	SetHaveMouse();

	if (mbDragging)
		return;

	mActiveDragMode = DrawMode::Scroll;
	mDragOriginX = x;
	mbDragging = true;

	SetCapture(mhwnd);
}

void ATUITapeViewControl::OnMouseUpR(sint32 x, sint32 y) {
	if (mbDragging)
		ReleaseCapture();
}

void ATUITapeViewControl::OnMouseWheel(sint32 x, sint32 y, float delta) {
	mZoomAccum += delta;

	int dz = (int)(mZoomAccum + (mZoomAccum < 0 ? -0.01f : 0.01f));
	mZoomAccum -= (float)dz;

	SetZoom(mZoom + dz, x);
}

void ATUITapeViewControl::OnMouseLeave() {
	if (::GetCapture() != mhwnd)
		mbHaveMouse = false;
}

void ATUITapeViewControl::OnHScroll(uint32 code) {
	SCROLLINFO si { sizeof(SCROLLINFO) };
	si.fMask = SIF_POS | SIF_PAGE | SIF_RANGE | SIF_TRACKPOS;

	if (!GetScrollInfo(mhwnd, SB_HORZ, &si))
		return;

	sint32 newPos = si.nPos;
	switch(code) {
		case SB_LEFT:
			newPos = 0;
			break;

		case SB_RIGHT:
			newPos = si.nMax;
			break;

		case SB_LINELEFT:
			if (newPos)
				--newPos;
			break;

		case SB_LINERIGHT:
			if (newPos < si.nMax)
				++newPos;
			break;

		case SB_PAGELEFT:
			if (newPos > (sint32)si.nPage)
				newPos -= (sint32)si.nPage;
			else
				newPos = 0;
			break;

		case SB_PAGERIGHT:
			if (newPos < si.nMax && si.nMax - newPos > (sint32)si.nPage)
				newPos += (sint32)si.nPage;
			else
				newPos = si.nMax;
			break;

		case SB_THUMBPOSITION:
			break;

		case SB_THUMBTRACK:
			newPos = si.nTrackPos;
			break;
	}

	if (newPos != si.nPos) {
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_POS;
		si.nPos = newPos;
		SetScrollInfo(mhwnd, SB_HORZ, &si, TRUE);
	}

	SetScrollX((sint64)newPos << mScrollShift);
}

void ATUITapeViewControl::OnPaint() {
	const auto [w, h] = GetClientArea().size();
	PAINTSTRUCT ps {};

	IVDDisplayRendererGDI *r = static_cast<IVDDisplayRendererGDI *>(mCanvas.Begin(ps, true));
	if (r) {
		r->SetColorRGB(0);
		r->FillRect(0, 0, w, h);

		if (mpImage && w) {
			const sint32 hw = w >> 1;

			const sint32 x1 = ps.rcPaint.left;
			const sint32 x2 = ps.rcPaint.right;
			const sint64 gx1 = mScrollX - hw + x1;
			const sint64 gx2 = mScrollX - hw + x2;

			sint64 idx1 = gx1;
			sint64 idx2 = gx2;

			if (mZoom > 0) {
				idx1 = (idx1 - 1) >> mZoom;
				idx2 = (idx2 >> mZoom) + 2;

				idx2 = std::min<sint64>(idx2, (sint64)mSampleCount << mZoom);
			} else {
				--idx1;
				++idx2;

				idx2 = std::min<sint64>(idx2, (((sint64)mSampleCount - 1) >> -mZoom) + 1);
			}

			if (idx1 < 0)
				idx1 = 0;

			const sint32 xinc = mZoom <= 0 ? 1 : 1 << mZoom;
			sint32 x = (sint32)((mZoom <= 0 ? idx1 : (idx1 << mZoom)) + hw - mScrollX);

			const int posshift = mZoom <= 0 ? -mZoom : 0;
			const sint32 posinc = 1 << posshift;
			const uint32 posStart = mZoom <= 0 ? (uint32)(idx1 << -mZoom) : (uint32)idx1;

			const sint32 n = (sint32)(idx2 - idx1);
			const uint32 posEnd = posStart + posinc * n;

			if (n > 0) {
				// draw regions (mark/raw/decoded)
				uint32 rpos = posStart + (posinc >> 1);
				sint32 rx1 = x;
				sint32 rx2 = x;
				ATCassetteRegionType lastRegionType {};

				static constexpr uint32 kRegionColors[] {
					0x1D1D1D,
					0x000060,
					0x003A00,
				};

				for(sint32 i = 0; i < n; ) {
					ATCassetteRegionInfo regionInfo = mpImage->GetRegionInfo(rpos + i*posinc);
					uint32 regionEnd = std::min(regionInfo.mRegionStart + regionInfo.mRegionLen, posEnd);
					uint32 pts = ((regionEnd - rpos - 1) >> posshift) + 1;
					sint32 rxi = (sint32)std::min<sint64>(x2, x + xinc*(sint64)pts);

					if (regionInfo.mRegionType != lastRegionType) {
						if (rx1 < rx2) {
							r->SetColorRGB(kRegionColors[(int)lastRegionType]);
							r->FillRect(rx1, 0, rx2 - rx1, h);
						}

						lastRegionType = regionInfo.mRegionType;
						rx1 = rx2;
					}

					rx2 = rxi;
					i = pts;
				}

				if (rx1 < rx2) {
					r->SetColorRGB(kRegionColors[(int)lastRegionType]);
					r->FillRect(rx1, 0, rx2 - rx1, h);
				}
			}

			// draw divisions
			sint64 div1 = std::max<sint64>(0, VDFloorToInt64((double)gx1 / mCurrentPixelsPerTimeMarker));
			sint64 div2 = VDCeilToInt64((double)gx2 / mCurrentPixelsPerTimeMarker);

			vdvector<std::pair<sint32, uint32>> divisionLabels;

			if (div1 < div2) {
				r->SetColorRGB(0x404040);

				for(sint64 div = div1; div < div2; ++div) {
					// convert division to x-position and time
					const double divgxf = (double)div * mCurrentPixelsPerTimeMarker;
					const sint32 divx = (sint32)(VDRoundToInt64(divgxf) - mScrollX + hw);
					const double t = ldexp(divgxf, -mZoom) * (1000.0 / kATCassetteDataSampleRateD);

					// draw divider
					r->FillRect(divx, 0, 1, h);

					// draw division label timestamp
					const uint32 msec = (uint32)(t + 0.5);
					divisionLabels.emplace_back(divx + 4, msec);
				}
			}

			// draw head position
			const sint64 xhead = SampleToClientXRaw(mHeadPosition);
			if (xhead >= ps.rcPaint.left - 16 && xhead < ps.rcPaint.right + 16) {
				r->SetColorRGB(mbHeadPlay ? 0x4080FF : mbHeadRecord ? 0xFF0000 : 0x808080);

				if (xhead >= ps.rcPaint.left && xhead < ps.rcPaint.right) {
					r->FillRect(xhead, 0, 1, h);
				}

				const vdpoint32 pts[3] {
					{ (sint32)xhead - 8, h },
					{ (sint32)xhead + 8, h },
					{ (sint32)xhead, h - 14 }
				};
				r->FillTri(pts);
			}

			// draw division labels
			r->SetTextFont(mhfont);
			r->SetTextColorRGB(0xEEEEEE);
			r->SetTextAlignment(IVDDisplayRendererGDI::TextAlign::Left, IVDDisplayRendererGDI::TextVAlign::Top);

			VDStringW s;
			for(auto [divx, msec] : divisionLabels) {
				uint32 sec = msec / 1000; msec %= 1000;
				uint32 min = sec / 60; sec %= 60;
				uint32 hr = min / 60; min %= 60;

				if (mbTimeMarkerShowMS)
					s.sprintf(L"%u:%02u:%02u.%03u", hr, min, sec, msec);
				else
					s.sprintf(L"%u:%02u:%02u", hr, min, sec);

				r->DrawTextSpan(divx + 4, 4, s.c_str(), s.size());
			}
			
			const bool hasWaveform = mpImage->GetWaveformLength() > 0;
			const bool showWaveform = mbShowWaveform && hasWaveform;
			const sint32 yhi = showWaveform ? h*5/8 : h/4;
			const sint32 ylo = showWaveform ? h*7/8 : h*3/4;

			if (n > 0) {
				uint32 pos = posStart;

				// draw waveform
				if (n > 1 && showWaveform) {
					const sint32 ywfhi = h / 8;
					const sint32 ywflo = h*3 / 8;
					const auto GetYForU8 = [=](uint8 v) { return ywflo + (((ywfhi - ywflo) * (sint32)v + 128) >> 8); };

					r->SetColorRGB(0x808080);
					r->FillRect(x1, GetYForU8(128), x2 - x1, 1);

					r->SetColorRGB(0xFFFFFF);

					if (mZoom < 0) {
						vdfastvector<vdrect32> rects(n);

						for(sint32 i = 0; i < n; ++i) {
							const auto minMax = mpImage->ReadWaveformMinMax(pos + posinc*i, posinc + 1, mbShowTurboData);

							const sint32 ry1 = GetYForU8(minMax.mMax);
							const sint32 ry2 = GetYForU8(minMax.mMin);

							rects[i] = { x + i, ry1, x + i + 1, ry2 };
						}

						r->MultiFillRect(rects.data(), n);
					} else {
						vdfastvector<vdpoint32> pts(n + 1);
						for(sint32 i = 0; i < n; ++i) {
							uint8 v = 0;
							mpImage->ReadWaveform(&v, pos + posinc*i, 1, mbShowTurboData);

							pts[i] = { x + xinc*i, GetYForU8(v) };
						}

						r->PolyLine(pts.data(), n - 1);
					}
				}

				// draw data
				if (mZoom < 0) {
					uint32 range = 1 << -mZoom;
					const float scale = 2.0f / (float)range;

					sint32 blitx = x;
					sint32 bliti = 0;
					uint32 *topPixels = mBltImage.GetPixelRow<uint32>(0);
					uint32 *midPixels = mBltImage.GetPixelRow<uint32>(1);
					uint32 *botPixels = mBltImage.GetPixelRow<uint32>(2);

					for(sint32 i = 0; i < n; ++i) {
						const IATCassetteImage::TransitionInfo ti = mpImage->GetTransitionInfo(pos, range, mbShowTurboData);
						const uint32 markCount = ti.mMarkBits + ti.mTransitionBits;
						const uint32 changeCount = ti.mTransitionBits;
						const uint32 spaceCount = ti.mSpaceBits + ti.mTransitionBits;

						topPixels[bliti] = (markCount > 0) ? mPalette[markCount >> mPaletteShift] : 0;
						midPixels[bliti] = (changeCount > 0) ? mPalette[changeCount >> mPaletteShift] : 0;
						botPixels[bliti] = (spaceCount > 0) ? mPalette[spaceCount >> mPaletteShift] : 0;
						++bliti;
						
						if (bliti >= 256 || i + 1 == n) {
							mBltImageView.Invalidate();

							VDDisplayBltOptions opts {};
							opts.mFilterMode = VDDisplayBltOptions::kFilterMode_Point;
							opts.mAlphaBlendMode = VDDisplayBltOptions::AlphaBlendMode::OverPreMultiplied;
							r->StretchBlt(blitx, yhi, bliti, 1, mBltImageView, 0, 0, bliti, 1, opts);
							r->StretchBlt(blitx, yhi+1, bliti, (ylo-yhi)-1, mBltImageView, 0, 1, bliti, 1, opts);
							r->StretchBlt(blitx, ylo, bliti, 1, mBltImageView, 0, 2, bliti, 1, opts);

							blitx += bliti;
							bliti = 0;
						}

						++x;
						pos += posinc;
					}
				} else {
					vdfastvector<vdpoint32> pts;
					pts.resize(n*2-1);

					for(sint32 i = 0; i < n; ++i) {
						const bool polarity = mpImage->GetBit(pos, mbShowTurboData);
						vdpoint32 pt { x, polarity ? yhi : ylo };

						pts[i*2] = pt;
						if (i)
							pts[i*2-1] = vdpoint32 { pt.x, pts[i*2-2].y };

						x += xinc;
						pos += posinc;
					}

					r->SetColorRGB(0xE0E0E0);
					r->PolyLine(pts.data(), pts.size() - 1);
				}
			}

			// draw analysis data
			PaintAnalysisChannel(r, mAnalysisChannels[0], posStart, posEnd, x1, x2, ylo + 5);
			PaintAnalysisChannel(r, mAnalysisChannels[1], posStart, posEnd, x1, x2, ylo + 10 + mAnalysisHeight);

			// draw selection
			if (mbSelectionValid) {
				sint64 selx1raw = SampleEdgeToClientXFloorRaw(std::min(mSelStartSample, mSelEndSample));

				if (mSelStartSample == mSelEndSample) {
					if (selx1raw >= x1 && selx1raw < x2)
						r->AlphaFillRect((sint32)selx1raw, 0, 1, h, 0xC0808080);
				} else {
					sint32 selx1 = (sint32)std::clamp<sint64>(selx1raw, x1, x2);
					sint32 selx2 = SampleEdgeToClientXCeil(std::max(mSelStartSample, mSelEndSample));

					if (selx2 > selx1)
						r->AlphaFillRect(selx1, 0, selx2 - selx1, h, (ATUIGetThemeColors().mHighlightedBg & 0xFFFFFF) | 0x80000000);
				}
			}

			// draw draw
			if (mbDrawValid) {
				sint32 drx1 = SampleToClientXPoint(std::min(mDrawStartSample, mDrawEndSample));
				sint32 drx2 = SampleToClientXPoint(std::max(mDrawStartSample, mDrawEndSample));
				sint32 dry = mbDrawPolarity ? yhi : ylo;

				r->SetColorRGB(0xFF0000);
				r->FillRect(drx1 - mPointRadius, dry - mPointRadius, (drx2 - drx1) + 2*mPointRadius + 1, mPointRadius*2+1);
			}
		}
		
		mCanvas.End(ps);
	}
}

void ATUITapeViewControl::PaintAnalysisChannel(IVDDisplayRendererGDI *r, const AnalysisChannel& ch, uint32 posStart, uint32 posEnd, sint32 x1, sint32 x2, sint32 y) {
	if (ch.mDecodedBlocks.mBlocks.empty())
		return;

	// determine overlapping block range
	auto itBlockStart = std::upper_bound(ch.mDecodedBlocks.mBlocks.begin(), ch.mDecodedBlocks.mBlocks.end(), posStart,
		[](uint32 pos, const DecodedBlock& dblock) {
			return pos < dblock.mSampleEnd;
		}
	);

	auto itBlockEnd = std::upper_bound(itBlockStart, ch.mDecodedBlocks.mBlocks.end(), posEnd,
		[](uint32 pos, const DecodedBlock& dblock) {
			return pos < dblock.mSampleStart;
		}
	);

	// process blocks in range
	if (itBlockEnd == itBlockStart)
		return;

	r->SetColorRGB(0xC0C0C0);
	r->SetTextAlignment(IVDDisplayRendererGDI::TextAlign::Center, IVDDisplayRendererGDI::TextVAlign::Top);

	const sint32 ya1 = y;
	const sint32 yah = mAnalysisHeight;
	const sint32 yah2 = yah >> 1;

	VDStringW s;
	for(const DecodedBlock& dblock : vdvector_view(itBlockStart, (size_t)(itBlockEnd - itBlockStart))) {
		const auto itByteStartAll = ch.mDecodedBlocks.mByteStartSamples.begin() + dblock.mStartByte;
		const auto itByteEndAll = itByteStartAll + dblock.mByteCount;
		auto itByteStart = std::upper_bound(itByteStartAll, itByteEndAll, posStart);
		auto itByteEnd = std::upper_bound(itByteStart, itByteEndAll, posEnd);

		if (itByteStart != itByteStartAll)
			--itByteStart;

		uint32 numBytes = itByteEnd - itByteStart;
		sint64 xbstart = SampleToClientXRaw(itByteStart[0]);
		ptrdiff_t byteDataOffset = itByteStart - ch.mDecodedBlocks.mByteStartSamples.begin();
		const uint8 *dataBytes = ch.mDecodedBlocks.mByteData.data() + byteDataOffset;
		const bool *statuses = ch.mDecodedBlocks.mByteFramingErrors.data() + byteDataOffset;
		const float samplesPerBit = dblock.mSamplesPerBit;
		const sint32 checksumPos = dblock.mChecksumPos ? (sint32)dblock.mChecksumPos - (sint32)(itByteStart - itByteStartAll) : -1;
		const sint32 validFramePos = dblock.mbValidFrame ? checksumPos : -1;

		for(uint32 i = 0; i < numBytes; ++i) {
			const uint32 byteEnd = itByteStart[i+1];
			const sint64 xbend = SampleToClientXRaw(byteEnd);

			if (xbstart >= x1 && xbstart < x2) {
				r->SetColorRGB((sint32)i < validFramePos ? 0x80D7C4 : 0xC0C0C0);
				r->FillRect((sint32)xbstart, ya1, 1, yah);
			}

			// see if we should draw bit sample markers
			if (xbend - xbstart > 20 && samplesPerBit) {
				r->SetColorRGB(0x606060);

				for(int j=0; j<10; ++j) {
					uint32 bitSamplePos = itByteStart[i] + (uint32)(samplesPerBit * ((float)j + 0.5f) + 0.5f);

					if (bitSamplePos < byteEnd) {
						const sint64 bitSampleX = SampleToClientXRaw(bitSamplePos);

						if (bitSampleX >= x1 && bitSampleX < x2)
							r->FillRect((sint32)bitSampleX, ya1, 1, yah2);
					}
				}
			}

			sint64 xbspace = xbend - xbstart;
			if (xbspace) {
				if (xbspace > mAnalysisTextMinWidth) {
					s.sprintf(L"%02X", dataBytes[i]);

					r->SetTextColorRGB(statuses[i] ? 0xFF4040 : (sint32)i < validFramePos || (sint32)i == checksumPos ? 0x80FFE0 : 0xE0E0E0);
					r->DrawTextSpan((sint32)(xbstart + ((xbend - xbstart) >> 1)), ya1, s.c_str(), 2);
				} else {
					r->SetColorRGB(statuses[i] ? 0xDF6060 : (sint32)i < validFramePos || (sint32)i == checksumPos ? 0x80DFC8 : 0x808080);
					r->FillRect(xbstart + 1, ya1 + 1, xbspace, yah - 2);
				}
			}

			xbstart = xbend;
		}

		if (xbstart >= x1 && xbstart < x2) {
			r->SetColorRGB(0xC0C0C0);
			r->FillRect((sint32)xbstart, ya1, 1, yah);
		}
	}
}

void ATUITapeViewControl::SetHaveMouse() {
	if (!mbHaveMouse) {
		mbHaveMouse = true;

		TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT)};
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = mhwnd;
		::TrackMouseEvent(&tme);
	}
}

void ATUITapeViewControl::SetScrollX(sint64 x) {
	if (mScrollX == x)
		return;

	const sint64 dx = x - mScrollX;

	mScrollX = x;

	SCROLLINFO si { sizeof(SCROLLINFO) };
	si.fMask = SIF_POS;
	si.nPos = x >> mScrollShift;
	SetScrollInfo(mhwnd, SB_HORZ, &si, TRUE);

	if (VDAbs64(dx) >= mWidth)
		Invalidate();
	else
		mCanvas.Scroll(-(sint32)dx, 0);
}

void ATUITapeViewControl::ScrollDeltaX(sint64 dx, bool deferred) {
	if (deferred) {
		mDeferredScrollX += dx;

		if (!mbDeferredScrollPending) {
			mbDeferredScrollPending = true;

			SetTimer(mhdlg, 100, 10, nullptr);
		}
	} else {
		SetScrollX(std::clamp<sint64>(mScrollX + dx, 0, mScrollMax));
	}
}

void ATUITapeViewControl::SetZoom(sint32 newZoom, sint32 centerClientX) {

	newZoom = std::clamp(newZoom, -20, 16);

	if (mZoom == newZoom)
		return;

	const sint32 w = GetClientArea().width();
	const sint32 hw = w >> 1;
	sint32 xoff = centerClientX - hw;
	const int zoomChange = newZoom - mZoom;
	mZoom = newZoom;

	if (zoomChange < 0)
		mScrollX = ((mScrollX + xoff) >> -zoomChange) - xoff;
	else
		mScrollX = ((mScrollX + xoff) << zoomChange) - xoff;

	UpdateScrollLimit();

	mScrollX = std::clamp<sint64>(mScrollX, 0, mScrollMax);

	UpdateHorizScroll();
	UpdatePalettes();
	UpdateDivisionSpacing();
	Invalidate();
}

uint32 ATUITapeViewControl::PreModify() {
	++mTapeChangedLock;

	return mpCasEmu ? mpCasEmu->OnPreModifyTape() : 0;
}

void ATUITapeViewControl::PostModify(uint32 newPos) {
	if (mpCasEmu)
		mpCasEmu->OnPostModifyTape(newPos);

	mSampleCount = mpImage->GetDataLength();
	--mTapeChangedLock;
}

void ATUITapeViewControl::UpdateScrollLimit() {
	sint64 len = mSampleCount;

	if (len) {
		if (mZoom < 0)
			len = ((len - 1) >> -mZoom) + 1;
		else
			len <<= mZoom;
	}

	mScrollMax = len;
	mScrollShift = 0;

	sint64 maxPos64 = len;
	while(maxPos64 > 0x1FFFFFFF) {
		++mScrollShift;
		maxPos64 >>= 1;
	}
}

void ATUITapeViewControl::UpdateHorizScroll() {
	const int w = GetClientArea().width();

	SCROLLINFO si { sizeof(SCROLLINFO) };
	si.fMask = SIF_PAGE | SIF_RANGE | SIF_DISABLENOSCROLL | SIF_POS;
	si.nMin = 0;
	si.nMax = (sint32)((mScrollMax + w) >> mScrollShift);
	si.nPage = ((w - 1) >> mScrollShift) + 1;
	si.nPos = (int)(mScrollX >> mScrollShift);

	SetScrollInfo(mhwnd, SB_HORZ, &si, TRUE);
}

void ATUITapeViewControl::UpdatePalettes() {
	if (mZoom >= 0)
		return;

	int palBits = std::min(8, -mZoom);
	int n = 1 << palBits;
	float x = 0;
	float xinc = (float)(1 << (8 - palBits)) / 256.0f * 2.0f;

	for(int i = 0; i <= n; ++i) {
		mPalette[i] = 0xFF404040 + 0x010101 * VDRoundToInt(191.0f * powf(std::min(1.0f, x), 1.0f/2.2f));

		x += xinc;
	}

	mPaletteShift = -mZoom - palBits;
}

void ATUITapeViewControl::UpdateFontMetrics() {
	if (!mhfont)
		mhfont = ATUICreateDefaultFontForDpiW32(ATUIGetWindowDpiW32(mhwnd));

	mAnalysisTextMinWidth = 0;
	mAnalysisHeight = 0;

	if (HDC hdc = GetDC(mhwnd)) {
		if (int savedState = SaveDC(hdc)) {
			if (HGDIOBJ hOldFont = SelectObject(hdc, mhfont)) {
				static const WCHAR kRepresentativeText[] = L"00:00:00.000";
				SIZE sz {};

				if (GetTextExtentPoint32W(hdc, kRepresentativeText, vdcountof(kRepresentativeText) - 1, &sz))
					mTargetPixelsPerTimeMarker = (sz.cx * 3) / 2;

				if (GetTextExtentPoint32W(hdc, L"00", 2, &sz)) {
					mAnalysisTextMinWidth = sz.cx;
					mAnalysisHeight = sz.cy;
				}
			}

			RestoreDC(hdc, savedState);
		}

		ReleaseDC(mhwnd, hdc);
	}

	mAnalysisHeight += 4;
	mAnalysisTextMinWidth += 4;

	UpdateDivisionSpacing();
}

void ATUITapeViewControl::UpdateDivisionSpacing() {
	// start with one marker per millisecond
	mCurrentPixelsPerTimeMarker = kATCassetteDataSampleRateD * ldexp(1.0, mZoom) / 1000.0;

	static const uint32 kSteps[] = {
		// milliseconds
		1, 2, 5, 10, 20, 50, 100, 200, 500,
		
		// seconds
		1000, 2000, 5000, 10000, 20000, 30000,

		// minutes
		60000*1, 60000*2, 60000*5, 60000*10, 60000*20, 60000*30
	};

	// try predefined steps
	mbTimeMarkerShowMS = false;

	for(uint32 step : kSteps) {
		double v = mCurrentPixelsPerTimeMarker * (double)step;

		if (v >= mTargetPixelsPerTimeMarker) {
			mCurrentPixelsPerTimeMarker = v;
			mbTimeMarkerShowMS = step < 1000;
			return;
		}
	}

	// switch to hours
	mCurrentPixelsPerTimeMarker *= 60000.0 * 60.0;

	// step to 2x, 5x, and 10x as we are still below the target
	while(mCurrentPixelsPerTimeMarker < mTargetPixelsPerTimeMarker) {
		if (double x2 = mCurrentPixelsPerTimeMarker * 2.0; x2 >= mTargetPixelsPerTimeMarker) {
			mCurrentPixelsPerTimeMarker = x2;
			break;
		}

		if (double x5 = mCurrentPixelsPerTimeMarker * 5.0; x5 >= mTargetPixelsPerTimeMarker) {
			mCurrentPixelsPerTimeMarker = x5;
			break;
		}

		mCurrentPixelsPerTimeMarker *= 10.0;
	}
}

void ATUITapeViewControl::UpdateHeadState() {
	if (mpCasEmu) {
		const bool play = mpCasEmu->IsPlayEnabled();
		const bool record = mpCasEmu->IsRecordEnabled();

		if (mbHeadPlay != play || mbHeadRecord != record) {
			mbHeadPlay = play;
			mbHeadRecord = record;

			InvalidateHeadArea();
		}
	}
}

void ATUITapeViewControl::UpdateHeadPosition() {
	if (mpCasEmu) {
		uint32 newPos = mpCasEmu->GetSamplePos();

		if (mHeadPosition != newPos) {
			sint64 xold = SampleToClientXRaw(mHeadPosition);
			sint64 xnew = SampleToClientXRaw(newPos);

			mHeadPosition = newPos;

			if (xold != xnew) {
				InvalidateHeadAreaAtGlobalX(xold);
				InvalidateHeadAreaAtGlobalX(xnew);
			}
		}
	}
}

void ATUITapeViewControl::InvalidateHeadArea() {
	InvalidateHeadAreaAtGlobalX(SampleToClientXRaw(mHeadPosition));
}

void ATUITapeViewControl::InvalidateHeadAreaAtGlobalX(sint64 x) {
	const sint32 x1 = (sint32)std::clamp<sint64>(x - 16, 0, mWidth);
	const sint32 x2 = (sint32)std::clamp<sint64>(x + 17, 0, mWidth);

	if (x1 < x2)
		InvalidateArea(vdrect32(x1, 0, x2, mHeight));
}

void ATUITapeViewControl::InvalidateRange(uint32 start, uint32 end, sint32 dx1, sint32 dx2) {
	if (start > end)
		return;

	const sint32 x1 = (sint32)std::clamp<sint64>(SampleToClientXRaw(start) + dx1, 0, mWidth);
	const sint32 x2 = (sint32)std::clamp<sint64>(SampleToClientXRaw(end  ) + dx2, 0, mWidth);

	if (x1 < x2)
		InvalidateArea(vdrect32(x1, 0, x2, mHeight));
}

void ATUITapeViewControl::InvalidateRangeDeferred(uint32 start, uint32 end) {
	if (start > end)
		return;

	if (mDeferredInvStart >= mDeferredInvEnd) {
		mDeferredInvStart = start;
		mDeferredInvEnd = end;

		PostMessage(mhwnd, WM_USER + 100, 0, 0);
	} else {
		mDeferredInvStart = std::min(mDeferredInvStart, start);
		mDeferredInvEnd = std::max(mDeferredInvEnd, end);
	}
}

uint32 ATUITapeViewControl::ClientXToSampleEdge(sint32 x, bool clampToLength) const {
	sint64 gpx = mScrollX + x - mCenterX;

	if (mZoom <= 0)
		gpx <<= -mZoom;
	else
		gpx = ((gpx >> (mZoom - 1)) + 1) >> 1;

	return (uint32)std::min<uint64>((uint64)std::max<sint64>(gpx, 0), clampToLength ? mSampleCount : kATCassetteDataLimit);
}

uint32 ATUITapeViewControl::ClientXToSample(sint32 x) const {
	sint64 gpx = mScrollX + x - mCenterX;

	if (mZoom <= 0)
		gpx <<= -mZoom;
	else
		gpx >>= mZoom;

	return (uint32)std::min<uint64>((uint64)std::max<sint64>(gpx, 0), mSampleCount);
}

sint64 ATUITapeViewControl::SampleEdgeToClientXFloorRaw(uint32 sample) const {
	sint64 gpx = (sint64)sample;

	if (mZoom <= 0)
		gpx >>= -mZoom;
	else
		gpx <<= mZoom;

	return gpx + mCenterX - mScrollX;
}

sint32 ATUITapeViewControl::SampleEdgeToClientXFloor(uint32 sample) const {
	return (sint32)std::clamp<sint64>(SampleEdgeToClientXFloorRaw(sample), 0, mWidth);
}

sint64 ATUITapeViewControl::SampleEdgeToClientXCeilRaw(uint32 sample) const {
	sint64 gpx = (sint64)sample;

	if (mZoom <= 0)
		gpx = -(-gpx >> -mZoom);
	else
		gpx <<= mZoom;

	return gpx + mCenterX - mScrollX;
}

sint32 ATUITapeViewControl::SampleEdgeToClientXCeil(uint32 sample) const {
	return (sint32)std::clamp<sint64>(SampleEdgeToClientXCeilRaw(sample), 0, mWidth);
}

sint64 ATUITapeViewControl::SampleToGlobalX(uint32 sample) const {
	sint64 gpx = (sint64)sample;
	if (mZoom < 0)
		gpx = ((gpx >> (-mZoom-1)) + 1) >> 1;
	else
		gpx <<= mZoom;

	return gpx;
}

sint64 ATUITapeViewControl::SampleToClientXRaw(uint32 sample) const {
	return SampleToGlobalX(sample) + mCenterX - mScrollX;
}

sint32 ATUITapeViewControl::SampleToClientXPoint(uint32 sample) const {
	return (sint32)std::clamp<sint64>(SampleToClientXRaw(sample), -mPointRadius, mWidth + mPointRadius);
}

void ATUITapeViewControl::PushUndo(uint32 start, uint32 len, uint32 newLen, UndoSelectionMode selMode) {
	UndoEntry ue;
	ue.mpData = mpImage->CopyRange(start, len);
	ue.mStart = start;
	ue.mLength = newLen;
	ue.mSelectionMode = selMode;

	PushUndo(std::move(ue));
}

void ATUITapeViewControl::PushUndo(UndoEntry&& ue) {
	mRedoQueue.clear();

	if (mUndoQueue.size() >= kUndoLimit)
		mUndoQueue.erase(mUndoQueue.begin());

	mUndoQueue.emplace_back(ue);
}

void ATUITapeViewControl::ExecuteUndoRedo(UndoEntry& ue) {
	uint32 deckPos = PreModify();

	vdrefptr saveData = mpImage->CopyRange(ue.mStart, ue.mLength);
	const uint32 clipLen = ue.mpData ? ue.mpData->GetLength() : 0;

	if (deckPos >= ue.mStart) {
		if (deckPos - ue.mStart >= ue.mLength)
			deckPos = (deckPos - ue.mLength) + clipLen;
		else
			deckPos = ue.mStart;
	}

	if (clipLen > ue.mLength) {
		ATCassetteWriteCursor cursor;
		cursor.mPosition = ue.mStart + ue.mLength;
		mpImage->WriteBlankData(cursor, clipLen - ue.mLength, true);
	} else if (clipLen < ue.mLength) {
		mpImage->DeleteRange(ue.mStart + clipLen, ue.mStart + ue.mLength);
	}

	if (ue.mpData)
		mpImage->ReplaceRange(ue.mStart, *ue.mpData);

	ue.mpData = std::move(saveData);
	ue.mLength = clipLen;

	switch(ue.mSelectionMode) {
		case UndoSelectionMode::None:
			break;

		case UndoSelectionMode::SelectionIsRange:
			SetSelection(ue.mStart, ue.mStart + ue.mLength);
			break;

		case UndoSelectionMode::SelectionToEnd:
			ue.mSelectionMode = UndoSelectionMode::EndToSelection;
			SetSelection(ue.mStart + ue.mLength, ue.mStart + ue.mLength);
			break;

		case UndoSelectionMode::EndToSelection:
			ue.mSelectionMode = UndoSelectionMode::SelectionToEnd;
			SetSelection(ue.mStart, ue.mStart + ue.mLength);
			break;
	}

	PostModify(deckPos);

	Invalidate();
}

void ATUITapeViewControl::Analyze(uint32 start, uint32 end) {
	AnalysisChannel& ch0 = mAnalysisChannels[0];

	if (ch0.mSampleStart < ch0.mSampleEnd) {
		InvalidateRange(ch0.mSampleStart, ch0.mSampleEnd, 0, 1);
	}

	ch0.mDecodedBlocks.Clear();

	switch(mAnalysisEncoding) {
		case Encoding::FSK:
			DecodeFSK(start, end, false, ch0.mDecodedBlocks);
			break;

		case Encoding::T2000:
			DecodeT2000(start, end, ch0.mDecodedBlocks);
			break;
	}

	ch0.mSampleStart = start;
	ch0.mSampleEnd = end;

	InvalidateRange(start, end, 0, 1);
}

void ATUITapeViewControl::OnByteDecoded(uint32 startPos, uint32 endPos, uint8 data, bool framingError, uint32 cyclesPerHalfBit) {
	if (!cyclesPerHalfBit)
		return;

	AnalysisChannel& ch1 = mAnalysisChannels[1];

	if (ch1.mSampleStart < ch1.mSampleEnd && startPos < ch1.mSampleEnd) {
		InvalidateRangeDeferred(ch1.mSampleStart, ch1.mSampleEnd);

		ch1.mSampleStart = startPos;
		ch1.mSampleEnd = startPos;
		ch1.mDecodedBlocks = {};
	}

	// (cycles/halfbit) / (bits/halfbit) = (cycles/bit)
	// (cycles/bit) / (cycles/sample) = (samples/bit)
	float samplesPerBit = (float)cyclesPerHalfBit * 2.0f / kATCassetteCyclesPerDataSample;

	DecodedBlocks& dblocks = ch1.mDecodedBlocks;
	if (dblocks.mBlocks.empty() || startPos - ch1.mSampleEnd > (uint32)(kATCassetteDataSampleRate / 20) || fabs(samplesPerBit - dblocks.mBlocks.back().mSamplesPerBit) > 0.1f) {
		auto& newdblock = dblocks.mBlocks.emplace_back(DecodedBlock());

		newdblock.mSampleStart = startPos;
		newdblock.mByteCount = 0;
		newdblock.mChecksumPos = 0;
		newdblock.mSamplesPerBit = samplesPerBit;

		// (samples/sec) / (samples/bit) = (bits/sec)
		newdblock.mBaudRate = kATCassetteDataSampleRate / newdblock.mSamplesPerBit;

		newdblock.mStartByte = (uint32)dblocks.mByteStartSamples.size();
		newdblock.mbValidFrame = false;

		dblocks.mByteStartSamples.push_back(startPos);
		dblocks.mByteData.push_back(0);
		dblocks.mByteFramingErrors.push_back(false);

		mSIOMonChecksum = 0;
		mSIOMonFramingErrors = 0;
	}

	if (framingError)
		++mSIOMonFramingErrors;

	DecodedBlock& dblock = dblocks.mBlocks.back();

	if (dblock.mByteCount >= 131 && mSIOMonChecksum == data) {
		if (!dblock.mbValidFrame || !mSIOMonFramingErrors)
			dblock.mChecksumPos = dblock.mByteCount;

		if (!mSIOMonFramingErrors)
			dblock.mbValidFrame = true;
	}

	const uint32 sum = (uint32)mSIOMonChecksum + data;
	mSIOMonChecksum = (uint8)(sum + (sum >> 8));

	++dblock.mByteCount;
	dblock.mSampleEnd = endPos;

	dblocks.mByteData.back() = data;
	dblocks.mByteData.push_back(0);

	dblocks.mByteFramingErrors.back() = framingError;
	dblocks.mByteFramingErrors.push_back(false);

	if (dblocks.mByteStartSamples.back() < startPos)
		dblocks.mByteStartSamples.back() = startPos;
	dblocks.mByteStartSamples.push_back(endPos);

	InvalidateRangeDeferred(ch1.mSampleEnd, endPos);
	ch1.mSampleEnd = endPos;
}

void ATUITapeViewControl::DecodeFSK(uint32 start, uint32 end, bool stopOnFramingError, DecodedBlocks& output) const {
	// capture range is 450-900 baud
	static constexpr uint32 kMinBitLen = (uint32)(kATCassetteDataSampleRate / 900.0f);
	static constexpr uint32 kMaxBitLen = (uint32)(kATCassetteDataSampleRate / 450.0f);

	if (start >= end)
		return;

	uint32 pos = start;

	while(pos < end) {
		const auto firstStartBitInfo = mpImage->FindNextBit(pos, end, false, mbShowTurboData);
		if (firstStartBitInfo.mBit)
			break;

		uint32 blockStart = firstStartBitInfo.mPos;
		uint32 blockEnd = end;
		uint32 blockDataEnd = end;

		// profile bit widths until next gap or end to get a rough estimate of bit period
		uint32 pos2 = blockStart;
		bool nextPolarity = true;
		uint32 numTalliedBits = 0;
		uint32 bitPeriodSum = 0;
		while(pos2 < end) {
			const auto nextBitInfo = mpImage->FindNextBit(pos2, end - 1, nextPolarity, mbShowTurboData);

			if (nextBitInfo.mPos - pos2 > kMaxBitLen * 10) {
				blockEnd = nextBitInfo.mPos;
				blockDataEnd = pos2;
				break;
			}

			uint32 dt = nextBitInfo.mPos - pos2;
			if (dt && dt >= kMinBitLen && dt <= kMaxBitLen) {
				++numTalliedBits;
				bitPeriodSum += dt;
			}

			nextPolarity = !nextPolarity;
			pos2 = nextBitInfo.mPos;
		}

		if (!numTalliedBits)
			break;

		// compute approximate average bit length
		float bitPeriodF = (float)bitPeriodSum / (float)numTalliedBits;
		uint32 bitPeriod = (uint32)VDRoundToInt(bitPeriodF);

		// restart and start parsing out bits
		DecodedBlock& dblock = output.mBlocks.emplace_back();
		dblock.mSampleStart = blockStart;
		dblock.mSampleEnd = blockEnd;
		dblock.mStartByte = (uint32)output.mByteData.size();
		dblock.mByteCount = 0;
		dblock.mChecksumPos = 0;
		dblock.mBaudRate = 0;

		pos2 = blockStart;

		bool blockEndedEarly = false;
		uint32 posLastByteEnd = pos2;
		while(pos2 < blockEnd) {
			const auto startBitInfo = mpImage->FindNextBit(pos2, blockEnd, false, mbShowTurboData);

			if (startBitInfo.mPos >= blockEnd)
				break;

			// check that we have enough room to fit a full byte
			if (blockEnd - pos2 < bitPeriod * 10) {
				blockEndedEarly = true;
				break;
			}

			// compute half bit offset to center of start bit
			uint32 startBitPos = startBitInfo.mPos + (bitPeriod >> 1);

			// sample start bit and verify that it's real
			if (mpImage->GetBit(startBitPos, mbShowTurboData)) {
				// not real -- skip it
				pos2 = startBitPos;
				posLastByteEnd = startBitPos;
				continue;
			}

			// sample data bits
			uint8 v = 0;
			for(int i=0; i<8; ++i)
				v = (v >> 1) + (mpImage->GetBit(startBitPos + (i + 1) * bitPeriod, mbShowTurboData) ? 0x80 : 0);

			// sample stop bit and check for framing error
			pos2 = startBitPos + 9 * bitPeriod;
			posLastByteEnd = startBitInfo.mPos + 10 * bitPeriod;

			if (!mpImage->GetBit(pos2, mbShowTurboData)) {
				if (stopOnFramingError) {
					blockEndedEarly = true;
					break;
				}

				output.mByteFramingErrors.push_back(true);
				pos2 = posLastByteEnd;
			} else
				output.mByteFramingErrors.push_back(false);

			output.mByteStartSamples.push_back(startBitInfo.mPos);
			output.mByteData.push_back(v);
		}

		if (output.mByteData.size() == dblock.mStartByte) {
			output.mBlocks.pop_back();
			break;
		}

		dblock.mByteCount = (uint32)output.mByteData.size() - dblock.mStartByte;

		// see if we can spot the checksum for a standard record (128+4 bytes),
		// or a longer record with the same framing
		if (dblock.mByteCount >= 132) {
			const uint8 *data = output.mByteData.begin() + dblock.mStartByte;
			const bool *frerrs = output.mByteFramingErrors.begin() + dblock.mStartByte;
			uint32 sum = 0;

			for(uint32 i = 0; i < 131; ++i)
				sum += data[i];

			uint8 chk = sum ? (sum - 1) % 255 + 1 : 0;
			bool framingOK = std::find(frerrs, frerrs + 131, true) == frerrs + 131;

			for(uint32 i = 131; i < dblock.mByteCount; ++i) {
				if (frerrs[i])
					framingOK = false;

				const uint8 c = data[i];

				if (chk == c) {
					if (framingOK || !dblock.mbValidFrame)
						dblock.mChecksumPos = i;

					if (framingOK)
						dblock.mbValidFrame = true;
				}

				sum = (uint32)chk + data[i];
				chk = (uint8)(sum + (sum >> 8));
			}
		}

		// push a dummy byte to delimit the last byte
		output.mByteStartSamples.push_back(std::min(posLastByteEnd, blockEnd));
		output.mByteData.push_back(0);
		output.mByteFramingErrors.push_back(false);

		// compute ideal baud rate based on estimated bit period
		const uint32 baudRateBitPeriod = (uint32)VDRoundToInt(kATCassetteDataSampleRate / bitPeriodF);

		// compute ideal baud rate based on the sample range
		const uint32 numBytes = dblock.mByteCount;
		const uint32 numBits = 10 * numBytes;
		const uint32 baudRateSampleRange = VDRoundToInt(kATCassetteDataSampleRate * (float)numBits / (float)(blockDataEnd - blockStart));

		// use the min the two
		uint32 baudRate = std::min(baudRateBitPeriod, baudRateSampleRange);

		dblock.mSamplesPerBit = bitPeriodF;
		dblock.mBaudRate = baudRate;

		// if the rounded baud rate causes us to go over, decrement it to fit
		ATCassetteWriteCursor writeCursor;
		writeCursor.mPosition = blockStart;
		for(int i=0; i<5; ++i) {
			uint32 neededSamples = mpImage->EstimateWriteStdData(writeCursor, numBytes, baudRate);
			if (neededSamples <= blockEnd - blockStart)
				break;

			--baudRate;
			VDASSERT(i != 4);
		}

		if (blockEndedEarly)
			break;

		pos = blockEnd;
	}
}

void ATUITapeViewControl::DecodeT2000(uint32 start, uint32 end, DecodedBlocks& output) const {
	if (start >= end)
		return;

	bool polarity = false;
	uint32 lastPulseWidth = 0;
	uint32 pilotWindow[16] {};
	uint32 pilotWindowIndex = 0;
	uint32 pilotWindowSum = 8;
	uint32 pilotCount = 0;

	enum class State : uint8 {
		Pilot,
		Sync1,
		Sync2,
		Data0a,
		Data0b,
		Data1a,
		Data1b,
		Data2a,
		Data2b,
		Data3a,
		Data3b,
		Data4a,
		Data4b,
		Data5a,
		Data5b,
		Data6a,
		Data6b,
		Data7a,
		Data7b,
	} state = State::Pilot;

	uint32 pos = start;
	uint8 c = 0;
	uint32 byteStart = 0;
	while(pos < end) {
		const auto bitInfo = mpImage->FindNextBit(pos, end, polarity, true);
		polarity = !polarity;

		if (bitInfo.mPos >= end)
			break;

		const uint32 pulseWidth = bitInfo.mPos - pos;
		pos = bitInfo.mPos;

		const uint32 cycleWidth = pulseWidth + lastPulseWidth;
		lastPulseWidth = pulseWidth;

		switch(state) {
			case State::Pilot: {
				uint32 avgPilotWidth = pilotWindowSum >> 4;
				pilotWindowSum -= pilotWindow[pilotWindowIndex];
				pilotWindowSum += cycleWidth;
				pilotWindow[pilotWindowIndex] = cycleWidth;
				pilotWindowIndex = (pilotWindowIndex + 1) & 15;

				if (avgPilotWidth > 5) {
					uint32 minPilotWidth = avgPilotWidth - 2;
					uint32 maxPilotWidth = avgPilotWidth + 2;

					if (cycleWidth < minPilotWidth) {
						if (pilotCount >= 16)
							state = State::Sync1;

						pilotCount = 0;
					} else if (cycleWidth > maxPilotWidth)
						pilotCount = 0;
					else
						++pilotCount;
				}
				break;
			}

			case State::Sync1:
				state = State::Sync2;
				break;

			case State::Sync2:
				state = State::Data0a;
				byteStart = pos;
				break;

			case State::Data0a:
			case State::Data1a:
			case State::Data2a:
			case State::Data3a:
			case State::Data4a:
			case State::Data5a:
			case State::Data6a:
			case State::Data7a:
				state = (State)((uint8)state + 1);
				break;

			case State::Data0b:
			case State::Data1b:
			case State::Data2b:
			case State::Data3b:
			case State::Data4b:
			case State::Data5b:
			case State::Data6b:
				c += c;
				if (cycleWidth >= 16)
					++c;
				state = (State)((uint8)state + 1);
				break;

			case State::Data7b: {
				c += c;
				if (cycleWidth >= 16)
					++c;

				if (output.mBlocks.empty()) {
					auto& dblock = output.mBlocks.emplace_back();
					dblock.mSampleStart = byteStart;
					dblock.mSampleEnd = pos;
					dblock.mSamplesPerBit = 0;
					dblock.mBaudRate = 0;
					dblock.mStartByte = 0;
					dblock.mByteCount = 0;
					dblock.mChecksumPos = 0;
					dblock.mbValidFrame = false;

					output.mByteStartSamples.push_back(byteStart);
				}

				auto& dblock2 = output.mBlocks.back();
				byteStart = pos;
				dblock2.mSampleEnd = pos;
				++dblock2.mByteCount;

				output.mByteStartSamples.push_back(pos);
				output.mByteData.push_back(c);

				state = State::Data0a;
				break;
			}
		}
	}

	if (!output.mBlocks.empty()) {
		auto& dblock = output.mBlocks.back();

		uint8 chk = output.mByteData[0];
		uint32 n = dblock.mByteCount;
		for(uint32 i = 1; i < n; ++i) {
			chk ^= output.mByteData[i];

			if (chk == 0) {
				dblock.mChecksumPos = i;
				dblock.mbValidFrame = true;
			}
		}

		output.mByteData.push_back(0);
		output.mByteFramingErrors.resize(output.mByteData.size(), false);
	}
}

////////////////////////////////////////////////////////////////////////////////

ATUITapeEditorDialog::ATUITapeEditorDialog()
	: VDDialogFrameW32(IDD_TAPEEDITOR)
{
	mpTapeView = new ATUITapeViewControl;

	mFnOnTapeDirtyStateChanged = [this] { OnTapeDirtyStateChanged(); };
	mFnOnTapeChanged = [this] {
		if (mpTapeView)
			mpTapeView->SetImage(g_sim.GetCassette().GetImage());
	};

	mToolbar.SetOnClicked([this](auto id) { OnToolbarCommand(id); });
}

bool ATUITapeEditorDialog::PreNCDestroy() {
	ATUIUnregisterModelessDialog(mhdlg);
	g_pATUITapeEditorDialog = nullptr;
	return true;
}

bool ATUITapeEditorDialog::OnLoaded() {
	mBaseCaption = GetCaption();

	g_pATUITapeEditorDialog = this;

	ATUIRegisterModelessDialog(mhdlg);
	SetCurrentSizeAsMinSize();
	
	ATUIRestoreWindowPlacement(mhdlg, "TapeEditorDialog");

	LoadAcceleratorTable(IDR_TAPEEDITOR_ACCEL);

	AddProxy(&mToolbar, IDC_TOOLBAR);
	AddProxy(&mStatusBar, IDC_STATUS);

	mpTapeView->CreateChild(mhwnd, IDC_STATIC, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE);

	mToolbar.SetDarkModeEnabled(true);
	mToolbar.AddButton(kCmdId_ModeScroll, -1, L"Scroll");
	mToolbar.AddButton(kCmdId_ModeDraw, -1, L"Draw");
	mToolbar.AddButton(kCmdId_ModeSelect, -1, L"Select");
	mToolbar.AddButton(kCmdId_ModeInsert, -1, L"Insert");
	mToolbar.AddDropdownButton(kCmdId_ModeAnalyze, -1, L"Analyze");
	mToolbar.AddSeparator();
	mToolbar.AddButton(kCmdId_Delete, -1, L"Delete");

	mpTapeView->SetImage(g_sim.GetCassette().GetImage());
	mpTapeView->SetCassetteEmulator(&g_sim.GetCassette());
	mpTapeView->SetOnDrawModeChanged([this] { DeferUpdateModeButtons(); DeferUpdateStatusMessage(); });
	mpTapeView->SetOnAnalysisEncodingChanged([this] { UpdateAnalyzeModeButton(); });
	mpTapeView->SetOnSelectionChanged([this] { DeferUpdateStatusMessage(); });

	auto& cas = g_sim.GetCassette();
	cas.TapeDirtyStateChanged += &mFnOnTapeDirtyStateChanged;
	cas.TapeChanged.Add(&mFnOnTapeChanged);

	UpdateModeButtons();
	UpdateAnalyzeModeButton();
	OnSize();
	OnTapeDirtyStateChanged();
	return true;
}

void ATUITapeEditorDialog::OnDestroy() {
	ATUISaveWindowPlacement(mhdlg, "TapeEditorDialog");

	auto& cas = g_sim.GetCassette();

	cas.TapeDirtyStateChanged -= &mFnOnTapeDirtyStateChanged;
	cas.TapeChanged.Remove(&mFnOnTapeChanged);

	VDDialogFrameW32::OnDestroy();
}

void ATUITapeEditorDialog::OnSize() {
	const auto sz = GetClientArea().size();

	mToolbar.SetPosition(vdpoint32(0, 0));
	mToolbar.AutoSize();

	mStatusBar.AutoLayout();

	const sint32 th = mToolbar.GetWindowArea().height();
	const sint32 sh = mStatusBar.GetWindowArea().height();

	mpTapeView->SetArea(vdrect32(0, th, sz.w, sz.h - sh));
}

bool ATUITapeEditorDialog::OnCommand(uint32 id, uint32 extcode) {
	switch(id) {
		case ID_FILE_NEW:
			New();
			return true;

		case ID_FILE_OPEN:
			Open();
			return true;

		case ID_FILE_RELOAD:
			Reload();
			return true;

		case ID_FILE_SAVEASCAS:
			SaveAsCAS();
			return true;

		case ID_FILE_SAVEASWAV:
			SaveAsWAV();
			return true;

		case ID_FILE_CLOSE:
			Close();
			return true;

		case ID_EDIT_CUT:
			Cut();
			return true;

		case ID_EDIT_COPY:
			Copy();
			return true;

		case ID_EDIT_PASTE:
			Paste();
			return true;

		case ID_EDIT_DELETE:
			Delete();
			return true;

		case ID_EDIT_UNDO:
			Undo();
			return true;

		case ID_EDIT_REDO:
			Redo();
			return true;

		case ID_EDIT_CONVERTTOSTANDARDBLOCK:
			ConvertToStdBlock();
			return true;

		case ID_EDIT_CONVERTTORAWBLOCK:
			ConvertToRawBlock();
			return true;

		case ID_DATA_EXTRACTCFILE:
			ExtractCFile();
			return true;

		case ID_MONITOR_CAPTURESIO:
			mpTapeView->SetSIOMonitorEnabled(!mpTapeView->GetSIOMonitorEnabled());
			return true;

		case ID_VIEW_FSKDATA:
			mpTapeView->SetShowTurboData(false);
			break;

		case ID_VIEW_TURBODATA:
			mpTapeView->SetShowTurboData(true);
			break;

		case ID_VIEW_SHOWWAVEFORM:
			mpTapeView->SetShowWaveform(!mpTapeView->GetShowWaveform());
			break;
		case ID_OPTIONS_STOREWAVEFORM:
			mpTapeView->SetStoreWaveformOnLoad(!mpTapeView->GetStoreWaveformOnLoad());
			break;
	}

	return false;
}

void ATUITapeEditorDialog::OnInitMenu(VDZHMENU hmenu) {
	const bool hasImage = mpTapeView->GetImage() != nullptr;

	const bool hasNonEmptySelection = hasImage && mpTapeView->HasNonEmptySelection();
	const bool hasSelection = hasImage && mpTapeView->HasSelection();

	VDEnableMenuItemByCommandW32(hmenu, ID_EDIT_UNDO, hasImage && mpTapeView->CanUndo());
	VDEnableMenuItemByCommandW32(hmenu, ID_EDIT_REDO, hasImage && mpTapeView->CanRedo());
	VDEnableMenuItemByCommandW32(hmenu, ID_EDIT_CUT, hasNonEmptySelection);
	VDEnableMenuItemByCommandW32(hmenu, ID_EDIT_COPY, hasNonEmptySelection);
	VDEnableMenuItemByCommandW32(hmenu, ID_EDIT_PASTE, hasImage && mpTapeView->HasClip());
	VDEnableMenuItemByCommandW32(hmenu, ID_EDIT_DELETE, hasNonEmptySelection);
	VDEnableMenuItemByCommandW32(hmenu, ID_DATA_EXTRACTCFILE, hasNonEmptySelection);

	VDEnableMenuItemByCommandW32(hmenu, ID_FILE_SAVEASCAS, hasImage);
	VDEnableMenuItemByCommandW32(hmenu, ID_FILE_SAVEASWAV, hasImage);

	VDCheckMenuItemByCommandW32(hmenu, ID_VIEW_SHOWWAVEFORM, mpTapeView->GetShowWaveform());
	VDCheckMenuItemByCommandW32(hmenu, ID_OPTIONS_STOREWAVEFORM, mpTapeView->GetStoreWaveformOnLoad());

	const bool showTurbo = mpTapeView->GetShowTurboData();
	VDEnableMenuItemByCommandW32(hmenu, ID_EDIT_CONVERTTOSTANDARDBLOCK, !showTurbo);
	VDEnableMenuItemByCommandW32(hmenu, ID_EDIT_CONVERTTORAWBLOCK, !showTurbo);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIEW_FSKDATA, !showTurbo);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIEW_TURBODATA, showTurbo);
}

void ATUITapeEditorDialog::OnTapeDirtyStateChanged() {
	const bool dirty = g_sim.GetCassette().IsImageDirty();

	VDStringW s;

	if (dirty)
		s += L'*';

	s.append(mBaseCaption);

	SetCaption(s.c_str());
}

void ATUITapeEditorDialog::OnToolbarCommand(uint32 id) {
	try {
		switch(id) {
			case kCmdId_ModeScroll:
				mpTapeView->SetDrawMode(ATUITapeViewControl::DrawMode::Scroll);
				break;

			case kCmdId_ModeDraw:
				mpTapeView->SetDrawMode(ATUITapeViewControl::DrawMode::Draw);
				break;

			case kCmdId_ModeSelect:
				mpTapeView->SetDrawMode(ATUITapeViewControl::DrawMode::Select);
				break;

			case kCmdId_ModeInsert:
				mpTapeView->SetDrawMode(ATUITapeViewControl::DrawMode::Insert);
				break;

			case kCmdId_ModeAnalyze:
				{
					static constexpr const wchar_t *kAnalyzeModes[] = {
						L"FSK",
						L"T2000",
						nullptr
					};

					sint32 index = mToolbar.ShowDropDownMenu(kCmdId_ModeAnalyze, kAnalyzeModes);

					if (index >= 0) {
						mpTapeView->SetDrawMode(ATUITapeViewControl::DrawMode::Analyze);
						mpTapeView->SetAnalysisEncoding((ATUITapeViewControl::Encoding)index);
					}
				};
				break;

			case kCmdId_Delete:
				mpTapeView->Delete();
				break;
		}
	} catch(const MyError& e) {
		ShowError(e);
	}
}

void ATUITapeEditorDialog::New() {
	if (!OKToDiscard())
		return;

	auto& cas = g_sim.GetCassette();
	cas.LoadNew();

	mpTapeView->SetImage(cas.GetImage());
}

void ATUITapeEditorDialog::Open() {
	if (!OKToDiscard())
		return;

	const VDStringW& path = VDGetLoadFileName('cass', (VDGUIHandle)mhdlg, L"Load cassette tape", g_ATUIFileFilter_LoadTape, nullptr);
	if (!path.empty())
		Load(path.c_str());
}

void ATUITapeEditorDialog::Reload() {
	if (!OKToDiscard())
		return;

	auto& cas = g_sim.GetCassette();
	IATCassetteImage *image = cas.GetImage();
	if (!image)
		return;

	if (cas.IsImagePersistent()) {
		mpTapeView->LockViewReset();

		try {
			Load(VDStringW(cas.GetPath()).c_str());
		} catch(...) {
			mpTapeView->UnlockViewReset();
			throw;
		}

		mpTapeView->UnlockViewReset();
	}
}

void ATUITapeEditorDialog::Load(const wchar_t *path) {
	auto& cas = g_sim.GetCassette();

	cas.Unload();

	vdrefptr<ATVFSFileView> view;
	ATVFSOpenFileView(path, false, ~view);

	ATCassetteLoadContext ctx;
	ctx.mTurboDecodeAlgorithm = cas.GetTurboDecodeAlgorithm();
	ctx.mbStoreWaveform = true;

	vdrefptr<IATCassetteImage> image;
	ATLoadCassetteImage(view->GetStream(), nullptr, ctx, ~image);

	cas.Load(image, path, true);
}

void ATUITapeEditorDialog::SaveAsCAS() {
	auto& cas = g_sim.GetCassette();
	IATCassetteImage *image = cas.GetImage();

	if (!image)
		return;

	const VDStringW& path = VDGetSaveFileName('cass', (VDGUIHandle)mhdlg, L"Save cassette tape", g_ATUIFileFilter_SaveTape, L"cas");
	if (!path.empty()) {
		VDFileStream fs(path.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
		ATSaveCassetteImageCAS(fs, image);

		cas.SetImagePersistent(path.c_str());
		cas.SetImageClean();
	}
}

void ATUITapeEditorDialog::SaveAsWAV() {
	auto& cas = g_sim.GetCassette();
	IATCassetteImage *image = cas.GetImage();

	if (!image)
		return;

	const VDStringW& path = VDGetSaveFileName('casa', (VDGUIHandle)mhdlg, L"Save cassette tape audio", g_ATUIFileFilter_SaveTapeAudio, L"wav");
	if (!path.empty()) {
		VDFileStream fs(path.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
		ATSaveCassetteImageWAV(fs, image);

		cas.SetImagePersistent(path.c_str());
		cas.SetImageClean();
	}
}

void ATUITapeEditorDialog::Cut() {
	mpTapeView->Cut();
}

void ATUITapeEditorDialog::Copy() {
	mpTapeView->Copy();
}

void ATUITapeEditorDialog::Paste() {
	mpTapeView->Paste();
}

void ATUITapeEditorDialog::Delete() {
	mpTapeView->Delete();
}

void ATUITapeEditorDialog::Undo() {
	mpTapeView->Undo();
}

void ATUITapeEditorDialog::Redo() {
	mpTapeView->Redo();
}

void ATUITapeEditorDialog::ConvertToStdBlock() {
	if (!mpTapeView->GetShowTurboData())
		mpTapeView->ConvertToStdBlock();
}

void ATUITapeEditorDialog::ConvertToRawBlock() {
	mpTapeView->ConvertToRawBlock();
}

void ATUITapeEditorDialog::ExtractCFile() {
	vdfastvector<uint8> data;
	mpTapeView->ExtractSelectionAsCFile(data);

	if (data.empty())
		throw MyError("There is no data to extract.");

	VDStringW path(VDGetSaveFileName('tapx', (VDGUIHandle)mhdlg, L"Save extracted file", L"All files\0*.*\0", nullptr));
	if (!path.empty()) {
		VDFile f(path.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);

		f.write(data.data(), (long)data.size());
		f.close();
	}
}

bool ATUITapeEditorDialog::OKToDiscard() {
	if (!g_sim.GetCassette().IsImageDirty())
		return true;

	return Confirm2("DiscardTape", L"The current tape has not been saved. Continue anyway?", L"Unsaved tape");
}

void ATUITapeEditorDialog::DeferUpdateModeButtons() {
	if (!mbPendingUpdateModeButtonsCall) {
		mbPendingUpdateModeButtonsCall = true;
		mbPendingUpdateModeButtons = true;

		PostCall([this] {
			if (mbPendingUpdateModeButtonsCall) {
				mbPendingUpdateModeButtonsCall = false;
				UpdateModeButtons();
			}
		});
	}
}

void ATUITapeEditorDialog::DeferUpdateStatusMessage() {
	if (!mbPendingUpdateStatusMessageCall) {
		mbPendingUpdateStatusMessageCall = true;
		mbPendingUpdateStatusMessage = true;

		PostCall([this] {
			if (mbPendingUpdateStatusMessageCall) {
				mbPendingUpdateStatusMessageCall = false;
				UpdateStatusMessage();
			}
		});
	}
}

void ATUITapeEditorDialog::UpdateModeButtons() {
	mbPendingUpdateModeButtons = false;

	const auto drawMode = mpTapeView->GetDrawMode();

	mToolbar.SetItemPressed(kCmdId_ModeScroll, drawMode == ATUITapeViewControl::DrawMode::Scroll);
	mToolbar.SetItemPressed(kCmdId_ModeDraw, drawMode == ATUITapeViewControl::DrawMode::Draw);
	mToolbar.SetItemPressed(kCmdId_ModeSelect, drawMode == ATUITapeViewControl::DrawMode::Select);
	mToolbar.SetItemPressed(kCmdId_ModeInsert, drawMode == ATUITapeViewControl::DrawMode::Insert);
	mToolbar.SetItemPressed(kCmdId_ModeAnalyze, drawMode == ATUITapeViewControl::DrawMode::Analyze);
}

void ATUITapeEditorDialog::UpdateAnalyzeModeButton() {
	const auto analysisEnc = mpTapeView->GetAnalysisEncoding();

	switch(analysisEnc) {
		case ATUITapeViewControl::Encoding::FSK:
			mToolbar.SetItemText(kCmdId_ModeAnalyze, L"Analyze (FSK)");
			break;

		case ATUITapeViewControl::Encoding::T2000:
			mToolbar.SetItemText(kCmdId_ModeAnalyze, L"Analyze (T2000)");
			break;
	}
}

void ATUITapeEditorDialog::UpdateStatusMessage() {
	mbPendingUpdateStatusMessage = false;

	const bool hasNonEmptySelection = mpTapeView->HasNonEmptySelection();

	switch(mpTapeView->GetDrawMode()) {
		case ATUITapeViewControl::DrawMode::Scroll:
			mStatusBar.SetCaption(L"Scroll tool: left-drag to scroll to other parts of the tape");
			return;

		case ATUITapeViewControl::DrawMode::Draw:
			mStatusBar.SetCaption(L"Draw tool: left-click or drag to set or reset bits");
			return;

		case ATUITapeViewControl::DrawMode::Select:
			if (!hasNonEmptySelection) {
				mStatusBar.SetCaption(L"Select tool: left-drag to select a region of bits");
				return;
			}
			break;

		case ATUITapeViewControl::DrawMode::Insert:
			if (!hasNonEmptySelection) {
				mStatusBar.SetCaption(L"Insert tool: left-drag to the right to insert tape");
				return;
			}
			break;

		case ATUITapeViewControl::DrawMode::Analyze:
			if (!hasNonEmptySelection) {
				mStatusBar.SetCaption(L"Analyze tool: left-drag over range to decode as standard bytes");
				return;
			}
			break;
	}	

	const uint32 selStart = mpTapeView->GetSelectionSortedStart();
	const uint32 selEnd = mpTapeView->GetSelectionSortedEnd();
	const float selStartTime = (float)selStart * kATCassetteSecondsPerDataSample;
	const float selEndTime = (float)selEnd * kATCassetteSecondsPerDataSample;

	VDStringW s;
	s.sprintf(L"Selected %.3fs in range %.3fs-%.3fs | %u sample%ls in %u-%u"
		, selEndTime - selStartTime
		, selStartTime
		, selEndTime
		, selEnd - selStart
		, selEnd - selStart != 1 ? L"s" : L""
		, selStart
		, selEnd
	);

	mStatusBar.SetCaption(s.c_str());
}

////////////////////////////////////////////////////////////////////////////////

void ATUIShowDialogTapeEditor() {
	if (!g_pATUITapeEditorDialog) {
		vdautoptr ed { new ATUITapeEditorDialog };

		if (!ed->Create(ATUIGetMainWindow()))
			return;

		ed->Show();
		ed.release();
	}

	g_pATUITapeEditorDialog->Activate();
}
