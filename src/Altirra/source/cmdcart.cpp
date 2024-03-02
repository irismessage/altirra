//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2015 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/error.h>
#include <vd2/Dita/services.h>
#include <at/atcore/device.h>
#include <at/atcore/devicemanager.h>
#include "cartridge.h"
#include "simulator.h"
#include "uiaccessors.h"
#include "uifilefilters.h"

extern ATSimulator g_sim;

bool ATUIConfirmDiscardCartridge(VDGUIHandle h);
int ATUIShowDialogCartridgeMapper(VDGUIHandle h, uint32 cartSize, const void *data);

void OnCommandAttachCartridge(bool cart2) {
	if (ATUIConfirmDiscardCartridge(ATUIGetMainWindow())) {
		VDStringW fn(VDGetLoadFileName('cart', ATUIGetMainWindow(), L"Load cartridge",
			g_ATUIFileFilter_LoadCartridge,
			L"bin"));

		if (!fn.empty()) {
			vdfastvector<uint8> captureBuffer;

			ATCartLoadContext cartctx = {};
			cartctx.mbReturnOnUnknownMapper = true;
			cartctx.mpCaptureBuffer = &captureBuffer;

			if (!g_sim.LoadCartridge(cart2, fn.c_str(), &cartctx)) {
				int mapper = ATUIShowDialogCartridgeMapper(ATUIGetMainWindow(), cartctx.mCartSize, captureBuffer.data());

				if (mapper >= 0) {
					cartctx.mbReturnOnUnknownMapper = false;
					cartctx.mCartMapper = mapper;

					g_sim.LoadCartridge(cart2, fn.c_str(), &cartctx);
				}
			}

			g_sim.ColdReset();
		}
	}
}

void OnCommandAttachCartridge() {
	OnCommandAttachCartridge(false);
}

void OnCommandAttachCartridge2() {
	OnCommandAttachCartridge(true);
}

void OnCommandDetachCartridge() {
	if (ATUIConfirmDiscardCartridge(ATUIGetMainWindow())) {
		if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
			g_sim.LoadCartridge5200Default();
		else
			g_sim.UnloadCartridge(0);

		g_sim.ColdReset();
	}
}

void OnCommandDetachCartridge2() {
	if (ATUIConfirmDiscardCartridge(ATUIGetMainWindow())) {
		g_sim.UnloadCartridge(1);
		g_sim.ColdReset();
	}
}

void OnCommandAttachNewCartridge(ATCartridgeMode mode) {
	if (ATUIConfirmDiscardCartridge(ATUIGetMainWindow())) {
		g_sim.LoadNewCartridge(mode);
		g_sim.ColdReset();
	}
}

void OnCommandAttachCartridgeBASIC() {
	if (ATUIConfirmDiscardCartridge(ATUIGetMainWindow())) {
		g_sim.LoadCartridgeBASIC();
		g_sim.ColdReset();
	}
}

void OnCommandCartActivateMenuButton() {
	ATUIActivateDeviceButton(kATDeviceButton_CartridgeResetBank, true);
}

void OnCommandCartToggleSwitch() {
	g_sim.SetCartridgeSwitch(!g_sim.GetCartridgeSwitch());
}

void OnCommandSaveCartridge() {
	ATCartridgeEmulator *cart = g_sim.GetCartridge(0);
	int mode = 0;

	if (cart)
		mode = cart->GetMode();

	if (!mode)
		throw MyError("There is no cartridge to save.");

	if (mode == kATCartridgeMode_SuperCharger3D)
		throw MyError("The current cartridge cannot be saved to an image file.");

	VDFileDialogOption opts[]={
		{VDFileDialogOption::kSelectedFilter, 0, NULL, 0, 0},
		{0}
	};

	int optval[1]={0};

	VDStringW fn(VDGetSaveFileName('cart', ATUIGetMainWindow(), L"Save cartridge",
		L"Cartridge image with header\0*.bin;*.car;*.rom\0"
		L"Raw cartridge image\0*.bin;*.car;*.rom;*.a52\0",

		L"car", opts, optval));

	if (!fn.empty()) {
		cart->Save(fn.c_str(), optval[0] == 1);
	}
}
