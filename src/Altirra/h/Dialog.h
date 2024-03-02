//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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

#ifndef f_AT_DIALOG_H
#define f_AT_DIALOG_H

#ifdef _MSC_VER
#pragma once
#endif

#define VDZCALLBACK __stdcall

#ifndef _WIN64
	typedef __w64 int		VDZINT_PTR;
	typedef __w64 unsigned	VDZUINT_PTR;
	typedef __w64 long		VDZLONG_PTR;
#else
	typedef __int64				VDZINT_PTR;
	typedef unsigned __int64	VDZUINT_PTR;
	typedef __int64				VDZLONG_PTR;
#endif

typedef struct HWND__	*VDZHWND;
typedef unsigned		VDZUINT;
typedef VDZUINT_PTR		VDZWPARAM;
typedef VDZLONG_PTR		VDZLPARAM;

class VDDialogFrameW32 {
public:
	VDZHWND GetWindowHandle() const { return mhdlg; }

	sintptr ShowDialog(VDGUIHandle hwndParent);

protected:
	VDDialogFrameW32(uint32 dlgid);

	void End(sintptr result);

	void SetFocusToControl(uint32 id);
	void EnableControl(uint32 id, bool enabled);

	bool GetControlText(uint32 id, VDStringW& s);
	void SetControlText(uint32 id, const wchar_t *s);
	void SetControlTextF(uint32 id, const wchar_t *format, ...);

	uint32 GetControlValueUint32(uint32 id);
	double GetControlValueDouble(uint32 id);

	void ExchangeControlValueUint32(bool write, uint32 id, uint32& val, uint32 minVal, uint32 maxVal);
	void ExchangeControlValueDouble(bool write, uint32 id, const wchar_t *format, double& val, double minVal, double maxVal);

	void CheckButton(uint32 id, bool checked);
	bool IsButtonChecked(uint32 id);

	void BeginValidation();
	bool EndValidation();

	void FailValidation(uint32 id);
	void SignalFailedValidation(uint32 id);

	// listbox
	sint32 LBGetSelectedIndex(uint32 id);
	void LBSetSelectedIndex(uint32 id, sint32 idx);
	void LBAddString(uint32 id, const wchar_t *s);
	void LBAddStringF(uint32 id, const wchar_t *format, ...);

	// combo
	sint32 CBGetSelectedIndex(uint32 id);
	void CBSetSelectedIndex(uint32 id, sint32 idx);
	void CBAddString(uint32 id, const wchar_t *s);

protected:
	virtual void OnDataExchange(bool write);
	virtual bool OnLoaded();
	virtual bool OnOK();
	virtual bool OnCancel();
	virtual bool OnCommand(uint32 id, uint32 extcode);
	virtual bool PreNCDestroy();

	bool	mbValidationFailed;

private:
	static VDZINT_PTR VDZCALLBACK StaticDlgProc(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam);
	VDZINT_PTR DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam);

	const char *mpDialogResourceName;
	uint32	mFailedId;

protected:
	VDZHWND	mhdlg;
};

#endif
