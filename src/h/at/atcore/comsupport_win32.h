//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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

#ifndef f_AT_ATCORE_COMSUPPORT_WIN32_H
#define f_AT_ATCORE_COMSUPPORT_WIN32_H

#include <vd2/system/atomic.h>
#include <windows.h>

class ATCOMRefCountW32 {
public:
	ATCOMRefCountW32() = default;
	virtual ~ATCOMRefCountW32() = default;

	ATCOMRefCountW32(const ATCOMRefCountW32&) {}
	ATCOMRefCountW32(ATCOMRefCountW32&&) {}
	ATCOMRefCountW32& operator=(const ATCOMRefCountW32&) { return *this; }
	ATCOMRefCountW32& operator=(ATCOMRefCountW32&&) { return *this; }

	ULONG AddRef() {
		return ++mRefCount;
	}

	ULONG Release() {
		DWORD rc = --mRefCount;

		if (!rc)
			delete this;

		VDASSERT(rc >= 0);

		return rc;
	}

protected:
	VDAtomicInt mRefCount{0};
};

template<class... Interfaces>
class ATCOMBaseW32 : public ATCOMRefCountW32, public Interfaces... {
public:
	ULONG STDMETHODCALLTYPE AddRef() override final;
	ULONG STDMETHODCALLTYPE Release() override final;
};

template<class... Interfaces>
ULONG STDMETHODCALLTYPE ATCOMBaseW32<Interfaces...>::AddRef() {
	return ATCOMRefCountW32::AddRef();
}

template<class... Interfaces>
ULONG STDMETHODCALLTYPE ATCOMBaseW32<Interfaces...>::Release() {
	return ATCOMRefCountW32::Release();
}

template<class Base, class... Interfaces>
class ATCOMQIW32 : public Base {
public:
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
};

template<typename T, typename U>
T ATStaticCastFirst(U ptr) {
	return nullptr;
}

template<typename T, typename U> requires (std::is_assignable_v<T&, U> || std::is_assignable_v<U&, T>)
T ATStaticCastFirst(U ptr) {
	return static_cast<T>(ptr);
}

template<typename T, typename... Interfaces> requires (!std::is_base_of_v<ATCOMBaseW32<Interfaces...>, std::remove_pointer_t<T>>)
T ATStaticCastFirst(ATCOMBaseW32<Interfaces...> *ptr) {
	T result = nullptr;

	(void)(... || (result = ATStaticCastFirst<T>(static_cast<Interfaces *>(ptr))));

	return result;
}

template<typename T, typename Base, typename... Interfaces> requires (!std::is_base_of_v<ATCOMQIW32<Base, Interfaces...>, std::remove_pointer_t<T>>)
T ATStaticCastFirst(ATCOMQIW32<Base, Interfaces...> *ptr) {
	return ATStaticCastFirst<T>(static_cast<Base *>(ptr));
}

template<typename Base, class... Interfaces>
HRESULT STDMETHODCALLTYPE ATCOMQIW32<Base, Interfaces...>::QueryInterface(REFIID riid, void **ppvObj) {
	if (!ppvObj)
		return E_POINTER;

	if ((... || (riid == __uuidof(Interfaces) ? (*ppvObj = ATStaticCastFirst<Interfaces *>(this)) != nullptr : false))) {
		this->AddRef();
		return S_OK;
	}

	*ppvObj = nullptr;
	return E_NOINTERFACE;
}

#endif
