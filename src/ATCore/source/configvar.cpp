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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#include <stdafx.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/hash.h>
#include <vd2/system/registry.h>
#include <at/atcore/configvar.h>

template class ATConfigVarT<ATConfigVarType::Bool, bool>;
template class ATConfigVarT<ATConfigVarType::Int32, sint32>;
template class ATConfigVarT<ATConfigVarType::Float, float>;
template class ATConfigVarT<ATConfigVarType::RGBColor, uint32>;

////////////////////////////////////////////////////////////////////////////////

class ATConfigVariableRegKey final : public VDRegistryAppKey {
public:
	ATConfigVariableRegKey(bool write = false) : VDRegistryAppKey("Config Vars", write) {}
};

////////////////////////////////////////////////////////////////////////////////

vdfastvector<ATConfigVar *>& ATGetConfigVarStorage() {
	static vdfastvector<ATConfigVar *> sConfigVars;

	return sConfigVars;
}

vdvector<VDStringA>& ATGetUndefinedConfigVarStorage() {
	static vdvector<VDStringA> sUndefinedConfigVars;

	return sUndefinedConfigVars;
}

void ATGetConfigVars(ATConfigVar **& p, size_t& n) {
	auto& vars = ATGetConfigVarStorage();

	p = vars.data();
	n = vars.size();
}

void ATGetUndefinedConfigVars(const VDStringA*& p, size_t& n) {
	auto& uvars = ATGetUndefinedConfigVarStorage();

	p = uvars.data();
	n = uvars.size();
}

void ATUnsetUndefinedConfigVar(const char *s) {
	auto& uvars = ATGetUndefinedConfigVarStorage();

	auto it = std::find_if(uvars.begin(), uvars.end(), [s](VDStringA& t) { return t == s; });

	if (it != uvars.end()) {
		ATConfigVariableRegKey k(true);
		k.removeValue(s);
		uvars.erase(it);
	}
}

void ATResetConfigVars() {
	VDRegistryAppKey k;

	k.removeKey("Config Vars");
}

void ATLoadConfigVars() {
	// It's expected that usually there won't be a lot of config vars overridden
	// if even any, so we optimize for that by enumerating the values first.

	ATConfigVariableRegKey k;
	vdfastvector<uint32> hashes;

	{
		VDRegistryValueIterator vi(k);

		const char *name = vi.Next();
		if (!name)
			return;

		// build a vector set of hashes of overridden vars
		do {
			hashes.push_back(VDHashString32(name));
		} while((name = vi.Next()));
	}

	std::sort(hashes.begin(), hashes.end());

	vdfastvector<bool> hashesUsed(hashes.size(), false);
	size_t numHashesUsed = 0;

	// iterate over internal vars, and load all the ones that match the
	// name set (it's okay if we get an occasional false positive)
	vdfastvector<ATConfigVar::OnChangedHandler> callbacks;
	for(ATConfigVar *var : ATGetConfigVarStorage()) {
		const uint32 hash = VDHashString32(var->mpVarName);
		auto it = std::lower_bound(hashes.begin(), hashes.end(), hash);

		if (it == hashes.end() || *it != hash)
			continue;

		bool& hashUsed = hashesUsed[it - hashes.begin()];
		if (!hashUsed) {
			hashUsed = true;
			++numHashesUsed;
		}

		if (var->FromPersistence(k)) {
			// accumulate the callback, don't call it yet as we may have
			// redundant callbacks and we don't want to have to require
			// callbacks to be super-fast
			if (var->mpOnChanged)
				callbacks.push_back(var->mpOnChanged);
		}
	}

	// check if we have any hashes that weren't used, and if so, add them
	// to the uvars list
	if (numHashesUsed < hashes.size()) {
		auto& uvars = ATGetUndefinedConfigVarStorage();

		VDRegistryValueIterator vi(k);

		while(const char *name = vi.Next()) {
			const uint32 hash = VDHashString32(name);
			auto it = std::lower_bound(hashes.begin(), hashes.end(), hash);

			if (it != hashes.end() && *it == hash) {
				if (!hashesUsed[it - hashes.begin()])
					uvars.emplace_back(name);
			}
		}
	}

	// reduce callbacks to unique set and dispatch
	std::sort(callbacks.begin(), callbacks.end());
	auto uniqueEnd = std::unique(callbacks.begin(), callbacks.end());

	for(auto it = callbacks.begin(); it != uniqueEnd; ++it)
		(*it)();
}

////////////////////////////////////////////////////////////////////////////////

ATConfigVar::ATConfigVar(const char *name, OnChangedHandler h)
	: mpVarName(name)
	, mpOnChanged(h)
	, mbOverridden(false)
{
	ATGetConfigVarStorage().push_back(this);
}

void ATConfigVar::NotifyChanged() {
	if (mpOnChanged)
		mpOnChanged();
}

////////////////////////////////////////////////////////////////////////////////

template<ATConfigVarType T_Type, typename T_Val>
ATConfigVarType ATConfigVarT<T_Type, T_Val>::GetVarType() const {
	return T_Type;
}

template<ATConfigVarType T_Type, typename T_Val>
void ATConfigVarT<T_Type, T_Val>::Unset() {
	if (mbOverridden) {
		mbOverridden = false;

		ATConfigVariableRegKey k(true);
		k.removeValue(mpVarName);
	}

	if (mValue != mDefaultValue) {
		mValue = mDefaultValue;

		NotifyChanged();
	}
}

template<ATConfigVarType T_Type, typename T_Val>
void ATConfigVarT<T_Type, T_Val>::operator=(const T_Val& val) {
	if (!mbOverridden || mValue != val) {
		mbOverridden = true;

		if (mValue != val) {
			mValue = val;
			NotifyChanged();
		}

		SaveValue();
	}
}

////////////////////////////////////////////////////////////////////////////////

bool ATConfigVarBool::FromPersistence(ATConfigVariableRegKey& k) {
	mbOverridden = true;

	bool v = k.getInt(mpVarName, mValue) != 0;
	if (mValue == v)
		return false;

	mValue = v;
	return true;
}

bool ATConfigVarBool::FromString(const char *s) {
	VDStringSpanA sp(s);

	bool val = false;

	if (sp.comparei("true") == 0 || sp == "1")
		val = true;
	else if (sp.comparei("false") == 0 || sp == "0")
		val = false;
	else
		return false;

	*this = val;
	return true;
}

VDStringA ATConfigVarBool::ToString() const {
	return VDStringA(mValue ? "true" : "false");
}

void ATConfigVarBool::SaveValue() const {
	ATConfigVariableRegKey k(true);
	k.setInt(mpVarName, mValue ? 1 : 0);
}

////////////////////////////////////////////////////////////////////////////////

bool ATConfigVarInt32::FromPersistence(ATConfigVariableRegKey& k) {
	mbOverridden = true;

	int v = k.getInt(mpVarName, mValue);
	if (mValue == v)
		return false;

	mValue = v;
	return true;
}

bool ATConfigVarInt32::FromString(const char *s) {
	int val = 0;
	char dummy;

	if (sscanf(s, "%d %c", &val, &dummy) == 1) {
		*this = val;

		return true;
	} else {
		return false;
	}
}

VDStringA ATConfigVarInt32::ToString() const {
	VDStringA s;

	s.sprintf("%d", mValue);
	return s;
}

void ATConfigVarInt32::SaveValue() const {
	ATConfigVariableRegKey k(true);
	k.setInt(mpVarName, mValue);
}

////////////////////////////////////////////////////////////////////////////////

bool ATConfigVarFloat::FromPersistence(ATConfigVariableRegKey& k) {
	mbOverridden = true;

	float v = VDGetIntAsFloat(k.getInt(mpVarName, VDGetFloatAsInt(mValue)));
	if (mValue == v || !isfinite(v))
		return false;

	mValue = v;
	return true;
}

bool ATConfigVarFloat::FromString(const char *s) {
	float val = 0;
	char dummy;

	if (sscanf(s, "%g %c", &val, &dummy) == 1 && isfinite(val)) {
		*this = val;

		return true;
	} else {
		return false;
	}
}

VDStringA ATConfigVarFloat::ToString() const {
	VDStringA s;

	s.sprintf("%g", mValue);
	return s;
}

void ATConfigVarFloat::SaveValue() const {
	ATConfigVariableRegKey k(true);
	k.setInt(mpVarName, VDGetFloatAsInt(mValue));
}

////////////////////////////////////////////////////////////////////////////////

bool ATConfigVarRGBColor::FromPersistence(ATConfigVariableRegKey& k) {
	mbOverridden = true;

	int v = k.getInt(mpVarName, mValue);
	if (mValue == v)
		return false;

	mValue = v;
	return true;
}

bool ATConfigVarRGBColor::FromString(const char *s) {
	int val = 0;
	int len = 0;
	char dummy;

	if (sscanf(s, "#%x%n%c", &val, &len, &dummy) == 1 && (len == 4 || len == 7)) {
		uint32 newValue = (uint32)val;

		if (len == 4)
			newValue = (val & 0xF) * 0x11 + (val & 0xF0)*0x110 + (val & 0xF00) * 0x1100;

		*this = newValue;

		return true;
	} else {
		return false;
	}
}

VDStringA ATConfigVarRGBColor::ToString() const {
	VDStringA s;

	s.sprintf("#%06X", mValue);
	return s;
}

void ATConfigVarRGBColor::SaveValue() const {
	ATConfigVariableRegKey k(true);
	k.setInt(mpVarName, mValue);
}

////////////////////////////////////////////////////////////////////////////////
