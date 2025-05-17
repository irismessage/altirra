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

#ifndef f_VD2_SYSTEM_VDSTL_ILIST_H
#define f_VD2_SYSTEM_VDSTL_ILIST_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>

///////////////////////////////////////////////////////////////////////////
//
//	vdlist
//
//	vdlist<T> is similar to list<T*>, except:
//
//	1) The node structure must be embedded as a superclass of T.
//     Thus, the client is in full control of allocation.
//	2) Node pointers may be converted back into iterators in O(1).
//
///////////////////////////////////////////////////////////////////////////

struct vdlist_node {
	vdlist_node *mListNodeNext, *mListNodePrev;
};

template<class T, class T_Nonconst>
class vdlist_iterator {
public:
	typedef ptrdiff_t difference_type;
	typedef T value_type;
	typedef T *pointer;
	typedef T& reference;
	typedef std::bidirectional_iterator_tag iterator_category;

	vdlist_iterator() {}
	vdlist_iterator(T *p) : mp(p) {}
	vdlist_iterator(const vdlist_iterator<T_Nonconst, T_Nonconst>& src) : mp(src.mp) {}

	T* operator *() const {
		return static_cast<T*>(mp);
	}

	bool operator==(const vdlist_iterator<T, T_Nonconst>& x) const {
		return mp == x.mp;
	}

	bool operator!=(const vdlist_iterator<T, T_Nonconst>& x) const {
		return mp != x.mp;
	}

	vdlist_iterator& operator++() {
		mp = mp->mListNodeNext;
		return *this;
	}

	vdlist_iterator& operator--() {
		mp = mp->mListNodePrev;
		return *this;
	}

	vdlist_iterator operator++(int) {
		vdlist_iterator tmp(*this);
		mp = mp->mListNodeNext;
		return tmp;
	}

	vdlist_iterator& operator--(int) {
		vdlist_iterator tmp(*this);
		mp = mp->mListNodePrev;
		return tmp;
	}

	vdlist_node *mp;
};

class vdlist_base {
public:
	typedef	vdlist_node						node;
	typedef	size_t							size_type;
	typedef	ptrdiff_t						difference_type;

	bool empty() const {
		return mAnchor.mListNodeNext == &mAnchor;
	}

	size_type size() const {
		node *p = { mAnchor.mListNodeNext };
		size_type s = 0;

		if (p != &mAnchor)
			do {
				++s;
				p = p->mListNodeNext;
			} while(p != &mAnchor);

		return s;
	}

	void clear() {
		mAnchor.mListNodePrev	= &mAnchor;
		mAnchor.mListNodeNext	= &mAnchor;
	}

	void pop_front() {
		mAnchor.mListNodeNext = mAnchor.mListNodeNext->mListNodeNext;
		mAnchor.mListNodeNext->mListNodePrev = &mAnchor;
	}

	void pop_back() {
		mAnchor.mListNodePrev = mAnchor.mListNodePrev->mListNodePrev;
		mAnchor.mListNodePrev->mListNodeNext = &mAnchor;
	}

	static void unlink(vdlist_node& node) {
		vdlist_node& n1 = *node.mListNodePrev;
		vdlist_node& n2 = *node.mListNodeNext;

		n1.mListNodeNext = &n2;
		n2.mListNodePrev = &n1;
	}

protected:
	node	mAnchor;
};

template<class T>
class vdlist : public vdlist_base {
public:
	typedef	T*								value_type;
	typedef	T**								pointer;
	typedef	const T**						const_pointer;
	typedef	T*&								reference;
	typedef	const T*&						const_reference;
	typedef	vdlist_iterator<T, T>						iterator;
	typedef vdlist_iterator<const T, T>					const_iterator;
	typedef std::reverse_iterator<iterator>			reverse_iterator;
	typedef std::reverse_iterator<const_iterator>	const_reverse_iterator;

	vdlist() {
		mAnchor.mListNodePrev	= &mAnchor;
		mAnchor.mListNodeNext	= &mAnchor;
	}

	iterator begin() {
		iterator it;
		it.mp = mAnchor.mListNodeNext;
		return it;
	}

	const_iterator begin() const {
		const_iterator it;
		it.mp = mAnchor.mListNodeNext;
		return it;
	}

	iterator end() {
		iterator it;
		it.mp = &mAnchor;
		return it;
	}

	const_iterator end() const {
		const_iterator it;
		it.mp = &mAnchor;
		return it;
	}

	reverse_iterator rbegin() {
		return reverse_iterator(begin());
	}

	const_reverse_iterator rbegin() const {
		return const_reverse_iterator(begin());
	}

	reverse_iterator rend() {
		return reverse_iterator(end);
	}

	const_reverse_iterator rend() const {
		return const_reverse_iterator(end());
	}

	const value_type front() const {
		return static_cast<T *>(mAnchor.mListNodeNext);
	}

	const value_type back() const {
		return static_cast<T *>(mAnchor.mListNodePrev);
	}

	iterator find(T *p) {
		iterator it;
		it.mp = mAnchor.mListNodeNext;

		if (it.mp != &mAnchor)
			do {
				if (it.mp == static_cast<node *>(p))
					break;

				it.mp = it.mp->mListNodeNext;
			} while(it.mp != &mAnchor);

		return it;
	}

	const_iterator find(T *p) const {
		const_iterator it;
		it.mp = mAnchor.mListNodeNext;

		if (it.mp != &mAnchor)
			do {
				if (it.mp == static_cast<node *>(p))
					break;

				it.mp = it.mp->mListNodeNext;
			} while(it.mp != &mAnchor);

		return it;
	}

	iterator fast_find(T *p) {
		iterator it(p);
		return it;
	}

	const_iterator fast_find(T *p) const {
		iterator it(p);
	}

	void push_front(T *p) {
		node& n = *p;
		n.mListNodePrev = &mAnchor;
		n.mListNodeNext = mAnchor.mListNodeNext;
		n.mListNodeNext->mListNodePrev = &n;
		mAnchor.mListNodeNext = &n;
	}

	void push_back(T *p) {
		node& n = *p;
		n.mListNodeNext = &mAnchor;
		n.mListNodePrev = mAnchor.mListNodePrev;
		n.mListNodePrev->mListNodeNext = &n;
		mAnchor.mListNodePrev = &n;
	}

	iterator erase(T *p) {
		return erase(fast_find(p));
	}

	iterator erase(iterator it) {
		node& n = *it.mp;

		n.mListNodePrev->mListNodeNext = n.mListNodeNext;
		n.mListNodeNext->mListNodePrev = n.mListNodePrev;

		it.mp = n.mListNodeNext;
		return it;
	}

	iterator erase(iterator i1, iterator i2) {
		node& np = *i1.mp->mListNodePrev;
		node& nn = *i2.mp;

		np.mListNodeNext = &nn;
		nn.mListNodePrev = &np;

		return i2;
	}

	void insert(iterator dst, T *src) {
		node& ns = *src;
		node& nd = *dst.mp;

		ns.mListNodeNext = &nd;
		ns.mListNodePrev = nd.mListNodePrev;
		nd.mListNodePrev->mListNodeNext = &ns;
		nd.mListNodePrev = &ns;
	}

	void insert(iterator dst, iterator i1, iterator i2) {
		if (i1 != i2) {
			node& np = *dst.mp->mListNodePrev;
			node& nn = *dst.mp;
			node& n1 = *i1.mp;
			node& n2 = *i2.mp->mListNodePrev;

			np.mListNodeNext = &n1;
			n1.mListNodePrev = &np;
			n2.mListNodeNext = &nn;
			nn.mListNodePrev = &n2;
		}
	}

	void splice(iterator dst, vdlist<T>& srclist) {
		insert(dst, srclist.begin(), srclist.end());
		srclist.clear();
	}

	void splice(iterator dst, vdlist<T>& srclist, iterator src) {
		T *v = *src;
		srclist.erase(src);
		insert(dst, v);
	}

	void splice(iterator dst, vdlist<T>& srclist, iterator i1, iterator i2) {
		if (dst.mp != i1.mp && dst.mp != i2.mp) {
			srclist.erase(i1, i2);
			insert(dst, i1, i2);
		}
	}
};

#endif
