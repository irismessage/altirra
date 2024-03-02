#ifndef f_VD2_SYSTEM_VDSTL_RANGE_H
#define f_VD2_SYSTEM_VDSTL_RANGE_H

template<class T_Iterator>
class vdrange {
public:
	typedef typename std::iterator_traits<T_Iterator>::value_type value_type;
	typedef typename std::iterator_traits<T_Iterator>::pointer pointer;
	typedef typename std::iterator_traits<T_Iterator>::reference reference;
	typedef typename std::iterator_traits<T_Iterator>::difference_type difference_type;

	vdrange(const T_Iterator& i1, const T_Iterator& i2);

	bool empty() const;
	size_t size() const;

	T_Iterator begin() const;
	T_Iterator end() const;

private:
	const T_Iterator mpBegin;
	const T_Iterator mpEnd;
};

template<class T_Iterator>
size_t vdrange<T_Iterator>::size() const {
	return std::distance(mpBegin, mpEnd);
}

template<class T_Iterator>
bool vdrange<T_Iterator>::empty() const {
	return mpBegin == mpEnd;
}

template<class T_Iterator>
T_Iterator vdrange<T_Iterator>::begin() const {
	return mpBegin;
}

template<class T_Iterator>
T_Iterator vdrange<T_Iterator>::end() const {
	return mpEnd;
}

///////////////////////////////////////////////////////////////////////////

template<class T_Iterator>
class vdrangeenum {
	typedef typename std::iterator_traits<T_Iterator>::value_type value_type;
	typedef typename std::iterator_traits<T_Iterator>::pointer pointer;
	typedef typename std::iterator_traits<T_Iterator>::reference reference;
	typedef typename std::iterator_traits<T_Iterator>::difference_type difference_type;
public:
	vdrangeenum(const vdrangeenum& r);
	vdrangeenum(const T_Iterator& i1, const T_Iterator& i2);

	template<class T>
	explicit vdrangeenum(T& r);

	bool empty() const;
	value_type& operator*() const;
	void operator++();

private:
	T_Iterator mpBegin;
	const T_Iterator mpEnd;
};

template<class T_Iterator>
vdrangeenum<T_Iterator>::vdrangeenum(const vdrangeenum& r)
	: mpBegin(r.mpBegin)
	, mpEnd(r.mpEnd)
{
}

template<class T_Iterator>
vdrangeenum<T_Iterator>::vdrangeenum(const T_Iterator& i1, const T_Iterator& i2)
	: mpBegin(i1)
	, mpEnd(i2)
{
}

template<class T_Iterator>
template<class T>
vdrangeenum<T_Iterator>::vdrangeenum(T& r)
	: mpBegin(r.begin())
	, mpEnd(r.end())
{
}

template<class T_Iterator>
bool vdrangeenum<T_Iterator>::empty() const {
	return mpBegin == mpEnd;
}

template<class T_Iterator>
typename vdrangeenum<T_Iterator>::value_type& vdrangeenum<T_Iterator>::operator*() const {
	return *mpBegin;
}

template<class T_Iterator>
void vdrangeenum<T_Iterator>::operator++() {
	++mpBegin;
}

///////////////////////////////////////////////////////////////////////////

template<class T>
vdrange<typename T::iterator> vdenumrange(T& container) {
	return vdrangeenum<typename T::iterator>(container.begin(), container.end());
}

template<class T, size_t N>
vdrangeenum<T *> vdenumrange(T (&container)[N]) {
	return vdrangeenum<T *>(&container[0], &container[N]);
}

#endif // f_VD2_SYSTEM_VDSTL_RANGE_H
