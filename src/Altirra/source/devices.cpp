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
#include <at/atcore/device.h>
#include <at/atdevices/devices.h>
#include "devicemanager.h"

void ATRegisterDeviceConfigurers(ATDeviceManager& dm);


extern const ATDeviceDefinition g_ATDeviceDefModem;
extern const ATDeviceDefinition g_ATDeviceDefBlackBox;
extern const ATDeviceDefinition g_ATDeviceDefMIO;
extern const ATDeviceDefinition g_ATDeviceDefHardDisks;
extern const ATDeviceDefinition g_ATDeviceDefIDEPhysDisk;
extern const ATDeviceDefinition g_ATDeviceDefIDERawImage;
extern const ATDeviceDefinition g_ATDeviceDefIDEVHDImage;
extern const ATDeviceDefinition g_ATDeviceDefRTime8;
extern const ATDeviceDefinition g_ATDeviceDefCovox;
extern const ATDeviceDefinition g_ATDeviceDefXEP80;
extern const ATDeviceDefinition g_ATDeviceDefSlightSID;
extern const ATDeviceDefinition g_ATDeviceDefDragonCart;
extern const ATDeviceDefinition g_ATDeviceDefSIOClock;
extern const ATDeviceDefinition g_ATDeviceDefTestSIOPoll3;
extern const ATDeviceDefinition g_ATDeviceDefTestSIOPoll4;
extern const ATDeviceDefinition g_ATDeviceDefTestSIOHighSpeed;
extern const ATDeviceDefinition g_ATDeviceDefPCLink;
extern const ATDeviceDefinition g_ATDeviceDefHostDevice;
extern const ATDeviceDefinition g_ATDeviceDefPrinter;
extern const ATDeviceDefinition g_ATDeviceDef850Modem;
extern const ATDeviceDefinition g_ATDeviceDef1030Modem;
extern const ATDeviceDefinition g_ATDeviceDefSX212;
extern const ATDeviceDefinition g_ATDeviceDefMidiMate;
extern const ATDeviceDefinition g_ATDeviceDefSDrive;
extern const ATDeviceDefinition g_ATDeviceDefSIO2SD;
extern const ATDeviceDefinition g_ATDeviceDefVeronica;
extern const ATDeviceDefinition g_ATDeviceDefRVerter;
extern const ATDeviceDefinition g_ATDeviceDefSoundBoard;
extern const ATDeviceDefinition g_ATDeviceDefPocketModem;
extern const ATDeviceDefinition g_ATDeviceDefKMKJZIDE;
extern const ATDeviceDefinition g_ATDeviceDefKMKJZIDE2;
extern const ATDeviceDefinition g_ATDeviceDefMyIDED1xx;
extern const ATDeviceDefinition g_ATDeviceDefMyIDED5xx;
extern const ATDeviceDefinition g_ATDeviceDefMyIDE2;
extern const ATDeviceDefinition g_ATDeviceDefSIDE;
extern const ATDeviceDefinition g_ATDeviceDefSIDE2;
extern const ATDeviceDefinition g_ATDeviceDefSIDE3;
extern const ATDeviceDefinition g_ATDeviceDefDongle;
extern const ATDeviceDefinition g_ATDeviceDefPBIDisk;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive810;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive810Archiver;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveHappy810;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive1050;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveUSDoubler;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveSpeedy1050;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveHappy1050;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveSuperArchiver;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveTOMS1050;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveTygrys1050;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive1050Duplicator;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive1050Turbo;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive1050TurboII;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveISPlate;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveIndusGT;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveXF551;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveATR8000;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrivePercomRFD;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrivePercomAT;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrivePercomATSPD;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive810Turbo;
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveAMDC;
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive815;
extern const ATDeviceDefinition g_ATDeviceDefBrowser;
extern const ATDeviceDefinition g_ATDeviceDefVBXE;
extern const ATDeviceDefinition g_ATDeviceDefXELCF;
extern const ATDeviceDefinition g_ATDeviceDefXELCF3;
extern const ATDeviceDefinition g_ATDeviceDefRapidus;
extern const ATDeviceDefinition g_ATDeviceDefWarpOS;
extern const ATDeviceDefinition g_ATDeviceDefCustom;
extern const ATDeviceDefinition g_ATDeviceDefBlockDevVFAT16;
extern const ATDeviceDefinition g_ATDeviceDefBlockDevVFAT32;
extern const ATDeviceDefinition g_ATDeviceDefBlockDevVSDFS;
extern const ATDeviceDefinition g_ATDeviceDefBlockDevTemporaryWriteFilter;
extern const ATDeviceDefinition g_ATDeviceDef1090;
extern const ATDeviceDefinition g_ATDeviceDefBit3FullView;
extern const ATDeviceDefinition g_ATDeviceDefComputerEyes;
extern const ATDeviceDefinition g_ATDeviceDefVideoGenerator;
extern const ATDeviceDefinition g_ATDeviceDefVideoStillImage;
extern const ATDeviceDefinition g_ATDeviceDefSimCovox;

void ATRegisterDevices(ATDeviceManager& dm) {
	static constexpr const ATDeviceDefinition *kDeviceDefs[]={
		&g_ATDeviceDefModem,
		&g_ATDeviceDefBlackBox,
		&g_ATDeviceDefMIO,
		&g_ATDeviceDefHardDisks,
		&g_ATDeviceDefIDEPhysDisk,
		&g_ATDeviceDefIDERawImage,
		&g_ATDeviceDefIDEVHDImage,
		&g_ATDeviceDefRTime8,
		&g_ATDeviceDefCovox,
		&g_ATDeviceDefXEP80,
		&g_ATDeviceDefSlightSID,
		&g_ATDeviceDefDragonCart,
		&g_ATDeviceDefSIOClock,
		&g_ATDeviceDefTestSIOPoll3,
		&g_ATDeviceDefTestSIOPoll4,
		&g_ATDeviceDefTestSIOHighSpeed,
		&g_ATDeviceDefPCLink,
		&g_ATDeviceDefHostDevice,
		&g_ATDeviceDefPrinter,
		&g_ATDeviceDef850Modem,
		&g_ATDeviceDef1030Modem,
		&g_ATDeviceDefSX212,
		&g_ATDeviceDefMidiMate,
		&g_ATDeviceDefSDrive,
		&g_ATDeviceDefSIO2SD,
		&g_ATDeviceDefVeronica,
		&g_ATDeviceDefRVerter,
		&g_ATDeviceDefSoundBoard,
		&g_ATDeviceDefPocketModem,
		&g_ATDeviceDefKMKJZIDE,
		&g_ATDeviceDefKMKJZIDE2,
		&g_ATDeviceDefMyIDED1xx,
		&g_ATDeviceDefMyIDED5xx,
		&g_ATDeviceDefMyIDE2,
		&g_ATDeviceDefSIDE,
		&g_ATDeviceDefSIDE2,
		&g_ATDeviceDefSIDE3,
		&g_ATDeviceDefDongle,
		&g_ATDeviceDefPBIDisk,
		&g_ATDeviceDefDiskDrive810,
		&g_ATDeviceDefDiskDrive810Archiver,
		&g_ATDeviceDefDiskDriveHappy810,
		&g_ATDeviceDefDiskDrive1050,
		&g_ATDeviceDefDiskDriveUSDoubler,
		&g_ATDeviceDefDiskDriveSpeedy1050,
		&g_ATDeviceDefDiskDriveHappy1050,
		&g_ATDeviceDefDiskDriveSuperArchiver,
		&g_ATDeviceDefDiskDriveTOMS1050,
		&g_ATDeviceDefDiskDriveTygrys1050,
		&g_ATDeviceDefDiskDrive1050Duplicator,
		&g_ATDeviceDefDiskDrive1050Turbo,
		&g_ATDeviceDefDiskDrive1050TurboII,
		&g_ATDeviceDefDiskDriveISPlate,
		&g_ATDeviceDefDiskDriveIndusGT,
		&g_ATDeviceDefDiskDriveXF551,
		&g_ATDeviceDefDiskDriveATR8000,
		&g_ATDeviceDefDiskDrivePercomRFD,
		&g_ATDeviceDefDiskDrivePercomAT,
		&g_ATDeviceDefDiskDrivePercomATSPD,
		&g_ATDeviceDefDiskDrive810Turbo,
		&g_ATDeviceDefDiskDriveAMDC,
		&g_ATDeviceDefDiskDrive815,
		&g_ATDeviceDefBrowser,
		&g_ATDeviceDefVBXE,
		&g_ATDeviceDefXELCF,
		&g_ATDeviceDefXELCF3,
		&g_ATDeviceDefRapidus,
		&g_ATDeviceDefWarpOS,
		&g_ATDeviceDefCustom,
		&g_ATDeviceDefBlockDevVFAT16,
		&g_ATDeviceDefBlockDevVFAT32,
		&g_ATDeviceDefBlockDevVSDFS,
		&g_ATDeviceDefBlockDevTemporaryWriteFilter,
		&g_ATDeviceDef1090,
		&g_ATDeviceDefBit3FullView,
		&g_ATDeviceDefComputerEyes,
		&g_ATDeviceDefVideoGenerator,
		&g_ATDeviceDefVideoStillImage,
		&g_ATDeviceDefSimCovox
	};

	for(const ATDeviceDefinition *def : kDeviceDefs)
		dm.AddDeviceDefinition(def);

	for(const ATDeviceDefinition *def : kATDeviceLibraryDefs)
		dm.AddDeviceDefinition(def);

	ATRegisterDeviceConfigurers(dm);
}

void ATRegisterDeviceXCmds(ATDeviceManager& dm) {
	void ATDeviceInitXCmdMountVHD(ATDeviceManager& dm);
	void ATDeviceInitXCmdExploreDisk(ATDeviceManager& dm);

	ATDeviceInitXCmdMountVHD(dm);
	ATDeviceInitXCmdExploreDisk(dm);
}
