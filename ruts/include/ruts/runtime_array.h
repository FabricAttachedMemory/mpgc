/*
 *
 *  Multi Process Garbage Collector
 *  Copyright © 2016 Hewlett Packard Enterprise Development Company LP.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  As an exception, the copyright holders of this Library grant you permission
 *  to (i) compile an Application with the Library, and (ii) distribute the 
 *  Application containing code generated by the Library and added to the 
 *  Application during this compilation process under terms of your choice, 
 *  provided you also meet the terms and conditions of the Application license.
 *
 */

/*
 * runtime_array.h
 *
 *  Created on: Aug 3, 2014
 *      Author: evank
 */

#ifndef RUNTIME_ARRAY_H_
#define RUNTIME_ARRAY_H_

#include <memory>
#include <cstdlib>
#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace ruts {
  template <typename T, typename Allocator = std::allocator<T> >
  class runtime_array {
  public:
    using value_type = T;
    using allocator_type = Allocator;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = typename std::allocator_traits<Allocator>::pointer;
    using const_pointer = typename std::allocator_traits<Allocator>::const_pointer;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  private:

    Allocator _alloc;
    T *_start;
    unsigned _size;
    enum Raw { RAW };
    explicit runtime_array(
        Raw raw,
        size_type count,
        const Allocator &alloc = Allocator())
    : _alloc(alloc),
      _start(count == 0 ? nullptr : _alloc.allocate(count)),
      _size(count)
    {
    }

    bool ensure_size(size_type n)
    {
      if (n == _size) {
        return true;
      }
      std::for_each(_start, _start+_size, [this](T &ptr) {
        _alloc.destroy(&ptr);
      });
      _alloc.deallocate(_start, _size);
      _size = n;
      _start = n == 0 ? nullptr : _alloc.allocate(n);
      return false;
    }
  public:
    explicit runtime_array(const Allocator &alloc = Allocator()) noexcept : _alloc{alloc}, _start{nullptr}, _size{0} {}

    explicit runtime_array(
        size_type count,
        const T& value,
        const Allocator &alloc = Allocator())
    : runtime_array(RAW, count, alloc)
    {
      std::uninitialized_fill_n(_start, count, value);
    }

    explicit runtime_array(
        size_type count,
        const Allocator &alloc = Allocator())
    : runtime_array(RAW, count, alloc)
    {
      std::for_each(_start, _start+_size, [this](T &ptr) {
        _alloc.construct(&ptr);
      });
    }

    /*
	template <class InputIt>
	runtime_array(
			size_type count,
			InputIt from,
			const Allocator &alloc = Allocator())
	: runtime_array(RAW, count, alloc)
	{
		std::uninitialized_copy_n(from, _size, _start);
	}
     */

    template <class RandomIt>
    runtime_array(
        RandomIt first,
        RandomIt last,
        const Allocator &alloc = Allocator())
        : runtime_array(RAW, std::distance(first, last), alloc)
          {
      std::uninitialized_copy_n(first, _size, _start);
          }

    template <class U, class A2>
    runtime_array(const runtime_array<U,A2> &other, const Allocator &alloc = Allocator())
    : runtime_array(other.size(), other.begin(), alloc)
      { /* empty */ }

    runtime_array(runtime_array &&other)
    :  _alloc(other._alloc), _start(other._start), _size(other._size)
    {
      other._start = nullptr;
      other._size = 0;
    }

    runtime_array( std::initializer_list<T> init, const Allocator &alloc = Allocator())
    : runtime_array(init.size(), init.begin(), alloc)
    {
      /* empty */
    }

    ~runtime_array() {
      ensure_size(0);
    }

    template <typename U, typename A2>
    runtime_array &operator=(const runtime_array<U,A2> &other) {
      assign(other.size(), other.begin());
      return *this;
    }

    runtime_array &operator=(std::initializer_list<T> ilist) {
      assign(ilist);
      return *this;
    }

    runtime_array &operator=(runtime_array &&other) {
      clear();
      swap(other);
      return *this;
    }

    void assign(size_type count, const T &value) {
      if (ensure_size(count)) {
        std::fill_n(_start, count, value);
      } else {
        std::uninitialized_fill_n(_start, count, value);
      }
    }

    template <typename InputIt>
    void assign(size_type count, InputIt first) {
      if (ensure_size(count)) {
        // Eclipse, but not GCC, doesn't know std::copy_n()
        // std::copy_n(first, count, _start);
        iterator result = _start;
        if (count > 0) {
          *result++ = *first;
          for (size_type i = 1; i < count; ++i) {
            *result++ = *++first;
          }
        }
      } else {
        std::uninitialized_copy_n(first, count, _start);
      }
    }

    template <typename RandomIt>
    void assign(RandomIt first, RandomIt last) {
      assign(std::distance(first,last), first);
    }

    void assign(std::initializer_list<T> ilist) {
      assign(ilist.size(), ilist.begin());
    }

    allocator_type get_allocator() const {
      return _alloc;
    }

    reference at(size_type pos) {
      if (pos >= _size) {
        throw std::out_of_range("position beyond size of array");
      }
      return operator[](pos);
    }

    const_reference at(size_type pos) const {
      if (pos >= _size) {
        throw std::out_of_range("position beyond size of array");
      }
      return operator[](pos);
    }

    reference operator[](size_type pos) {
      return _start[pos];
    }
    constexpr const_reference operator[](size_type pos) const {
      return _start[pos];
    }

    reference front() {
      return _start[0];
    }

    const_reference front() const {
      return _start[0];
    }

    reference back() {
      return _start[_size-1];
    }

    const_reference back() const {
      return _start[_size-1];
    }

    T *data() {
      return _start;
    }

    const T *data() const {
      return _start;
    }

    iterator begin() {
      return _start;
    }

    const_iterator begin() const {
      return cbegin();
    }

    const_iterator cbegin() const {
      return _start;
    }

    iterator end() {
      return _start+_size;
    }

    const_iterator end() const {
      return cend();
    }

    const_iterator cend() const {
      return _start+_size;
    }

    reverse_iterator rbegin() {
      return reverse_iterator(end());
    }
    const_reverse_iterator crbegin() const {
      return reverse_iterator(cend());
    }
    const_reverse_iterator rbegin() const {
      return crbegin();
    }
    reverse_iterator rend() {
      return reverse_iterator(begin());
    }
    const_reverse_iterator crend() const {
      return reverse_iterator(cbegin());
    }
    const_reverse_iterator rend() const {
      return crend();
    }

    bool empty() const {
      return _size == 0;
    }

    size_type size() const {
      return _size;
    }

    void clear() {
      ensure_size(0);
    }

    void swap(runtime_array &other) {
      std::swap(_start, other._start);
      std::swap(_size, other._size);
      std::swap(_alloc, other._alloc);
    }

    // not implementing max_size()

  };


  template <typename T, typename Alloc>
  void swap(runtime_array<T,Alloc> &lhs, runtime_array<T,Alloc> &rhs) {
    lhs.swap(rhs);
  }

}




#endif /* RUNTIME_ARRAY_H_ */