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

#ifndef f_PORTMANAGER_H
#define f_PORTMANAGER_H

#include <at/atcore/deviceport.h>

class ATPIAEmulator;
class ATGTIAEmulator;
class ATPokeyEmulator;
class ATLightPenPort;

class ATPortManager final : public IATDevicePortManager {
public:
	void Init(ATPIAEmulator& pia, ATGTIAEmulator& gtia, ATPokeyEmulator& pokey, ATAnticEmulator& antic, ATLightPenPort& lightPen);
	void Shutdown();

	void SetPorts34Enabled(bool enable);
	void SetTriggerOverride(unsigned index, bool down);
	void SetPotOverride(unsigned index, bool pulledUp);
	void ReapplyTriggers();

	void AllocControllerPort(int controllerIndex, IATDeviceControllerPort **port) override;

	int AllocInput() override;
	void FreeInput(int index) override;
	void SetInput(int index, uint32 rval) override;
	uint32 GetOutputState() const override;
	int AllocOutput(ATPortOutputFn fn, void *ptr, uint32 changeMask) override;
	void ModifyOutputMask(int index, uint32 changeMask) override;
	void FreeOutput(int index) override;

private:
	class ControllerPort;

	void FreeController(ControllerPort& cport);
	void AdjustTriggerDown(int index, bool down);
	void AdjustPotPos(unsigned potIndex, sint32& potInputIndex, uint32 hiPos);
	void UnsetPotPos(unsigned potIndex, sint32& potInputIndex);
	void UpdatePot(unsigned potIndex);
	uint32 GetCyclesToBeamPosition(int xcyc, int y) const;

	ATPIAEmulator *mpPIA = nullptr;
	ATGTIAEmulator *mpGTIA = nullptr;
	ATPokeyEmulator *mpPOKEY = nullptr;
	ATAnticEmulator *mpANTIC = nullptr;
	ATLightPenPort *mpLightPen = nullptr;
	bool mbPorts34Enabled = false;

	vdfastvector<ControllerPort *> mControllerPorts;
	uint32 mTriggerDownCounts[4] {};

	bool mbTriggerOverride[4] {};

	static constexpr uint32 kPotHiPosMinUngrounded = 1;
	static constexpr uint32 kPotHiPosMax = 228 << 16;
	static constexpr uint32 kPotGrounded = UINT32_C(0x7FFFFFFF);

	vdfastvector<uint32> mPotHiPosFreeIndices[8];
	vdfastvector<uint32> mPotHiPosArrays[8];
	uint32 mPotHiPos[8] {};
	uint32 mPotBasePos[8] {};
};

#endif

