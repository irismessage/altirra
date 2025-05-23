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

#ifndef f_AT_ATCORE_DEVICEMANAGER_H
#define f_AT_ATCORE_DEVICEMANAGER_H

#include <vd2/system/function.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdstl_hashset.h>
#include <at/atcore/device.h>
#include <at/atcore/devicestorage.h>
#include <at/atcore/notifylist.h>

class IATDevice;
class IATDeviceParent;
class IATDeviceBus;
struct ATDeviceDefinition;
class ATPropertySet;
struct VDJSONValueRef;
class VDJSONWriter;
class VDRegistryKey;
class ATDeviceManager;
class IATObjectState;
template<typename T> class vdrefptr;
struct ATSnapshotStatus;

enum ATSettingsCategory : uint32;

typedef void (*ATDeviceFactoryFn)(const ATPropertySet& pset, IATDevice **);
typedef bool (*ATDeviceConfigureFn)(VDGUIHandle parent, ATPropertySet& pset);

struct ATParsedDevicePath {
	bool mbValid;
	IATDevice *mpDevice;
	IATDeviceParent *mpDeviceBusParent;
	IATDeviceBus *mpDeviceBus;
	uint32 mDeviceBusIndex;
};

class IATDeviceChangeCallback {
public:
	virtual void OnDeviceAdded(uint32 iid, IATDevice *dev, void *iface) = 0;
	virtual void OnDeviceRemoving(uint32 iid, IATDevice *dev, void *iface) = 0;
	virtual void OnDeviceRemoved(uint32 iid, IATDevice *dev, void *iface) = 0;
};

template<class T>
class ATDeviceInterfaceIterator {
public:
	typedef ptrdiff_t difference_type;
	typedef T *value_type;
	typedef value_type* pointer;
	typedef value_type& reference;
	typedef std::random_access_iterator_tag iterator_category;

	ATDeviceInterfaceIterator(void *const *p) : mp(p) {}

	bool operator==(const ATDeviceInterfaceIterator& other) const { return mp == other.mp; }
	bool operator!=(const ATDeviceInterfaceIterator& other) const { return mp != other.mp; }
	bool operator< (const ATDeviceInterfaceIterator& other) const { return mp <  other.mp; }
	bool operator<=(const ATDeviceInterfaceIterator& other) const { return mp <= other.mp; }
	bool operator> (const ATDeviceInterfaceIterator& other) const { return mp >  other.mp; }
	bool operator>=(const ATDeviceInterfaceIterator& other) const { return mp >= other.mp; }

	ATDeviceInterfaceIterator& operator++() {
		++mp;
		return *this;
	}

	ATDeviceInterfaceIterator& operator--() {
		--mp;
		return *this;
	}

	ATDeviceInterfaceIterator operator++(int) { ATDeviceInterfaceIterator it(*this); ++*this; return it; }
	ATDeviceInterfaceIterator operator--(int) { ATDeviceInterfaceIterator it(*this); --*this; return it; }

	ATDeviceInterfaceIterator operator+(ptrdiff_t n) { return ATDeviceInterfaceIterator(mp + n); }
	ATDeviceInterfaceIterator operator-(ptrdiff_t n) { return ATDeviceInterfaceIterator(mp - n); }

	ATDeviceInterfaceIterator& operator+=(ptrdiff_t n) { mp += n; return *this; }
	ATDeviceInterfaceIterator& operator-=(ptrdiff_t n) { mp -= n; return *this; }

	ptrdiff_t operator-(const ATDeviceInterfaceIterator& other) { return mp - other.mp; }

	T *operator*() const { return (T *)*mp; }
	T *operator->() const { return (T *)*mp; }
	T *operator[](ptrdiff_t offset) const { return (T *)mp[offset]; }

private:
	void *const *mp;

};

template<class T>
inline ATDeviceInterfaceIterator<T> operator+(ptrdiff_t n, const ATDeviceInterfaceIterator<T>& b) { return b + n; }

template<class T>
class ATDeviceInterfaceSequence {
public:
	typedef size_t size_type;
	typedef ptrdiff_t difference_type;
	typedef T* const  value_type;
	typedef value_type* pointer_type;
	typedef const value_type* const_pointer_type;
	typedef value_type& reference_type;
	typedef const value_type& const_reference_type;
	typedef ATDeviceInterfaceIterator<T> iterator;
	typedef ATDeviceInterfaceIterator<T> const_iterator;

	ATDeviceInterfaceSequence(void *const *p, void *const *q) : mp(p), mq(q) {}

	iterator begin() { return iterator(mp); }
	const_iterator begin() const { return const_iterator(mp); }
	const_iterator cbegin() const { return const_iterator(mp); }
	iterator end() { return iterator(mq); }
	const_iterator end() const { return const_iterator(mq); }
	const_iterator cend() const { return const_iterator(mq); }

private:
	void *const *mp;
	void *const *mq;
};

struct ATDeviceXCmdInfo {
	bool mbRequiresElevation = false;
	VDStringW mDisplayName;
};

class IATDeviceXCmd : public IVDRefCount {
public:
	virtual bool IsPostRefreshNeeded() const { return true; }
	virtual bool IsSupported(IATDevice *dev, int busIndex) const = 0;
	virtual ATDeviceXCmdInfo GetInfo() const = 0;
	virtual void Invoke(ATDeviceManager& devmgr, IATDevice *dev, int busIndex) = 0;
};

class ATDeviceManager final : public IATDeviceManager, public IATDeviceStorageManager {
	ATDeviceManager(const ATDeviceManager&) = delete;
	ATDeviceManager& operator=(const ATDeviceManager&) = delete;
public:
	ATDeviceManager();
	~ATDeviceManager();

	// Manually increment the change counter; useful when setting
	// properties, since this bypasses the device manager. If you
	// are hitting this every frame you are doing it too much.
	void IncrementChangeCounter() { ++mChangeCounter; }
	uint32 GetChangeCounter() const { return mChangeCounter; }

	void Init();

	ATDeviceInterfaceSequence<IATDevice> GetDevices(bool nonChildOnly, bool visibleOnly, bool externalOnly) const {
		auto ilist = GetInterfaceList(0, nonChildOnly, visibleOnly, externalOnly);

		return ATDeviceInterfaceSequence<IATDevice>(ilist->begin(), ilist->end());
	}

	template<class T>
	ATDeviceInterfaceSequence<T> GetInterfaces(bool nonChildOnly, bool visibleOnly, bool externalOnly) const {
		auto *ilist = GetInterfaceList(T::kTypeID, nonChildOnly, visibleOnly, externalOnly);

		return ATDeviceInterfaceSequence<T>(ilist->begin(), ilist->end());
	}

	template<class T>
	T *GetInterface() { return static_cast<T *>(GetInterface(T::kTypeID)); }

	IATDevice *AddDevice(const char *tag, const ATPropertySet& pset, bool child = false);
	IATDevice *AddDevice(const ATDeviceDefinition *def, const ATPropertySet& pset, bool child = false);
	void AddDevice(IATDevice *dev, bool child = false);
	void RemoveDevice(const char *tag);
	void RemoveDevice(IATDevice *dev);
	void RemoveAllDevices(bool includeInternal);
	void ToggleDevice(const char *tag);

	uint32 GetDeviceCount() const;
	IATDevice *GetDeviceByTag(const char *tag, uint32 index = 0, bool visibleOnly = false, bool externalOnly = false) const;
	IATDevice *GetDeviceByIndex(uint32 i) const;

	ATParsedDevicePath ParsePath(const char *path) const;
	VDStringA GetPathForDevice(IATDevice *dev) const;
	void AppendPathForDevice(VDStringA& s, IATDevice *dev, bool recurse) const;

	void *GetInterface(uint32 id) const;

	const ATDeviceDefinition *GetDeviceDefinition(const char *tag) const;
	ATDeviceConfigureFn GetDeviceConfigureFn(const char *tag) const;

	void AddDeviceDefinition(const ATDeviceDefinition *def);
	void AddDeviceConfigurer(const char *tag, ATDeviceConfigureFn configurer);

	void AddDeviceChangeCallback(uint32 iid, IATDeviceChangeCallback *cb);
	void RemoveDeviceChangeCallback(uint32 iid, IATDeviceChangeCallback *cb);
	void AddInitCallback(vdfunction<void(IATDevice& dev)> cb);

	void AddDeviceStatusCallback(const vdfunction<void(IATDevice&)> *cb);
	void RemoveDeviceStatusCallback(const vdfunction<void(IATDevice&)> *cb);

	void ReconfigureDevice(IATDevice& dev, const ATPropertySet& pset);
	void MarkAndSweep(IATDevice *const *pExcludedDevs, size_t numExcludedDevs, vdfastvector<IATDevice *>& garbage);

	void SerializeDevice(IATDevice *dev, VDStringW& str, bool compact, bool includeChildren) const;
	void DeserializeDevices(IATDeviceParent *parent, IATDeviceBus *bus, const wchar_t *str);

	void SerializeProps(const ATPropertySet& props, VDStringW& str) const;
	void DeserializeProps(ATPropertySet& props, const wchar_t *str);

	void RegisterService(uint32 iid, void *p);
	void UnregisterService(uint32 iid);

	template<typename T>
	void RegisterService(T *p) {
		RegisterService(T::kTypeID, p);
	}

	template<typename T>
	void UnregisterService(T *p) {
		UnregisterService(T::kTypeID);
	}

	void RegisterExtendedCommand(IATDeviceXCmd& extCmd);
	vdfastvector<IATDeviceXCmd *> GetExtendedCommandsForDevice(IATDevice *dev, int busIndex) const;

	void LoadSettings(VDRegistryKey& key);
	void SaveSettings(VDRegistryKey& key);

	void GetSnapshotStatus(ATSnapshotStatus& status) const;
	void LoadState(const IATObjectState *);
	vdrefptr<IATObjectState> SaveState() const;

public:		// IATDeviceManager
	using IATDeviceManager::GetService;
	void *GetService(uint32 iid) override;
	void NotifyDeviceStatusChanged(IATDevice& dev) override;

public:		// IATDeviceStorageManager
	void RegisterDeviceStorage(IATDeviceStorage& storage) override;
	void UnregisterDeviceStorage(IATDeviceStorage& storage) override;

	bool LoadNVRAM(const char *name, void *buf, size_t len) override;
	bool LoadNVRAMInt(const char *name, sint32& val) override;
	void SaveNVRAM(const char *name, const void *buf, size_t len) override;
	void SaveNVRAMInt(const char *name, sint32 val) override;

protected:
	typedef vdfastvector<void *> InterfaceList;

	const InterfaceList *GetInterfaceList(uint32 iid, bool rootOnly, bool visibleOnly, bool externalOnly) const;
	void Mark(IATDevice *dev, IATDevice *const *pExcludedDevs, size_t numExcludedDevs, vdhashset<IATDevice *>& devSet);
	void SerializeDevice(IATDevice *dev, VDJSONWriter& writer, bool includeChildren) const;
	void DeserializeDevice(IATDeviceParent *parent, IATDeviceBus *bus, const VDJSONValueRef& val);
	void SerializeProps(const ATPropertySet& props, VDJSONWriter& writer) const;
	void DeserializeProps(ATPropertySet& props, const VDJSONValueRef& val);

	uint32 mChangeCounter = 0;

	struct DeviceEntry {
		IATDevice *mpDevice;
		const char *mpTag;
		bool mbChild;
		bool mbHidden;
		bool mbInternal;
	};

	vdfastvector<DeviceEntry> mDevices;

	mutable vdhashmap<uint64, InterfaceList> mInterfaceListCache;

	vdfastvector<const ATDeviceDefinition *> mDeviceDefinitions;

	struct DeviceConfigurer {
		const char *mpTag;
		ATDeviceConfigureFn mpConfigure;
	};

	vdfastvector<DeviceConfigurer> mDeviceConfigurers;

	vdhashmap<uint32, vdfastvector<IATDeviceChangeCallback *>> mChangeCallbacks;

	vdvector<vdfunction<void(IATDevice&)>> mInitHandlers;
	ATNotifyList<const vdfunction<void(IATDevice&)> *> mStatusHandlers;

	vdhashmap<uint32, void *> mServices;

	struct NVRAM {
		vdfastvector<uint8> mBlobVal;
		sint32 mIntVal = 0;
		bool mbIsInt = false;
	};
	vdhashmap<VDStringA, NVRAM, vdstringhashi, vdstringpredi> mNVRAMs;
	vdfastvector<IATDeviceStorage *> mStorage;

	vdvector<vdrefptr<IATDeviceXCmd>> mExtCmds;
};

#endif
