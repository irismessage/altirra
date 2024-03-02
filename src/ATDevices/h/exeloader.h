//	Altirra - Atari 800/800XL/5200 emulator
//	Device emulation library - EXE loader module
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

#ifndef f_AT_ATDEVICES_EXELOADER_H
#define f_AT_ATDEVICES_EXELOADER_H

#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicesio.h>

class ATPropertySet;

class ATDeviceExeLoader final : public ATDevice, public IATDeviceSIO {
	ATDeviceExeLoader(const ATDeviceExeLoader&);
	ATDeviceExeLoader& operator=(const ATDeviceExeLoader&);

public:
	ATDeviceExeLoader();
	~ATDeviceExeLoader();
	
	void *AsInterface(uint32 iid) override;

public:
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettings(ATPropertySet& pset) override;
	bool SetSettings(const ATPropertySet& pset) override;
	void Init() override;
	void Shutdown() override;

public:
	void Load(const wchar_t *path);

public:
	void InitSIO(IATDeviceSIOManager *mgr) override;
	CmdResponse OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) override;
	void OnSerialAbortCommand() override;
	void OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) override;
	void OnSerialFence(uint32 id) override;
	CmdResponse OnSerialAccelCommand(const ATDeviceSIORequest& request) override;

private:
	IATDeviceSIO::CmdResponse OnCmdReadSegment(const ATDeviceSIOCommand& cmd);
	void SetupHighSpeed(const ATDeviceSIOCommand& cmd);

	void RecreatePreloadSegments();
	void QueuePreload(const uint8 *data, size_t len);

	IATDeviceSIOManager *mpSIOMgr = nullptr;

	VDStringW mPath;
	bool mbAutoDisableBasic = false;

	vdfastvector<uint8> mExeData;
	uint32 mSegmentIndex;

	struct Segment {
		uint16 mStart;
		uint16 mLen;
		uint32 mOffset;
	};

	vdfastvector<Segment> mSegments;

	vdfastvector<uint8> mPreloadData;
	vdfastvector<Segment> mPreloadSegments;
};

#endif
