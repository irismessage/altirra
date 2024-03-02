//	Altirra - Atari 800/800XL/5200 emulator
//	Device emulation library - device registration
//	Copyright (C) 2009-2018 Avery Lee
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

#include <stdafx.h>
#include <at/atcore/device.h>
#include <at/atdevices/devices.h>

extern const ATDeviceDefinition g_ATDeviceDefDiskDrive;
extern const ATDeviceDefinition g_ATDeviceDefExeLoader;
extern const ATDeviceDefinition g_ATDeviceDefCorvus;
extern const ATDeviceDefinition g_ATDeviceDefLoopback;
extern const ATDeviceDefinition g_ATDeviceDefParallelFileWriter;

const std::initializer_list<const ATDeviceDefinition *> kATDeviceLibraryDefs {
	&g_ATDeviceDefDiskDrive,
	&g_ATDeviceDefExeLoader,
	&g_ATDeviceDefCorvus,
	&g_ATDeviceDefLoopback,
	&g_ATDeviceDefParallelFileWriter
};
