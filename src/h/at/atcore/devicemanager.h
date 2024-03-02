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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_ATCORE_DEVICEMANAGER_H
#define f_AT_ATCORE_DEVICEMANAGER_H

#include <vd2/system/function.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdstl_hashset.h>

class IATDevice;
class IATDeviceParent;
class ATPropertySet;
struct VDJSONValueRef;
class VDJSONWriter;

typedef void (*ATDeviceFactoryFn)(const ATPropertySet& pset, IATDevice **);
typedef bool (*ATDeviceConfigureFn)(VDGUIHandle parent, ATPropertySet& pset);

class IATDeviceChangeCallback {
public:
	virtual void OnDeviceAdded(uint32 iid, IATDevice *dev, void *iface) = 0;
	virtual void OnDeviceRemoved(uint32 iid, IATDevice *dev, void *iface) = 0;
};

class ATDeviceManager {
	ATDeviceManager(const ATDeviceManager&);
	ATDeviceManager& operator=(const ATDeviceManager&);
public:
	ATDeviceManager();
	~ATDeviceManager();

	void Init();

	template<class T>
	void ForEachDevice(bool nonChildOnly, const T& fn) const {
		for(auto it = mDevices.begin(), itEnd = mDevices.end();
			it != itEnd;
			++it)
		{
			if (!nonChildOnly || !it->mbChild)
				fn(it->mpDevice);
		}
	}

	template<class T_Interface, class T_Fn>
	void ForEachInterface(bool nonChildOnly, const T_Fn& fn) {
		for(auto it = mDevices.begin(), itEnd = mDevices.end();
			it != itEnd;
			++it)
		{
			if (!nonChildOnly || !it->mbChild) {
				T_Interface *iface = vdpoly_cast<T_Interface *>(it->mpDevice);

				if (iface)
					fn(*iface);
			}
		}		
	}

	IATDevice *AddDevice(const char *tag, const ATPropertySet& pset, bool child);
	void AddDevice(IATDevice *dev, bool child);
	void RemoveDevice(const char *tag);
	void RemoveDevice(IATDevice *dev);
	void RemoveAllDevices();
	void ToggleDevice(const char *tag);

	uint32 GetDeviceCount() const;
	IATDevice *GetDeviceByTag(const char *tag) const;
	IATDevice *GetDeviceByIndex(uint32 i) const;

	ATDeviceConfigureFn GetDeviceConfigureFn(const char *tag) const;

	void AddDeviceFactory(const char *tag, ATDeviceFactoryFn factory);
	void AddDeviceConfigurer(const char *tag, ATDeviceConfigureFn configurer);

	void AddDeviceChangeCallback(uint32 iid, IATDeviceChangeCallback *cb);
	void RemoveDeviceChangeCallback(uint32 iid, IATDeviceChangeCallback *cb);
	void AddInitCallback(vdfunction<void(IATDevice& dev)> cb);

	void MarkAndSweep(IATDevice *const *pExcludedDevs, size_t numExcludedDevs, vdfastvector<IATDevice *>& garbage);

	void SerializeDevice(IATDevice *dev, VDStringW& str) const;
	void DeserializeDevices(IATDeviceParent *parent, const wchar_t *str);

	void SerializeProps(const ATPropertySet& props, VDStringW& str) const;
	void DeserializeProps(ATPropertySet& props, const wchar_t *str);

protected:
	void Mark(IATDevice *dev, IATDevice *const *pExcludedDevs, size_t numExcludedDevs, vdhashset<IATDevice *>& devSet);
	void SerializeDevice(IATDevice *dev, VDJSONWriter& writer) const;
	void DeserializeDevice(IATDeviceParent *parent, const VDJSONValueRef& val);
	void SerializeProps(const ATPropertySet& props, VDJSONWriter& writer) const;
	void DeserializeProps(ATPropertySet& props, const VDJSONValueRef& val);

	struct DeviceEntry {
		IATDevice *mpDevice;
		bool mbChild;
	};

	vdfastvector<DeviceEntry> mDevices;

	struct DeviceFactory {
		const char *mpTag;
		ATDeviceFactoryFn mpCreate;
	};

	vdfastvector<DeviceFactory> mDeviceFactories;

	struct DeviceConfigurer {
		const char *mpTag;
		ATDeviceConfigureFn mpConfigure;
	};

	vdfastvector<DeviceConfigurer> mDeviceConfigurers;

	vdhashmap<uint32, vdfastvector<IATDeviceChangeCallback *>> mChangeCallbacks;

	vdvector<vdfunction<void(IATDevice&)>> mInitHandlers;
};

#endif
