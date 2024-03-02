//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2010 Avery Lee
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

#ifndef f_AT_UIRENDER_H
#define f_AT_UIRENDER_H

#include <vd2/system/refcount.h>

struct VDPixmap;
class ATAudioMonitor;
class ATSlightSIDEmulator;
class IVDDisplayCompositor;
class ATUIManager;

struct ATUIAudioStatus {
	int mUnderflowCount;
	int mOverflowCount;
	int mDropCount;
	int mMeasuredMin;
	int mMeasuredMax;
	int mTargetMin;
	int mTargetMax;
	double mIncomingRate;
	double mExpectedRate;
	bool mbStereoMixing;
};

class IATUIRenderer : public IVDRefCount {
public:
	virtual void SetStatusFlags(uint32 flags) = 0;
	virtual void ResetStatusFlags(uint32 flags) = 0;
	virtual void PulseStatusFlags(uint32 flags) = 0;

	virtual void SetStatusCounter(uint32 index, uint32 value) = 0;

	virtual void SetDiskMotorActivity(uint32 index, bool on) = 0;

	virtual void SetHActivity(bool write) = 0;
	virtual void SetIDEActivity(bool write, uint32 lba) = 0;
	virtual void SetPCLinkActivity(bool write) = 0;

	virtual void SetFlashWriteActivity() = 0;

	virtual void SetCassetteIndicatorVisible(bool vis) = 0;
	virtual void SetCassettePosition(float pos) = 0;

	virtual void SetRecordingPosition() = 0;
	virtual void SetRecordingPosition(float time, sint64 size) = 0;

	virtual void SetModemConnection(const char *str) = 0;

	virtual void SetStatusMessage(const wchar_t *s) = 0;

	virtual void SetLedStatus(uint8 ledMask) = 0;
	
	virtual void SetHeldButtonStatus(uint8 consolMask) = 0;

	virtual void ClearWatchedValue(int index) = 0;
	virtual void SetWatchedValue(int index, uint32 value, int len) = 0;

	virtual void SetAudioStatus(ATUIAudioStatus *status) = 0;

	virtual void SetAudioMonitor(bool secondary, ATAudioMonitor *monitor) = 0;

	virtual void SetSlightSID(ATSlightSIDEmulator *emu) = 0;

	virtual void SetFpsIndicator(float fps) = 0;

	virtual void SetHoverTip(int px, int py, const wchar_t *text) = 0;

	virtual void SetPaused(bool paused) = 0;

	virtual void SetUIManager(ATUIManager *mgr) = 0;
	virtual void Relayout(int w, int h) = 0;

	virtual void Update() = 0;
};

void ATCreateUIRenderer(IATUIRenderer **r);

#endif
