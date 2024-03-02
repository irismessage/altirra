//	Altirra - Atari 800/800XL/5200 emulator
//	Core library - notification list
//	Copyright (C) 2009-2016 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_ATCORE_NOTIFYLIST_H
#define f_AT_ATCORE_NOTIFYLIST_H

#include <vd2/system/vdstl.h>

/// Reentrancy-safe notification list.
///
/// ATNotifyList solves the common problem of allowing adds and removes to
/// a list while it is being iterated. It offers the following guarantees:
///
/// - Any new elements are added on the end, and are not seen by any
///   notifications in progress.
///
/// - After an element is removed, notifications are no longer dispatched
///   for that element until it is subsequently re-added. This applies
///   even to any notifications currently in progress that have not reached
///   that element yet.
///
/// - Adds and removes preserve the ordering of existing items. Notifications
///   occur in insertion order.
///
/// - Iterating over the list (Notify) does not require additional space.
///
/// However, the notification list is not thread-safe.
///
template<class T>
class ATNotifyList {
public:
	void Clear();

	/// Adds a new item to the list. No check is made for a duplicate.
	/// Complexity: Amortized O(1)
	void Add(T v);

	/// Removes the first item from the list that matches the supplied value,
	/// if any. Silently exits if not found.
	/// Complexity: O(N)
	void Remove(T v);

	/// Process each element through a callback. If the callback returns
	/// true, stop and return true; otherwise, return false once all
	/// elements have been processed.
	bool Notify(const vdfunction<bool(T)>& fn);

	/// Process each element through a callback. If the callback returns
	/// true, stop and return true; otherwise, return false once all
	/// elements have been processed. All elements processed by the callback
	/// are removed.
	bool NotifyAndClear(const vdfunction<bool(T)>& fn);

private:
	typedef typename std::conditional<std::is_trivial<T>::value, vdfastvector<T>, vdvector<T>>::type List;

	struct Iterator {
		Iterator *mpNext;
		typename List::size_type mIndex;
		typename List::size_type mLength;
	};

	List mList;
	Iterator *mpIteratorList = nullptr;
	typename List::size_type mFirstValid = 0;
};

template<class T>
void ATNotifyList<T>::Clear() {
	mList.clear();
	
	for(Iterator *p = mpIteratorList; p; p = p->mpNext) {
		p->mIndex = 0;
		p->mLength = 0;
	}
}

template<class T>
void ATNotifyList<T>::Add(T v) {
	mList.push_back(v);
}

template<class T>
void ATNotifyList<T>::Remove(T v) {
	auto it = std::find(mList.begin() + mFirstValid, mList.end(), v);

	if (it != mList.end()) {
		typename List::size_type pos = (typename List::size_type)(it - mList.begin());

		for(Iterator *p = mpIteratorList; p; p = p->mpNext) {
			VDASSERT(p->mLength > 0);
			--p->mLength;

			if (p->mIndex > pos)
				--p->mIndex;
		}

		mList.erase(it);
	}
}

template<class T>
bool ATNotifyList<T>::Notify(const vdfunction<bool(T)>& fn) {
	if (mList.empty())
		return false;

	bool interrupted = false;

	Iterator it = { mpIteratorList, mFirstValid, mList.size() };
	mpIteratorList = &it;

	while(it.mIndex < it.mLength) {
		auto v = mList[it.mIndex++];

		if (fn(v)) {
			interrupted = true;
			break;
		}
	}

	VDASSERT(mpIteratorList == &it);
	mpIteratorList = it.mpNext;

	return interrupted;
}

template<class T>
bool ATNotifyList<T>::NotifyAndClear(const vdfunction<bool(T)>& fn) {
	if (mList.empty())
		return false;

	bool interrupted = false;

	Iterator it = { mpIteratorList, 0, mList.size() };
	mpIteratorList = &it;

	while(it.mIndex < it.mLength) {
		auto v = mList[it.mIndex++];

		if (mFirstValid < it.mIndex)
			mFirstValid = it.mIndex;

		if (fn(v)) {
			interrupted = true;
			break;
		}
	}

	VDASSERT(mpIteratorList == &it);
	mpIteratorList = it.mpNext;

	if (mFirstValid > 0) {
		mList.erase(mList.begin(), mList.begin() + mFirstValid);

		for(Iterator *p = mpIteratorList; p; p = p->mpNext) {
			VDASSERT(p->mLength >= mFirstValid);
			p->mLength -= mFirstValid;

			if (p->mIndex >= mFirstValid)
				p->mIndex -= mFirstValid;
			else
				p->mIndex = 0;
		}

		mFirstValid = 0;
	}

	return interrupted;
}

#endif
