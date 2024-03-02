//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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

#ifndef f_AT_UICONFGENERIC_H
#define f_AT_UICONFGENERIC_H

#include <vd2/system/refcount.h>

class IATUIConfigPropView {
public:
	virtual IATUIConfigPropView& SetTag(const char *tag) = 0;
	virtual IATUIConfigPropView& SetLabel(const wchar_t *label) = 0;
	virtual IATUIConfigPropView& SetHelp(const wchar_t *text) = 0;
	virtual IATUIConfigPropView& SetHelp(const wchar_t *caption, const wchar_t *text) = 0;
	virtual IATUIConfigPropView& SetEnableExpr(vdfunction<bool()> fn) = 0;
};

///////////////////////////////////////////////////////////////////////////

class IATUIConfigBoolView {
public:
	virtual IATUIConfigPropView *operator->() = 0;
	virtual IATUIConfigBoolView& SetDefault(bool val) = 0;
	virtual bool GetValue() const = 0;
	virtual void SetValue(bool v) = 0;
};

class IATUIConfigIntView {
public:
	virtual IATUIConfigPropView *operator->() = 0;
	virtual IATUIConfigIntView& SetDefault(sint32 val) = 0;
	virtual sint32 GetValue() const = 0;
	virtual void SetValue(sint32 v) = 0;
};

class IATUIConfigStringView {
public:
	virtual IATUIConfigPropView *operator->() = 0;
	virtual const wchar_t *GetValue() const = 0;
	virtual void SetValue(const wchar_t *s) = 0;
};

///////////////////////////////////////////////////////////////////////////

class IATUIConfigSliderView {
public:
	virtual IATUIConfigIntView *operator->() = 0;
	virtual IATUIConfigSliderView& SetRange(sint32 minVal, sint32 maxVal) = 0;
	virtual IATUIConfigSliderView& SetPage(sint32 pageSize) = 0;
};

class IATUIConfigPathView {
public:
	virtual IATUIConfigStringView& AsStringView() = 0;

	virtual IATUIConfigPathView& SetBrowseCaption(const wchar_t *caption) = 0;
	virtual IATUIConfigPathView& SetBrowseKey(uint32 key) = 0;
	virtual IATUIConfigPathView& SetSave() = 0;
	virtual IATUIConfigPathView& SetType(const wchar_t *filter, const wchar_t *ext) = 0;
	virtual IATUIConfigPathView& SetTypeImage() = 0;
};

///////////////////////////////////////////////////////////////////////////

class IATUIConfigView {
public:
	virtual IATUIConfigBoolView& AddCheckbox() = 0;
	virtual IATUIConfigPathView& AddPath() = 0;

	virtual void Read(const ATPropertySet& pset) = 0;
	virtual void Write(ATPropertySet& pset) const = 0;
};

class IATUIConfigController {
public:
	virtual void BuildDialog(IATUIConfigView& view) = 0;
};

bool ATUIShowDialogGenericConfig(VDGUIHandle h, IATUIConfigController& controller);
bool ATUIShowDialogGenericConfig(VDGUIHandle h, ATPropertySet& pset, const wchar_t *name, vdfunction<void(IATUIConfigView&)> fn);

#endif
