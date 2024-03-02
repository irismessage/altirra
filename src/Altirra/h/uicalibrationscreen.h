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

#ifndef f_AT_UICALIBRATIONSCREEN_H
#define f_AT_UICALIBRATIONSCREEN_H

#include <at/atui/uicontainer.h>

class ATUIListView;
class ATUIButton;

class ATUICalibrationScreen final : public ATUIContainer {
public:
	ATUICalibrationScreen();
	~ATUICalibrationScreen();

	static void ShowDialog();

private:
	void OnCreate() override;
	void OnActionStop(uint32 id) override;

	void UpdatePanel();

	vdrefptr<ATUIListView> mpListView;
	vdrefptr<ATUIWidget> mpPanel;
	vdrefptr<ATUIButton> mpOKButton;
};

#endif
