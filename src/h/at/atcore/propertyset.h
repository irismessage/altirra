//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2022 Avery Lee
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

#ifndef f_AT_ATCORE_PROPERTYSET_H
#define f_AT_ATCORE_PROPERTYSET_H

#include <vd2/system/hash.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdstl_hashmap.h>
#include <vd2/system/VDString.h>

enum ATPropertyType {
	kATPropertyType_None,
	kATPropertyType_Bool,
	kATPropertyType_Int32,
	kATPropertyType_Uint32,
	kATPropertyType_Float,
	kATPropertyType_Double,
	kATPropertyType_String16
};

struct ATPropertyValue {
	ATPropertyType mType;

	union {
		bool mValBool;
		sint32 mValI32;
		uint32 mValU32;
		float mValF;
		double mValD;
		wchar_t *mValStr16;
	};
};

class ATPropertySet {
public:
	ATPropertySet();
	vdnothrow ATPropertySet(ATPropertySet&&) noexcept;
	ATPropertySet(const ATPropertySet&);
	~ATPropertySet();

	ATPropertySet& operator=(const ATPropertySet&);
	vdnothrow ATPropertySet& operator=(ATPropertySet&&) noexcept;

	[[nodiscard]] bool IsEmpty() const { return mProperties.empty(); }

	void Clear();

	template<class T>
	void EnumProperties(const T& functor) const;

	void EnumProperties(void (*fn)(const char *name, const ATPropertyValue& val, void *data), void *data) const;

	void Unset(const char *name);

	void SetBool(const char *name, bool val);
	void SetInt32(const char *name, sint32 val);
	void SetUint32(const char *name, uint32 val);
	void SetFloat(const char *name, float val);
	void SetDouble(const char *name, double val);
	void SetString(const char *name, const wchar_t *val);

	[[nodiscard]] bool GetBool(const char *name, bool def = 0) const;
	[[nodiscard]] sint32 GetInt32(const char *name, sint32 def = 0) const;
	[[nodiscard]] uint32 GetUint32(const char *name, uint32 def = 0) const;
	[[nodiscard]] float GetFloat(const char *name, float def = 0) const;
	[[nodiscard]] double GetDouble(const char *name, double def = 0) const;
	[[nodiscard]] const wchar_t *GetString(const char *name, const wchar_t *def = 0) const;

	bool TryGetBool(const char *name, bool& val) const;
	bool TryGetInt32(const char *name, sint32& val) const;
	bool TryGetUint32(const char *name, uint32& val) const;
	bool TryGetFloat(const char *name, float& val) const;
	bool TryGetDouble(const char *name, double& val) const;
	bool TryGetString(const char *name, const wchar_t *& val) const;

protected:
	const ATPropertyValue *GetProperty(const char *name) const;
	ATPropertyValue& CreateProperty(const char *name, ATPropertyType type);

	template<class T>
	static void EnumPropsAdapter(const char *name, const ATPropertyValue& val, void *data);

	typedef vdhashmap<const char *, ATPropertyValue, vdhash<VDStringA>, vdstringpred> Properties;
	Properties mProperties;
};

template<class T>
void ATPropertySet::EnumPropsAdapter(const char *name, const ATPropertyValue& val, void *data) {
	(*(T *)data)(name, val);
}

template<class T>
void ATPropertySet::EnumProperties(const T& functor) const {
	EnumProperties(EnumPropsAdapter<T>, (void *)&functor);
}

#endif
