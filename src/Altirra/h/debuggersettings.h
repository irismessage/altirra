//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2019 Avery Lee
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

#ifndef f_AT_DEBUGGERSETTINGS_H
#define f_AT_DEBUGGERSETTINGS_H

#include <vd2/system/vdstl.h>
#include <at/atcore/enumparse.h>

class ATDebuggerSettingBase {
protected:
	static void Load(const char *name, bool& value);
	static void Save(const char *name, const bool& value);

	template<typename T, typename = typename std::enable_if<std::is_enum_v<T>>::type>
	static void Load(const char *name, T& value) {
		value = (T)LoadEnum(name, (uint32)value, ATGetEnumLookupTable<T>());
	}

	template<typename T, typename = typename std::enable_if<std::is_enum_v<T>>::type>
	static void Save(const char *name, const T& value) {
		SaveEnum(name, (uint32)value, ATGetEnumLookupTable<T>());
	}

	static uint32 LoadEnum(const char *name, uint32 value, const ATEnumLookupTable& enumTable);
	static void SaveEnum(const char *name, uint32 value, const ATEnumLookupTable& enumTable);
};

template<typename T>
class ATDebuggerSetting final : public ATDebuggerSettingBase {
	ATDebuggerSetting(const ATDebuggerSetting&) = delete;
	ATDebuggerSetting& operator=(const ATDebuggerSetting&) = delete;
public:
	ATDebuggerSetting(const char *persistedName, const T& defaultValue);

	ATDebuggerSetting& operator=(const T& value);

	operator T();

	void AddView(vdlist_node& node);

private:
	const char *mpPersistedName;
	vdlist<vdlist_node> mViews;
	T mValue;
	bool mbValueLoaded;
};

template<typename T>
class ATDebuggerSettingView final : public vdlist_node {
	ATDebuggerSettingView(const ATDebuggerSettingView&) = delete;
	ATDebuggerSettingView& operator=(const ATDebuggerSettingView&) = delete;
public:
	ATDebuggerSettingView();
	~ATDebuggerSettingView();

	void Attach(ATDebuggerSetting<T>& setting);

	template<typename Fn>
	void Attach(ATDebuggerSetting<T>& setting, Fn fn);

	operator T() const { return mLocalValue; }
	ATDebuggerSettingView& operator=(const T&);

private:
	friend class ATDebuggerSetting<T>;
	T mLocalValue {};
	ATDebuggerSetting<T> *mpSetting = nullptr;
	vdfunction<void()> mpChanged;
};

template<typename T>
template<typename Fn>
void ATDebuggerSettingView<T>::Attach(ATDebuggerSetting<T>& setting, Fn fn) {
	mpChanged = std::move(fn);
	Attach(setting);
}

extern ATDebuggerSetting<bool> g_ATDbgSettingHistoryShowPC;
extern ATDebuggerSetting<bool> g_ATDbgSettingHistoryShowGlobalPC;
extern ATDebuggerSetting<bool> g_ATDbgSettingHistoryShowRegisters;
extern ATDebuggerSetting<bool> g_ATDbgSettingHistoryShowSpecialRegisters;
extern ATDebuggerSetting<bool> g_ATDbgSettingHistoryShowFlags;
extern ATDebuggerSetting<bool> g_ATDbgSettingShowCodeBytes;
extern ATDebuggerSetting<bool> g_ATDbgSettingShowLabels;
extern ATDebuggerSetting<bool> g_ATDbgSettingShowLabelNamespaces;
extern ATDebuggerSetting<bool> g_ATDbgSettingShowProcedureBreaks;
extern ATDebuggerSetting<bool> g_ATDbgSettingShowCallPreviews;
extern ATDebuggerSetting<bool> g_ATDbgSettingShowSourceInDisasm;
extern ATDebuggerSetting<bool> g_ATDbgSettingCollapseLoops;
extern ATDebuggerSetting<bool> g_ATDbgSettingCollapseCalls;
extern ATDebuggerSetting<bool> g_ATDbgSettingCollapseInterrupts;

enum class ATDebugger816MXPredictionMode : uint8 {
	Auto,
	CurrentContext,
	M8X8,
	M8X16,
	M16X8,
	M16X16,
	Emulation,
};

AT_DECLARE_ENUM_TABLE(ATDebugger816MXPredictionMode);

extern ATDebuggerSetting<ATDebugger816MXPredictionMode> g_ATDbgSetting816MXPredictionMode;
extern ATDebuggerSetting<bool> g_ATDbgSetting816PredictD;

#endif
