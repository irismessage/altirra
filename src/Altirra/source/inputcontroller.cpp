//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009 Avery Lee
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
#include "inputcontroller.h"
#include "inputmanager.h"
#include "scheduler.h"
#include "gtia.h"
#include "pokey.h"

///////////////////////////////////////////////////////////////////////////

ATPortController::ATPortController()
	: mPortValue(0xFF)
	, mbTrigger1(false)
	, mbTrigger2(false)
{
}

ATPortController::~ATPortController() {
}

void ATPortController::Init(ATGTIAEmulator *gtia, ATPokeyEmulator *pokey, int index) {
	mpGTIA = gtia;
	mpPokey = pokey;
	mTriggerIndex = index;
}

int ATPortController::AllocatePortInput(bool port2) {
	PortInputs::iterator it(std::find(mPortInputs.begin(), mPortInputs.end(), 0));
	int index = it - mPortInputs.begin();

	uint32 v = port2 ? 0xC0000000 : 0x80000000;
	if (it != mPortInputs.end())
		*it = v;
	else
		mPortInputs.push_back(v);

	return index;
}

void ATPortController::FreePortInput(int index) {
	if ((uint32)index >= mPortInputs.size()) {
		VDASSERT(false);
		return;
	}

	uint8 oldVal = (uint8)mPortInputs[index];
	if (oldVal) {
		mPortInputs[index] = 0;
		UpdatePortValue();
	}
}

void ATPortController::SetPortInput(int index, uint32 portBits) {
	uint32 oldVal = mPortInputs[index];
	if (oldVal != portBits) {
		mPortInputs[index] = (oldVal & 0xf0000000) + portBits;
		UpdatePortValue();
	}
}

void ATPortController::ResetPotPositions() {
	mpPokey->SetPotPos(mTriggerIndex * 2 + 0, 228);
	mpPokey->SetPotPos(mTriggerIndex * 2 + 1, 228);
}

void ATPortController::SetPotPosition(int offset, uint8 pos) {
	mpPokey->SetPotPos(mTriggerIndex * 2 + offset, pos);
}

void ATPortController::UpdatePortValue() {
	PortInputs::const_iterator it(mPortInputs.begin()), itEnd(mPortInputs.end());
	
	uint32 portval = 0;
	while(it != itEnd) {
		uint32 pv = *it++;

		if (pv & 0x40000000)
			pv <<= 4;

		portval |= pv;
	}

	mPortValue = ~(uint8)portval;

	bool trigger1 = (portval & 0x100) != 0;
	bool trigger2 = (portval & 0x1000) != 0;

	if (mbTrigger1 != trigger1) {
		mbTrigger1 = trigger1;

		mpGTIA->SetControllerTrigger(mTriggerIndex + 0, trigger1);
	}

	if (mbTrigger2 != trigger2) {
		mbTrigger2 = trigger2;

		mpGTIA->SetControllerTrigger(mTriggerIndex + 1, trigger2);
	}
}

///////////////////////////////////////////////////////////////////////////

ATPortInputController::ATPortInputController()
	: mpPortController(NULL)
{
}

ATPortInputController::~ATPortInputController() {
	Detach();
}

void ATPortInputController::Attach(ATPortController *pc, bool port2) {
	mpPortController = pc;
	mPortInputIndex = pc->AllocatePortInput(port2);
	mbPort2 = port2;

	OnAttach();
}

void ATPortInputController::Detach() {
	if (mpPortController) {
		OnDetach();

		mpPortController->FreePortInput(mPortInputIndex);
		mpPortController = NULL;
	}
}

void ATPortInputController::SetPortOutput(uint32 portBits) {
	if (mpPortController)
		mpPortController->SetPortInput(mPortInputIndex, portBits);
}

void ATPortInputController::SetPotPosition(bool second, uint8 pos) {
	if (mpPortController)
		mpPortController->SetPotPosition((mbPort2 ? 2 : 0) + (second ? 1 : 0), pos);
}

///////////////////////////////////////////////////////////////////////////

ATMouseController::ATMouseController()
	: mTargetX(0)
	, mTargetY(0)
	, mAccumX(0)
	, mAccumY(0)
	, mpUpdateEvent(NULL)
	, mpScheduler(NULL)
{
	mbButtonState[0] = false;
	mbButtonState[1] = false;
}

ATMouseController::~ATMouseController() {
}

void ATMouseController::Init(ATScheduler *scheduler) {
	mpScheduler = scheduler;
}

void ATMouseController::SetPosition(int x, int y) {
	mTargetX = x;
	mTargetY = y;
}

void ATMouseController::AddDelta(int dx, int dy) {
	mTargetX += dx;
	mTargetY += dy;
}

void ATMouseController::SetButtonState(int button, bool state) {
	mbButtonState[button] = state;
}

void ATMouseController::SetDigitalTrigger(uint32 trigger, bool state) {
	switch(trigger) {
		case kATInputTrigger_Button0:
			mbButtonState[0] = state;
			break;
		case kATInputTrigger_Button0 + 1:
			mbButtonState[1] = state;
			break;
	}
}

void ATMouseController::ApplyImpulse(uint32 trigger, int ds) {
	switch(trigger) {
		case kATInputTrigger_Axis0:
			mTargetX += ds;
			break;
		case kATInputTrigger_Axis0+1:
			mTargetY += ds;
			break;
	}
}

void ATMouseController::OnScheduledEvent(uint32 id) {
	mpUpdateEvent = mpScheduler->AddEvent(32, this, 1);
	Update();
}

void ATMouseController::Update() {
	bool changed = false;

	const int posX = mTargetX >> 3;
	if (mAccumX != posX) {
		if (mAccumX < posX)
			++mAccumX;
		else
			--mAccumX;

		changed = true;
	}

	const int posY = mTargetY >> 3;
	if (mAccumY != posY) {
		if (mAccumY < posY)
			++mAccumY;
		else
			--mAccumY;

		changed = true;
	}

	if (changed) {
		static const uint8 kTabX[4] = { 0x00, 0x02, 0x03, 0x01 };
		static const uint8 kTabY[4] = { 0x00, 0x08, 0x0c, 0x04 };

		uint32 val = kTabX[mAccumX & 3] + kTabY[mAccumY & 3];

		if (mbButtonState[0])
			val |= 0x100;

		SetPortOutput(val);
	}
}

void ATMouseController::OnAttach() {
	if (!mpUpdateEvent)
		mpUpdateEvent = mpScheduler->AddEvent(8, this, 1);
}

void ATMouseController::OnDetach() {
	if (mpUpdateEvent) {
		mpScheduler->RemoveEvent(mpUpdateEvent);
		mpUpdateEvent = NULL;
		mpScheduler = NULL;
	}
}

///////////////////////////////////////////////////////////////////////////

ATPaddleController::ATPaddleController()
	: mbSecond(false)
	, mPortBits(0)
	, mRawPos(0)
{
}

ATPaddleController::~ATPaddleController() {
}

void ATPaddleController::SetHalf(bool second) {
	mbSecond = second;
}

void ATPaddleController::AddDelta(int delta) {
	int oldPos = mRawPos / 6;

	mRawPos -= delta;

	if (mRawPos < 0)
		mRawPos = 0;

	if (mRawPos > (228 * 6))
		mRawPos = (228 * 6);

	int newPos = mRawPos / 6;

	if (newPos != oldPos)
		SetPotPosition(mbSecond, newPos);
}

void ATPaddleController::SetTrigger(bool enable) {
	const uint32 newbits = (enable ? mbSecond ? 0x08 : 0x04 : 0x00);

	if (mPortBits != newbits) {
		mPortBits = newbits;
		SetPortOutput(newbits);
	}
}

void ATPaddleController::SetDigitalTrigger(uint32 trigger, bool state) {
	if (trigger == kATInputTrigger_Button0)
		SetTrigger(state);
}

void ATPaddleController::ApplyImpulse(uint32 trigger, int ds) {
	if (trigger == kATInputTrigger_Axis0)
		AddDelta(ds);
}

///////////////////////////////////////////////////////////////////////////

ATJoystickController::ATJoystickController()
	: mPortBits(0)
{
}

ATJoystickController::~ATJoystickController() {
}

void ATJoystickController::SetDigitalTrigger(uint32 trigger, bool state) {
	uint32 mask = 0;

	switch(trigger) {
		case kATInputTrigger_Button0:
			mask = 0x100;
			break;

		case kATInputTrigger_Up:
			mask = 0x01;
			break;

		case kATInputTrigger_Down:
			mask = 0x02;
			break;

		case kATInputTrigger_Left:
			mask = 0x04;
			break;

		case kATInputTrigger_Right:
			mask = 0x08;
			break;

		default:
			return;
	}

	uint32 bit = state ? mask : 0;

	if ((mPortBits ^ bit) & mask) {
		mPortBits ^= mask;

		UpdatePortOutput();
	}
}

void ATJoystickController::UpdatePortOutput() {
	uint32 v = mPortBits;

	if ((v & 0x0c) == 0x0c)
		v &= ~0x0c;

	if ((v & 0x03) == 0x03)
		v &= ~0x03;

	SetPortOutput(v);
}

///////////////////////////////////////////////////////////////////////////

ATConsoleController::ATConsoleController(ATInputManager *im)
	: mpParent(im)
{
}

ATConsoleController::~ATConsoleController() {
}

void ATConsoleController::SetDigitalTrigger(uint32 trigger, bool state) {
	IATInputConsoleCallback *cb = mpParent->GetConsoleCallback();

	if (cb)
		cb->SetConsoleTrigger(trigger, state);
}
