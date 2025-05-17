//	Altirra - Atari 800/800XL/5200 emulator
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

#ifndef f_AT_UIDEVICES_H
#define f_AT_UIDEVICES_H

#include <vd2/system/vdstl_hashset.h>
#include <vd2/system/vdstl_hashmap.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/uiproxies.h>

class IATDevice;
class IATDeviceXCmd;
class ATPropertySet;
class ATDeviceManager;

class ATUIControllerDevices final {
	ATUIControllerDevices(const ATUIControllerDevices&) = delete;
	ATUIControllerDevices& operator=(const ATUIControllerDevices&) = delete;
public:
	ATUIControllerDevices(VDDialogFrameW32& parent, ATDeviceManager& devMgr, VDUIProxyTreeViewControl& treeView, VDUIProxyButtonControl& settingsView, VDUIProxyButtonControl& removeView,
		VDUIProxyButtonControl& moreView);
	~ATUIControllerDevices();

	void OnDataExchange(bool write);
	void OnDpiChanged();
	void Add();
	void Remove();
	void RemoveAll();
	void Settings();
	void More();
	void Copy();
	void Paste();
	void CreateDeviceNode(VDUIProxyTreeViewControl::NodeRef parentNode, IATDevice *dev, const wchar_t *prefix);
	void SaveSettings(const char *configTag, const ATPropertySet& props);

private:
	struct DeviceNode;
	class XCmdRemove;
	class XCmdSettings;

	void OnItemSelectionChanged(VDUIProxyTreeViewControl *sender, int idx);
	void OnItemDoubleClicked(VDUIProxyTreeViewControl *sender, bool *handled);
	void OnItemGetDisplayAttributes(VDUIProxyTreeViewControl *sender, VDUIProxyTreeViewControl::GetDispAttrEvent *event);
	bool OnContextMenu(const VDUIProxyTreeViewControl::ContextMenuEvent& event);
	void OnDeviceStatusChanged(IATDevice& dev);

	void RefreshDeviceStatuses();
	void RefreshNodeErrors(DeviceNode& devnode);
	bool DisplayMore(const VDUIProxyTreeViewControl::ContextMenuEvent& event, bool fromButton);

	bool GetXCmdContext(IATDevice *&dev, sint32& busIndex);
	bool GetXCmdContext(VDUIProxyTreeViewControl::NodeRef selectedTreeNode, DeviceNode *selectedDeviceNode, IATDevice *&dev, sint32& busIndex);
	void ExecuteXCmd(IATDevice *dev, sint32 busIndex, IATDeviceXCmd& xcmd);

	void Remove(DeviceNode *p);
	void Settings(DeviceNode *p);
	void UpdateIcons();

	VDDialogFrameW32& mParent;
	ATDeviceManager& mDevMgr;
	VDUIProxyTreeViewControl& mTreeView;
	VDUIProxyButtonControl& mSettingsView;
	VDUIProxyButtonControl& mRemoveView;
	VDUIProxyButtonControl& mMoreView;

	vdhashmap<IATDevice *, vdrefptr<DeviceNode>> mDeviceNodeLookup;
	vdhashset<vdrefptr<IATDevice>> mDevicesToRefresh;
	bool mbDeviceRefreshQueued = false;

	VDDelegate mDelSelectionChanged;
	VDDelegate mDelDoubleClicked;
	VDDelegate mDelGetDisplayAttributes;
	vdfunction<void(IATDevice&)> mDeviceStatusCallback;
};

#endif
