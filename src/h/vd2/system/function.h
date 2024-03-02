//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2014 Avery Lee, All Rights Reserved.
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

#ifndef f_VD2_SYSTEM_FUNCTION_H
#define f_VD2_SYSTEM_FUNCTION_H

#include <functional>
#include <cstddef>

//////////////////////////////////////////////////////////////////////////////
// vdfunction
//
// vdfunction<T> is an implementation of std::function<T>. It mostly works
// the same way:
//
// + Supports small object optimization for up to 2*sizeof(void *).
// + Supports lambdas on VS2010.
// + Supports reference optimization when std::reference_wrapper used.
// + Fast dispatch: function pointer, no virtual calls.
// - target() and target_type() are not supported.
// - assign() is not supported.
// - bad_function_call is not supported -- calling through an unbound function
//   object invokes undefined behavior.
//
//////////////////////////////////////////////////////////////////////////////

class vdfuncbase;

struct vdfunctraits {
	void (*mpDestroy)(const vdfuncbase& obj);
	void (*mpCopy)(vdfuncbase& dst, const vdfuncbase& src);
	void (*mpMove)(vdfuncbase& dst, vdfuncbase&& src);
};

template<class F>
struct vdfunc_ti {
	static void destroy(const vdfuncbase& obj);
	static void copy(vdfuncbase& dst, const vdfuncbase& src);
	static void move(vdfuncbase& dst, vdfuncbase&& src);

	static const vdfunctraits sObject;
};

template<class F>
void vdfunc_ti<F>::destroy(const vdfuncbase& obj) {
	((F *)&obj.mData)->~F();
}

template<class F>
void vdfunc_ti<F>::copy(vdfuncbase& dst, const vdfuncbase& src) {
	new(&dst.mData) F(*(F *)&src.mData);
}

template<class F>
void vdfunc_ti<F>::move(vdfuncbase& dst, vdfuncbase&& src) {
	new(&dst.mData) F(std::move(*(F *)&src.mData));
}

template<class F>
const vdfunctraits vdfunc_ti<F>::sObject = { destroy, copy, move };

template<class F>
struct vdfunc_th {
	static void destroy(const vdfuncbase& obj);
	static void copy(vdfuncbase& dst, const vdfuncbase& src);

	static const vdfunctraits sObject;
};

template<class F>
void vdfunc_th<F>::destroy(const vdfuncbase& obj) {
	delete (F *)obj.mData.p[0];
}

template<class F>
void vdfunc_th<F>::copy(vdfuncbase& dst, const vdfuncbase& src) {
	dst.mData.p[0] = new F(*(F *)src.mData.p[0]);
}

template<class F>
const vdfunctraits vdfunc_th<F>::sObject = { destroy, copy, nullptr };

//////////////////////////////////////////////////////////////////////////////

template<class> class vdfunction;

//////////////////////////////////////////////////////////////////////////////

class vdfuncbase {
public:
	vdfuncbase() = default;
	vdfuncbase(const vdfuncbase&);
	inline vdfuncbase(vdfuncbase&&);
	inline ~vdfuncbase();

	vdfuncbase& operator=(const vdfuncbase&);
	vdfuncbase& operator=(vdfuncbase&&);

	inline operator bool() const;

protected:
	void swap(vdfuncbase& other);
	void clear();

public:
	void (*mpFn)() = nullptr;
	union Data {
		void *p[2];
		void (*fn)();
	} mData;
	const vdfunctraits *mpTraits = nullptr;
};

inline vdfuncbase::vdfuncbase(vdfuncbase&& src)
	: mpFn(src.mpFn)
	, mData(src.mData)
	, mpTraits(src.mpTraits)
{
	src.mpTraits = nullptr;
	src.mpFn = nullptr;

	if (mpTraits->mpMove)
		mpTraits->mpMove(*this, std::move(src));
}

inline vdfuncbase::~vdfuncbase() {
	clear();
	if (mpTraits)
		mpTraits->mpDestroy(*this);
}

inline vdfuncbase::operator bool() const {
	return mpFn != nullptr;
}

inline bool operator==(const vdfuncbase& fb, std::nullptr_t) { return fb.mpFn == nullptr; }
inline bool operator==(std::nullptr_t, const vdfuncbase& fb) { return fb.mpFn == nullptr; }
inline bool operator!=(const vdfuncbase& fb, std::nullptr_t) { return fb.mpFn != nullptr; }
inline bool operator!=(std::nullptr_t, const vdfuncbase& fb) { return fb.mpFn != nullptr; }

//////////////////////////////////////////////////////////////////////////////

template<class T>
struct vdfunc_mode
	: public std::integral_constant<unsigned,
			sizeof(T) <= sizeof(vdfuncbase::Data) && std::alignment_of<void *>::value % std::alignment_of<T>::value == 0
				? std::is_pod<T>::value
					? 0
					: 1
				: 2
	> {};

template<unsigned char Mode> struct vdfunc_construct;

template<>
struct vdfunc_construct<0> {		// trivial
	template<class F>
	static void go(vdfuncbase& func, const F& f) {
		new(&func.mData) F(f);
	}
};

template<>
struct vdfunc_construct<1> {		// direct (requires copy/destruction)
	template<class F>
	static void go(vdfuncbase& func, const F& f) {
		new(&func.mData) F(f);
		func.mpTraits = &vdfunc_ti<F>::sObject;
	}
};

template<>
struct vdfunc_construct<2> {		// indirect (uses heap)
	template<class F>
	static void go(vdfuncbase& func, const F& f) {
		func.mData.p[0] = new F(f);
		func.mpTraits = &vdfunc_th<F>::sObject;
	}
};

//////////////////////////////////////////////////////////////////////////////

template<class R, class ...Args>
class vdfunction<R(Args...)> : public vdfuncbase {
public:
	typedef R result_type;

	vdfunction() = default;
	vdfunction(std::nullptr_t) {}
	vdfunction(const vdfunction& src) : vdfuncbase(src) {}
	template<class F> vdfunction(std::reference_wrapper<F> f);
	template<class F> vdfunction(F f);

	vdfunction& operator=(std::nullptr_t) { vdfuncbase::clear(); return *this; }

	template<class F>
	vdfunction& operator=(F&& f) { vdfunction(std::forward<F>(f)).swap(*this); return *this; }

	template<class F>
	vdfunction& operator=(std::reference_wrapper<F> f) { vdfunction(f).swap(*this); }

	void swap(vdfunction& other) {
		vdfuncbase::swap(other);
	}

	R operator()(Args... args) const {
		return reinterpret_cast<R (*)(const vdfuncbase *, Args...)>(mpFn)(this, std::forward<Args>(args)...);
	}
};

struct vdfunc_rd {
	template<class R, class F, class ...Args>
	static R go(const vdfuncbase *p, Args... args) {
		return (*(F *)&p->mData)(std::forward<Args>(args)...);
	}
};

struct vdfunc_ri {
	template<class R, class F, class ...Args>
	static R go(const vdfuncbase *p, Args... args) {
		return (*(F *)p->mData.p[0])(std::forward<Args>(args)...);
	}
};

template<class R, class... Args>
template<class F>
vdfunction<R(Args...)>::vdfunction(std::reference_wrapper<F> f) {
	auto fn = vdfunc_ri::go<R, F, Args...>;
	mpFn = reinterpret_cast<void (*)()>(fn);
	mData.p[0] = (void *)&f.get();
}

template<class R, class... Args>
template<class F>
vdfunction<R(Args...)>::vdfunction(F f) {
	typedef decltype(f((*(std::remove_reference<Args>::type *)nullptr)...)) validity_test;

	vdfunc_construct<vdfunc_mode<F>::value>::go(*this, f);
	const auto fn = std::conditional<vdfunc_mode<F>::value == 2, vdfunc_ri, vdfunc_rd>::type::template go<R, F, Args...>;
	mpFn = reinterpret_cast<void (*)()>(fn);
}

//////////////////////////////////////////////////////////////////////////////

#endif
