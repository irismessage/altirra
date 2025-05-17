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

#include <stdafx.h>
#include <vd2/system/error.h>
#include <vd2/system/refcount.h>
#include <vd2/system/registry.h>
#include <vd2/system/unknown.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonvalue.h>
#include <vd2/vdjson/jsonwriter.h>
#include <at/atcore/device.h>
#include <at/atcore/devicesio.h>
#include <at/atcore/devicecio.h>
#include <at/atcore/deviceparent.h>
#include <at/atcore/deviceprinter.h>
#include <at/atcore/devicesnapshot.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/savestate.h>
#include <at/atcore/snapshotimpl.h>
#include "devicemanager.h"

ATDeviceManager::ATDeviceManager() {
	RegisterService<IATDeviceStorageManager>(this);
}

ATDeviceManager::~ATDeviceManager() {
}

void ATDeviceManager::Init() {
}

IATDevice *ATDeviceManager::AddDevice(const char *tag, const ATPropertySet& pset, bool child) {
	const ATDeviceDefinition *def = GetDeviceDefinition(tag);
	if (!def)
		return nullptr;

	return AddDevice(def, pset, child);
}

IATDevice *ATDeviceManager::AddDevice(const ATDeviceDefinition *def, const ATPropertySet& pset, bool child) {
	vdrefptr<IATDevice> dev;
	def->mpFactoryFn(pset, ~dev);

	if (dev) {
		VDASSERT(vdpoly_cast<IATDevice *>(dev) != nullptr);

		dev->SetSettings(pset);
		AddDevice(dev, child);
	}

	return dev;
}

void ATDeviceManager::AddDevice(IATDevice *dev, bool child) {
	try {
		mInterfaceListCache.clear();

		dev->SetManager(this);

		for(const auto& fn : mInitHandlers)
			fn(*dev);

		ATDeviceInfo info;
		dev->GetDeviceInfo(info);

		DeviceEntry ent = {
			.mpDevice = dev,
			.mpTag = info.mpDef->mpTag,
			.mbChild = child,
			.mbHidden = (info.mpDef->mFlags & kATDeviceDefFlag_Hidden) != 0,
			.mbInternal = (info.mpDef->mFlags & kATDeviceDefFlag_Internal) != 0
		};

		mDevices.push_back(ent);

		try {
			dev->AddRef();

			dev->Init();
		} catch(...) {
			mDevices.pop_back();
			dev->Release();
			dev->SetManager(nullptr);
			throw;
		}
	} catch(...) {
		dev->Shutdown();
		throw;
	}

	dev->ColdReset();
	dev->PeripheralColdReset();
	dev->ComputerColdReset();

	++mChangeCounter;

	for(const auto& changeClass : mChangeCallbacks) {
		const uint32 iid = changeClass.first;
		void *iface = dev->AsInterface(iid);

		if (iface) {
			for(IATDeviceChangeCallback *cb : changeClass.second)
				cb->OnDeviceAdded(iid, dev, iface);
		}
	}
}

void ATDeviceManager::RemoveDevice(const char *tag) {
	if (IATDevice *dev = GetDeviceByTag(tag))
		RemoveDevice(dev);
}

void ATDeviceManager::RemoveDevice(IATDevice *dev) {
	mInterfaceListCache.clear();

	for(auto it = mDevices.begin(), itEnd = mDevices.end();
		it != itEnd;
		++it)
	{
		if (it->mpDevice == dev) {
			if (it->mbChild) {
				IATDeviceParent *parent = dev->GetParent();

				if (parent)
					parent->GetDeviceBus(dev->GetParentBusIndex())->RemoveChildDevice(dev);
			}

			mDevices.erase(it);

			++mChangeCounter;

			for(const auto& changeClass : mChangeCallbacks) {
				const uint32 iid = changeClass.first;
				void *iface = dev->AsInterface(iid);

				if (iface) {
					for(IATDeviceChangeCallback *cb : changeClass.second)
						cb->OnDeviceRemoving(iid, dev, iface);
				}
			}

			dev->Shutdown();

			for(const auto& changeClass : mChangeCallbacks) {
				const uint32 iid = changeClass.first;
				void *iface = dev->AsInterface(iid);

				if (iface) {
					for(IATDeviceChangeCallback *cb : changeClass.second)
						cb->OnDeviceRemoved(iid, dev, iface);
				}
			}

			dev->SetManager(nullptr);
			dev->Release();
			break;
		}
	}
}

void ATDeviceManager::RemoveAllDevices(bool includeInternal) {
	auto devs = GetDevices(false, false, !includeInternal);
	vdfastvector<IATDevice *> devices(devs.begin(), devs.end());

	for(IATDevice *dev : devices)
		RemoveDevice(dev);
}

void ATDeviceManager::ToggleDevice(const char *tag) {
	IATDevice *dev = GetDeviceByTag(tag);

	if (dev)
		RemoveDevice(dev);
	else
		AddDevice(tag, ATPropertySet());
}

uint32 ATDeviceManager::GetDeviceCount() const {
	return mDevices.size();
}

IATDevice *ATDeviceManager::GetDeviceByTag(const char *tag, uint32 index, bool visibleOnly, bool externalOnly) const {
	for(auto it = mDevices.begin(), itEnd = mDevices.end();
		it != itEnd;
		++it)
	{
		if (visibleOnly && it->mbHidden)
			continue;

		if (externalOnly && it->mbInternal)
			continue;

		if (!strcmp(it->mpTag, tag)) {
			if (!index--)
				return it->mpDevice;
		}
	}

	return nullptr;
}

IATDevice *ATDeviceManager::GetDeviceByIndex(uint32 i) const {
	return i < mDevices.size() ? mDevices[i].mpDevice : nullptr;
}

ATParsedDevicePath ATDeviceManager::ParsePath(const char *path) const {
	ATParsedDevicePath result {};

	const auto parseComponent = [](const char *& path) -> std::pair<VDStringSpanA, uint32> {
		if (*path != '/')
			return {};

		++path;

		const char *nameStart = path;

		if (!isalnum((unsigned char)*path))
			return {};

		++path;

		while(isalnum((unsigned char)*path))
			++path;

		const char *nameEnd = path;
		uint32 index = 0;

		if (*path == '.') {
			++path;

			if (!isdigit((unsigned char)*path))
				return {};

			do {
				++path;
			} while(isdigit((unsigned char)*path));
		}

		if (*path)
			return {};

		return { VDStringSpanA(nameStart, nameEnd), index };
	};

	IATDeviceParent *parent = nullptr;
	IATDeviceBus *bus = nullptr;
	uint32 busIndex = 0;
	for(;;) {
		const auto deviceComponent = parseComponent(path);
		if (deviceComponent.first.empty())
			break;

		IATDevice *dev = nullptr;
		if (parent) {
			vdfastvector<IATDevice *> children;
			bus->GetChildDevices(children);

			uint32 matchIndex = deviceComponent.second;
			for(IATDevice *child : children) {
				ATDeviceInfo info;
				child->GetDeviceInfo(info);

				if (deviceComponent.first == info.mpDef->mpTag) {
					if (!matchIndex--) {
						dev = child;
						break;
					}
				}
			}
		} else {
			dev = GetDeviceByTag(VDStringA(deviceComponent.first).c_str(), deviceComponent.second);
			if (!dev)
				break;
		}

		if (!*path) {
			result.mbValid = true;
			result.mpDevice = dev;
		}

		auto busComponent = parseComponent(path);
		if (busComponent.first.empty())
			break;

		parent = vdpoly_cast<IATDeviceParent *>(dev);
		if (!parent)
			break;

		for(busIndex = 0; ; ++busIndex) {
			bus = parent->GetDeviceBus(busIndex);
			if (!bus)
				break;

			if (busComponent.first == bus->GetBusTag()) {
				if (!busComponent.second--)
					break;
			}
		}

		if (!bus)
			break;

		if (!*path) {
			result.mbValid = true;
			result.mpDeviceBusParent = parent;
			result.mpDeviceBus = bus;
			result.mDeviceBusIndex = busIndex;
		}
	}

	return result;
}

VDStringA ATDeviceManager::GetPathForDevice(IATDevice *dev) const {
	VDStringA s;
	AppendPathForDevice(s, dev, true);

	return s;
}

void ATDeviceManager::AppendPathForDevice(VDStringA& s, IATDevice *dev, bool recurse) const {
	ATDeviceInfo info;
	dev->GetDeviceInfo(info);

	IATDeviceParent *parent = dev->GetParent();
	uint32_t devIndex = 0;

	if (parent) {
		if (recurse) {
			IATDevice *parentDev = vdpoly_cast<IATDevice *>(parent);
			AppendPathForDevice(s, parentDev, true);
		}

		const uint32 busIndex = dev->GetParentBusIndex();
		IATDeviceBus *bus = parent->GetDeviceBus(busIndex);

		s += '/';
		s += bus->GetBusTag();

		vdfastvector<IATDevice *> siblings;
		bus->GetChildDevices(siblings);

		for(IATDevice *sibling : siblings) {
			if (sibling == dev)
				break;

			ATDeviceInfo info2;
			sibling->GetDeviceInfo(info2);

			if (info2.mpDef == info.mpDef)
				++devIndex;
		}
	} else {
		for(IATDevice *sibling : GetDevices(true, false, true)) {
			if (sibling == dev)
				break;

			ATDeviceInfo info2;
			sibling->GetDeviceInfo(info2);

			if (info2.mpDef == info.mpDef)
				++devIndex;
		}
	}

	s += '/';
	s.append(info.mpDef->mpTag);

	if (devIndex)
		s.append_sprintf(".%u", devIndex);
}

void *ATDeviceManager::GetInterface(uint32 id) const {
	for(const auto& entry : mDevices) {
		void *p = entry.mpDevice->AsInterface(id);

		if (p)
			return p;
	}

	return nullptr;
}


const ATDeviceDefinition *ATDeviceManager::GetDeviceDefinition(const char *tag) const {
	for(const ATDeviceDefinition *def : mDeviceDefinitions) {
		if (!strcmp(tag, def->mpTag))
			return def;
	}

	return nullptr;
}

ATDeviceConfigureFn ATDeviceManager::GetDeviceConfigureFn(const char *tag) const {
	for(auto it = mDeviceConfigurers.begin(), itEnd = mDeviceConfigurers.end();
		it != itEnd;
		++it)
	{
		if (!strcmp(tag, it->mpTag))
			return it->mpConfigure;
	}

	return nullptr;
}

void ATDeviceManager::AddDeviceDefinition(const ATDeviceDefinition *def) {
	mDeviceDefinitions.push_back(def);
}

void ATDeviceManager::AddDeviceConfigurer(const char *tag, ATDeviceConfigureFn configurer) {
	auto& fac = mDeviceConfigurers.push_back();

	fac.mpTag = tag;
	fac.mpConfigure = configurer;
}

void ATDeviceManager::AddDeviceChangeCallback(uint32 iid, IATDeviceChangeCallback *cb) {
	mChangeCallbacks[iid].push_back(cb);
}

void ATDeviceManager::RemoveDeviceChangeCallback(uint32 iid, IATDeviceChangeCallback *cb) {
	auto it = mChangeCallbacks.find(iid);

	if (it != mChangeCallbacks.end()) {
		auto it2 = std::find(it->second.begin(), it->second.end(), cb);

		if (it2 != it->second.end()) {
			*it2 = it->second.back();
			it->second.pop_back();

			if (it->second.empty())
				mChangeCallbacks.erase(it);
		}
	}
}

void ATDeviceManager::AddInitCallback(vdfunction<void(IATDevice& dev)> cb) {
	mInitHandlers.push_back(std::move(cb));
}

void ATDeviceManager::AddDeviceStatusCallback(const vdfunction<void(IATDevice&)> *cb) {
	mStatusHandlers.Add(cb);
}

void ATDeviceManager::RemoveDeviceStatusCallback(const vdfunction<void(IATDevice&)> *cb) {
	mStatusHandlers.Remove(cb);
}

void ATDeviceManager::ReconfigureDevice(IATDevice& dev, const ATPropertySet& pset) {
	if (dev.SetSettings(pset)) {
		IncrementChangeCounter();
		return;
	}

	vdrefptr devholder(&dev);

	vdfastvector<IATDevice *> childDevices;
	IATDevice *devPtr = &dev;
	MarkAndSweep(&devPtr, 1, childDevices);

	IATDeviceParent *parent = dev.GetParent();
	uint32 busIndex = 0;
	IATDeviceBus *bus = nullptr;

	if (parent) {
		busIndex = dev.GetParentBusIndex();
		bus = parent->GetDeviceBus(busIndex);
		bus->RemoveChildDevice(&dev);
	}

	ATDeviceInfo devInfo;
	dev.GetDeviceInfo(devInfo);
	
	const VDStringA configTag(devInfo.mpDef->mpConfigTag);

	RemoveDevice(&dev);

	devholder.clear();

	while(!childDevices.empty()) {
		IATDevice *child = childDevices.back();
		childDevices.pop_back();

		RemoveDevice(child);
	}

	IATDevice *newChild = AddDevice(configTag.c_str(), pset, parent != nullptr);

	if (bus && newChild) {
		bus->AddChildDevice(newChild);
	}

	MarkAndSweep(nullptr, 0, childDevices);

	while(!childDevices.empty()) {
		IATDevice *child = childDevices.back();
		childDevices.pop_back();

		RemoveDevice(child);
	}
}

void ATDeviceManager::MarkAndSweep(IATDevice *const *pExcludedDevs, size_t numExcludedDevs, vdfastvector<IATDevice *>& garbage) {
	vdhashset<IATDevice *> devSet;

	for(auto it = mDevices.begin(), itEnd = mDevices.end();
		it != itEnd;
		++it)
	{
		if (it->mbChild)
			devSet.insert(it->mpDevice);
	}

	for(auto it = mDevices.begin(), itEnd = mDevices.end();
		it != itEnd;
		++it)
	{
		if (!it->mbChild)
			Mark(it->mpDevice, pExcludedDevs, numExcludedDevs, devSet);
	}

	for(size_t i=0; i<numExcludedDevs; ++i) {
		devSet.erase(pExcludedDevs[i]);
	}

	for(auto it = devSet.begin(), itEnd = devSet.end();
		it != itEnd;
		++it)
	{
		garbage.push_back(*it);
	}
}

void ATDeviceManager::RegisterExtendedCommand(IATDeviceXCmd& extCmd) {
	mExtCmds.emplace_back(&extCmd);
}

vdfastvector<IATDeviceXCmd *> ATDeviceManager::GetExtendedCommandsForDevice(IATDevice *dev, int busIndex) const {
	vdfastvector<IATDeviceXCmd *> devExtCmds;

	for(const auto& extCmd : mExtCmds) {
		if (extCmd->IsSupported(dev, busIndex))
			devExtCmds.push_back(extCmd);
	}

	return devExtCmds;
}

void ATDeviceManager::LoadSettings(VDRegistryKey& key) {
	mNVRAMs.clear();

	VDRegistryValueIterator it(key);
	while(const char *name = it.Next()) {
		const int len = key.getBinaryLength(name);

		if (len > 0) {
			vdfastvector<uint8> buf(len, 0);

			if (key.getBinary(name, (char *)buf.data(), len)) {
				NVRAM& nvblob = mNVRAMs.insert_as(name).first->second;
				nvblob.mBlobVal = std::move(buf);
				nvblob.mbIsInt = false;
			}
		} else {
			if (key.getValueType(name) == VDRegistryKey::kTypeInt) {
				NVRAM& nvblob = mNVRAMs.insert_as(name).first->second;
				nvblob.mIntVal = key.getInt(name);
				nvblob.mbIsInt = true;
			}
		}
	}

	for(IATDeviceStorage *storage : mStorage)
		storage->LoadStorage(*this);
}

void ATDeviceManager::SaveSettings(VDRegistryKey& key) {
	for(IATDeviceStorage *storage : mStorage)
		storage->SaveStorage(*this);

	for(const auto& nvr : mNVRAMs) {
		if (nvr.second.mbIsInt)
			key.setInt(nvr.first.c_str(), nvr.second.mIntVal);
		else
			key.setBinary(nvr.first.c_str(), (const char *)nvr.second.mBlobVal.data(), (int)nvr.second.mBlobVal.size());
	}
}

class ATSaveStateDeviceNode final : public ATSnapExchangeObject<ATSaveStateDeviceNode, "ATSaveStateDeviceNode"> {
public:
	bool InitFrom(const ATDeviceManager& devMgr, IATDevice *dev, ATSnapshotContext& ctx);

	template<ATExchanger T>
	void Exchange(T& ex);

	VDStringW mParentBusTag;
	VDStringW mDeviceTag;
	vdrefptr<IATObjectState> mDeviceState;
	vdvector<vdrefptr<ATSaveStateDeviceNode>> mChildDevices;
};

bool ATSaveStateDeviceNode::InitFrom(const ATDeviceManager& devMgr, IATDevice *dev, ATSnapshotContext& ctx) {
	if (dev) {
		IATDeviceSnapshot *devSnap = vdpoly_cast<IATDeviceSnapshot *>(dev);
		if (devSnap)
			mDeviceState = devSnap->SaveState(ctx);

		ATDeviceInfo devInfo;
		dev->GetDeviceInfo(devInfo);

		mDeviceTag = VDTextAToW(devInfo.mpDef->mpTag);

		IATDeviceParent *devParent = vdpoly_cast<IATDeviceParent *>(dev);
		if (devParent) {
			uint32 busIndex = 0;
			for(;;) {
				IATDeviceBus *bus = devParent->GetDeviceBus(busIndex);
				if (!bus)
					break;

				const VDStringW busTag(VDTextAToW(bus->GetBusTag()));

				vdfastvector<IATDevice *> children;
				bus->GetChildDevices(children);

				for(IATDevice *child : children) {
					vdrefptr devState { new ATSaveStateDeviceNode };

					if (devState->InitFrom(devMgr, child, ctx)) {
						devState->mParentBusTag = busTag;

						mChildDevices.emplace_back(std::move(devState));
					}
				}

				++busIndex;
			}
		}
	} else {
		for(IATDevice *rootDev : devMgr.GetDevices(true, false, false)) {
			vdrefptr devState { new ATSaveStateDeviceNode };

			if (devState->InitFrom(devMgr, rootDev, ctx))
				mChildDevices.emplace_back(std::move(devState));
		}
	}

	return mDeviceState || !mChildDevices.empty();
}

template<ATExchanger T>
void ATSaveStateDeviceNode::Exchange(T& ex) {
	ex.Transfer("tag", &mDeviceTag);
	ex.Transfer("state", &mDeviceState);
	ex.Transfer("bus_tag", &mParentBusTag);
	ex.Transfer("child_devices", &mChildDevices);
}

class ATSaveStateDeviceManager final : public ATSnapExchangeObject<ATSaveStateDeviceManager, "ATSaveStateDeviceManager"> {
public:
	void InitFrom(const ATDeviceManager& dm, ATSnapshotContext& ctx);

	template<ATExchanger T>
	void Exchange(T& ex);

	vdrefptr<ATSaveStateDeviceNode> mpRootNode;
};

void ATSaveStateDeviceManager::InitFrom(const ATDeviceManager& dm, ATSnapshotContext& ctx) {
	mpRootNode = new ATSaveStateDeviceNode;
	mpRootNode->InitFrom(dm, nullptr, ctx);
}

template<ATExchanger T>
void ATSaveStateDeviceManager::Exchange(T& ex) {
	ex.Transfer("root_node", &mpRootNode);
}

void ATDeviceManager::GetSnapshotStatus(ATSnapshotStatus& status) const {
	for(IATDevice *dev : GetDevices(false, false, false)) {
		IATDeviceSnapshot *devSnap = vdpoly_cast<IATDeviceSnapshot *>(dev);

		if (devSnap)
			devSnap->GetSnapshotStatus(status);
	}
}

void ATDeviceManager::LoadState(const IATObjectState *state) {
	ATSnapshotContext ctx;

	if (!state) {
		for(IATDevice *dev : GetDevices(false, false, false)) {
			IATDeviceSnapshot *devSnap = vdpoly_cast<IATDeviceSnapshot *>(dev);

			if (devSnap)
				devSnap->LoadState(nullptr, ctx);
		}

		return;
	}
	
	const auto LoadDevice = [this, &ctx](const auto& self, IATDevice& dev, const VDStringW& parentBusTag, auto& states) -> void {
		ATDeviceInfo info;
		dev.GetDeviceInfo(info);

		VDStringW tag(VDTextAToW(info.mpDef->mpTag));

		auto it = std::find_if(states.begin(), states.end(),
			[&tag, &parentBusTag](const ATSaveStateDeviceNode *devState) {
				return devState->mDeviceTag == tag && devState->mParentBusTag == parentBusTag;
			}
		);

		vdrefptr<ATSaveStateDeviceNode> devState;

		if (it != states.end()) {
			devState = std::move(*it);
			states.erase(it);
		}

		IATDeviceParent *devParent = vdpoly_cast<IATDeviceParent *>(&dev);
		if (devParent) {
			vdvector<vdrefptr<ATSaveStateDeviceNode>> childStates;

			if (devState)
				childStates = devState->mChildDevices;

			uint32 busIndex = 0;
			for(;;) {
				IATDeviceBus *bus = devParent->GetDeviceBus(busIndex);
				if (!bus)
					break;

				VDStringW parentBusTag = VDTextAToW(bus->GetBusTag());

				vdfastvector<IATDevice *> children;
				bus->GetChildDevices(children);

				for(IATDevice *child : children)
					self(self, *child, parentBusTag, childStates);

				++busIndex;
			}
		}

		IATDeviceSnapshot *devSnap = vdpoly_cast<IATDeviceSnapshot *>(&dev);
		if (devSnap)
			devSnap->LoadState(devState ? devState->mDeviceState : nullptr, ctx);
	};

	auto& dmState = atser_cast<const ATSaveStateDeviceManager&>(*state);
	if (!dmState.mpRootNode)
		throw ATInvalidSaveStateException();

	auto devStates = dmState.mpRootNode->mChildDevices;
	for(IATDevice *dev : GetDevices(true, false, false)) {
		LoadDevice(LoadDevice, *dev, VDStringW(), devStates);
	}
}

vdrefptr<IATObjectState> ATDeviceManager::SaveState() const {
	vdrefptr<ATSaveStateDeviceManager> state { new ATSaveStateDeviceManager };

	ATSnapshotContext ctx;
	state->InitFrom(*this, ctx);

	return state;
}

void *ATDeviceManager::GetService(uint32 iid) {
	auto it = mServices.find(iid);

	return it != mServices.end() ? it->second : nullptr;
}

void ATDeviceManager::NotifyDeviceStatusChanged(IATDevice& dev) {
	mStatusHandlers.NotifyAll(
		[&dev](const auto *fn) {
			(*fn)(dev);
		}
	);
}

void ATDeviceManager::RegisterDeviceStorage(IATDeviceStorage& storage) {
	mStorage.push_back(&storage);
}

void ATDeviceManager::UnregisterDeviceStorage(IATDeviceStorage& storage) {
	auto it = std::find(mStorage.begin(), mStorage.end(), &storage);

	if (it != mStorage.end()) {
		*it = mStorage.back();
		mStorage.pop_back();
	}
}

bool ATDeviceManager::LoadNVRAM(const char *name, void *buf, size_t len) {
	const auto it = mNVRAMs.find_as(name);

	if (it == mNVRAMs.end())
		return false;

	const NVRAM& nvram = it->second;
	if (nvram.mbIsInt)
		return false;

	const size_t avail = nvram.mBlobVal.size();
	const size_t tc = std::min<size_t>(len, avail);

	if (tc)
		memcpy(buf, nvram.mBlobVal.data(), tc);

	return true;
}

bool ATDeviceManager::LoadNVRAMInt(const char *name, sint32& val) {
	const auto it = mNVRAMs.find_as(name);

	if (it == mNVRAMs.end())
		return false;

	const NVRAM& nvram = it->second;
	if (!nvram.mbIsInt)
		return false;

	val = nvram.mIntVal;
	return true;
}

void ATDeviceManager::SaveNVRAM(const char *name, const void *buf, size_t len) {
	auto& nvram = mNVRAMs.insert_as(name).first->second;

	nvram.mBlobVal.assign((const uint8 *)buf, (const uint8 *)buf + len);
	nvram.mbIsInt = false;
}

void ATDeviceManager::SaveNVRAMInt(const char *name, sint32 val) {
	auto& nvram = mNVRAMs.insert_as(name).first->second;

	nvram.mIntVal = val;
	nvram.mbIsInt = true;
}

auto ATDeviceManager::GetInterfaceList(uint32 iid, bool rootOnly, bool visibleOnly, bool externalOnly) const -> const InterfaceList * {
	auto r = mInterfaceListCache.insert(
		iid
		+ (rootOnly ? UINT64_C(1) << 32 : UINT64_C(0))
		+ (visibleOnly ? UINT64_C(1) << 33 : UINT64_C(0))
		+ (externalOnly ? UINT64_C(1) << 34 : UINT64_C(0))
	);
	InterfaceList& ilist = r.first->second;

	if (r.second) {
		for(const DeviceEntry& de : mDevices) {
			if ((!rootOnly || !de.mbChild) && (!visibleOnly || !de.mbHidden) && (!externalOnly || !de.mbInternal)) {
				void *p = iid ? de.mpDevice->AsInterface(iid) : de.mpDevice;

				if (p)
					ilist.push_back(p);
			}
		}
	}

	return &ilist;
}

void ATDeviceManager::Mark(IATDevice *dev, IATDevice *const *pExcludedDevs, size_t numExcludedDevs, vdhashset<IATDevice *>& devSet) {
	for(size_t i=0; i<numExcludedDevs; ++i) {
		if (dev == pExcludedDevs[i])
			return;
	}

	auto *parent = vdpoly_cast<IATDeviceParent *>(dev);
	if (!parent)
		return;

	for(uint32 busIndex = 0; ; ++busIndex) {
		IATDeviceBus *bus = parent->GetDeviceBus(busIndex);
		if (!bus)
			break;

		vdfastvector<IATDevice *> children;
		bus->GetChildDevices(children);

		while(!children.empty()) {
			IATDevice *child = children.back();

			auto it = devSet.find(child);
			if (it != devSet.end()) {
				devSet.erase(it);
				Mark(child, pExcludedDevs, numExcludedDevs, devSet);
			}

			children.pop_back();
		}
	}
}

namespace {
	class StringWriter : public IVDJSONWriterOutput {
	public:
		StringWriter(VDStringW& str) : mStr(str) {}

		virtual void WriteChars(const wchar_t *src, uint32 len) {
			mStr.append(src, src+len);
		}

		VDStringW& mStr;
	};
}

void ATDeviceManager::SerializeDevice(IATDevice *dev, VDStringW& str, bool compact, bool includeChildren) const {
	StringWriter stringWriter(str);

	VDJSONWriter writer;
	writer.Begin(&stringWriter, compact);
	SerializeDevice(dev, writer, includeChildren);
	writer.End();
}

void ATDeviceManager::SerializeDevice(IATDevice *dev, VDJSONWriter& out, bool includeChildren) const {
	if (dev) {
		ATDeviceInfo devInfo;
		dev->GetDeviceInfo(devInfo);

		out.OpenObject();
		out.WriteMemberName(L"tag");
		VDStringW tagStr = VDTextAToW(devInfo.mpDef->mpTag);

		// If this is an internal device, prefix the tag with _ so no previous version attempts
		// to create it.
		if (devInfo.mpDef->mFlags & kATDeviceDefFlag_Internal)
			tagStr.insert(tagStr.begin(), L'_');

		out.WriteString(tagStr.c_str());

		ATPropertySet pset;
		dev->GetSettings(pset);

		if (!pset.IsEmpty()) {
			out.WriteMemberName(L"params");

			SerializeProps(pset, out);
		}

		IATDeviceParent *dp = vdpoly_cast<IATDeviceParent *>(dev);
		if (dp && includeChildren) {
			bool busesOpen = false;
			uint32 busIndex = 0;
			vdfastvector<IATDevice *> children;

			for(;;) {
				IATDeviceBus *bus = dp->GetDeviceBus(busIndex);

				if (!bus)
					break;

				children.clear();
				bus->GetChildDevices(children);

				if (!children.empty()) {
					if (!busesOpen) {
						busesOpen = true;

						out.WriteMemberName(L"buses");
						out.OpenObject();
					}

					VDStringW busName;
					busName.sprintf(L"%u", busIndex);
					out.WriteMemberName(busName.c_str());
					out.OpenObject();

					out.WriteMemberName(L"children");
					out.OpenArray();

					for(auto it = children.begin(), itEnd = children.end();
						it != itEnd;
						++it)
					{
						SerializeDevice(*it, out, true);
					}

					out.Close();
					out.Close();
				}

				++busIndex;
			}

			if (busesOpen)
				out.Close();
		}

		out.Close();
	} else {
		out.OpenArray();

		for(IATDevice *child : GetDevices(true, true, false))
			SerializeDevice(child, out, includeChildren);

		out.Close();
	}
}

void ATDeviceManager::SerializeProps(const ATPropertySet& props, VDStringW& str) const {
	StringWriter stringWriter(str);

	VDJSONWriter writer;
	writer.Begin(&stringWriter, true);
	SerializeProps(props, writer);
	writer.End();
}

void ATDeviceManager::SerializeProps(const ATPropertySet& pset, VDJSONWriter& out) const {
	out.OpenObject();

	pset.EnumProperties(
		[&out](const char *name, const ATPropertyValue& val) {
			out.WriteMemberName(VDTextAToW(name).c_str());

			switch(val.mType) {
				case kATPropertyType_Bool:
					out.WriteBool(val.mValBool);
					break;

				case kATPropertyType_Int32:
					out.WriteReal((double)val.mValI32);
					break;

				case kATPropertyType_Uint32:
					out.WriteReal((double)val.mValU32);
					break;

				case kATPropertyType_Float:
					out.WriteReal((double)val.mValF);
					break;

				case kATPropertyType_Double:
					out.WriteReal(val.mValD);
					break;

				case kATPropertyType_String16:
					out.WriteString(val.mValStr16);
					break;

				default:
					out.WriteNull();
					break;
			}
		}
	);

	out.Close();
}

void ATDeviceManager::DeserializeDevices(IATDeviceParent *parent, IATDeviceBus *bus, const wchar_t *str) {
	VDJSONDocument doc;
	VDJSONReader reader;
	if (!reader.Parse(str, wcslen(str)*sizeof(wchar_t), doc))
		return;

	const auto& rootVal = doc.Root();
	if (doc.mValue.mType == VDJSONValue::kTypeObject) {
		DeserializeDevice(parent, nullptr, rootVal);
	} else if (doc.mValue.mType == VDJSONValue::kTypeArray) {
		size_t n = rootVal.GetArrayLength();

		for(size_t i=0; i<n; ++i)
			DeserializeDevice(parent, nullptr, rootVal[i]);
	}
}

void ATDeviceManager::DeserializeDevice(IATDeviceParent *parent, IATDeviceBus *bus, const VDJSONValueRef& node) {
	const wchar_t *tag = node["tag"].AsString();
	if (!*tag)
		return;

	ATPropertySet pset;

	DeserializeProps(pset, node["params"]);

	auto tag2 = VDTextWToA(tag);
	if (tag2.empty())
		return;

	bool internal = false;
	if (tag2[0] == '_') {
		tag2.erase(tag2.begin());
		internal = true;
	}

	const ATDeviceDefinition *def = GetDeviceDefinition(tag2.c_str());
	IATDevice *dev = nullptr;

	if (!def)
		return;

	if (def->mFlags & kATDeviceDefFlag_Internal) {
		// This device is an internal device, so:
		// - we cannot add it if it doesn't already exist
		// - it cannot be a child device
		// - the serialized tag should start with _ to indicate internal

		if (bus || !internal)
			return;

		dev = GetDeviceByTag(tag2.c_str());

		if (dev) {
			VDVERIFY(dev->SetSettings(pset));
		}
	} else {
		if (internal)
			return;

		try {
			dev = AddDevice(def, pset, bus != nullptr || parent != nullptr);
		} catch(const MyError&) {
			return;
		}
	}

	if (!dev)
		return;
	
	if (bus) {
		bus->AddChildDevice(dev);

		if (!dev->GetParent())
			return;
	} else if (parent) {
		for(uint32 busIndex = 0; ; ++busIndex) {
			IATDeviceBus *tryBus = parent->GetDeviceBus(busIndex);

			if (!tryBus)
				break;

			tryBus->AddChildDevice(dev);
			if (dev->GetParent())
				break;
		}

		if (!dev->GetParent())
			return;
	}

	IATDeviceParent *devParent = vdpoly_cast<IATDeviceParent *>(dev);
	if (devParent) {
		auto buses = node["buses"];
		if (buses.IsObject()) {
			for(const auto& busEntry : buses.AsObject()) {
				const wchar_t *busIndexStr = busEntry.GetName();
				unsigned busIndex;
				wchar_t dummy;

				if (1 == swscanf(busIndexStr, L"%u%lc", &busIndex, &dummy)) {
					IATDeviceBus *deviceBus = devParent->GetDeviceBus(busIndex);

					if (deviceBus) {
						auto busNode = busEntry.GetValue();

						if (busNode.IsObject()) {
							auto children = busNode["children"];
							if (children.IsArray()) {
								size_t numChildren = children.GetArrayLength();
								for(size_t i=0; i<numChildren; ++i) {
									DeserializeDevice(devParent, deviceBus, children[i]);
								}
							}
						}
					}
				}
			}
		} else {
			auto children = node["children"];
			if (children.IsArray()) {
				size_t numChildren = children.GetArrayLength();
				for(size_t i=0; i<numChildren; ++i) {
					DeserializeDevice(devParent, nullptr, children[i]);
				}
			}
		}
	}
}

void ATDeviceManager::DeserializeProps(ATPropertySet& props, const wchar_t *str) {
	VDJSONDocument doc;
	VDJSONReader reader;
	if (!reader.Parse(str, wcslen(str)*sizeof(wchar_t), doc))
		return;

	const auto& rootVal = doc.Root();
	DeserializeProps(props, rootVal);
}

void ATDeviceManager::DeserializeProps(ATPropertySet& pset, const VDJSONValueRef& val) {
	for(const auto& propEntry : val.AsObject()) {
		const VDStringA& name = VDTextWToA(propEntry.GetName());
		const auto& value = propEntry.GetValue();

		switch(value->mType) {
			case VDJSONValue::kTypeBool:
				pset.SetBool(name.c_str(), value.AsBool());
				break;

			case VDJSONValue::kTypeInt:
				pset.SetDouble(name.c_str(), (double)value.AsInt64());
				break;

			case VDJSONValue::kTypeReal:
				pset.SetDouble(name.c_str(), value.AsDouble());
				break;

			case VDJSONValue::kTypeString:
				pset.SetString(name.c_str(), value.AsString());
				break;
		}
	}
}

void ATDeviceManager::RegisterService(uint32 iid, void *p) {
	mServices[iid] = p;
}

void ATDeviceManager::UnregisterService(uint32 iid) {
	mServices.erase(iid);
}
