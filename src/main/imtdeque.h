/*
 * imtdeque.h
 *
 *  Created on: 18. 3. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_IMTDEQUE_H_
#define SRC_MAIN_IMTDEQUE_H_
#include <shared/refcnt.h>
#include <imtjson/container.h>

#include "abstractarray.h"

using json::Container;

template<typename T>
class ImtDeque: public AbstractArray<T> {
public:

	ImtDeque ();
	ImtDeque (std::size_t sz, T value);

	class Array: public AbstractArray<T >, public ondra_shared::RefCntObj {
	public:
		virtual bool isLeaf() const = 0;

	};
	using PArray = ondra_shared::RefCntPtr<Array>;


	virtual T operator[](std::size_t) const override;
	virtual std::size_t size() const override;

	class Iterator {
	public:
		Iterator(PArray a, std::size_t idx);
		T operator *() const;
		bool operator==(const Iterator &other) const;
		bool operator!=(const Iterator &other) const;
		Iterator &operator++();
		Iterator operator++(int);
		Iterator &operator--();
		Iterator operator--(int);
		Iterator operator+(int x);
		Iterator operator-(int x);
		Iterator &operator+=(int x);
		Iterator &operator-=(int x);
	protected:
		PArray a;
		std::size_t idx;
	};

	Iterator begin() const;
	Iterator end() const;

	ImtDeque push_back(T x) const;
	ImtDeque pop_back() const;
	ImtDeque push_front(T x) const;
	ImtDeque pop_front() const;
	T front() const;
	T back() const;
	ImtDeque copy() const;


protected:

	class Leaf: public Array {
	public:
		Leaf(T x):x(x) {}
		virtual T operator[](std::size_t) const override {return x;}
		virtual std::size_t size() const override  {return 1;}
		virtual bool isLeaf() const override {return true;}

	protected:
		T x;

	};

	class Node: public Array {
	public:
		Node(const PArray &left, const PArray &right):left(left),right(right),lsz(left->size()),rsz(right->size()) {}
		virtual T operator[](std::size_t idx) const override {return idx < lsz?left->operator[](idx):right->operator[](idx-lsz);}
		virtual std::size_t size() const override {return lsz+rsz;}
		virtual bool isLeaf() const override {return false;}
		PArray push_back(T x) const;
		PArray push_front(T x) const;
	protected:
		PArray left, right;
		std::size_t lsz, rsz;
	};

	class EmptyArray: public Array {
	public:
		virtual T operator[](std::size_t) const override {throw std::runtime_error("Bounds error");}
		virtual std::size_t size() const override  {return 0;}
		virtual bool isLeaf() const override {return true;}
	};


	static PArray emptyArray() {
		static PArray x = new EmptyArray();
		return x;
	}

	class MultiLeaf: public json::Container<T>, public Array {
	public:
		using json::Container<T>::Container;
		virtual T operator[](std::size_t idx) const override {return json::Container<T>::operator [](idx);}
		virtual std::size_t size() const override  {return json::Container<T>::size();}
		virtual bool isLeaf() const override {return true;}
	};

	PArray a;
	std::size_t sz;
	std::size_t offset;

	ImtDeque(PArray a):a(a),sz(a->size()),offset(0) {}
	ImtDeque(PArray a,std::size_t sz,std::size_t offset):a(a),sz(sz),offset(0) {}
};

template<typename T>
inline T ImtDeque<T>::operator [](std::size_t idx) const {
	return a->operator[](idx+offset);
}

template<typename T>
inline std::size_t ImtDeque<T>::size() const {
	return sz;
}

template<typename T>
inline ImtDeque<T>::Iterator::Iterator(PArray a, std::size_t idx):a(a),idx(idx) {}

template<typename T>
inline T ImtDeque<T>::Iterator::operator *() const {
	return a->operator[](idx);
}


template<typename T>
inline bool ImtDeque<T>::Iterator::operator ==(const Iterator &other) const {
	return a == other.a && idx == other.idx;
}

template<typename T>
inline bool ImtDeque<T>::Iterator::operator !=(const Iterator &other) const {
	return !operator==(other);
}

template<typename T>
inline typename ImtDeque<T>::Iterator ImtDeque<T>::begin() const {
	return Iterator(a,offset);
}

template<typename T>
inline typename ImtDeque<T>::Iterator ImtDeque<T>::end() const {
	return Iterator(a,offset+sz);
}

template<typename T>
inline ImtDeque<T> ImtDeque<T>::push_back(T x) const {
	auto osz = a->size();
	if (osz>sz*2 || offset+sz < osz) {
		auto m = MultiLeaf::create(sz+1);
		for (auto y: *this) m->push_back(y);
		m->push_back(x);
		return ImtDeque(PArray::staticCast(m));
	} else if (a->isLeaf()) {
		return ImtDeque(new Node(a, new Leaf(x)),offset,sz+1);
	} else {
		const Node *n = static_cast<const Node *>(a);
		auto r = n->push_back(x);
		return ImtDeque(PArray::staticCast(r),offset,sz+1);
	}
}

template<typename T>
inline ImtDeque<T> ImtDeque<T>::pop_back() const {
	if (sz) {
		return ImtDeque(a,sz-1,offset);
	} else {
		return *this;
	}
}

template<typename T>
inline ImtDeque<T> ImtDeque<T>::push_front(T x) const {
	auto osz = a->size();
	if (osz>sz*2 || offset != 0) {
		auto m = MultiLeaf::create(sz+1);
		m->push_back(x);
		for (auto y: *this) m->push_back(y);
		return ImtDeque(PArray::staticCast(m));
	} else if (a->isLeaf()) {
		return ImtDeque(new Node(new Leaf(x),a),0,sz+1);
	} else {
		const Node *n = static_cast<const Node *>(a);
		auto r = n->push_front(x);
		return ImtDeque(PArray::staticCast(r),0,sz+1);
	}
}

template<typename T>
inline ImtDeque<T> ImtDeque<T>::pop_front() const {
	if (sz) {
		return ImtDeque(a,sz-1,offset+1);
	} else {
		return *this;
	}
}

template<typename T>
inline T ImtDeque<T>::front() const {
	return a->operator[](offset);
}

template<typename T>
inline ImtDeque<T>::ImtDeque():a(emptyArray()),sz(0),offset(0) {}

template<typename T>
inline ImtDeque<T>::ImtDeque(std::size_t sz, T value):a(new Leaf(value)),sz(sz),offset(0) {}

template<typename T>
inline T ImtDeque<T>::back() const {
	return a->operator[](offset+sz-1);
}


template<typename T>
inline typename ImtDeque<T>::Iterator::Iterator& ImtDeque<T>::Iterator::operator ++() {
	idx++;return *this;
}

template<typename T>
inline typename ImtDeque<T>::Iterator::Iterator ImtDeque<T>::Iterator::operator ++(int ) {
	auto x = *this;
	idx++; return x;
}

template<typename T>
inline typename ImtDeque<T>::Iterator::Iterator& ImtDeque<T>::Iterator::operator --() {
	idx--; return *this;
}

template<typename T>
inline typename ImtDeque<T>::Iterator::Iterator ImtDeque<T>::Iterator::operator --(int) {
	auto x = *this;
	idx--; return x;
}

template<typename T>
inline typename ImtDeque<T>::Iterator::Iterator ImtDeque<T>::Iterator::operator +(int x) {
	return Iterator(a, idx+x);
}

template<typename T>
inline typename ImtDeque<T>::Iterator::Iterator ImtDeque<T>::Iterator::operator -(int x) {
	return Iterator(a, idx-x);
}

template<typename T>
inline typename ImtDeque<T>::Iterator::Iterator& ImtDeque<T>::Iterator::operator +=(int x) {
	idx+=x;return *this;
}

template<typename T>
inline typename ImtDeque<T>::Iterator::Iterator& ImtDeque<T>::Iterator::operator -=(int x) {
	idx-=x;return *this;
}


template<typename T>
inline ImtDeque<T> ImtDeque<T>::copy() const {
	auto ml = MultiLeaf::create(sz);
	for (auto x: *this) ml->push_back(x);
	return ImtDeque(PArray::staticCast(ml));
}

template<typename T>
inline typename ImtDeque<T>::PArray ImtDeque<T>::Node::push_back(T x) const {
	if (lsz<=rsz) {
		auto ml = MultiLeaf::create(lsz+rsz+1);
		for (std::size_t i = 0; i < lsz; i++) ml->push_back(left->operator[](i));
		for (std::size_t i = 0; i < rsz; i++) ml->push_back(right->operator[](i));
		ml->push_back(x);
		return PArray::staticCast(ml);
	} else if (right->isNode()) {
		auto r = static_cast<const Node *>(right);
		return new Node(left,r->push_back(x));
	} else {
		return new Node(left,new Node(right, new Leaf(x)));
	}
}

template<typename T>
inline typename ImtDeque<T>::PArray ImtDeque<T>::Node::push_front(T x) const {
	if (lsz>=rsz) {
		auto ml = MultiLeaf::create(lsz+rsz+1);
		ml->push_back(x);
		for (std::size_t i = 0; i < lsz; i++) ml->push_back(left->operator[](i));
		for (std::size_t i = 0; i < rsz; i++) ml->push_back(right->operator[](i));
		return PArray::staticCast(ml);
	} else if (right->isNode()) {
		auto l = static_cast<const Node *>(left);
		return new Node(l->push_front(x),left);
	} else {
		return new Node(new Node(new Leaf(x), left), right);
	}
}

#endif /* SRC_MAIN_IMTDEQUE_H_ */
