//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_DEVICEIMPL_H
#define f_AT_ATCORE_DEVICEIMPL_H

#include <vd2/system/refcount.h>
#include <at/atcore/device.h>

class IATDeviceManager;

class ATDevice : public vdrefcounted<IATDevice> {
	ATDevice(const ATDevice&) = delete;
	ATDevice& operator=(const ATDevice&) = delete;
public:
	ATDevice();
	~ATDevice();

	virtual void *AsInterface(uint32 iid) override;

	virtual void SetManager(IATDeviceManager *devMgr);
	virtual IATDeviceParent *GetParent() override;
	virtual uint32 GetParentBusIndex() override;
	virtual void SetParent(IATDeviceParent *parent, uint32 busIndex) override;
	virtual void GetSettingsBlurb(VDStringW& buf) override;
	virtual void GetSettings(ATPropertySet& settings) override;
	virtual bool SetSettings(const ATPropertySet& settings) override;
	virtual void Init() override;
	virtual void Shutdown() override;
	virtual uint32 GetComputerPowerOnDelay() const override;
	virtual void WarmReset() override;
	virtual void ColdReset() override;
	virtual void ComputerColdReset() override;
	virtual void PeripheralColdReset() override;

	virtual void SetTraceContext(ATTraceContext *context) override;
	virtual bool GetErrorStatus(uint32 idx, VDStringW& error) override;

protected:
	void *GetService(uint32 iid) const;

	template<typename T>
	T *GetService() const {
		return (T *)GetService(T::kTypeID);
	}

	IATDeviceManager *mpDeviceManager = nullptr;
	IATDeviceParent *mpDeviceParent = nullptr;
	uint32 mDeviceParentBusIndex = 0;
};

#endif
