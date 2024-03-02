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
class ATAnticEmulator;

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
	kATInputTrigger_5200_0		= 0x0400,
	kATInputTrigger_5200_1		= 0x0401,
	kATInputTrigger_5200_2		= 0x0402,
	kATInputTrigger_5200_3		= 0x0403,
	kATInputTrigger_5200_4		= 0x0404,
	kATInputTrigger_5200_5		= 0x0405,
	kATInputTrigger_5200_6		= 0x0406,
	kATInputTrigger_5200_7		= 0x0407,
	kATInputTrigger_5200_8		= 0x0408,
	kATInputTrigger_5200_9		= 0x0409,
	kATInputTrigger_5200_Star	= 0x040A,
	kATInputTrigger_5200_Pound	= 0x040B,
	kATInputTrigger_5200_Start	= 0x040C,
	kATInputTrigger_5200_Pause	= 0x040D,
	kATInputTrigger_5200_Reset	= 0x040E,
	kATInputTrigger_Axis0		= 0x0800,
	kATInputTrigger_Flag0		= 0x0900,
	kATInputTrigger_Mask		= 0xFFFF,

	// D2D: Button state as usual.
	// D2A: Absolute positioning.
	// A2D: Threshold.
	// A2A: Absolute positioning.
	kATInputTriggerMode_Default		= 0x00000000,

	kATInputTriggerMode_AutoFire	= 0x00010000,

	kATInputTriggerMode_Toggle		= 0x00020000,
	kATInputTriggerMode_ToggleAF	= 0x00030000,

	// D2D: N/A
	// D2A: Accumulate deltas.
	// A2D: N/A
	// A2A: Accumulate deltas.
	kATInputTriggerMode_Relative	= 0x00040000,

	// D2D: N/A
	// D2A: Position -> Value.
	// A2D: N/A
	// A2A: Position -> Value.
	kATInputTriggerMode_Absolute	= 0x00050000,

	// D2D: Starts on, invert state.
	// D2A: N/A
	// A2D: N/A
	// A2A: Axis reversed.
	kATInputTriggerMode_Inverted	= 0x00060000,

	kATInputTriggerMode_Mask		= 0x000F0000,
	kATInputTriggerSpeed_Mask		= 0x00F00000,
	kATInputTriggerSpeed_Shift		= 20
};

///////////////////////////////////////////////////////////////////////////

class ATLightPenPort {
public:
	ATLightPenPort();

	void Init(ATAnticEmulator *antic);

	void SetAdjust(int x, int y) {
		mAdjustX = x;
		mAdjustY = y;
	}

	int GetAdjustX() const { return mAdjustX; }
	int GetAdjustY() const { return mAdjustY; }

	void SetIgnorePort34(bool ignore);
	void SetColorClockPhase(bool odd);
	void SetPortTriggerState(int index, bool state);

protected:
	ATAnticEmulator *mpAntic;
	uint8 mTriggerState;
	uint8 mTriggerStateMask;
	int mAdjustX;
	int mAdjustY;
	bool mbOddPhase;
};

///////////////////////////////////////////////////////////////////////////

class ATPortController {
public:
	ATPortController();
	~ATPortController();

	void Init(ATGTIAEmulator *gtia, ATPokeyEmulator *pokey, ATLightPenPort *lightPen, int index);
	void SetMultiMask(uint8 mask);

	ATGTIAEmulator& GetGTIA() const { return *mpGTIA; }
	ATPokeyEmulator& GetPokey() const { return *mpPokey; }
	uint8 GetPortValue() const { return mPortValue; }

	int AllocatePortInput(bool port2, int multiIndex);
	void FreePortInput(int index);

	void SetPortInput(int index, uint32 portBits);
	void ResetPotPositions();
	void SetPotPosition(int offset, uint8 pos);

protected:
	uint8 mPortValue;
	bool mbTrigger1;
	bool mbTrigger2;
	uint32 mMultiMask;

	ATGTIAEmulator *mpGTIA;
	ATPokeyEmulator *mpPokey;
	ATLightPenPort *mpLightPen;
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

	void Attach(ATPortController *pc, bool port2, int multiIndex = -1);
	void Detach();

	virtual void Tick() {}

	virtual bool Select5200Controller(int index, bool potsEnabled) { return false; }
	virtual void SetDigitalTrigger(uint32 trigger, bool state) {}

	/// Apply a delta to an analog target. The delta is in 16:16 fixed point, where
	/// +/-2 is full range (since the value range is [-1, 1]).
	virtual void ApplyImpulse(uint32 trigger, int ds) {}

	/// Set the position of an analog target. The position is in 16:16 fixed point,
	/// where the range is [-1, 1].
	virtual void ApplyAnalogInput(uint32 trigger, int ds) {}

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
	ATMouseController(bool amigaMode);
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

	uint32	mPortBits;
	uint32	mTargetX;
	uint32	mTargetY;
	uint32	mAccumX;
	uint32	mAccumY;
	bool	mbButtonState[2];
	bool	mbAmigaMode;

	ATEvent *mpUpdateEvent;
	ATScheduler *mpScheduler;
};

///////////////////////////////////////////////////////////////////////////

class ATTrackballController : public ATPortInputController, public IATSchedulerCallback {
	ATTrackballController(const ATTrackballController&);
	ATTrackballController& operator=(const ATTrackballController&);
public:
	ATTrackballController();
	~ATTrackballController();

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

	uint32	mPortBits;
	uint32	mTargetX;
	uint32	mTargetY;
	uint32	mAccumX;
	uint32	mAccumY;
	bool	mbButtonState;

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
	virtual void ApplyAnalogInput(uint32 trigger, int ds);
	virtual void ApplyImpulse(uint32 trigger, int ds);

	virtual void Tick();
protected:
	bool mbSecond;
	uint32 mPortBits;
	int mRawPos;
	int mRotIndex;
	int mRotX[4];
	int mRotY[4];
	float mRotXLast;
	float mRotYLast;
};

///////////////////////////////////////////////////////////////////////////

class ATTabletController : public ATPortInputController {
	ATTabletController(const ATTabletController&);
	ATTabletController& operator=(const ATTabletController&);
public:
	ATTabletController(int styDownPos, bool invertY);
	~ATTabletController();

	virtual void SetDigitalTrigger(uint32 trigger, bool state);
	virtual void ApplyAnalogInput(uint32 trigger, int ds);
	virtual void ApplyImpulse(uint32 trigger, int ds);

protected:
	void AddDelta(int axis, int delta);
	void SetPos(int axis, int pos);

	uint32 mPortBits;
	int mRawPos[2];
	bool	mbInvertY;
	bool	mbStylusDown;
	int		mStylusDownPos;
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

class ATInputStateController : public ATPortInputController {
	ATInputStateController(const ATInputStateController&);
	ATInputStateController& operator=(const ATInputStateController&);
public:
	ATInputStateController(ATInputManager *im, uint32 flagBase);
	~ATInputStateController();

	virtual void SetDigitalTrigger(uint32 trigger, bool state);

protected:
	ATInputManager *const mpParent;
	const uint32 mFlagBase;
};

///////////////////////////////////////////////////////////////////////////

class AT5200ControllerController : public ATPortInputController {
	AT5200ControllerController(const AT5200ControllerController&);
	AT5200ControllerController& operator=(const AT5200ControllerController&);
public:
	AT5200ControllerController(int index, bool trackball);
	~AT5200ControllerController();

	virtual bool Select5200Controller(int index, bool potsEnabled);
	virtual void SetDigitalTrigger(uint32 trigger, bool state);
	virtual void ApplyImpulse(uint32 trigger, int ds);
	virtual void ApplyAnalogInput(uint32 trigger, int ds);
	virtual void Tick();

protected:
	virtual void OnAttach();
	virtual void OnDetach();
	void SetKeyState(uint8 index, bool state);
	void UpdateTopButtonState();
	void SetPot(int index, int pos);
	void UpdatePot(int index);

	bool mbActive;
	bool mbPotsEnabled;
	bool mbTrackball;
	int mIndex;
	int mPot[2];
	int mJitter[2];

	bool mbUp;
	bool mbDown;
	bool mbLeft;
	bool mbRight;
	bool mbTopButton;

	bool mbKeyMatrix[64];
};

///////////////////////////////////////////////////////////////////////////

class ATLightPenController : public ATPortInputController, public IATSchedulerCallback {
	ATLightPenController(const ATLightPenController&);
	ATLightPenController& operator=(const ATLightPenController&);
public:
	ATLightPenController();
	~ATLightPenController();

	void Init(ATScheduler *fastScheduler, ATLightPenPort *lpp);

	virtual void SetDigitalTrigger(uint32 trigger, bool state);
	virtual void ApplyImpulse(uint32 trigger, int ds);
	virtual void ApplyAnalogInput(uint32 trigger, int ds);
	virtual void Tick();

protected:
	virtual void OnScheduledEvent(uint32 id);
	virtual void OnAttach();
	virtual void OnDetach();

	uint32	mPortBits;
	sint32	mPosX;
	sint32	mPosY;
	bool	mbPenDown;
	bool	mbOddPhase;

	ATEvent *mpLPEvent;
	ATScheduler *mpScheduler;
	ATLightPenPort *mpLightPen;
};

///////////////////////////////////////////////////////////////////////////

class ATKeypadController : public ATPortInputController {
	ATKeypadController(const ATKeypadController&);
	ATKeypadController& operator=(const ATKeypadController&);
public:
	ATKeypadController();
	~ATKeypadController();

	virtual void SetDigitalTrigger(uint32 trigger, bool state);

protected:
	virtual void OnAttach();
	virtual void OnDetach();

	uint32	mPortBits;
};

///////////////////////////////////////////////////////////////////////////

#endif	// f_AT_INPUTCONTROLLER_H
