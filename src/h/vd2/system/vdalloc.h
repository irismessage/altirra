//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
//	Beginning with 1.6.0, the VirtualDub system library is licensed
//	differently than the remainder of VirtualDub.  This particular file is
//	thus licensed as follows (the "zlib" license):
//
//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.

#ifndef f_VD2_SYSTEM_VDALLOC_H
#define f_VD2_SYSTEM_VDALLOC_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <type_traits>
#include <stdlib.h>

class IVDRefCount;
class IVDRefUnknown;

// Why don't I use STL auto_ptr?  Two reasons.  First, auto_ptr has
// the overhead of an ownership flag, and second, auto_ptr can't
// be used with malloc() blocks.  So think of these as auto_ptr
// objects, but not quite....

#pragma warning(push)
#pragma warning(disable: 4284)		// operator-> must return pointer to UDT

class vdautoblockptr {
	vdautoblockptr(const vdautoblockptr&) = delete;
	vdautoblockptr& operator=(const vdautoblockptr&) = delete;
protected:
	void *ptr;

public:
	explicit vdautoblockptr(void *p = 0) : ptr(p) {}
	~vdautoblockptr() { free(ptr); }

	vdautoblockptr& operator=(void *src) { free(ptr); ptr = src; return *this; }

	operator void*() const { return ptr; }

	void from(vdautoblockptr& src) { free(ptr); ptr=src.ptr; src.ptr=0; }
	void *get() const { return ptr; }
	void *release() { void *v = ptr; ptr = NULL; return v; }
};

template<class T> class vdautoptr2 {
	vdautoptr2(const vdautoptr2&) = delete;
	vdautoptr2& operator=(const vdautoptr2&) = delete;
protected:
	T *ptr;

public:
	explicit vdautoptr2(T *p = 0) : ptr(p) {}

	vdautoptr2(vdautoptr2&& other)
		: ptr(other.ptr)
	{
		other.ptr = nullptr;
	}

	~vdautoptr2() { free((void *)ptr); }

	vdautoptr2<T>& operator=(T *src) { free((void *)ptr); ptr = src; return *this; }

	vdautoptr2& operator=(vdautoptr2&& other) {
		if (ptr) {
			free(ptr);
			ptr = nullptr;
		}

		ptr = other.ptr;
		other.ptr = nullptr;

		return *this;
	}

	operator T*() const { return ptr; }
	T& operator*() const { return *ptr; }
	T *operator->() const { return ptr; }

	vdautoptr2<T>& from(vdautoptr2<T>& src) { free((void *)ptr); ptr=src.ptr; src.ptr=0; }
	T *get() const { return ptr; }
	T *release() { T *v = ptr; ptr = NULL; return v; }
};

template<class T> class vdautoptr {
protected:
	T *ptr;

public:
	vdautoptr(const vdautoptr&) = delete;
	vdautoptr& operator=(vdautoptr&) = delete;

	vdnothrow vdautoptr(std::nullptr_t) noexcept : ptr(nullptr) {}
	vdnothrow explicit vdautoptr(T *p = nullptr) noexcept : ptr(p) {}

	vdnothrow vdautoptr(vdautoptr&& src) noexcept : ptr(src.ptr) {
		src.ptr = nullptr;
	}

	template<class U, typename = std::enable_if<std::is_convertible_v<U, T>>>
	vdnothrow vdautoptr(vdautoptr<U>&& src) noexcept : ptr(src.release()) {
	}

	~vdautoptr() {
		static_assert(!std::is_base_of<IVDRefCount, T>::value && !std::is_base_of<IVDRefUnknown, T>::value,
			"vdautoptr<T> cannot be used on reference counted classes. Use vdrefptr<T> instead.");
		delete ptr;
	}

	vdnothrow vdautoptr& operator=(vdautoptr&& src) noexcept {
		delete ptr;
		ptr = nullptr;

		ptr = src.ptr;
		src.ptr = nullptr;

		return *this;
	}

	vdautoptr<T>& operator=(T *src) { delete ptr; ptr = src; return *this; }

	operator T*() const { return ptr; }
	T& operator*() const { return *ptr; }
	T *operator->() const { return ptr; }

	T** operator~() {
		if (ptr) {
			delete ptr;
			ptr = NULL;
		}

		return &ptr;
	}

	void from(vdautoptr<T>& src) { delete ptr; ptr=src.ptr; src.ptr=0; }
	T *get() const { return ptr; }
	T *release() { T *v = ptr; ptr = NULL; return v; }

	void reset() {
		if (ptr) {
			delete ptr;
			ptr = NULL;
		}
	}

	void swap(vdautoptr<T>& other) {
		T *p = other.ptr;
		other.ptr = ptr;
		ptr = p;
	}
};

template<class T>
vdautoptr<T> vdmakeautoptr(T *p) { return vdautoptr<T>(p); }

template<class T, class... Args>
vdautoptr<T> vdmakeunique(Args&&... args) {
	return vdautoptr<T>(new T(std::forward<Args>(args)...));
}

template<class T> class vdautoarrayptr {
protected:
	T *ptr;

public:
	explicit vdautoarrayptr(T *p = 0) : ptr(p) {}
	~vdautoarrayptr() { delete[] ptr; }

	vdautoarrayptr<T>& operator=(T *src) { delete[] ptr; ptr = src; return *this; }

	T& operator[](int offset) const { return ptr[offset]; }

	void from(vdautoarrayptr<T>& src) { delete[] ptr; ptr=src.ptr; src.ptr=0; }
	T *get() const { return ptr; }
	T *release() { T *v = ptr; ptr = NULL; return v; }
};

///////////////////////////////////////////////////////////////////////////

struct vdsafedelete_t {};
extern vdsafedelete_t vdsafedelete;

template<class T>
inline vdsafedelete_t& operator<<=(vdsafedelete_t& x, T *& p) {
	if (p) {
		delete p;
		p = 0;
	}

	return x;
}

template<class T>
inline vdsafedelete_t& operator,(vdsafedelete_t& x, T *& p) {
	if (p) {
		delete p;
		p = 0;
	}

	return x;
}

template<class T, size_t N>
inline vdsafedelete_t& operator<<=(vdsafedelete_t& x, T *(&p)[N]) {
	for(size_t i=0; i<N; ++i) {
		if (p[i]) {
			delete p[i];
			p[i] = 0;
		}
	}

	return x;
}

template<class T, size_t N>
inline vdsafedelete_t& operator,(vdsafedelete_t& x, T *(&p)[N]) {
	for(size_t i=0; i<N; ++i) {
		if (p[i]) {
			delete p[i];
			p[i] = 0;
		}
	}

	return x;
}

///////////////////////////////////////////////////////////////////////////

#pragma warning(pop)

#endif
