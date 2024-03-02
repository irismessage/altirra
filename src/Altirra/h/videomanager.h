//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2018 Avery Lee
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

#ifndef f_AT_VIDEOMANAGER_H
#define f_AT_VIDEOMANAGER_H

#include <at/atcore/devicevideo.h>

class ATVideoManager final : public IATDeviceVideoManager {
public:
	void ResetActivityCounters();
	sint32 CheckForNewlyActiveOutputs();

	uint32 GetOutputCount() const override;
	uint32 GetOutputListChangeCount() const override;
	IATDeviceVideoOutput *GetOutput(uint32 index) const override;
	IATDeviceVideoOutput *GetOutputByName(const char *name) const override;
	sint32 IndexOfOutput(IATDeviceVideoOutput *output) const override;

	void AddVideoOutput(IATDeviceVideoOutput *output) override;
	void RemoveVideoOutput(IATDeviceVideoOutput *output) override;

	ATNotifyList<const vdfunction<void(uint32 index)> *>& OnAddedOutput() override {
		return mOnAddedOutput;
	}

	ATNotifyList<const vdfunction<void(uint32 index)> *>& OnRemovingOutput() override {
		return mOnRemovingOutput;
	}

private:
	struct OutputInfo {
		IATDeviceVideoOutput *mpOutput;
		uint32 mLastActivityCounter;
	};

	vdfastvector<OutputInfo> mOutputs;
	uint32 mListChangeCount = 1;

	ATNotifyList<const vdfunction<void(uint32 index)> *> mOnAddedOutput;
	ATNotifyList<const vdfunction<void(uint32 index)> *> mOnRemovingOutput;
};

#endif
