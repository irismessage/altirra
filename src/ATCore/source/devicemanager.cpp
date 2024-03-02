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

#include <stdafx.h>
#include <vd2/system/error.h>
#include <vd2/system/refcount.h>
#include <vd2/system/unknown.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonvalue.h>
#include <vd2/vdjson/jsonwriter.h>
#include <at/atcore/device.h>
#include <at/atcore/devicesio.h>
#include <at/atcore/devicecio.h>
#include <at/atcore/devicemanager.h>
#include <at/atcore/deviceprinter.h>
#include <at/atcore/propertyset.h>

ATDeviceManager::ATDeviceManager() {
}

ATDeviceManager::~ATDeviceManager() {
}

void ATDeviceManager::Init() {
}

IATDevice *ATDeviceManager::AddDevice(const char *tag, const ATPropertySet& pset, bool child, bool hidden) {
	vdrefptr<IATDevice> dev;

	for(auto it = mDeviceFactories.begin(), itEnd = mDeviceFactories.end();
		it != itEnd;
		++it)
	{
		if (!strcmp(it->mpTag, tag)) {
			it->mpCreate(pset, ~dev);
			break;
		}
	}

	if (dev) {
		dev->SetSettings(pset);
		AddDevice(dev, child, hidden);
	}

	return dev;
}

void ATDeviceManager::AddDevice(IATDevice *dev, bool child, bool hidden) {
	try {
		mInterfaceListCache.clear();

		for(const auto& fn : mInitHandlers)
			fn(*dev);

		ATDeviceInfo info;
		dev->GetDeviceInfo(info);

		DeviceEntry ent = { dev, info.mpDef->mpTag, child, hidden };
		mDevices.push_back(ent);

		try {
			dev->AddRef();

			dev->Init();
		} catch(...) {
			mDevices.pop_back();
			dev->Release();
			throw;
		}
	} catch(...) {
		dev->Shutdown();
		throw;
	}

	dev->ColdReset();

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
				// scan all possible parents
				for(auto it2 = mDevices.begin(), it2End = mDevices.end();
					it2 != it2End;
					++it2)
				{
					IATDeviceParent *parent = vdpoly_cast<IATDeviceParent *>(it2->mpDevice);

					if (parent)
						parent->RemoveChildDevice(dev);
				}
			}

			mDevices.erase(it);

			++mChangeCounter;

			for(const auto& changeClass : mChangeCallbacks) {
				const uint32 iid = changeClass.first;
				void *iface = dev->AsInterface(iid);

				if (iface) {
					for(IATDeviceChangeCallback *cb : changeClass.second)
						cb->OnDeviceRemoved(iid, dev, iface);
				}
			}

			dev->Shutdown();
			dev->Release();
			break;
		}
	}
}

void ATDeviceManager::RemoveAllDevices(bool includeHidden) {
	auto devs = GetDevices(false, !includeHidden);
	vdfastvector<IATDevice *> devices(devs.begin(), devs.end());

	for(IATDevice *dev : devices)
		RemoveDevice(dev);
}

void ATDeviceManager::ToggleDevice(const char *tag) {
	IATDevice *dev = GetDeviceByTag(tag);

	if (dev)
		RemoveDevice(dev);
	else
		AddDevice(tag, ATPropertySet(), false, false);
}

uint32 ATDeviceManager::GetDeviceCount() const {
	return mDevices.size();
}

IATDevice *ATDeviceManager::GetDeviceByTag(const char *tag) const {
	for(auto it = mDevices.begin(), itEnd = mDevices.end();
		it != itEnd;
		++it)
	{
		if (!strcmp(it->mpTag, tag))
			return it->mpDevice;
	}

	return nullptr;
}

IATDevice *ATDeviceManager::GetDeviceByIndex(uint32 i) const {
	return i < mDevices.size() ? mDevices[i].mpDevice : nullptr;
}

void *ATDeviceManager::GetInterface(uint32 id) const {
	for(const auto& entry : mDevices) {
		void *p = entry.mpDevice->AsInterface(id);

		if (p)
			return p;
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

void ATDeviceManager::AddDeviceFactory(const char *tag, ATDeviceFactoryFn factory) {
	auto& fac = mDeviceFactories.push_back();

	fac.mpTag = tag;
	fac.mpCreate = factory;
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

auto ATDeviceManager::GetInterfaceList(uint32 iid, bool rootOnly, bool visibleOnly) const -> const InterfaceList * {
	auto r = mInterfaceListCache.insert(iid + (rootOnly ? UINT64_C(1) << 32 : UINT64_C(0)) + (visibleOnly ? UINT64_C(1) << 33 : UINT64_C(0)));
	InterfaceList& ilist = r.first->second;

	if (r.second) {
		if (iid) {
			for(auto it = mDevices.begin(), itEnd = mDevices.end();
				it != itEnd;
				++it)
			{
				if ((!rootOnly || !it->mbChild) && (!visibleOnly || !it->mbHidden)) {
					void *p = it->mpDevice->AsInterface(iid);

					if (p)
						ilist.push_back(p);
				}
			}
		} else {
			for(auto it = mDevices.begin(), itEnd = mDevices.end();
				it != itEnd;
				++it)
			{
				if ((!rootOnly || !it->mbChild) && (!visibleOnly || !it->mbHidden))
					ilist.push_back(it->mpDevice);
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
	if (parent) {
		vdfastvector<IATDevice *> children;
		parent->GetChildDevices(children);

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

void ATDeviceManager::SerializeDevice(IATDevice *dev, VDStringW& str) const {
	StringWriter stringWriter(str);

	VDJSONWriter writer;
	writer.Begin(&stringWriter, true);
	SerializeDevice(dev, writer);
	writer.End();
}

void ATDeviceManager::SerializeDevice(IATDevice *dev, VDJSONWriter& out) const {
	if (dev) {
		ATDeviceInfo devInfo;
		dev->GetDeviceInfo(devInfo);

		out.OpenObject();
		out.WriteMemberName(L"tag");
		out.WriteString(VDTextAToW(devInfo.mpDef->mpTag).c_str());

		ATPropertySet pset;
		dev->GetSettings(pset);

		if (!pset.IsEmpty()) {
			out.WriteMemberName(L"params");

			SerializeProps(pset, out);
		}

		IATDeviceParent *dp = vdpoly_cast<IATDeviceParent *>(dev);
		if (dp) {
			vdfastvector<IATDevice *> children;
			dp->GetChildDevices(children);

			if (!children.empty()) {
				out.WriteMemberName(L"children");
				out.OpenArray();

				for(auto it = children.begin(), itEnd = children.end();
					it != itEnd;
					++it)
				{
					SerializeDevice(*it, out);
				}

				out.Close();
			}
		}

		out.Close();
	} else {
		out.OpenArray();

		for(IATDevice *child : GetDevices(true, true))
			SerializeDevice(child, out);

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

void ATDeviceManager::DeserializeDevices(IATDeviceParent *parent, const wchar_t *str) {
	VDJSONDocument doc;
	VDJSONReader reader;
	if (!reader.Parse(str, wcslen(str)*sizeof(wchar_t), doc))
		return;

	const auto& rootVal = doc.Root();
	if (doc.mValue.mType == VDJSONValue::kTypeObject) {
		DeserializeDevice(parent, rootVal);
	} else if (doc.mValue.mType == VDJSONValue::kTypeArray) {
		size_t n = rootVal.GetArrayLength();

		for(size_t i=0; i<n; ++i)
			DeserializeDevice(parent, rootVal[i]);
	}
}

void ATDeviceManager::DeserializeDevice(IATDeviceParent *parent, const VDJSONValueRef& node) {
	const wchar_t *tag = node["tag"].AsString();
	if (!*tag)
		return;

	ATPropertySet pset;

	DeserializeProps(pset, node["params"]);

	IATDevice *dev;
	try {
		dev = AddDevice(VDTextWToA(tag).c_str(), pset, parent != nullptr, false);
	} catch(const MyError&) {
		return;
	}

	if (dev && parent)
		parent->AddChildDevice(dev);

	IATDeviceParent *devParent = vdpoly_cast<IATDeviceParent *>(dev);
	if (devParent) {
		auto children = node["children"];
		size_t numChildren = children.GetArrayLength();
		for(size_t i=0; i<numChildren; ++i) {
			DeserializeDevice(devParent, children[i]);
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
	auto params = val.AsObject();
	while (params.IsValid()) {
		const VDStringA& name = VDTextWToA(params.GetName());
		const auto& value = params.GetValue();

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

		++params;
	}
}
