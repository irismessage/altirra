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

#ifndef f_AT_INPUTMANAGER_H
#define f_AT_INPUTMANAGER_H

class ATPortController;
class ATJoystickController;
class ATMouseController;
class ATPaddleController;
class IATJoystickManager;
class ATScheduler;

enum ATInputMode {
	kATInputMode_None,
	kATInputMode_JoyKeys,
	kATInputMode_Mouse,
	kATInputMode_Paddle
};

class ATInputManager {
public:
	ATInputManager();
	~ATInputManager();

	void Init(IATJoystickManager *joysticks, ATScheduler *scheduler, ATPortController *porta, ATPortController *portb);
	void Shutdown();

	ATInputMode GetPortMode(int port) const { return mInputModes[port]; }
	void SetPortMode(int port, ATInputMode inputMode);

	void SwapPorts(int p1, int p2);
	void Poll();

	bool IsJoystickActive() const { return mInputModes[0] == kATInputMode_JoyKeys || mInputModes[1] == kATInputMode_JoyKeys; }
	void SetJoystickInput(int input, bool val);

	bool IsMouseActive() const { return mInputModes[0] == kATInputMode_Mouse || mInputModes[1] == kATInputMode_Mouse; }
	void AddMouseDelta(int dx, int dy);
	void SetMouseButton(int input, bool val);

	bool IsPaddleActive() const { return mInputModes[0] == kATInputMode_Paddle || mInputModes[1] == kATInputMode_Paddle; }
	void AddPaddleDelta(int dx);
	void SetPaddleButton(bool val);

protected:
	void ReconfigurePorts();

	ATPortController *mpPorts[2];
	ATInputMode mInputModes[2];

	IATJoystickManager *mpJoyMgr;
	ATJoystickController *mpJoy;
	ATJoystickController *mpJoy1;
	ATJoystickController *mpJoy2;
	ATPaddleController *mpPaddle;
	ATMouseController *mpMouse;
};

#endif	// f_AT_INPUTMANAGER_H
