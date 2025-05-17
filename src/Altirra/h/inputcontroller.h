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

#include <optional>
#include <vd2/system/vdstl.h>
#include <vd2/system/vectors.h>
#include <at/atcore/notifylist.h>
#include <at/atcore/scheduler.h>
#include "inputtypes.h"

class IATDevicePortManager;
class IATDeviceControllerPort;
class ATPokeyEmulator;
class ATInputManager;
class ATAnticEmulator;
class ATGTIAEmulator;
class ATGTIALightSensor;
class ATPIAEmulator;
class ATPortInputController;
struct ATInputPointerInfo;

///////////////////////////////////////////////////////////////////////////

enum ATInputTrigger : uint32 {
	kATInputTrigger_Button0		= 0x0000,
	kATInputTrigger_Up			= 0x0100,
	kATInputTrigger_Down		= 0x0101,
	kATInputTrigger_Left		= 0x0102,
	kATInputTrigger_Right		= 0x0103,
	kATInputTrigger_ScrollUp	= 0x0104,
	kATInputTrigger_ScrollDown	= 0x0105,
	kATInputTrigger_Start		= 0x0200,
	kATInputTrigger_Select		= 0x0201,
	kATInputTrigger_Option		= 0x0202,
	kATInputTrigger_Turbo		= 0x0203,
	kATInputTrigger_ColdReset	= 0x0204,
	kATInputTrigger_WarmReset	= 0x0205,
	kATInputTrigger_Rewind		= 0x0206,
	kATInputTrigger_RewindMenu	= 0x0207,
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
	kATInputTrigger_UILeft		= 0x0500,
	kATInputTrigger_UIRight		= 0x0501,
	kATInputTrigger_UIUp		= 0x0502,
	kATInputTrigger_UIDown		= 0x0503,
	kATInputTrigger_UIAccept	= 0x0504,	// PSx[X], Xbox[A]
	kATInputTrigger_UIReject	= 0x0505,	// PSx[O], Xbox[B]
	kATInputTrigger_UIMenu		= 0x0506,	// PSx[T], Xbox[Y]
	kATInputTrigger_UIOption	= 0x0507,	// PSx[S], Xbox[X]
	kATInputTrigger_UISwitchLeft	= 0x0508,
	kATInputTrigger_UISwitchRight	= 0x0509,
	kATInputTrigger_UILeftShift		= 0x050A,
	kATInputTrigger_UIRightShift	= 0x050B,
	kATInputTrigger_Axis0		= 0x0800,
	kATInputTrigger_Flag0		= 0x0900,
	kATInputTrigger_ClassMask	= 0xFF00,
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
	kATInputTriggerSpeed_Shift		= 20,
	kATInputTriggerAccel_Mask		= 0x0F000000,
	kATInputTriggerAccel_Shift		= 24
};

///////////////////////////////////////////////////////////////////////////

enum class ATLightPenNoiseMode : uint8 {
	None,
	Low,
	High
};

AT_DECLARE_ENUM_TABLE(ATLightPenNoiseMode);

class ATLightPenPort {
public:
	static const int kHorizOffset;

	ATLightPenPort();

	void Init(ATAnticEmulator& antic, ATGTIAEmulator& gtia);

	ATGTIAEmulator& GetGTIA() const { return *mpGTIA; }

	void SetAdjust(bool pen, vdint2 offset) {
		mAdjust[pen] = offset;
	}

	vdint2 GetAdjust(bool pen) const { return mAdjust[pen]; }

	bool GetImmediateUpdateEnabled() const;
	void SetImmediateUpdateEnabled(bool enable);

	ATLightPenNoiseMode GetNoiseMode() const;
	void SetNoiseMode(ATLightPenNoiseMode mode);

	void ApplyCorrection(int dx, int dy);
	void TriggerCorrection(bool phase, int x, int y);

	void SetIgnorePort34(bool ignore);
	void SetPosition(bool pen, int x, int y);
	void SetPortTriggerState(int index, bool state);

	ATNotifyList<const vdfunction<void(bool, int, int)> *>& OnTriggerCorrectionEvent() {
		return mTriggerCorrectionEvent;
	}

protected:
	void SetAnticPenPosition(int x, int y);

	ATAnticEmulator *mpAntic = nullptr;
	ATGTIAEmulator *mpGTIA = nullptr;
	uint8 mTriggerState = 0;
	uint8 mTriggerStateMask = 0x03;
	vdint2 mAdjust[2] {};
	bool mbLastWasPen = false;
	bool mbOddPhase = false;
	bool mbImmediateUpdate = false;
	int mPenBeamX = 0;
	int mPenBeamY = 0;
	ATLightPenNoiseMode mNoiseMode = ATLightPenNoiseMode::None;

	ATNotifyList<const vdfunction<void(bool, int, int)> *> mTriggerCorrectionEvent;
};

///////////////////////////////////////////////////////////////////////////

class ATPortInputController {
	ATPortInputController(const ATPortInputController&) = delete;
	ATPortInputController& operator=(const ATPortInputController&) = delete;
public:
	friend class ATPortController;

	ATPortInputController();
	virtual ~ATPortInputController();

	void Attach(IATDevicePortManager& portMgr, int portIndex, int multiIndex = -1);
	void Detach();
	virtual void ColdReset() {}

	virtual void Tick() {}

	virtual bool IsActive() const { return true; }

	void SelectMultiJoy(uint8 mask);
	virtual bool Select5200Controller(int index, bool potsEnabled) { return false; }
	virtual void SetDigitalTrigger(uint32 trigger, bool state) {}

	/// Apply a delta to an analog target. The delta is in 16:16 fixed point, where
	/// +/-2 is full range (since the value range is [-1, 1]).
	virtual void ApplyImpulse(uint32 trigger, int ds) {}

	/// Set the position of an analog target. The position is in 16:16 fixed point,
	/// where the range is [-1, 1].
	virtual void ApplyAnalogInput(uint32 trigger, int ds) {}

	virtual ATInputPointerCoordinateSpace GetPointerCoordinateSpace() const { return ATInputPointerCoordinateSpace::None; }
	virtual void GetPointers(vdfastvector<ATInputPointerInfo>& pointers) const {}

protected:
	void SetPortOutput(uint32 portBits);
	void ResetPotPositions();
	void SetPotPosition(bool second, uint8 pos);
	void SetPotHiPosition(bool second, int pos, bool grounded = false);
	void SetOutputMonitorMask(uint8 mask);
	void UpdateOutput();

	virtual void OnAttach() {}
	virtual void OnDetach() {}
	virtual void OnPortOutputChanged(uint8 outputState) {}

	vdrefptr<IATDeviceControllerPort> mpControllerPort;
	uint8 mPortOutputState {};

	uint8 mMultiMask {};
};

///////////////////////////////////////////////////////////////////////////

class ATMouseController final : public ATPortInputController, public IATSchedulerCallback {
	ATMouseController(const ATMouseController&) = delete;
	ATMouseController& operator=(const ATMouseController&) = delete;
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
	void EnableUpdate();

	void OnScheduledEvent(uint32 id);
	void Update(bool doX, bool doY);
	void UpdatePort();

	void OnAttach();
	void OnDetach();

	uint32	mPortBits;
	uint32	mTargetX;
	uint32	mTargetY;
	uint32	mAccumX;
	uint32	mAccumY;
	bool	mbButtonState[2];
	bool	mbAmigaMode;

	ATEvent *mpUpdateXEvent;
	ATEvent *mpUpdateYEvent;
	ATScheduler *mpScheduler;
};

///////////////////////////////////////////////////////////////////////////

class ATTrackballController final : public ATPortInputController, public IATSchedulerCallback {
	ATTrackballController(const ATTrackballController&) = delete;
	ATTrackballController& operator=(const ATTrackballController&) = delete;
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
	uint16	mAccumX;
	uint16	mAccumY;
	bool	mbButtonState;

	ATEvent *mpUpdateEvent;
	ATScheduler *mpScheduler;
};

///////////////////////////////////////////////////////////////////////////

class ATPaddleController final : public ATPortInputController {
	ATPaddleController(const ATPaddleController&) = delete;
	ATPaddleController& operator=(const ATPaddleController&) = delete;
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

	virtual void OnDetach();

protected:
	bool mbSecond = false;
	bool mbLeft = false;
	bool mbRight = false;
	uint32 mPortBits = 0;
	int mRawPos = (228 << 16) + 0x8000;
	int mRotIndex = 0;
	int mRotX[4] {};
	int mRotY[4] {};
	float mRotXLast = 0;
	float mRotYLast = 0;
};

///////////////////////////////////////////////////////////////////////////

class ATTabletController final : public ATPortInputController {
	ATTabletController(const ATTabletController&) = delete;
	ATTabletController& operator=(const ATTabletController&) = delete;
public:
	ATTabletController(int styUpPos, bool invertY);
	~ATTabletController();

	void SetDigitalTrigger(uint32 trigger, bool state) override;
	void ApplyAnalogInput(uint32 trigger, int ds) override;
	void ApplyImpulse(uint32 trigger, int ds) override;

protected:
	void AddDelta(int axis, int delta);
	void SetPos(int axis, int pos);

	uint32 mPortBits;
	int mRawPos[2];
	bool	mbInvertY;
	bool	mbStylusUp;
	int		mStylusUpPos;
};

///////////////////////////////////////////////////////////////////////////

class ATJoystickController final : public ATPortInputController {
	ATJoystickController(const ATJoystickController&) = delete;
	ATJoystickController& operator=(const ATJoystickController&) = delete;
public:
	ATJoystickController();
	~ATJoystickController();

	virtual void SetDigitalTrigger(uint32 trigger, bool state);

protected:
	void UpdatePortOutput();

	uint32 mPortBits;
};

///////////////////////////////////////////////////////////////////////////

class ATDrivingController final : public ATPortInputController {
	ATDrivingController(const ATDrivingController&) = delete;
	ATDrivingController& operator=(const ATDrivingController&) = delete;
public:
	ATDrivingController();
	~ATDrivingController();

	void SetDigitalTrigger(uint32 trigger, bool state) override;
	void ApplyImpulse(uint32 trigger, int ds) override;

protected:
	void AddDelta(int delta);

	uint32 mPortBits = 0;
	uint32 mRawPos = 0;
};

///////////////////////////////////////////////////////////////////////////

class ATConsoleController final : public ATPortInputController {
	ATConsoleController(const ATConsoleController&) = delete;
	ATConsoleController& operator=(const ATConsoleController&) = delete;
public:
	ATConsoleController(ATInputManager *im);
	~ATConsoleController();

	virtual void SetDigitalTrigger(uint32 trigger, bool state);

protected:
	ATInputManager *const mpParent;
};

///////////////////////////////////////////////////////////////////////////

class ATInputStateController final : public ATPortInputController {
	ATInputStateController(const ATInputStateController&) = delete;
	ATInputStateController& operator=(const ATInputStateController&) = delete;
public:
	ATInputStateController(ATInputManager *im, uint32 flagBase);
	~ATInputStateController();

	virtual void SetDigitalTrigger(uint32 trigger, bool state);

protected:
	ATInputManager *const mpParent;
	const uint32 mFlagBase;
};

///////////////////////////////////////////////////////////////////////////

class AT5200ControllerController final : public ATPortInputController {
	AT5200ControllerController(const AT5200ControllerController&) = delete;
	AT5200ControllerController& operator=(const AT5200ControllerController&) = delete;
public:
	AT5200ControllerController(int index, bool trackball, ATPokeyEmulator& pokey);
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
	void SetPot(int index, int pos, bool enableJitter);
	void UpdatePot(int index);

	ATPokeyEmulator *mpPokey = nullptr;
	bool mbActive;
	bool mbPotsEnabled;
	bool mbTrackball;
	int mIndex;
	int mPot[2];
	int mJitter[2];
	uint32 mJitterLFSR = 1;

	bool mbUp = false;
	bool mbDown = false;
	bool mbLeft = false;
	bool mbRight = false;
	bool mbTopButton = false;
	bool mbImpulseMode = false;
	float mImpulseAccum[2] = {};
	int mImpulseAccum2[2] = {};

	bool mbKeyMatrix[64];
};

///////////////////////////////////////////////////////////////////////////

class ATLightPenController final : public ATPortInputController, public IATSchedulerCallback {
	ATLightPenController(const ATLightPenController&) = delete;
	ATLightPenController& operator=(const ATLightPenController&) = delete;
public:
	enum class Type : uint8 {
		LightGun,
		LightPen,
		LightPenStack
	};

	ATLightPenController(Type type);
	~ATLightPenController();

	void Init(ATScheduler *fastScheduler, ATLightPenPort *lpp);

	bool IsActive() const override { return mbPenDown; }

	void SetDigitalTrigger(uint32 trigger, bool state) override;
	void ApplyImpulse(uint32 trigger, int ds) override;
	void ApplyAnalogInput(uint32 trigger, int ds) override;
	ATInputPointerCoordinateSpace GetPointerCoordinateSpace() const override { return ATInputPointerCoordinateSpace::Beam; }
	void GetPointers(vdfastvector<ATInputPointerInfo>& pointers) const override;
	void Tick() override;

protected:
	void OnScheduledEvent(uint32 id) override;
	void OnAttach() override;
	void OnDetach() override;

	void UpdatePortState();
	void SetLightSensingEnabled(bool enabled);
	void AssertLPInput(std::optional<int> forcedY = {});
	void DeassertLPInput();

	vdint2 GetRawBeamPos() const;
	vdint2 GetAnticLightPos(ATLightPenNoiseMode noiseMode) const;
	vdfloat2 GetBeamPointerPos() const;

	Type	mType {};
	uint32	mPortBits {};
	sint32	mPosX {};
	sint32	mPosY {};
	bool	mbTriggerPressed {};
	bool	mbPenDown {};

	ATEvent *mpLPDeassertEvent = nullptr;
	ATScheduler *mpScheduler = nullptr;
	ATLightPenPort *mpLightPen = nullptr;
	ATGTIALightSensor *mpLightSensor = nullptr;
};

///////////////////////////////////////////////////////////////////////////

class ATKeypadController final : public ATPortInputController {
	ATKeypadController(const ATKeypadController&) = delete;
	ATKeypadController& operator=(const ATKeypadController&) = delete;
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

class ATKeyboardController final : public ATPortInputController {
	ATKeyboardController(const ATKeyboardController&) = delete;
	ATKeyboardController& operator=(const ATKeyboardController&) = delete;
public:
	ATKeyboardController();
	~ATKeyboardController();

	void SetDigitalTrigger(uint32 trigger, bool state) override;

protected:
	void OnAttach() override;
	void OnDetach() override;
	void OnPortOutputChanged(uint8 outputState) override;

	void UpdatePortOutput();

	uint32	mKeyState;
};

///////////////////////////////////////////////////////////////////////////

class ATPowerPadController final : public ATPortInputController {
public:
	ATPowerPadController();
	~ATPowerPadController();

	void ColdReset() override;

	void SetDigitalTrigger(uint32 trigger, bool state) override;
	void ApplyAnalogInput(uint32 trigger, int ds) override;
	void ApplyImpulse(uint32 trigger, int ds) override;
	ATInputPointerCoordinateSpace GetPointerCoordinateSpace() const override { return ATInputPointerCoordinateSpace::Normalized; }
	void GetPointers(vdfastvector<ATInputPointerInfo>& pointers) const override;

private:
	void OnAttach() override;
	void OnPortOutputChanged(uint8 outputState) override;

	void AddDelta(int axis, sint32 delta);
	void SetPos(int axis, sint32 pos);
	void CopyTouch(int dst, int src);
	void InvalidateMatrix();
	void UpdateMatrix();
	void UpdatePortOutput();

	struct SwitchLine {
	public:
		void Clear();
		void Set(int index);
		void SetRange(int start, int end);
		bool Any() const;
		int FindNext(int start) const;

	private:
		uint32 mSwitches[4] {};
	};

	sint32 mAxis[3] {};
	bool mbMatrixValid = false;
	bool mbSense = false;
	bool mbSetShift = false;
	uint8 mTouchActiveMask = 0;

	int mScanX = 0;
	int mScanY = 0;
	uint32 mShiftRegister = 0;
	uint8 mPrevPortState = 0xFF;

	struct Touch {
		// Center position in switch coordinates (120x120 switches over 0,0-119,119).
		vdfloat2 mPos { 0, 0 };

		// Radius in switches.
		float mRadius = 0;

		// Whether this touch is active (pressed).
		bool mbActive = false;
	};

	Touch mTouches[4];

	SwitchLine mColumnArray[120];
	SwitchLine mRowArray;
};

#endif	// f_AT_INPUTCONTROLLER_H
