//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2007 Avery Lee, All Rights Reserved.
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

#ifndef VD2_SYSTEM_VDSTL_H
#define VD2_SYSTEM_VDSTL_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <limits.h>
#include <stdexcept>
#include <initializer_list>
#include <memory>
#include <string.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/memory.h>

///////////////////////////////////////////////////////////////////////////

template<class T>
T *vdmove_forward(T *src1, T *src2, T *dst) {
	T *p = src1;
	while(p != src2) {
		*dst = std::move(*p);
		++dst;
		++p;
	}

	return dst;
}

template<class T>
T *vdmove_backward(T *src1, T *src2, T *dst) {
	T *p = src2;
	while(p != src1) {
		--dst;
		--p;
		*dst = std::move(*p);
	}

	return dst;
}

///////////////////////////////////////////////////////////////////////////

template<class T, size_t N> char (&VDCountOfHelper(const T(&)[N]))[N];

#define vdcountof(array) (sizeof(VDCountOfHelper(array)))

///////////////////////////////////////////////////////////////////////////

class vdallocator_base {
protected:
	void VDNORETURN throw_oom();
};

template<class T>
class vdallocator : public vdallocator_base {
public:
	typedef	size_t		size_type;
	typedef	ptrdiff_t	difference_type;
	typedef	T*			pointer;
	typedef	const T*	const_pointer;
	typedef	T&			reference;
	typedef	const T&	const_reference;
	typedef	T			value_type;

	template<class U> struct rebind { typedef vdallocator<U> other; };

	pointer			address(reference x) const			{ return &x; }
	const_pointer	address(const_reference x) const	{ return &x; }

	pointer allocate(size_type n, void *p_close = 0) {
		pointer p = (pointer)malloc(n*sizeof(T));

		if (!p)
			throw_oom();

		return p;
	}

	void deallocate(pointer p, size_type n) {
		free(p);
	}

	size_type		max_size() const throw()			{ return ((~(size_type)0) >> 1) / sizeof(T); }
};

///////////////////////////////////////////////////////////////////////////

template<class T, unsigned kAlignment = 16>
class vdaligned_alloc {
public:
	typedef	size_t		size_type;
	typedef	ptrdiff_t	difference_type;
	typedef	T*			pointer;
	typedef	const T*	const_pointer;
	typedef	T&			reference;
	typedef	const T&	const_reference;
	typedef	T			value_type;

	vdaligned_alloc() {}

	template<class U, unsigned kAlignment2>
	vdaligned_alloc(const vdaligned_alloc<U, kAlignment2>&) {}

	template<class U> struct rebind { typedef vdaligned_alloc<U, kAlignment> other; };

	pointer			address(reference x) const			{ return &x; }
	const_pointer	address(const_reference x) const	{ return &x; }

	pointer			allocate(size_type n, void *p = 0)	{ return (pointer)VDAlignedMalloc(n*sizeof(T), kAlignment); }
	void			deallocate(pointer p, size_type n)	{ VDAlignedFree(p); }
	size_type		max_size() const throw()			{ return INT_MAX; }
};

///////////////////////////////////////////////////////////////////////////////

#if defined(_DEBUG) && defined(_MSC_VER) && !defined(__clang__)
	#define VD_ACCELERATE_TEMPLATES
#endif

#ifndef VDTINLINE
	#ifdef VD_ACCELERATE_TEMPLATES
		#ifndef VDTEXTERN
			#define VDTEXTERN extern
		#endif

		#define VDTINLINE
	#else
		#define VDTINLINE inline
	#endif
#endif

///////////////////////////////////////////////////////////////////////////////

template<class T>
class vdspan {
public:
	typedef	T					value_type;
	typedef	T*					pointer;
	typedef	const T*			const_pointer;
	typedef	T&					reference;
	typedef	const T&			const_reference;
	typedef	size_t				size_type;
	typedef	ptrdiff_t			difference_type;
	typedef	pointer				iterator;
	typedef const_pointer		const_iterator;
	typedef std::reverse_iterator<iterator>		reverse_iterator;
	typedef std::reverse_iterator<const_iterator>	const_reverse_iterator;

	VDTINLINE vdspan();

	template<size_t N>
	VDTINLINE vdspan(T (&arr)[N]);

	template<typename U>
	VDTINLINE vdspan(U&& range);

	VDTINLINE vdspan(T *p1, T *p2);
	VDTINLINE vdspan(T *p1, size_type len);

public:
	VDTINLINE bool					empty() const;
	VDTINLINE size_type				size() const;

	VDTINLINE pointer				data();
	VDTINLINE const_pointer			data() const;

	VDTINLINE const_iterator		cbegin() const;
	VDTINLINE const_iterator		cend() const;

	VDTINLINE iterator				begin();
	VDTINLINE const_iterator			begin() const;
	VDTINLINE iterator				end();
	VDTINLINE const_iterator			end() const;

	VDTINLINE reverse_iterator		rbegin();
	VDTINLINE const_reverse_iterator	rbegin() const;
	VDTINLINE reverse_iterator		rend();
	VDTINLINE const_reverse_iterator	rend() const;

	VDTINLINE reference				front();
	VDTINLINE const_reference		front() const;
	VDTINLINE reference				back();
	VDTINLINE const_reference		back() const;

	VDTINLINE reference				operator[](size_type n);
	VDTINLINE const_reference		operator[](size_type n) const;

	VDTINLINE vdspan first(size_type n) const;
	VDTINLINE vdspan last(size_type n) const;
	VDTINLINE vdspan subspan(size_type pos) const;
	VDTINLINE vdspan subspan(size_type pos, size_type n) const;

protected:
	T *mpBegin;
	T *mpEnd;
};

#ifdef VD_ACCELERATE_TEMPLATES
	VDTEXTERN template class vdspan<char>;
	VDTEXTERN template class vdspan<uint8>;
	VDTEXTERN template class vdspan<uint16>;
	VDTEXTERN template class vdspan<uint32>;
	VDTEXTERN template class vdspan<uint64>;
	VDTEXTERN template class vdspan<sint8>;
	VDTEXTERN template class vdspan<sint16>;
	VDTEXTERN template class vdspan<sint32>;
	VDTEXTERN template class vdspan<sint64>;
	VDTEXTERN template class vdspan<float>;
	VDTEXTERN template class vdspan<double>;
	VDTEXTERN template class vdspan<wchar_t>;
#endif

template<class T> VDTINLINE vdspan<T>::vdspan() : mpBegin(NULL), mpEnd(NULL) {}

template<typename T>
template<size_t N>
VDTINLINE vdspan<T>::vdspan(T (&arr)[N])
	: mpBegin(&arr[0]), mpEnd(&arr[N]) 
{
}

template<typename T>
template<typename U>
VDTINLINE vdspan<T>::vdspan(U&& range)
	: mpBegin(std::begin(range))
	, mpEnd(std::end(range))
{
}

template<class T> VDTINLINE vdspan<T>::vdspan(T *p1, T *p2) : mpBegin(p1), mpEnd(p2) {}
template<class T> VDTINLINE vdspan<T>::vdspan(T *p, size_type len) : mpBegin(p), mpEnd(p+len) {}
template<class T> VDTINLINE bool					vdspan<T>::empty() const { return mpBegin == mpEnd; }
template<class T> VDTINLINE typename vdspan<T>::size_type			vdspan<T>::size() const { return size_type(mpEnd - mpBegin); }
template<class T> VDTINLINE typename vdspan<T>::pointer				vdspan<T>::data() { return mpBegin; }
template<class T> VDTINLINE typename vdspan<T>::const_pointer		vdspan<T>::data() const { return mpBegin; }
template<class T> VDTINLINE typename vdspan<T>::const_iterator		vdspan<T>::cbegin() const { return mpBegin; }
template<class T> VDTINLINE typename vdspan<T>::const_iterator		vdspan<T>::cend() const { return mpEnd; }
template<class T> VDTINLINE typename vdspan<T>::iterator			vdspan<T>::begin() { return mpBegin; }
template<class T> VDTINLINE typename vdspan<T>::const_iterator		vdspan<T>::begin() const { return mpBegin; }
template<class T> VDTINLINE typename vdspan<T>::iterator			vdspan<T>::end() { return mpEnd; }
template<class T> VDTINLINE typename vdspan<T>::const_iterator		vdspan<T>::end() const { return mpEnd; }
template<class T> VDTINLINE typename vdspan<T>::reverse_iterator		vdspan<T>::rbegin() { return reverse_iterator(mpEnd); }
template<class T> VDTINLINE typename vdspan<T>::const_reverse_iterator vdspan<T>::rbegin() const { return const_reverse_iterator(mpEnd); }
template<class T> VDTINLINE typename vdspan<T>::reverse_iterator		vdspan<T>::rend() { return reverse_iterator(mpBegin); }
template<class T> VDTINLINE typename vdspan<T>::const_reverse_iterator vdspan<T>::rend() const { return const_reverse_iterator(mpBegin); }
template<class T> VDTINLINE typename vdspan<T>::reference			vdspan<T>::front() { return *mpBegin; }
template<class T> VDTINLINE typename vdspan<T>::const_reference		vdspan<T>::front() const { return *mpBegin; }
template<class T> VDTINLINE typename vdspan<T>::reference			vdspan<T>::back() { VDASSERT(mpBegin != mpEnd); return mpEnd[-1]; }
template<class T> VDTINLINE typename vdspan<T>::const_reference		vdspan<T>::back() const { VDASSERT(mpBegin != mpEnd); return mpEnd[-1]; }
template<class T> VDTINLINE typename vdspan<T>::reference			vdspan<T>::operator[](size_type n) { VDASSERT(n < size_type(mpEnd - mpBegin)); return mpBegin[n]; }
template<class T> VDTINLINE typename vdspan<T>::const_reference		vdspan<T>::operator[](size_type n) const { VDASSERT(n < size_type(mpEnd - mpBegin)); return mpBegin[n]; }
template<class T> VDTINLINE vdspan<T> vdspan<T>::first(size_type n) const { return vdspan(mpBegin, n); }
template<class T> VDTINLINE vdspan<T> vdspan<T>::last(size_type n) const { return vdspan(mpEnd - n, mpEnd); }
template<class T> VDTINLINE vdspan<T> vdspan<T>::subspan(size_type pos) const { return vdspan(mpBegin + pos, mpEnd); }
template<class T> VDTINLINE vdspan<T> vdspan<T>::subspan(size_type pos, size_type n) const { return vdspan(mpBegin + pos, mpBegin + pos + n); }

template<typename R>
vdspan(R&&) -> vdspan<std::remove_reference_t<decltype(*std::declval<R>().begin())>>;

///////////////////////////////////////////////////////////////////////////////

template<class T>
bool operator==(const vdspan<T>& x, const vdspan<T>& y) {
	auto len = x.size();
	if (len != y.size())
		return false;

	const T *px = x.data();
	const T *py = y.data();

	for(decltype(len) i=0; i<len; ++i) {
		if (px[i] != py[i])
			return false;
	}

	return true;
}

template<class T>
inline bool operator!=(const vdspan<T>& x, const vdspan<T>& y) { return !(x == y); }

///////////////////////////////////////////////////////////////////////////////

#include <vd2/system/vdstl_block.h>
#include <vd2/system/vdstl_fastdeque.h>
#include <vd2/system/vdstl_fastvector.h>
#include <vd2/system/vdstl_hash.h>
#include <vd2/system/vdstl_hashmap.h>
#include <vd2/system/vdstl_ilist.h>
#include <vd2/system/vdstl_structex.h>
#include <vd2/system/vdstl_vector.h>

#endif
