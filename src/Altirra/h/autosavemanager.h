//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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

#ifndef f_AT_AUTOSAVEMANAGER_H
#define f_AT_AUTOSAVEMANAGER_H

#include <deque>
#include <vd2/system/date.h>
#include <vd2/system/refcount.h>
#include <vd2/system/unknown.h>
#include <at/atcore/serializable.h>
#include "cpu.h"

class ATSimulator;

class IATAutoSaveView : public IVDRefUnknown {
public:
	virtual const VDPixmap *GetImage() const = 0;
	virtual float GetImagePAR() const = 0;
	virtual vdrect32f GetImageBeamRect() const = 0;
	virtual VDDate GetTimestamp() const = 0;
	virtual double GetRealTimeSeconds() const = 0;
	virtual double GetSimulatedTimeSeconds() const = 0;
	virtual uint32 GetColdStartId() const = 0;

	virtual void ApplyState() = 0;
};

class IATAutoSaveManager : public IVDRefCount {
public:
	virtual double GetCurrentRunTimeSeconds() const = 0;
	virtual uint32 GetCurrentColdStartId() const = 0;

	virtual bool GetRewindEnabled() const = 0;
	virtual void SetRewindEnabled(bool enable) = 0;

	virtual void Rewind() = 0;
	virtual void GetRewindStates(vdvector<vdrefptr<IATAutoSaveView>>& stateViews) = 0;
};

vdrefptr<IATAutoSaveManager> ATCreateAutoSaveManager(ATSimulator& parent);

#endif
