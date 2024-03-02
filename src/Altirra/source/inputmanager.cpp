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
#include "inputmanager.h"
#include "inputcontroller.h"
#include "joystick.h"

ATInputManager::ATInputManager()
	: mpJoy(new ATJoystickController)
	, mpJoy1(new ATJoystickController)
	, mpJoy2(new ATJoystickController)
	, mpMouse(new ATMouseController)
	, mpPaddle(new ATPaddleController)
{
	mInputModes[0] = kATInputMode_JoyKeys;
	mInputModes[1] = kATInputMode_None;
}

ATInputManager::~ATInputManager() {
}

void ATInputManager::Init(IATJoystickManager *joysticks, ATScheduler *sched, ATPortController *porta, ATPortController *portb) {
	mpJoyMgr = joysticks;
	mpPorts[0] = porta;
	mpPorts[1] = portb;
	mpMouse->Init(sched);

	ReconfigurePorts();
}

void ATInputManager::Shutdown() {
	delete mpPaddle;
	mpPaddle = NULL;

	delete mpMouse;
	mpMouse = NULL;

	delete mpJoy2;
	mpJoy2 = NULL;

	delete mpJoy1;
	mpJoy1 = NULL;

	delete mpJoy;
	mpJoy = NULL;
}

void ATInputManager::SetPortMode(int port, ATInputMode inputMode) {
	mInputModes[port] = inputMode;

	for(int i=0; i<2; ++i) {
		if (port != i && mInputModes[i] == inputMode)
			mInputModes[i] = kATInputMode_None;
	}

	ReconfigurePorts();
}

void ATInputManager::SwapPorts(int p1, int p2) {
	std::swap(mInputModes[0], mInputModes[1]);
	ReconfigurePorts();
}

void ATInputManager::Poll() {
	if (mpJoyMgr) {
		mpJoyMgr->Poll();
		uint32 data = mpJoyMgr->GetJoystickPortStates();

		mpJoy1->SetDirection(0, (data & 1) != 0);
		mpJoy1->SetDirection(1, (data & 2) != 0);
		mpJoy1->SetDirection(2, (data & 4) != 0);
		mpJoy1->SetDirection(3, (data & 8) != 0);
		mpJoy2->SetDirection(0, (data & 16) != 0);
		mpJoy2->SetDirection(1, (data & 32) != 0);
		mpJoy2->SetDirection(2, (data & 64) != 0);
		mpJoy2->SetDirection(3, (data & 128) != 0);
		mpJoy1->SetTrigger((data & 0x10000) != 0);
		mpJoy2->SetTrigger((data & 0x20000) != 0);
	}
}

void ATInputManager::SetJoystickInput(int input, bool val) {
	if (input == 4)
		mpJoy->SetTrigger(val);
	else
		mpJoy->SetDirection(input, val);
}

void ATInputManager::AddMouseDelta(int dx, int dy) {
	mpMouse->AddDelta(dx, dy);
}

void ATInputManager::SetMouseButton(int input, bool val) {
	mpMouse->SetButtonState(input, val);
}

void ATInputManager::AddPaddleDelta(int dx) {
	mpPaddle->AddDelta(dx);
}

void ATInputManager::SetPaddleButton(bool val) {
	mpPaddle->SetTrigger(val);
}

void ATInputManager::ReconfigurePorts() {
	mpJoy->Detach();
	mpJoy1->Detach();
	mpJoy2->Detach();
	mpMouse->Detach();

	mpJoy1->Attach(mpPorts[0], false);
	mpJoy2->Attach(mpPorts[0], true);

	switch(mInputModes[0]) {
		case kATInputMode_JoyKeys:
			mpJoy->Attach(mpPorts[0], false);
			break;

		case kATInputMode_Mouse:
			mpMouse->Attach(mpPorts[0], false);
			break;

		case kATInputMode_Paddle:
			mpPaddle->Attach(mpPorts[0], false);
			break;
	}

	switch(mInputModes[1]) {
		case kATInputMode_JoyKeys:
			mpJoy->Attach(mpPorts[0], true);
			break;

		case kATInputMode_Mouse:
			mpMouse->Attach(mpPorts[0], true);
			break;

		case kATInputMode_Paddle:
			mpPaddle->Attach(mpPorts[0], true);
			break;
	}
}
