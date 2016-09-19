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

#ifndef UTIL_ATOMIC_H
#define UTIL_ATOMIC_H

#include <atomic>
#include "ruts/ints.h"

namespace ruts {

  /*
   * All subclasses need to import the constructors and assignment.
   *
   * Remember that if you override one of a name, you have to import
   * the name, otherwise you lose the rest of the overloads.
   */
  template <typename T>
  class default_atomic {
  protected:
    using contained_type = T;
  private:
    using shadow_type = typename ruts::ints::uint_t<8*sizeof(T)>::exact;

    using atomic_shadow = std::atomic<shadow_type>;

    atomic_shadow _internal;
    

    static const shadow_type &shadow(const contained_type &p) {
      return reinterpret_cast<const shadow_type &>(p);
    }
    static shadow_type &shadow(contained_type &p) {
      return reinterpret_cast<shadow_type &>(p);
    }
    static contained_type unshadow(shadow_type s) {
      return reinterpret_cast<const contained_type &>(s);
    }
  protected:
    constexpr static std::memory_order load_order(std::memory_order order) {
      return (order == std::memory_order_acq_rel ? std::memory_order_acquire
	      : order == std::memory_order_release ? std::memory_order_relaxed
	      : order);
    }
  public:
    constexpr default_atomic(const contained_type &desired)
      : _internal(shadow(desired))
    {}
    default_atomic() : default_atomic(contained_type()) {};
    default_atomic(const default_atomic &) = delete;

    contained_type operator =(const contained_type &desired) {
      store(desired);
      return desired;
    }
    contained_type operator =(const contained_type &desired) volatile {
      store(desired);
      return desired;
    }
    default_atomic &operator =(const default_atomic &) = delete;
    default_atomic &operator =(const default_atomic &) volatile = delete;

    bool is_lock_free() const {
      return shadow().is_lock_free();
    }
    bool is_lock_free() const volatile {
      return shadow().is_lock_free();
    }

    void store(const contained_type &desired,
	       std::memory_order order = std::memory_order_seq_cst)
    {
      _internal.store(shadow(desired), order);
    }
    void store(const contained_type &desired,
	       std::memory_order order = std::memory_order_seq_cst) volatile
    {
      _internal.store(shadow(desired), order);
    }

    contained_type load(std::memory_order order = std::memory_order_seq_cst) const
    {
      return unshadow(_internal.load(order));
    }
    contained_type load(std::memory_order order = std::memory_order_seq_cst) const volatile
    {
      return unshadow(_internal.load(order));
    }

    operator contained_type() const {
      return load();
    }
    operator contained_type() const volatile {
      return load();
    }

    contained_type exchange(const contained_type &desired, 
			    std::memory_order order = std::memory_order_seq_cst)
    {
      return unshadow(_internal.exchange(shadow(desired), order));
    }
    contained_type exchange(const contained_type &desired, 
			    std::memory_order order = std::memory_order_seq_cst) volatile
    {
      return unshadow(_internal.exchange(shadow(desired), order));
    }

    
    bool compare_exchange_weak(contained_type &expected,
			       const contained_type &desired, 
			       std::memory_order success,
			       std::memory_order failure)
    {
      return _internal.compare_exchange_weak(shadow(expected),
					     shadow(desired),
					     success, failure);
    }
    bool compare_exchange_weak(contained_type &expected,
			       const contained_type &desired, 
			       std::memory_order order = std::memory_order_seq_cst)
    {
      return compare_exchange_weak(expected, desired, order, load_order(order));
    }    
    bool compare_exchange_weak(contained_type &expected,
			       const contained_type &desired, 
			       std::memory_order success,
			       std::memory_order failure) volatile
    {
      return _internal.compare_exchange_weak(shadow(expected),
					     shadow(desired),
					     success, failure);
    }
    bool compare_exchange_weak(contained_type &expected,
			       const contained_type &desired, 
			       std::memory_order order = std::memory_order_seq_cst) volatile
    {
      return compare_exchange_weak(expected, desired, order, load_order(order));
    }    
    bool compare_exchange_strong(contained_type &expected,
				 const contained_type &desired, 
				 std::memory_order success,
				 std::memory_order failure)
    {
      return _internal.compare_exchange_strong(shadow(expected),
					       shadow(desired),
					       success, failure);
    }
    bool compare_exchange_strong(contained_type &expected,
				 const contained_type &desired, 
				 std::memory_order order = std::memory_order_seq_cst)
    {
      return compare_exchange_strong(expected, desired, order, load_order(order));
    }    
    bool compare_exchange_strong(contained_type &expected,
				 const contained_type &desired, 
				 std::memory_order success,
				 std::memory_order failure) volatile
    {
      return _internal.compare_exchange_strong(shadow(expected),
					       shadow(desired),
					       success, failure);
    }
    bool compare_exchange_strong(contained_type &expected,
				 const contained_type &desired, 
				 std::memory_order order = std::memory_order_seq_cst) volatile
    {
      return compare_exchange_strong(expected, desired, order, load_order(order));
    }    
    
  };
}


#endif