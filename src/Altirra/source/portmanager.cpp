//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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
#include <at/ataudio/pokey.h>
#include "antic.h"
#include "portmanager.h"
#include "gtia.h"
#include "pia.h"
#include "inputcontroller.h"

class ATPortManager::ControllerPort final : public IATDeviceControllerPort {
public:
	ControllerPort(ATPortManager& parent, int index, bool enabled) : mpParent(&parent), mPortIndex(index), mbSystemEnabled(enabled), mbEnabled(enabled) {}

	void AddRef() override;
	void Release() override;

	void Shutdown();

	void SetSystemEnabled(bool enable12, bool enable34);
	
	void SetEnabled(bool enabled) override;

	void SetDirOutputMask(uint8 mask) override;
	void SetDirInput(uint8 mask) override;
	void SetOnDirOutputChanged(uint8 mask, vdfunction<void()> handler, bool callNow) override;
	uint8 GetCurrentDirOutput() const override;

	void SetTriggerDown(bool down) override;

	void ResetPotPosition(bool potB) override;
	void SetPotPosition(bool potB, sint32 pos) override;
	void SetPotHiresPosition(bool potB, sint32 hiPos, bool grounded) override;
	uint32 GetCyclesToBeamPosition(int xcyc, int y) const override;

private:
	void AttachToPIA();
	void DetachFromPIA();
	void UpdatePIAInput();
	void OnPIAOutputChanged(uint32 outputState);

	void AttachTrigger();
	void DetachTrigger();

	void AttachPots();
	void DetachPots();

	void UpdateEnabled();

	ATPortManager *mpParent = nullptr;
	vdfunction<void()> mpOnOutputChanged;
	uint32 mRefCount = 1;
	int mPortIndex = 0;
	int mPIAInput = -1;
	int mPIAOutput = -1;
	uint8 mDirOutputMask = 0;
	uint8 mDirOutput = 15;
	uint8 mDirInput = 15;
	bool mbSystemEnabled = false;
	bool mbLocalEnabled = true;
	bool mbEnabled = false;
	bool mbTriggerDown = false;

	sint32 mPotInputIndex[2] { -1, -1 };
	uint32 mPotHiPos[2] { kPotHiPosMax, kPotHiPosMax };
};

void ATPortManager::ControllerPort::AddRef() {
	++mRefCount;
}

void ATPortManager::ControllerPort::Release() {
	if (!--mRefCount) {
		mpOnOutputChanged = nullptr;

		if (mpParent) {
			if (mbEnabled) {
				DetachFromPIA();
				DetachPots();
				DetachTrigger();
			}

			mpParent->FreeController(*this);
		}

		delete this;
	}
}

void ATPortManager::ControllerPort::Shutdown() {
	DetachFromPIA();
	mpParent = nullptr;
}

void ATPortManager::ControllerPort::SetSystemEnabled(bool enable12, bool enable34) {
	const bool enable = mPortIndex < 2 ? enable12 : enable34;

	if (mbSystemEnabled != enable) {
		mbSystemEnabled = enable;

		UpdateEnabled();
	}
}

void ATPortManager::ControllerPort::SetEnabled(bool enabled) {
	if (mbLocalEnabled != enabled) {
		mbLocalEnabled = enabled;

		UpdateEnabled();
	}
}

void ATPortManager::ControllerPort::SetDirOutputMask(uint8 mask) {
	mask &= 15;

	if (mDirOutputMask != mask) {
		const uint8 enablingMask = mask & ~mDirOutputMask;
		mDirOutputMask = mask;

		if (mbEnabled) {
			if (mPIAOutput < 0)
				AttachToPIA();
			else if (mpParent) {
				if (mask) {
					mpParent->mpPIA->ModifyOutputMask(mPIAOutput, (uint32)mask << (mPortIndex * 4));
				} else {
					mpParent->mpPIA->FreeOutput(mPIAOutput);
					mPIAOutput = -1;
				}
			}

			// if we are turning on mask bits, make sure those bits are up to date in our
			// cache, and dispatch notifications as needed
			if (enablingMask && mpParent)
				OnPIAOutputChanged(mpParent->mpPIA->GetOutputState());
		}
	}
}

void ATPortManager::ControllerPort::SetDirInput(uint8 mask) {
	mask &= 15;

	if (mDirInput != mask) {
		mDirInput = mask;

		if (mbEnabled) {
			if (mPIAInput < 0)
				AttachToPIA();
			else
				UpdatePIAInput();
		}
	}
}

void ATPortManager::ControllerPort::SetOnDirOutputChanged(uint8 mask, vdfunction<void()> handler, bool callNow) {
	mpOnOutputChanged = nullptr;
	SetDirOutputMask(mask);

	mpOnOutputChanged = std::move(handler);

	if (callNow && mpOnOutputChanged)
		mpOnOutputChanged();
}

uint8 ATPortManager::ControllerPort::GetCurrentDirOutput() const {
	return !mbEnabled ? 15 : mpParent ? (mpParent->GetOutputState() >> (mPortIndex * 4)) & 15 : 0;
}

void ATPortManager::ControllerPort::SetTriggerDown(bool down) {
	if (mbTriggerDown != down) {
		mbTriggerDown = down;

		if (mpParent && mbEnabled)
			mpParent->AdjustTriggerDown(mPortIndex, down);
	}
}

void ATPortManager::ControllerPort::ResetPotPosition(bool potB) {
	SetPotPosition(potB, 228);
}

void ATPortManager::ControllerPort::SetPotPosition(bool potB, sint32 pos) {
	SetPotHiresPosition(potB, pos << 16, false);
}

void ATPortManager::ControllerPort::SetPotHiresPosition(bool potB, sint32 hiPos, bool grounded) {
	const int potOffset = potB ? 1 : 0;

	hiPos = grounded ? kPotGrounded : std::clamp(hiPos, 1, 228 << 16);

	if (mPotHiPos[potOffset] != hiPos) {
		mPotHiPos[potOffset] = hiPos;

		if (mpParent && mbEnabled)
			mpParent->AdjustPotPos(mPortIndex * 2 + potOffset, mPotInputIndex[potOffset], hiPos);
	}
}

uint32 ATPortManager::ControllerPort::GetCyclesToBeamPosition(int xcyc, int y) const {
	if (!mpParent)
		return 1;

	return mpParent->GetCyclesToBeamPosition(xcyc, y);
}

void ATPortManager::ControllerPort::AttachToPIA() {
	if (!mpParent)
		return;

	if (mPIAInput < 0 && mDirInput != 15) {
		mPIAInput = mpParent->mpPIA->AllocInput();
		UpdatePIAInput();
	}

	if (mPIAOutput < 0 && mDirOutputMask) {
		mPIAOutput = mpParent->mpPIA->AllocOutput(
			[](void *self, uint32 outputState) {
				((ControllerPort *)self)->OnPIAOutputChanged(outputState);
			},
			this,
			(uint32)mDirOutputMask << (mPortIndex * 4));
	}
}

void ATPortManager::ControllerPort::DetachFromPIA() {
	if (mPIAInput >= 0) {
		mpParent->mpPIA->FreeInput(mPIAInput);
		mPIAInput = -1;
	}

	if (mPIAOutput >= 0) {
		mpParent->mpPIA->FreeOutput(mPIAOutput);
		mPIAOutput = -1;
	}
}

void ATPortManager::ControllerPort::UpdatePIAInput() {
	if (mpParent)
		mpParent->mpPIA->SetInput(mPIAInput, ~((uint32)(~mDirInput & 15) << (mPortIndex * 4)));
}

void ATPortManager::ControllerPort::OnPIAOutputChanged(uint32 outputState) {
	const uint8 newOutput = (outputState >> (mPortIndex * 4)) & 15;
	const uint8 delta = mDirOutput ^ newOutput;

	if (delta) {
		mDirOutput = newOutput;

		if ((delta & mDirOutputMask) && mpOnOutputChanged)
			mpOnOutputChanged();
	}
}

void ATPortManager::ControllerPort::AttachTrigger() {
	if (mpParent && mbTriggerDown)
		mpParent->AdjustTriggerDown(mPortIndex, true);
}

void ATPortManager::ControllerPort::DetachTrigger() {
	if (mpParent && mbTriggerDown)
		mpParent->AdjustTriggerDown(mPortIndex, false);
}

void ATPortManager::ControllerPort::AttachPots() {
	if (mpParent) {
		mpParent->AdjustPotPos(mPortIndex * 2 + 0, mPotInputIndex[0], mPotHiPos[0]);
		mpParent->AdjustPotPos(mPortIndex * 2 + 1, mPotInputIndex[1], mPotHiPos[1]);
	}
}

void ATPortManager::ControllerPort::DetachPots() {
	if (mpParent) {
		mpParent->UnsetPotPos(mPortIndex * 2 + 0, mPotInputIndex[0]);
		mpParent->UnsetPotPos(mPortIndex * 2 + 1, mPotInputIndex[1]);
	}
}

void ATPortManager::ControllerPort::UpdateEnabled() {
	bool enable = mbLocalEnabled && mbSystemEnabled;

	if (mbEnabled != enable) {
		mbEnabled = enable;

		if (enable) {
			AttachToPIA();
			AttachPots();
			AttachTrigger();
		} else {
			DetachFromPIA();
			DetachPots();
			DetachTrigger();
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

void ATPortManager::Init(ATPIAEmulator& pia, ATGTIAEmulator& gtia, ATPokeyEmulator& pokey, ATAnticEmulator& antic, ATLightPenPort& lightPen) {
	mpPIA = &pia;
	mpGTIA = &gtia;
	mpPOKEY = &pokey;
	mpLightPen = &lightPen;
	mpANTIC = &antic;

	for(int i=0; i<8; ++i) {
		mPotBasePos[i] = kPotHiPosMax;
		mPotHiPos[i] = kPotHiPosMax;
		mpPOKEY->SetPotPos(i, 228);
	}
}

void ATPortManager::Shutdown() {
	for(ControllerPort *port : mControllerPorts)
		port->Shutdown();

	mControllerPorts.clear();
}

void ATPortManager::SetPorts34Enabled(bool enable) {
	if (mbPorts34Enabled != enable) {
		mbPorts34Enabled = enable;

		for(ControllerPort *port : mControllerPorts)
			port->SetSystemEnabled(true, mbPorts34Enabled);
	}
}

void ATPortManager::SetTriggerOverride(unsigned index, bool down) {
	if (mbTriggerOverride[index] != down) {
		mbTriggerOverride[index] = down;

		if (down)
			mpGTIA->SetControllerTrigger(index, true);
		else
			mpGTIA->SetControllerTrigger(index, mTriggerDownCounts[index] && (index < 2 || mbPorts34Enabled));
	}
}

void ATPortManager::SetPotOverride(unsigned index, bool pulledUp) {
	const uint32 pos = pulledUp ? 0 : kPotHiPosMax;

	if (mPotBasePos[index] != pos) {
		mPotBasePos[index] = pos;

		UpdatePot(index);
	}
}

void ATPortManager::ReapplyTriggers() {
	const bool states[4] {
		mTriggerDownCounts[0] != 0 || mbTriggerOverride[0],
		mTriggerDownCounts[1] != 0 || mbTriggerOverride[1],
		(mbPorts34Enabled && mTriggerDownCounts[2] != 0) || mbTriggerOverride[2],
		(mbPorts34Enabled && mTriggerDownCounts[3] != 0) || mbTriggerOverride[3]
	};

	for(int i=0; i<4; ++i) {
		mpGTIA->SetControllerTrigger(i, states[i]);
		mpLightPen->SetPortTriggerState(i, states[i]);
	}
}

void ATPortManager::AllocControllerPort(int controllerIndex, IATDeviceControllerPort **port) {
	vdrefptr<ControllerPort> newPort { new ControllerPort(*this, controllerIndex, controllerIndex < 2 || mbPorts34Enabled) };

	mControllerPorts.push_back(newPort);

	*port = newPort;
}

int ATPortManager::AllocInput() {
	return mpPIA->AllocInput();
}

void ATPortManager::FreeInput(int index) {
	mpPIA->FreeInput(index);
}

void ATPortManager::SetInput(int index, uint32 rval) {
	mpPIA->SetInput(index, rval);
}

uint32 ATPortManager::GetOutputState() const {
	return mpPIA->GetOutputState();
}

int ATPortManager::AllocOutput(ATPortOutputFn fn, void *ptr, uint32 changeMask) {
	return mpPIA->AllocOutput(fn, ptr, changeMask);
}

void ATPortManager::ModifyOutputMask(int index, uint32 changeMask) {
	mpPIA->ModifyOutputMask(index, changeMask);
}

void ATPortManager::FreeOutput(int index) {
	mpPIA->FreeOutput(index);
}

void ATPortManager::FreeController(ControllerPort& cport) {
	auto it = std::find(mControllerPorts.begin(), mControllerPorts.end(), &cport);
	if (it != mControllerPorts.end()) {
		*it = mControllerPorts.back();
		mControllerPorts.pop_back();
	} else {
		VDFAIL("Controller being freed not found in controller list.");
	}
}

void ATPortManager::AdjustTriggerDown(int index, bool down) {
	VDASSERT(index >= 0 && index < 4);
	if (down) {
		if (!mTriggerDownCounts[index]++) {
			if (index < 2 || mbPorts34Enabled) {
				mpGTIA->SetControllerTrigger(index, true);
				mpLightPen->SetPortTriggerState(index, true);
			}
		}
	} else {
		if (!--mTriggerDownCounts[index]) {
			if (index < 2 || mbPorts34Enabled) {
				mpGTIA->SetControllerTrigger(index, false);
				mpLightPen->SetPortTriggerState(index, false);
			}
		}
	}
}

void ATPortManager::AdjustPotPos(unsigned potIndex, sint32& potInputIndex, uint32 hiPos) {
	if (hiPos >= kPotHiPosMax) {
		if (potInputIndex >= 0) {
			UnsetPotPos(potIndex, potInputIndex);
		}
	} else {
		auto& potHiPositions = mPotHiPosArrays[potIndex];

		if (potInputIndex < 0) {
			auto& freeIndices = mPotHiPosFreeIndices[potIndex];
			if (freeIndices.empty()) {
				freeIndices.push_back((uint32)potHiPositions.size());
				potHiPositions.push_back(kPotHiPosMax);
			}

			potInputIndex = (sint32)freeIndices.back();
			freeIndices.pop_back();
		}

		bool needUpdate = false;

		if (hiPos < mPotHiPos[potIndex] || mPotHiPos[potIndex] == potHiPositions[potInputIndex])
			needUpdate = true;
		
		potHiPositions[potInputIndex] = hiPos;

		if (needUpdate)
			UpdatePot(potIndex);
	}
}

void ATPortManager::UnsetPotPos(unsigned potIndex, sint32& potInputIndex) {
	if (potInputIndex >= 0) {
		auto& freeIndices = mPotHiPosFreeIndices[potIndex];
		auto it = std::lower_bound(freeIndices.begin(), freeIndices.end(), (uint32)potInputIndex);
		freeIndices.insert(it, (uint32)potInputIndex);

		auto& potHiPositions = mPotHiPosArrays[potIndex];
		potHiPositions[potInputIndex] = kPotHiPosMax;

		while(!freeIndices.empty() && freeIndices.back() == potHiPositions.size()) {
			freeIndices.pop_back();
			potHiPositions.pop_back();
		}

		UpdatePot(potIndex);

		potInputIndex = -1;
	}
}

void ATPortManager::UpdatePot(unsigned potIndex) {
	uint32 lowestHiPos = mPotBasePos[potIndex];

	for(uint32 hiPos : mPotHiPosArrays[potIndex]) {
		if (lowestHiPos > hiPos)
			lowestHiPos = hiPos;
	}

	if (mPotHiPos[potIndex] != lowestHiPos) {
		mPotHiPos[potIndex] = lowestHiPos;

		mpPOKEY->SetPotPosHires(potIndex, (int)lowestHiPos, lowestHiPos >= kPotGrounded);
	}
}

uint32 ATPortManager::GetCyclesToBeamPosition(int xcyc, int y) const {
	// adjust relative to current position
	xcyc -= mpANTIC->GetBeamX();
	y -= mpANTIC->GetBeamY();

	// wrap x
	xcyc %= 114;
	if (xcyc < 0) {
		xcyc += 114;
		--y;
	}

	// wrap y
	int h = (int)mpANTIC->GetScanlineCount();
	y %= h;
	if (y < 0)
		y += h;

	uint32 cyc = (uint32)(xcyc + y * 114);

	if (!cyc)
		cyc = h * 114;

	return cyc;
}
