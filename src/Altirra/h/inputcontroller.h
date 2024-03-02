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

#ifndef f_AT_INPUTCONTROLLER_H
#define f_AT_INPUTCONTROLLER_H

#include <vd2/system/vdstl.h>
#include "scheduler.h"

class ATGTIAEmulator;
class ATPokeyEmulator;

///////////////////////////////////////////////////////////////////////////

class ATPortController {
public:
	ATPortController();
	~ATPortController();

	void Init(ATGTIAEmulator *gtia, ATPokeyEmulator *pokey, int index);

	uint8 GetPortValue() const { return mPortValue; }

	int AllocatePortInput(bool port2);
	void FreePortInput(int index);

	void SetPortInput(int index, uint32 portBits);
	void SetPotPosition(int offset, uint8 pos);

protected:
	uint8 mPortValue;
	bool mbTrigger1;
	bool mbTrigger2;

	ATGTIAEmulator *mpGTIA;
	ATPokeyEmulator *mpPokey;
	int mTriggerIndex;

	typedef vdfastvector<uint32> PortInputs;
	PortInputs mPortInputs;

	void UpdatePortValue();
};

///////////////////////////////////////////////////////////////////////////

class ATPortInputController {
	ATPortInputController(const ATPortInputController&);
	ATPortInputController& operator=(const ATPortInputController&);
public:
	ATPortInputController();
	~ATPortInputController();

	void Attach(ATPortController *pc, bool port2);
	void Detach();

protected:
	void SetPortOutput(uint32 portBits);
	void SetPotPosition(bool second, uint8 pos);

	virtual void OnAttach() {}
	virtual void OnDetach() {}

	ATPortController *mpPortController;
	int mPortInputIndex;
	bool mbPort2;
};

///////////////////////////////////////////////////////////////////////////

class ATMouseController : public ATPortInputController, public IATSchedulerCallback {
	ATMouseController(const ATMouseController&);
	ATMouseController& operator=(const ATMouseController&);
public:
	ATMouseController();
	~ATMouseController();

	void Init(ATScheduler *scheduler);

	void SetPosition(int x, int y);
	void AddDelta(int dx, int dy);

	void SetButtonState(int button, bool state);

protected:
	void OnScheduledEvent(uint32 id);
	void Update();

	void OnAttach();
	void OnDetach();

	int		mTargetX;
	int		mTargetY;
	int		mAccumX;
	int		mAccumY;
	bool	mbButtonState[2];

	ATEvent *mpUpdateEvent;
	ATScheduler *mpScheduler;
};

///////////////////////////////////////////////////////////////////////////

class ATPaddleController : public ATPortInputController {
	ATPaddleController(const ATPaddleController&);
	ATPaddleController& operator=(const ATPaddleController&);
public:
	ATPaddleController();
	~ATPaddleController();

	void SetHalf(bool second);

	void AddDelta(int delta);
	void SetTrigger(bool enable);

protected:
	bool mbSecond;
	uint32 mPortBits;
	int mRawPos;
};

///////////////////////////////////////////////////////////////////////////

class ATJoystickController : public ATPortInputController {
	ATJoystickController(const ATJoystickController&);
	ATJoystickController& operator=(const ATJoystickController&);
public:
	ATJoystickController();
	~ATJoystickController();

	void SetDirection(int dir, bool enable);
	void SetTrigger(bool enable);

protected:
	void UpdatePortOutput();

	uint32 mPortBits;
};

///////////////////////////////////////////////////////////////////////////

class ATKeyboardController {
public:
};

///////////////////////////////////////////////////////////////////////////

#endif	// f_AT_INPUTCONTROLLER_H
