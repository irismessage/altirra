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
class ATInputManager;

///////////////////////////////////////////////////////////////////////////

enum ATInputTrigger {
	kATInputTrigger_Button0		= 0x0000,
	kATInputTrigger_Up			= 0x0100,
	kATInputTrigger_Down		= 0x0101,
	kATInputTrigger_Left		= 0x0102,
	kATInputTrigger_Right		= 0x0103,
	kATInputTrigger_Start		= 0x0200,
	kATInputTrigger_Select		= 0x0201,
	kATInputTrigger_Option		= 0x0202,
	kATInputTrigger_Turbo		= 0x0203,
	kATInputTrigger_ColdReset	= 0x0204,
	kATInputTrigger_WarmReset	= 0x0205,
	kATInputTrigger_KeySpace	= 0x0300,
	kATInputTrigger_Axis0		= 0x0800
};

///////////////////////////////////////////////////////////////////////////

class ATPortController {
public:
	ATPortController();
	~ATPortController();

	void Init(ATGTIAEmulator *gtia, ATPokeyEmulator *pokey, int index);

	ATGTIAEmulator& GetGTIA() const { return *mpGTIA; }
	uint8 GetPortValue() const { return mPortValue; }

	int AllocatePortInput(bool port2);
	void FreePortInput(int index);

	void SetPortInput(int index, uint32 portBits);
	void ResetPotPositions();
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

	virtual void SetDigitalTrigger(uint32 trigger, bool state) {}
	virtual void ApplyImpulse(uint32 trigger, int ds) {}

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

	virtual void SetDigitalTrigger(uint32 trigger, bool state);
	virtual void ApplyImpulse(uint32 trigger, int ds);

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

	virtual void SetDigitalTrigger(uint32 trigger, bool state);
	virtual void ApplyImpulse(uint32 trigger, int ds);

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

	virtual void SetDigitalTrigger(uint32 trigger, bool state);

protected:
	void UpdatePortOutput();

	uint32 mPortBits;
};

///////////////////////////////////////////////////////////////////////////

class ATConsoleController : public ATPortInputController {
	ATConsoleController(const ATConsoleController&);
	ATConsoleController& operator=(const ATConsoleController&);
public:
	ATConsoleController(ATInputManager *im);
	~ATConsoleController();

	virtual void SetDigitalTrigger(uint32 trigger, bool state);

protected:
	ATInputManager *const mpParent;
};

///////////////////////////////////////////////////////////////////////////

#endif	// f_AT_INPUTCONTROLLER_H
