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

#include <stdafx.h>
#include <at/atcore/devicesnapshot.h>
#include <at/atcore/serialization.h>
#include "autosavemanager.h"
#include "savestatetypes.h"
#include "simulator.h"
#include "simeventmanager.h"

class ATAutoSaveManager;

class ATAutoSaveEntry : public vdrefcounted<IATAutoSaveView> {
public:
	ATAutoSaveEntry(ATAutoSaveManager& parent) : mParent(parent) {}

	const IATSerializable& GetSaveState() { return *mpSaveState; }

	void SetSaveData(vdrefptr<IATSerializable> save, vdrefptr<IATSerializable> saveInfo);
	void SetRealTimestamp(const VDDate& date) { mTimestamp = date; }
	void SetRealTimeSeconds(double t) { mRealRunTime = t; }
	void SetSimulatedTimeSeconds(double t) { mSimulatedRunTime = t; }
	void SetColdResetId(uint32 id) { mColdStartId = id; }

public:
	void *AsInterface(uint32) override { return nullptr; }

	VDDate GetTimestamp() const override { return mTimestamp; }
	double GetRealTimeSeconds() const override { return mRealRunTime; }
	double GetSimulatedTimeSeconds() const override { return mSimulatedRunTime; }
	uint32 GetColdStartId() const override { return mColdStartId; }
	const VDPixmap *GetImage() const override;
	float GetImagePAR() const override;
	vdrect32f GetImageBeamRect() const override;

	void ApplyState() override;

private:
	friend class ATAutoSaveManager;

	ATAutoSaveManager& mParent;
	VDDate mTimestamp {};
	double mRealRunTime = 0;
	double mSimulatedRunTime = 0;
	uint32 mColdStartId = 0;
	bool mbIsNearSave = true;
	vdrefptr<IATSerializable> mpSaveState;
	vdrefptr<IATSerializable> mpSaveStateInfo;
};

////////////////////////////////////////////////////////////////////////////////

class ATAutoSaveManager : public vdrefcounted<IATAutoSaveManager> {
	ATAutoSaveManager(const ATAutoSaveManager&) = delete;
	ATAutoSaveManager& operator=(const ATAutoSaveManager&) = delete;

public:
	ATAutoSaveManager(ATSimulator& parent);
	~ATAutoSaveManager();

	double GetCurrentRunTimeSeconds() const override;
	uint32 GetCurrentColdStartId() const override;

	bool GetRewindEnabled() const;
	void SetRewindEnabled(bool enable);

	void Rewind() override;
	void GetRewindStates(vdvector<vdrefptr<IATAutoSaveView>>& stateViews) override;

private:
	friend class ATAutoSaveEntry;

	void ClearRewindSaves();
	void OnReset();
	void OnVBLANK();
	ATCPUStepResult OnNMIExecuted();
	void DoSave();
	void ApplyState(ATAutoSaveEntry& entry);

	ATSimulator& mParent;
	std::deque<vdrefptr<ATAutoSaveEntry>> mAutoSaveQueue;
	vdfastvector<uint8> mFarSaveCounters;
	bool mbSavePending = false;
	bool mbRewindEnabled = false;

	VDDate mLastDate {};
	uint32 mLastFrame = 0;
	VDDate mLastRewindDate {};

	uint32 mNumNearSaves = 0;
	uint32 mMaxNearSaves = 5;
	uint32 mMaxFarSaves = 20;
	uint32 mFarSaveNumerator = 1;
	uint32 mFarSaveDenominator = 3;

	ATCPUStepCondition mStepCondition;

	uint32 mEventRegVBLANK = 0;
	uint32 mEventRegWarmReset = 0;
	uint32 mEventRegColdReset = 0;
};

////////////////////////////////////////////////////////////////////////////////

void ATAutoSaveEntry::SetSaveData(vdrefptr<IATSerializable> save, vdrefptr<IATSerializable> saveInfo) {
	mpSaveState = std::move(save);
	mpSaveStateInfo = std::move(saveInfo);
}

const VDPixmap *ATAutoSaveEntry::GetImage() const {
	ATSaveStateInfo *info = atser_cast<ATSaveStateInfo *>(mpSaveStateInfo);

	return info ? &info->mImage : nullptr;
}

float ATAutoSaveEntry::GetImagePAR() const {
	ATSaveStateInfo *info = atser_cast<ATSaveStateInfo *>(mpSaveStateInfo);

	return info ? std::clamp<float>(info->mImagePAR, 0.1f, 10.0f) : 1.0f;
}

vdrect32f ATAutoSaveEntry::GetImageBeamRect() const {
	ATSaveStateInfo *info = atser_cast<ATSaveStateInfo *>(mpSaveStateInfo);

	if (!info)
		return {};

	return vdrect32f(
		info->mImageX1,
		info->mImageY1,
		info->mImageX2,
		info->mImageY2
	);
}

void ATAutoSaveEntry::ApplyState() {
	mParent.ApplyState(*this);
}

////////////////////////////////////////////////////////////////////////////////

ATAutoSaveManager::ATAutoSaveManager(ATSimulator& parent)
	: mParent(parent)
{
	mStepCondition.mbWaitForNMI = true;
	mStepCondition.mpCallback = [](ATCPUEmulator *cpu, uint32 pc, bool call, void *data) -> ATCPUStepResult {
		auto *thisptr = (ATAutoSaveManager *)data;

		return thisptr->OnNMIExecuted();
	};
	mStepCondition.mpCallbackData = this;

	mEventRegVBLANK = mParent.GetEventManager()->AddEventCallback(kATSimEvent_VBLANK, [this] { OnVBLANK(); });
	mEventRegWarmReset = mParent.GetEventManager()->AddEventCallback(kATSimEvent_WarmReset, [this] { OnReset(); });
	mEventRegColdReset = mParent.GetEventManager()->AddEventCallback(kATSimEvent_ColdReset, [this] { OnReset(); });

	mFarSaveCounters.resize(mMaxFarSaves, 0);
}

ATAutoSaveManager::~ATAutoSaveManager() {
	mParent.GetEventManager()->RemoveEventCallback(mEventRegVBLANK);
	mParent.GetEventManager()->RemoveEventCallback(mEventRegWarmReset);
	mParent.GetEventManager()->RemoveEventCallback(mEventRegColdReset);

	mParent.GetCPU().RemoveStepCondition(mStepCondition);
}

double ATAutoSaveManager::GetCurrentRunTimeSeconds() const {
	return mParent.SimulatedSecondsSinceColdReset();
}

uint32 ATAutoSaveManager::GetCurrentColdStartId() const {
	return mParent.GetColdStartId();
}

bool ATAutoSaveManager::GetRewindEnabled() const {
	return mbRewindEnabled;
}

void ATAutoSaveManager::SetRewindEnabled(bool enable) {
	if (mbRewindEnabled != enable) {
		mbRewindEnabled = enable;

		if (!enable) {
			ClearRewindSaves();
			mParent.GetCPU().RemoveStepCondition(mStepCondition);
			mbSavePending = false;
		}
	}
}

void ATAutoSaveManager::Rewind() {
	if (mAutoSaveQueue.empty())
		return;

	ATAutoSaveEntry *save = mAutoSaveQueue.back();

	// If we are within half a second of the last rewind, rewind two back if possible.
	const VDDate now = VDGetCurrentDate();
	if (mAutoSaveQueue.size() > 1) {
#if 0
		static const auto kRewindPastThreshold = VDDateInterval::FromSeconds(0.5f);
		const VDDateInterval timeSinceLastRewind = now - mLastRewindDate;

		if (timeSinceLastRewind < kRewindPastThreshold) {
			save = mAutoSaveQueue.end()[-2];
		}
#else
		if (mParent.RealSecondsSinceColdReset() - save->GetRealTimeSeconds() < 0.5)
			save = mAutoSaveQueue.end()[-2];

#endif
	}

	mLastRewindDate = now;

	ApplyState(*save);
}

void ATAutoSaveManager::GetRewindStates(vdvector<vdrefptr<IATAutoSaveView>>& stateViews) {
	for(const auto& save : mAutoSaveQueue)
		stateViews.push_back(save);
}

void ATAutoSaveManager::ClearRewindSaves() {
	mAutoSaveQueue.clear();
	mNumNearSaves = 0;
}

void ATAutoSaveManager::OnReset() {
	mLastFrame = mParent.GetAntic().GetRawFrameCounter();
}

void ATAutoSaveManager::OnVBLANK() {
	if (!mbRewindEnabled)
		return;

	if (!mbSavePending) {
		static constexpr VDDateInterval kOneSecond = VDDateInterval::FromSeconds(1.0f);
		VDDateInterval realTimePassed = (VDGetCurrentDate() - mLastDate).Abs();

		const uint32 frame = mParent.GetAntic().GetRawFrameCounter();
		const uint32 framesPassed = (uint32)(frame - mLastFrame);

		if (realTimePassed < kOneSecond || framesPassed < (mParent.IsVideo50Hz() ? 49U : 59U)) {
			mParent.GetCPU().RemoveStepCondition(mStepCondition);
			return;
		}

		mbSavePending = true;
	}

	if (mParent.GetAntic().IsVBIEnabled()) {
		mParent.GetCPU().RemoveStepCondition(mStepCondition);
		mStepCondition.mbWaitForNMI = true;
		mParent.GetCPU().AddStepCondition(mStepCondition);
	} else
		DoSave();
}

ATCPUStepResult ATAutoSaveManager::OnNMIExecuted() {
	if (mbSavePending && mParent.GetAntic().GetBeamY() == 248)
		DoSave();

	mParent.GetCPU().RemoveStepCondition(mStepCondition);

	return kATCPUStepResult_Continue;
}

void ATAutoSaveManager::DoSave() {
	mbSavePending = false;

	// check if it is safe to save
	if (mParent.GetSnapshotStatus().mbInDiskTransfer)
		return;

	// record the last frame and real time that we took a save
	mLastDate = VDGetCurrentDate();
	mLastFrame = mParent.GetAntic().GetRawFrameCounter();

	// Make room for the save.
	//
	// - If we don't have the full number of near saves yet, drop the
	//   farthest save if necessary.
	//
	// - If we do have the full number of near saves, promote the oldest
	//   near save to a far save, and then start running the far counters
	//   to see which far save we should drop.

	if (mNumNearSaves >= mMaxNearSaves) {
		size_t idx = mMaxFarSaves;

		while(idx) {
			if ((mFarSaveCounters[idx - 1] += mFarSaveNumerator) >= mFarSaveDenominator) {
				mFarSaveCounters[idx - 1] = 0;

				break;
			}

			--idx;
		}

		// adjust for far slots we may not have
		size_t numFarSaves = mAutoSaveQueue.size() - mMaxNearSaves;
		size_t missingFarSaves = mMaxFarSaves - numFarSaves;

		if (idx < missingFarSaves)
			idx = 0;
		else
			idx -= missingFarSaves;

		// if we didn't choose a slot to drop but are still over, then
		// force slot 0 to drop
		if (idx == 0 && mAutoSaveQueue.size() >= mMaxNearSaves + mMaxFarSaves)
			idx = 1;

		if (idx) {
			mAutoSaveQueue.erase(mAutoSaveQueue.begin() + (idx - 1));
		}

		// promote oldest near save to a far save
		if (mMaxFarSaves > 0)
			mAutoSaveQueue.end()[-(ptrdiff_t)mMaxNearSaves]->mbIsNearSave = false;
	} else {
		++mNumNearSaves;
	}

	vdrefptr save(new ATAutoSaveEntry(*this));
	save->mbIsNearSave = true;

	vdrefptr<IATSerializable> saveState;
	vdrefptr<IATSerializable> saveStateInfo;
	mParent.CreateSnapshot(~saveState, ~saveStateInfo);

	save->SetRealTimestamp(mLastDate);
	save->SetSimulatedTimeSeconds(atser_cast<ATSaveStateInfo *>(saveStateInfo)->mSimRunTimeSeconds);
	save->SetRealTimeSeconds(mParent.RealSecondsSinceColdReset());
	save->SetColdResetId(atser_cast<ATSaveStateInfo *>(saveStateInfo)->mColdStartId);

	save->SetSaveData(std::move(saveState), std::move(saveStateInfo));

	mAutoSaveQueue.emplace_back(std::move(save));
}

void ATAutoSaveManager::ApplyState(ATAutoSaveEntry& entry) {
	auto it = std::find_if(
		mAutoSaveQueue.begin(),
		mAutoSaveQueue.end(),
		[&entry](const vdrefptr<ATAutoSaveEntry>& ase) {
			return ase == &entry;
		}
	);

	auto itEnd = mAutoSaveQueue.end();

	if (it == itEnd)
		return;

	size_t numToDelete = (itEnd - it) - 1;

	while(numToDelete--) {
		if (mAutoSaveQueue.back()->mbIsNearSave) {
			VDASSERT(mNumNearSaves > 0);
			--mNumNearSaves;
		}

		mAutoSaveQueue.pop_back();
	}

	mParent.ApplySnapshot(entry.GetSaveState(), nullptr);

	// reset last saved counters so we don't immediately resave
	mLastDate = VDGetCurrentDate();
	mLastFrame = mParent.GetAntic().GetRawFrameCounter();
}

////////////////////////////////////////////////////////////////////////////////

vdrefptr<IATAutoSaveManager> ATCreateAutoSaveManager(ATSimulator& parent) {
	return vdmakerefptr(new ATAutoSaveManager(parent));
}
