//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 2024 Avery Lee, All Rights Reserved.
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

#ifndef f_VD2_SYSTEM_VDSTL_FASTVECTOR_H
#define f_VD2_SYSTEM_VDSTL_FASTVECTOR_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>

template<class T, class S, class A = vdallocator<T> >
class vdfastvector_base : public vdspan<T> {
protected:
	using vdspan<T>::mpBegin;
	using vdspan<T>::mpEnd;

public:
	typedef typename vdspan<T>::value_type value_type;
	typedef typename vdspan<T>::pointer pointer;
	typedef typename vdspan<T>::const_pointer const_pointer;
	typedef typename vdspan<T>::reference reference;
	typedef typename vdspan<T>::const_reference const_reference;
	typedef typename vdspan<T>::size_type size_type;
	typedef typename vdspan<T>::difference_type difference_type;
	typedef typename vdspan<T>::iterator iterator;
	typedef typename vdspan<T>::const_iterator const_iterator;
	typedef typename vdspan<T>::reverse_iterator reverse_iterator;
	typedef typename vdspan<T>::const_reverse_iterator const_reverse_iterator;

	~vdfastvector_base() {
		static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>);

		if (static_cast<const S&>(m).is_deallocatable_storage(mpBegin))
			m.deallocate(mpBegin, m.eos - mpBegin);
	}

	size_type capacity() const { return size_type(m.eos - mpBegin); }

public:
	T *alloc(size_type n) {
		size_type offset = (size_type)(mpEnd - mpBegin);
		resize(offset + n);
		return mpBegin + offset;
	}

	void assign(std::initializer_list<T> ilist) {
		assign(ilist.begin(), ilist.end());
	}

	void assign(const T *p1, const T *p2) {
		resize((size_type)(p2 - p1));
		memcpy(mpBegin, p1, (char *)p2 - (char *)p1);
	}

	template<typename RandomAccessIterator, typename =
		typename std::enable_if<
			std::is_same<typename std::iterator_traits<RandomAccessIterator>::iterator_category, std::random_access_iterator_tag>::value
			&& !std::is_pointer<RandomAccessIterator>::value
		>::type
	>
	void assign(RandomAccessIterator it1, RandomAccessIterator it2) {
		resize((size_type)(it2 - it1));

		pointer dst = mpBegin;
		while(it1 != it2) {
			*dst++ = *it1;
			++it1;
		}
	}

	void clear() {
		mpEnd = mpBegin;
	}

	iterator erase(iterator it) {
		VDASSERT(it - mpBegin < mpEnd - mpBegin);

		memmove(it, it+1, (char *)mpEnd - (char *)(it+1));

		--mpEnd;

		return it;
	}

	iterator erase(iterator it1, iterator it2) {
		VDASSERT(it1 - mpBegin <= mpEnd - mpBegin);
		VDASSERT(it2 - mpBegin <= mpEnd - mpBegin);
		VDASSERT(it1 <= it2);

		memmove(it1, it2, (char *)mpEnd - (char *)it2);

		mpEnd -= (it2 - it1);

		return it1;
	}

	iterator insert(iterator it, const T& value) {
		const T temp(value);		// copy in case value is inside container.

		if (mpEnd == m.eos) {
			difference_type delta = it - mpBegin;
			_reserve_always_add_one();
			it = mpBegin + delta;
		}

		memmove(it+1, it, (char *)mpEnd - (char *)it);
		*it = temp;
		++mpEnd;
		VDASSERT(mpEnd <= m.eos);

		return it;
	}

	iterator insert(iterator it, size_type n, const T& value) {
		const T temp(value);		// copy in case value is inside container.

		ptrdiff_t bytesToInsert = n * sizeof(T);

		if ((char *)m.eos - (char *)mpEnd < bytesToInsert) {
			difference_type delta = it - mpBegin;
			_reserve_always_add(n);
			it = mpBegin + delta;
		}

		memmove((char *)it + bytesToInsert, it, (char *)mpEnd - (char *)it);
		for(size_t i=0; i<n; ++i)
			*it++ = temp;
		mpEnd += n;
		VDASSERT(mpEnd <= m.eos);
		return it;
	}

	iterator insert(iterator it, const T *p1, const T *p2) {
		ptrdiff_t elementsToCopy = p2 - p1;
		ptrdiff_t bytesToCopy = (char *)p2 - (char *)p1;

		if ((char *)m.eos - (char *)mpEnd < bytesToCopy) {
			difference_type delta = it - mpBegin;
			_reserve_always_add(bytesToCopy);
			it = mpBegin + delta;
		}

		memmove((char *)it + bytesToCopy, it, (char *)mpEnd - (char *)it);
		memcpy(it, p1, bytesToCopy);
		mpEnd += elementsToCopy;
		VDASSERT(mpEnd <= m.eos);
		return it;
	}

	reference push_back() {
		if (mpEnd == m.eos)
			_reserve_always_add_one();

		return *mpEnd++;
	}

	void push_back(const T& value) {
		T temp(value);		// copy in case value is inside container.

		if (mpEnd == m.eos)
			_reserve_always_add_one();

		new(mpEnd++) T(std::move(temp));
	}

	void pop_back() {
		VDASSERT(mpBegin != mpEnd);
		--mpEnd;
	}

	template<typename... T_Args>
	reference emplace_back(T_Args&&... args) {
		if (mpEnd == m.eos)
			_reserve_always_add_one();

		return *new(mpEnd++) T{std::forward<T_Args>(args)...};
	}

	void resize(size_type n) {
		if (n*sizeof(T) > size_type((char *)m.eos - (char *)mpBegin))
			_reserve_always_amortized(n);

		mpEnd = mpBegin + n;
	}

	void resize(size_type n, const T& value) {
		const T temp(value);

		if (n*sizeof(T) > size_type((char *)m.eos - (char *)mpBegin)) {
			_reserve_always_amortized(n);
		}

		const iterator newEnd(mpBegin + n);
		if (newEnd > mpEnd)
			std::fill(mpEnd, newEnd, temp);
		mpEnd = newEnd;
	}

	void reserve(size_type n) {
		if (n*sizeof(T) > size_type((char *)m.eos - (char *)mpBegin))
			_reserve_always(n);
	}

protected:
	VDNOINLINE void _reserve_always_add_one() {
		_reserve_always((m.eos - mpBegin) * 2 + 1);
	}

	VDNOINLINE void _reserve_always_add(size_type n) {
		_reserve_always((m.eos - mpBegin) * 2 + n);
	}

	VDNOINLINE void _reserve_always(size_type n) {
		size_type oldSize = mpEnd - mpBegin;
		T *oldStorage = mpBegin;
		T *newStorage = m.allocate(n, NULL);

		memcpy(newStorage, mpBegin, (char *)mpEnd - (char *)mpBegin);
		if (static_cast<const S&>(m).is_deallocatable_storage(oldStorage))
			m.deallocate(oldStorage, m.eos - mpBegin);
		mpBegin = newStorage;
		mpEnd = newStorage + oldSize;
		m.eos = newStorage + n;
	}

	VDNOINLINE void _reserve_always_amortized(size_type n) {
		size_type nextCapacity = (size_type)((m.eos - mpBegin)*2);

		if (nextCapacity < n)
			nextCapacity = n;

		_reserve_always(nextCapacity);
	}

	struct Misc : A, S {
		T *eos;
	} m;
};

///////////////////////////////////////////////////////////////////////////////

struct vdfastvector_storage {
	bool is_deallocatable_storage(void *p) const {
		return p != 0;
	}
};

template<class T, class A = vdallocator<T> >
class vdfastvector : public vdfastvector_base<T, vdfastvector_storage, A> {
protected:
	using vdfastvector_base<T, vdfastvector_storage, A>::m;
	using vdfastvector_base<T, vdfastvector_storage, A>::mpBegin;
	using vdfastvector_base<T, vdfastvector_storage, A>::mpEnd;

public:
	typedef typename vdfastvector_base<T, vdfastvector_storage, A>::value_type value_type;
	typedef typename vdfastvector_base<T, vdfastvector_storage, A>::pointer pointer;
	typedef typename vdfastvector_base<T, vdfastvector_storage, A>::const_pointer const_pointer;
	typedef typename vdfastvector_base<T, vdfastvector_storage, A>::reference reference;
	typedef typename vdfastvector_base<T, vdfastvector_storage, A>::const_reference const_reference;
	typedef typename vdfastvector_base<T, vdfastvector_storage, A>::size_type size_type;
	typedef typename vdfastvector_base<T, vdfastvector_storage, A>::difference_type difference_type;
	typedef typename vdfastvector_base<T, vdfastvector_storage, A>::iterator iterator;
	typedef typename vdfastvector_base<T, vdfastvector_storage, A>::const_iterator const_iterator;
	typedef typename vdfastvector_base<T, vdfastvector_storage, A>::reverse_iterator reverse_iterator;
	typedef typename vdfastvector_base<T, vdfastvector_storage, A>::const_reverse_iterator const_reverse_iterator;

	vdfastvector() {
		m.eos = NULL;
	}

	vdfastvector(size_type len) {
		mpBegin = m.allocate(len, NULL);
		mpEnd = mpBegin + len;
		m.eos = mpEnd;
	}

	vdfastvector(size_type len, const T& fill) {
		mpBegin = m.allocate(len, NULL);
		mpEnd = mpBegin + len;
		m.eos = mpEnd;

		std::fill(mpBegin, mpEnd, fill);
	}

	vdfastvector(const vdfastvector& x) {
		size_type n = x.mpEnd - x.mpBegin;
		mpBegin = m.allocate(n, NULL);
		mpEnd = mpBegin + n;
		m.eos = mpEnd;
		memcpy(mpBegin, x.mpBegin, sizeof(T) * n);
	}

	vdnothrow vdfastvector(vdfastvector&& x) vdnoexcept {
		mpBegin = x.mpBegin;
		mpEnd = x.mpEnd;
		m.eos = x.m.eos;

		x.mpBegin = nullptr;
		x.mpEnd = nullptr;
		x.m.eos = nullptr;
	}

	template<typename InputIterator, typename = typename std::enable_if<!std::is_integral<InputIterator>::value>::type>
	vdfastvector(InputIterator it1, InputIterator it2) {
		m.eos = NULL;

		this->assign(it1, it2);
	}

	vdfastvector(const std::initializer_list<T>& ilist) {
		m.eos = nullptr;
		this->assign(ilist.begin(), ilist.end());
	}

	vdfastvector& operator=(const vdfastvector& x) {
		if (this != &x)
			this->assign(x.mpBegin, x.mpEnd);

		return *this;
	}

	vdnothrow vdfastvector& operator=(vdfastvector&& x) vdnoexcept {
		if (mpBegin)
			m.deallocate(mpBegin, m.eos - mpBegin);

		mpBegin = x.mpBegin;
		mpEnd = x.mpEnd;
		m.eos = x.m.eos;

		x.mpBegin = nullptr;
		x.mpEnd = nullptr;
		x.m.eos = nullptr;

		return *this;
	}

	void swap(vdfastvector& x) {
		T *p;

		p = mpBegin;		mpBegin = x.mpBegin;		x.mpBegin = p;
		p = mpEnd;			mpEnd = x.mpEnd;			x.mpEnd = p;
		p = m.eos;			m.eos = x.m.eos;			x.m.eos = p;
	}
};

///////////////////////////////////////////////////////////////////////////////

template<class T, size_t N>
struct vdfastfixedvector_storage {
	T mArray[N];

	bool is_deallocatable_storage(void *p) const {
		return p != mArray;
	}
};

template<class T, size_t N, class A = vdallocator<T> >
class vdfastfixedvector : public vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A> {
protected:
	using vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A>::mpBegin;
	using vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A>::mpEnd;
	using vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A>::m;

public:
	typedef typename vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A>::value_type value_type;
	typedef typename vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A>::pointer pointer;
	typedef typename vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A>::const_pointer const_pointer;
	typedef typename vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A>::reference reference;
	typedef typename vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A>::const_reference const_reference;
	typedef typename vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A>::size_type size_type;
	typedef typename vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A>::difference_type difference_type;
	typedef typename vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A>::iterator iterator;
	typedef typename vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A>::const_iterator const_iterator;
	typedef typename vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A>::reverse_iterator reverse_iterator;
	typedef typename vdfastvector_base<T, vdfastfixedvector_storage<T, N>, A>::const_reverse_iterator const_reverse_iterator;

	vdfastfixedvector() {
		mpBegin = m.mArray;
		mpEnd = m.mArray;
		m.eos = m.mArray + N;
	}

	vdfastfixedvector(size_type len) {
		if (len <= N) {
			mpBegin = m.mArray;
			mpEnd = m.mArray + len;
			m.eos = m.mArray + N;
		} else {
			mpBegin = m.allocate(len, NULL);
			mpEnd = mpBegin + len;
			m.eos = mpEnd;
		}
	}

	vdfastfixedvector(size_type len, const T& fill) {
		mpBegin = m.allocate(len, NULL);
		mpEnd = mpBegin + len;
		m.eos = mpEnd;

		std::fill(mpBegin, mpEnd, fill);
	}

	vdfastfixedvector(const vdfastfixedvector& x) {
		size_type n = x.mpEnd - x.mpBegin;

		if (n <= N) {
			mpBegin = m.mArray;
			mpEnd = m.mArray + n;
			m.eos = m.mArray + N;
		} else {
			mpBegin = m.allocate(n, NULL);
			mpEnd = mpBegin + n;
			m.eos = mpEnd;
		}

		memcpy(mpBegin, x.mpBegin, sizeof(T) * n);
	}

	vdfastfixedvector(const value_type *p, const value_type *q) {
		mpBegin = m.mArray;
		mpEnd = m.mArray;
		m.eos = m.mArray + N;

		assign(p, q);
	}

	vdfastfixedvector& operator=(const vdfastfixedvector& x) {
		if (this != &x)
			assign(x.mpBegin, x.mpEnd);

		return *this;
	}

	void swap(vdfastfixedvector& x) {
		size_t this_bytes = (char *)mpEnd - (char *)mpBegin;
		size_t other_bytes = (char *)x.mpEnd - (char *)x.mpBegin;

		T *p;

		if (mpBegin == m.mArray) {
			if (x.mpBegin == x.m.mArray) {
				if (this_bytes < other_bytes) {
					VDSwapMemory(m.mArray, x.m.mArray, this_bytes);
					memcpy((char *)m.mArray + this_bytes, (char *)x.m.mArray + this_bytes, other_bytes - this_bytes);
				} else {
					VDSwapMemory(m.mArray, x.m.mArray, other_bytes);
					memcpy((char *)m.mArray + other_bytes, (char *)x.m.mArray + other_bytes, this_bytes - other_bytes);
				}

				mpEnd = (T *)((char *)mpBegin + other_bytes);
				x.mpEnd = (T *)((char *)x.mpBegin + this_bytes);
			} else {
				memcpy(x.m.mArray, mpBegin, this_bytes);

				mpBegin = x.mpBegin;
				mpEnd = x.mpEnd;
				m.eos = x.m.eos;

				x.mpBegin = x.m.mArray;
				x.mpEnd = (T *)((char *)x.m.mArray + this_bytes);
				x.m.eos = x.m.mArray + N;
			}
		} else {
			if (x.mpBegin == x.m.mArray) {
				memcpy(x.m.mArray, mpBegin, other_bytes);

				x.mpBegin = mpBegin;
				x.mpEnd = mpEnd;
				x.m.eos = m.eos;

				mpBegin = m.mArray;
				mpEnd = (T *)((char *)m.mArray + other_bytes);
				m.eos = m.mArray + N;
			} else {
				p = mpBegin;		mpBegin = x.mpBegin;		x.mpBegin = p;
				p = mpEnd;			mpEnd = x.mpEnd;			x.mpEnd = p;
				p = m.eos;			m.eos = x.m.eos;			x.m.eos = p;
			}
		}
	}
};

#endif
