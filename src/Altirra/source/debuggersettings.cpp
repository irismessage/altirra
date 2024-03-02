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

#include <stdafx.h>
#include <vd2/system/registry.h>
#include <at/atcore/enumparseimpl.h>
#include "debuggersettings.h"

////////////////////////////////////////////////////////////////////////////////

AT_DEFINE_ENUM_TABLE_BEGIN(ATDebugger816PredictionMode)
	{ ATDebugger816PredictionMode::Auto, "auto" },
	{ ATDebugger816PredictionMode::CurrentContext, "current_context" },
	{ ATDebugger816PredictionMode::M8X8, "m8x8" },
	{ ATDebugger816PredictionMode::M8X16, "m8x16" },
	{ ATDebugger816PredictionMode::M16X8, "m16x8" },
	{ ATDebugger816PredictionMode::M16X16, "m16x16" },
	{ ATDebugger816PredictionMode::Emulation, "emulation" },
AT_DEFINE_ENUM_TABLE_END(ATDebugger816PredictionMode, ATDebugger816PredictionMode::Auto)

////////////////////////////////////////////////////////////////////////////////

void ATDebuggerSettingBase::Load(const char *name, bool& value) {
	VDRegistryAppKey key("Settings", false);

	value = key.getBool(name, value);
}

void ATDebuggerSettingBase::Save(const char *name, const bool& value) {
	VDRegistryAppKey key("Settings", true);

	key.setBool(name, value);
}

uint32 ATDebuggerSettingBase::LoadEnum(const char *name, uint32 value, const ATEnumLookupTable& enumTable) {
	VDRegistryAppKey key("Settings", false);

	VDStringW s;
	if (!key.getString(name, s))
		return value;

	return ATParseEnum(enumTable, s).mValue;
}

void ATDebuggerSettingBase::SaveEnum(const char *name, uint32 value, const ATEnumLookupTable& enumTable) {
	VDRegistryAppKey key("Settings", true);

	key.setString(name, ATEnumToString(enumTable, value));
}

////////////////////////////////////////////////////////////////////////////////

template<typename T>
ATDebuggerSetting<T>::ATDebuggerSetting(const char *persistedName, const T& defaultValue)
	: mpPersistedName(persistedName)
	, mValue(defaultValue)
	, mbValueLoaded(false)
{
}

template<typename T>
ATDebuggerSetting<T>& ATDebuggerSetting<T>::operator=(const T& value) {
	if (mValue != value) {
		mValue = value;

		Save(mpPersistedName, mValue);

		for(vdlist_node *p : mViews) {
			auto *view = static_cast<ATDebuggerSettingView<T> *>(p);

			view->mLocalValue = mValue;

			if (view->mpChanged) {
				view->mpChanged();
			}
		}
	}

	return *this;
}

template<typename T>
ATDebuggerSetting<T>::operator T() {
	if (!mbValueLoaded)
		Load(mpPersistedName, mValue);

	return mValue;
}

template<typename T>
void ATDebuggerSetting<T>::AddView(vdlist_node& node) {
	if (!mbValueLoaded)
		Load(mpPersistedName, mValue);

	mViews.push_back(&node);

	ATDebuggerSettingView<T>& view = static_cast<ATDebuggerSettingView<T>&>(node);
	view.mLocalValue = mValue;
}

///////////////////////////////////////////////////////////////////////////

template<typename T>
ATDebuggerSettingView<T>::ATDebuggerSettingView() {
	mListNodeNext = this;
	mListNodePrev = this;
}

template<typename T>
ATDebuggerSettingView<T>::~ATDebuggerSettingView() {
	vdlist<vdlist_node>::unlink(*this);
}

template<typename T>
void ATDebuggerSettingView<T>::Attach(ATDebuggerSetting<T>& setting) {
	mpSetting = &setting;
	setting.AddView(*this);
}

template<typename T>
ATDebuggerSettingView<T>& ATDebuggerSettingView<T>::operator=(const T& value) {
	if (mpSetting)
		*mpSetting = value;

	return *this;
}

template class ATDebuggerSetting<bool>;
template class ATDebuggerSettingView<bool>;
template class ATDebuggerSetting<ATDebugger816PredictionMode>;
template class ATDebuggerSettingView<ATDebugger816PredictionMode>;

ATDebuggerSetting<bool> g_ATDbgSettingHistoryShowPC					("Debugger: Show PC in History", true);
ATDebuggerSetting<bool> g_ATDbgSettingHistoryShowGlobalPC			("Debugger: Show global PC in History", false);
ATDebuggerSetting<bool> g_ATDbgSettingHistoryShowRegisters			("Debugger: Show registers in History", true);
ATDebuggerSetting<bool> g_ATDbgSettingHistoryShowSpecialRegisters	("Debugger: Show special registers in History", false);
ATDebuggerSetting<bool> g_ATDbgSettingHistoryShowFlags				("Debugger: Show flags in History", true);
ATDebuggerSetting<bool> g_ATDbgSettingShowCodeBytes					("Debugger: Show code bytes", true);
ATDebuggerSetting<bool> g_ATDbgSettingShowLabels					("Debugger: Show labels", true);
ATDebuggerSetting<bool> g_ATDbgSettingShowLabelNamespaces			("Debugger: Show label namespaces", true);
ATDebuggerSetting<bool> g_ATDbgSettingShowProcedureBreaks			("Debugger: Show procedure breaks", true);
ATDebuggerSetting<bool> g_ATDbgSettingShowCallPreviews				("Debugger: Show call previews", true);
ATDebuggerSetting<bool> g_ATDbgSettingShowSourceInDisasm			("Debugger: Show source in disasm", false);
ATDebuggerSetting<bool> g_ATDbgSettingCollapseLoops					("Debugger: Collapse loops", true);
ATDebuggerSetting<bool> g_ATDbgSettingCollapseCalls					("Debugger: Collapse calls", true);
ATDebuggerSetting<bool> g_ATDbgSettingCollapseInterrupts			("Debugger: Collapse interrupts", true);
ATDebuggerSetting<ATDebugger816PredictionMode> g_ATDbgSetting816PredictionMode	("Debugger: 816 prediction mode", ATDebugger816PredictionMode::Auto);
