//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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

#ifndef f_AT_ATCORE_CONFIGVAR_H
#define f_AT_ATCORE_CONFIGVAR_H

#include <vd2/system/vdtypes.h>

class VDStringA;
class ATConfigVariableRegKey;

enum class ATConfigVarType : uint8 {
	Invalid,
	Bool,
	Int32,
	Float,
	RGBColor
};

class ATConfigVar {
	ATConfigVar(const ATConfigVar&) = delete;
	ATConfigVar& operator=(const ATConfigVar&) = delete;
public:
	typedef void (*OnChangedHandler)();

	const char *mpVarName;
	OnChangedHandler mpOnChanged;
	bool mbOverridden;

	virtual ATConfigVarType GetVarType() const = 0;
	virtual bool FromPersistence(ATConfigVariableRegKey& k) = 0;
	virtual bool FromString(const char *s) = 0;
	virtual VDStringA ToString() const = 0;
	virtual void Unset() = 0;

protected:
	ATConfigVar(const char *name, OnChangedHandler h);

	void NotifyChanged();
};

template<ATConfigVarType T_Type, typename T_Val>
class ATConfigVarT : public ATConfigVar {
public:
	ATConfigVarT(const char *name, T_Val val, OnChangedHandler h = nullptr) : ATConfigVar(name, h), mValue(val), mDefaultValue(val) {}

	ATConfigVarType GetVarType() const override;
	void Unset();

	operator T_Val() const & { return mValue; }

	// Config vars should never be temporaries. If they are, something is extremely
	// wrong.
	operator T_Val() const && = delete;

protected:
	T_Val mValue;
	T_Val mDefaultValue;
};

class ATConfigVarBool final : public ATConfigVarT<ATConfigVarType::Bool, bool> {
public:
	using ATConfigVarT::ATConfigVarT;

	bool FromPersistence(ATConfigVariableRegKey& k) override;
	bool FromString(const char *s) override;
	VDStringA ToString() const override;
};

class ATConfigVarInt32 final : public ATConfigVarT<ATConfigVarType::Int32, sint32> {
public:
	using ATConfigVarT::ATConfigVarT;

	bool FromPersistence(ATConfigVariableRegKey& k) override;
	bool FromString(const char *s) override;
	VDStringA ToString() const override;
};

class ATConfigVarFloat final : public ATConfigVarT<ATConfigVarType::Float, float> {
public:
	using ATConfigVarT::ATConfigVarT;

	bool FromPersistence(ATConfigVariableRegKey& k) override;
	bool FromString(const char *s) override;
	VDStringA ToString() const override;
};

class ATConfigVarRGBColor final : public ATConfigVarT<ATConfigVarType::RGBColor, uint32> {
public:
	using ATConfigVarT::ATConfigVarT;

	bool FromPersistence(ATConfigVariableRegKey& k) override;
	bool FromString(const char *s) override;
	VDStringA ToString() const override;
};

void ATGetConfigVars(ATConfigVar **& p, size_t& n);
void ATResetConfigVars();
void ATLoadConfigVars();

class VDStringA;
void ATGetUndefinedConfigVars(const VDStringA*& p, size_t& n);
void ATUnsetUndefinedConfigVar(const char *s);

#endif
