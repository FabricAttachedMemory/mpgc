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
 * external_gc_ptr.h
 *
 *  Created on: Oct 21, 2014
 *      Author: evank
 */

#ifndef EXTERNAL_GC_PTR_H_
#define EXTERNAL_GC_PTR_H_

#include <unordered_map>
#include <memory>
#include <array>
#include <mutex>
#include <stack>
#include <cstdlib>
#include <atomic>
#include <ostream>
#include "mpgc/gc_ptr.h"

namespace mpgc {
  extern void initialize();
  
    namespace inbound_pointers {
      using target_base = const gc_allocated;
      using index_type = std::size_t;
      using ptrint = std::uintptr_t;
      constexpr static index_type block_size = 10000;
      constexpr static index_type n_blocks = 100000;
      constexpr static ptrint global_cache_size = 1<<20;
      constexpr static ptrint local_cache_size = 1<<12;

      class global_cache {
        constexpr static ptrint mask = global_cache_size-1;

        class entry {
          std::atomic_flag _lock = ATOMIC_FLAG_INIT;
          std::weak_ptr<target_base> _ptr;

          class maybe_lock {
            std::atomic_flag &_lock;
            const bool _locked;
          public:
            explicit maybe_lock(std::atomic_flag &flag)
            : _lock{flag},
              _locked{!flag.test_and_set()}
            {}
            ~maybe_lock() {
              if (_locked) {
                _lock.clear();
              }
            }
            operator bool() const {
              return _locked;
            }
          };
        public:

          template <typename Creator>
          std::shared_ptr<target_base> get(target_base *ptr, const Creator &creator) {
            // It would be nice to only grab the lock when we're changing it, but
            // reading a weak pointer isn't necessarily atomic, since there are
            // two pointers in it.  There's a proposal for an atomic_weak_ptr class,
            // but it's not in yet.  So we may create a spurious new one even if
            // the lock was held by somebody reading.  Hopefully, the thread-local
            // caches will keep this from happening too often.
            maybe_lock ml(_lock);
            if (!ml) {
              // We didn't grab the lock.  We can't read (it may be changing).
              // Just create and return
              return creator(ptr);
            }
            std::shared_ptr<target_base> sp = _ptr.lock();
            // Could be null, expired, or the wrong one.
            if (sp.get() != ptr) {
              sp = creator(ptr);
              _ptr = sp;
            }
            return sp;
          }
        };

        entry _entries[global_cache_size];
      public:
        template <typename Creator>
        std::shared_ptr<target_base> get(target_base *ptr, const Creator &creator) {
          ptrint i = reinterpret_cast<ptrint>(ptr) & mask;
          return _entries[i].get(ptr, creator);
        }

        static global_cache &instance() {
          static global_cache c;
          return c;
        }


      };

      class inbound_table {
      public:

        struct slot {
          gc_ptr<target_base> ptr;
          index_type next_free = 0;
          void reset(const gc_anchor &p) {
            assert(ptr == nullptr);
            ptr = p;
            next_free = 0;
          }
          void release(index_type nf) {
            next_free = nf;
            ptr = nullptr;
          }
        };


        using block_type = std::array<slot, block_size>;
        using spine_type = std::array<std::unique_ptr<block_type>, n_blocks>;

        spine_type _spine;
        volatile index_type _first_empty_block = 0;
        index_type _first_empty_slot = block_size;
        std::stack<index_type> _free_lists;
        std::mutex _mutex;

        using lock_type = std::lock_guard<std::mutex>;

        /*
         * This should probably be private and friended to mpgc::gc_handshake::initialize();
         */
        static inbound_table *table(bool from_gc) {
          static inbound_table *t = new inbound_table;
          if (!from_gc) {
            mpgc::initialize_thread();
          }
          return t;
        }

        static inbound_table &table() {
	  /*
	   * We intentionally leak the table, because we need to
	   * ensure that objects that were created before us (e.g.,
	   * statics) that have inbound pointers can be destroyed.
           *
           * The static is thread-local to ensure that table(false)
           * (and, therefore, mpgc::initialize()) are called in each
           * thread that first touch the GC by this path.
	   */
          static thread_local inbound_table *t = table(false);
          return *t;
        }

        slot &lookup(index_type b, index_type i) {
          auto &block = _spine[b];
          return (*block)[i];
        }

        slot &operator[](index_type i) {
          index_type b = i / block_size;
          index_type s = i % block_size;
          return lookup(b, s);
        }

        template <typename Fn>
        void for_each_slot(Fn&& func) {
          for (index_type b = 0; b < _first_empty_block; b++) {
            auto &block = *_spine[b];
            for (slot &slot : block) {
              std::forward<Fn>(func)(slot.ptr.as_offset_pointer());
            }
          }
        }

        constexpr static index_type index_of(index_type b, index_type i) {
          return b*block_size + i;
        }
        index_type get_free_list() {
          lock_type lock(_mutex);
          if (_free_lists.empty()) {
            if (_first_empty_slot == block_size) {
              _spine[_first_empty_block] = std::make_unique<block_type>();
              /* We must guarantee that the assignment takes before we increment
               * _first_empty_block otherwise there exist a race where the gc thread
               * may try to access an uninitialized block.
               */
              _first_empty_block++;
              _first_empty_slot = 0;
            }
            index_type b = _first_empty_block-1;
            lookup(b, _first_empty_slot).next_free = 0;
            index_type head = index_of(b, _first_empty_slot);
            _first_empty_slot++;
            return head;
          } else {
            index_type head = _free_lists.top();
            _free_lists.pop();
            return head;
          }
        }

        void release_free_list(index_type head) {
          lock_type lock(_mutex);
          _free_lists.push(head);
        }
      };

      class ic_control {
      public:
        constexpr static ptrint mask = local_cache_size -1;
        using cache_type = std::array<std::weak_ptr<target_base>, local_cache_size>;

        inbound_table &_table = inbound_table::table();
        global_cache &_global_cache = global_cache::instance();
        const std::unique_ptr<cache_type> _local_cache_ptr = std::make_unique<cache_type>();
        cache_type &_local_cache = *_local_cache_ptr;

        index_type _free = 0;

        ~ic_control() {
          if (_free != 0) {
            _table.release_free_list(_free);
          }
        }

        static ic_control &block() {
          static thread_local ic_control b;
          return b;
        }

        void release(index_type index) {
          auto &slot = _table[index];
          slot.release(_free);
          _free = index;
        }

        template <typename T>
        std::shared_ptr<T> lookup(const gc_ptr<T> &gcp) {
          if (gcp == nullptr) {
            return nullptr;
          }
          target_base *p = gcp.as_bare_pointer();
          ptrint i = reinterpret_cast<ptrint>(p) & mask;
          std::weak_ptr<target_base> &wp = _local_cache[i];
          std::shared_ptr<target_base> sp = wp.lock();
          if (sp == nullptr) {
            auto create = [this, &gcp](target_base *ptr) {
              if (_free == 0) {
                _free = _table.get_free_list();
              }
              index_type i = _free;
              auto &slot = _table[i];
              _free = slot.next_free;
              slot.reset(gcp);
              auto deleter = [i](target_base *) {
                ic_control::block().release(i);
              };
              std::shared_ptr<target_base> res{ptr, deleter};
              return res;
            };
            sp = _global_cache.get(p, create);
          }
          std::shared_ptr<const T> cp = std::static_pointer_cast<const T>(sp);
          return std::const_pointer_cast<T>(cp);
        }

      };

    }
    template <typename T>
    class external_gc_ptr
    {
      std::shared_ptr<T> _shared_ptr;
      T *bare_ptr() const {
        return _shared_ptr.get();
      }
      template <typename X, typename = std::enable_if_t<is_gc_allocated<X>::value>>
      explicit external_gc_ptr(const std::shared_ptr<X> &sp)
      : _shared_ptr(sp)
      {}

      template <typename X> using compatible = std::enable_if_t<std::is_convertible<X*,T*>::value>;
      template <typename X> friend class external_gc_ptr;
      template <typename X> friend class external_gc_sub_ptr;
    public:
      constexpr external_gc_ptr(nullptr_t np = nullptr) {}

      template <typename X, typename = compatible<X> >
      external_gc_ptr(const gc_ptr<X> &ptr)
      : external_gc_ptr{inbound_pointers::ic_control::block().lookup(ptr)}
      {}

      external_gc_ptr(const external_gc_ptr &) = default;
      template <typename X, typename = compatible<X> >
      external_gc_ptr(const external_gc_ptr<X> &ptr)
      : _shared_ptr{ptr._shared_ptr}
      {}

      external_gc_ptr(external_gc_ptr &&) = default;
      template <typename X, typename = compatible<X> >
      external_gc_ptr(external_gc_ptr<X> &&ptr)
      : _shared_ptr{std::move(ptr._shared_ptr)}
      {}

      external_gc_ptr &operator =(const external_gc_ptr &) = default;
      template <typename X, typename = compatible<X> >
      external_gc_ptr &operator =(const external_gc_ptr<X> &ptr) {
        _shared_ptr = ptr._shared_ptr;
        return *this;
      }

      external_gc_ptr &operator =(external_gc_ptr &&) = default;
      template <typename X, typename = compatible<X> >
      external_gc_ptr &operator =(external_gc_ptr<X> &&ptr) {
        _shared_ptr = std::move(ptr._shared_ptr);
        return *this;
      }


      gc_ptr<T> value() const {
        return gc_ptr_from_bare_ptr(bare_ptr());
      }
      T &operator *() const {
        return *bare_ptr();
      }
#if 0
      // Taking this out.  Now that we have a ctor for gc_ptr that
      // takes an external_gc_ptr, it shouldn't be necessary.
      template <typename X, typename = std::enable_if_t<std::is_convertible<T*,X*>::value> >
      operator gc_ptr<X>() const {
        return value();
      }
#endif      
      T *operator ->() const {
        return bare_ptr();
      }
      bool is_null() const {
        return !_shared_ptr;
      }

      template <typename X>
      bool operator==(const external_gc_ptr<X> &rhs) const {
        return _shared_ptr == rhs._shared_ptr;
      }
      template <typename X>
      bool operator==(const std::shared_ptr<X> &rhs) const {
        return _shared_ptr == rhs._shared_ptr;
      }
      template <typename X>
      bool operator==(const gc_ptr<X> &rhs) const {
        return _shared_ptr.get() == rhs.as_bare_pointer();
      }

      bool operator==(const T *rhs) const {
        return _shared_ptr.get() == rhs;
      }
      bool operator==(nullptr_t) const {
        return is_null();
      }
      template <typename X>
      bool operator !=(X&& rhs) const {
        return !((*this) == std::forward<X>(rhs));
      }

      void swap(external_gc_ptr &other) {
        std::swap(_shared_ptr, other._shared_ptr);
      }

      template <typename X, typename Y> friend external_gc_ptr<X> std::static_pointer_cast(const external_gc_ptr<Y> &);
      template <typename X, typename Y> friend external_gc_ptr<X> std::dynamic_pointer_cast(const external_gc_ptr<Y> &);
      template <typename X, typename Y> friend external_gc_ptr<X> std::const_pointer_cast(const external_gc_ptr<Y> &);

      constexpr std::uint64_t hash1() const {
        return ruts::hash1<T*>()(bare_ptr());
      }
      constexpr std::uint64_t hash2() const {
        return ruts::hash2<T*>()(bare_ptr());
      }

      template <typename S=T, typename E=std::enable_if_t<is_gc_array<S>::value> >
      typename S::size_type size() const {
        return _shared_ptr == nullptr ? 0 : _shared_ptr->size();
      }
      template <typename S=T, typename E=std::enable_if_t<is_gc_array<S>::value> >
      bool empty() const {
        /* There shouldn't be an array if the size is zero */
        return _shared_ptr == nullptr;
      }

      template <typename S=T, typename E=std::enable_if_t<is_gc_array<S>::value> >
      auto &operator[](typename S::size_type pos) const {
        // throw something if null
        return (*_shared_ptr)[pos];
      }

      template <typename S=T, typename E=std::enable_if_t<is_gc_array<S>::value && std::is_const<S>::value >>
                                                    operator typename S::const_iterator() const {
        return _shared_ptr == nullptr ? typename S::const_iterator{} : _shared_ptr->cbegin();
      }

      template <typename S=T, typename E=std::enable_if_t<is_gc_array<S>::value && !std::is_const<S>::value >>
                                                     operator typename S::iterator() const {
        return _shared_ptr == nullptr ? typename S::iterator{} : _shared_ptr->begin();
      }

      template <typename S=T, typename E=std::enable_if_t<is_gc_array<S>::value>>
                                                     auto operator +(typename S::const_iterator::difference_type delta) const {
        // If _shared_ptr is null, delta had better be zero.  To be safe, we'll just return null
        return _shared_ptr == nullptr ? decltype(_shared_ptr->begin()+delta){} : _shared_ptr->begin()+delta;
      }

      template <typename S=T, typename E=std::enable_if_t<is_gc_array<S>::value>>
        auto begin() const {
        return _shared_ptr == nullptr ? decltype(_shared_ptr->begin()){} : _shared_ptr->begin();
      }
      template <typename S=T, typename E=std::enable_if_t<is_gc_array<S>::value>>
        auto end() const {
        return _shared_ptr == nullptr ? decltype(_shared_ptr->end()){} : _shared_ptr->end();
      }
      template <typename S=T, typename E=std::enable_if_t<is_gc_array<S>::value>>
        auto cbegin() const {
        return _shared_ptr == nullptr ? decltype(_shared_ptr->cbegin()){} : _shared_ptr->cbegin();
      }
      template <typename S=T, typename E=std::enable_if_t<is_gc_array<S>::value>>
        auto cend() const {
        return _shared_ptr == nullptr ? decltype(_shared_ptr->cend()){} : _shared_ptr->cend();
      }
      template <typename S=T, typename E=std::enable_if_t<is_gc_array<S>::value>>
        auto rbegin() const {
        return _shared_ptr == nullptr ? decltype(_shared_ptr->rbegin()){} : _shared_ptr->rbegin();
      }
      template <typename S=T, typename E=std::enable_if_t<is_gc_array<S>::value>>
        auto rend() const {
        return _shared_ptr == nullptr ? decltype(_shared_ptr->rend()){} : _shared_ptr->rend();
      }
      template <typename S=T, typename E=std::enable_if_t<is_gc_array<S>::value>>
        auto crbegin() const {
        return _shared_ptr == nullptr ? decltype(_shared_ptr->crbegin()){} : _shared_ptr->crbegin();
      }
      template <typename S=T, typename E=std::enable_if_t<is_gc_array<S>::value>>
        auto crend() const {
        return _shared_ptr == nullptr ? decltype(_shared_ptr->crend()){} : _shared_ptr->crend();
      }


    };

    template <typename X, typename Y>
    bool operator==(const std::shared_ptr<X> &lhs, const external_gc_ptr<Y> &rhs) {
      return rhs == lhs;
    }
    template <typename X, typename Y>
    bool operator==(const gc_ptr<X> &lhs, const external_gc_ptr<Y> &rhs) {
      return rhs == lhs;
    }
    template <typename X, typename Y>
    bool operator==(X *lhs, const external_gc_ptr<Y> &rhs) {
      return rhs == lhs;
    }
    template <typename Y>
    bool operator==(nullptr_t, const external_gc_ptr<Y> &rhs) {
      return rhs.is_null();
    }
    template <typename X, typename Y>
    bool operator!=(const std::shared_ptr<X> &lhs, const external_gc_ptr<Y> &rhs) {
      return rhs != lhs;
    }
    template <typename X, typename Y>
    bool operator!=(const gc_ptr<X> &lhs, const external_gc_ptr<Y> &rhs) {
      return rhs != lhs;
    }
    template <typename X, typename Y>
    bool operator!=(X *lhs, const external_gc_ptr<Y> &rhs) {
      return rhs != lhs;
    }
    template <typename Y>
    bool operator!=(nullptr_t, const external_gc_ptr<Y> &rhs) {
      return !rhs.is_null();
    }

  /*
   * A smart pointer into a gc-allocated object.  Holds an anchor on the object
   * to ensure it doesn't go away
   */
  template <typename T>
  class external_gc_sub_ptr {
  public:
    using value_type = T;
    using element_type = value_type;
    using reference = external_gc_sub_ref<T>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::random_access_iterator_tag;
    using pointer = value_type *;
  private:
    external_gc_ptr<gc_allocated> _enclosing;
    value_type *_ptr;

    
  public:
    constexpr external_gc_sub_ptr(nullptr_t n = nullptr) : _enclosing{}, _ptr{} {}
    constexpr external_gc_sub_ptr(T *p, const  external_gc_ptr<gc_allocated> &e)
      : _enclosing{e}, _ptr{p}
    {}

    template <typename U, typename C,
              typename = std::enable_if_t<std::is_assignable<T*&, U*>::value> >
    constexpr external_gc_sub_ptr(U C::*m, const gc_ptr<C> &e) : external_gc_sub_ptr(&((*e).*m), e) {}

    external_gc_sub_ptr(const external_gc_sub_ptr &) = default;
    template <typename U, typename = std::enable_if_t<std::is_assignable<T*&, U*>::value> >
    constexpr external_gc_sub_ptr(const external_gc_sub_ptr<U> &rhs)
      : _enclosing{rhs._enclosing}, _ptr{rhs._ptr}
    {}
    template <typename U, typename = std::enable_if_t<std::is_assignable<T*&, U*>::value> >
    constexpr external_gc_sub_ptr(const gc_sub_ptr<U> &rhs)
      : _enclosing{rhs._enclosing}, _ptr{rhs.as_bare_pointer()}
    {}

    external_gc_sub_ptr(external_gc_sub_ptr &&) = default;
    template <typename U, typename = std::enable_if_t<std::is_assignable<T*&, U*>::value> >
    constexpr external_gc_sub_ptr(external_gc_sub_ptr<U> &&rhs)
      : _enclosing{std::move(rhs._enclosing)},
        _ptr{std::move(rhs._ptr)}
    {}

    external_gc_sub_ptr &operator =(const external_gc_sub_ptr &) = default;
    template <typename U, typename = std::enable_if_t<std::is_assignable<T*&, U*>::value> >
    external_gc_sub_ptr &operator =(const external_gc_sub_ptr<U> &rhs) {
      // TODO: There's a race condition here.  This will go away if
      // we require these to be on the stack objs.
      _enclosing = rhs._enclosing;
      _ptr = rhs._ptr;
      return *this;
    }
    template <typename U, typename = std::enable_if_t<std::is_assignable<T*&, U*>::value> >
    external_gc_sub_ptr &operator =(const gc_sub_ptr<U> &rhs) {
      // TODO: There's a race condition here.  This will go away if
      // we require these to be on the stack objs.
      _enclosing = rhs._enclosing;
      _ptr = rhs.as_bare_pointer();
      return *this;
    }

    external_gc_sub_ptr &operator =(external_gc_sub_ptr &&) = default;
    template <typename U, typename = std::enable_if_t<std::is_assignable<T*&, U*>::value> >
    external_gc_sub_ptr &operator =(external_gc_sub_ptr<U> &&rhs) {
      // TODO: There's a race condition here.  This will go away if
      // we require these to be on the stack objs.
      _ptr = std::move(rhs._ptr);
      _enclosing = std::move(rhs._enclosing);
    }

    constexpr bool is_null() const {
      return _ptr == nullptr;
    }

    constexpr bool operator ==(nullptr_t) const {
      return is_null();
    }
    template <typename U>
    constexpr bool operator ==(const external_gc_sub_ptr<U> &rhs) const {
      return _ptr == rhs._ptr;
    }
    template <typename U>
    constexpr bool operator ==(const gc_sub_ptr<U> &rhs) const {
      return _ptr == rhs.as_bare_pointer();
    }
    template <typename U>
    bool operator ==(U *ptr) const {
      return _ptr == ptr;
    }

    template <typename U>
    bool operator !=(U&& rhs) const {
      return !(*this == std::forward<U>(rhs));
    }

    /*
     * Returns true when bound to an object, false when unbound
     * If we allow this, then begin()+5 is technically ambiguous
     * between this->operator +(5) and static_cast<bool>(*this)+5;
     * Dumb.
     */

//    operator bool() const {
//      return _ptr != nullptr;
//    }

    value_type *operator ->() const {
      return _ptr;
    }

    reference operator *() const {
      assert(!is_null());
      return reference{_ptr, _enclosing};
    }

    reference operator [](size_type i) const {
      assert(!is_null());
      return reference{*(_ptr+i), _enclosing};
    }

    external_gc_sub_ptr &operator +=(difference_type diff) {
      _ptr += diff;
      return *this;
    }
    external_gc_sub_ptr &operator -=(difference_type diff) {
      return (*this) += (-diff);
    }

    external_gc_sub_ptr &operator ++() {
      return (*this) += 1;
    }
    external_gc_sub_ptr &operator --() {
      return (*this) -= 1;
    }

    external_gc_sub_ptr operator ++(int) {
      external_gc_sub_ptr old{*this};
      operator++();
      return old;
    }
    external_gc_sub_ptr operator --(int) {
      external_gc_sub_ptr old{*this};
      operator--();
      return old;
    }

    constexpr external_gc_sub_ptr operator +(difference_type diff) const {
      return external_gc_sub_ptr{_ptr+diff, _enclosing};
    }
    constexpr external_gc_sub_ptr operator -(difference_type diff) const {
      return external_gc_sub_ptr{_ptr-diff, _enclosing};
    }

    template <typename U>
    constexpr difference_type operator -(const external_gc_sub_ptr<U> &other) const {
      return _ptr - other._ptr;
    }
    template <typename U>
    constexpr difference_type operator -(const gc_sub_ptr<U> &other) const {
      return _ptr - other.as_bare_pointer();
    }

    template <typename U>
    bool operator <(const external_gc_sub_ptr<U> &other) const {
      return _ptr < other._ptr;
    }
    template <typename U>
    bool operator >(const external_gc_sub_ptr<U> &other) const {
      return _ptr > other._ptr;
    }
    template <typename U>
    bool operator <=(const external_gc_sub_ptr<U> &other) const {
      return _ptr <= other._ptr;
    }
    template <typename U>
    bool operator >=(const external_gc_sub_ptr<U> &other) const {
      return _ptr >= other._ptr;
    }

    template <typename U>
    bool operator <(const gc_sub_ptr<U> &other) const {
      return _ptr < other.as_bare_pointer();
    }
    template <typename U>
    bool operator >(const gc_sub_ptr<U> &other) const {
      return _ptr > other.as_bare_pointer();
    }
    template <typename U>
    bool operator <=(const gc_sub_ptr<U> &other) const {
      return _ptr <= other.as_bare_pointer();
    }
    template <typename U>
    bool operator >=(const gc_sub_ptr<U> &other) const {
      return _ptr >= other.as_bare_pointer();
    }

    template <typename C, typename Tr>
    std::basic_ostream<C,Tr> &print_on(std::basic_ostream<C,Tr> &os) const {
      const char *e = reinterpret_cast<const char *>(_enclosing.bare_ptr());
      const char *p = reinterpret_cast<const char *>(_ptr);
      return os << _enclosing << "+" << (p-e);
    }
  };

  template <typename U>
  inline bool operator ==(nullptr_t, const external_gc_sub_ptr<U> &rhs) {
    return rhs == nullptr;
  }
  template <typename T, typename U>
  inline bool operator ==(T *lhs, const external_gc_sub_ptr<U> &rhs) {
    return rhs == lhs;
  }
  template <typename T, typename U>
  inline bool operator ==(const gc_sub_ptr<U> &lhs, const external_gc_sub_ptr<U> &rhs) {
    return rhs == lhs;
  }
  template <typename U>
  inline bool operator !=(nullptr_t, const external_gc_sub_ptr<U> &rhs) {
    return rhs != nullptr;
  }
  template <typename T, typename U>
  inline bool operator !=(T *lhs, const external_gc_sub_ptr<U> &rhs) {
    return rhs != lhs;
  }
  template <typename T, typename U>
  inline bool operator !=(const gc_sub_ptr<T> &lhs, const external_gc_sub_ptr<U> &rhs) {
    return rhs != lhs;
  }
  template <typename T, typename U>
  inline bool operator <(const gc_sub_ptr<T> &lhs, const external_gc_sub_ptr<U> &rhs) {
    return rhs >= lhs;
  }
  template <typename T, typename U>
  inline bool operator <=(const gc_sub_ptr<T> &lhs, const external_gc_sub_ptr<U> &rhs) {
    return rhs > lhs;
  }
  template <typename T, typename U>
  inline bool operator >(const gc_sub_ptr<T> &lhs, const external_gc_sub_ptr<U> &rhs) {
    return rhs <= lhs;
  }
  template <typename T, typename U>
  inline bool operator >=(const gc_sub_ptr<T> &lhs, const external_gc_sub_ptr<U> &rhs) {
    return rhs < lhs;
  }

  template <typename T>
  class external_gc_sub_ref {
    const external_gc_ptr<gc_allocated> _enclosing;
    T &_ref;
    template <typename U> friend class external_gc_sub_ptr;
    external_gc_sub_ref(T &r, const external_gc_ptr<gc_allocated> &obj) : _enclosing{obj}, _ref{r} {}
  public:
    external_gc_sub_ref &operator =(const T &rhs) {
      _ref = rhs;
      return *this;
    }
    external_gc_sub_ref &operator =(T &&rhs) {
      _ref = std::move(rhs);
      return *this;
    }
    template <typename U>
    external_gc_sub_ref &operator =(const external_gc_sub_ref<U> &rhs) {
      _ref = rhs._ref;
      return *this;
    }
    external_gc_sub_ref &operator =(external_gc_sub_ref &rhs) {
      _ref = rhs._ref;
      return *this;
    }
    // Note: If you put this into a T& and the anchor goes away,
    // the reference may become invalid.
    operator T &() const {
      return _ref;
    }

    template <typename S = T>
    decltype(std::declval<S>().operator->()) operator ->() const {
      return _ref.operator->();
    }
  };

  template <typename T>
  struct gc_traits<external_gc_ptr<T>> {
    static_assert(ruts::fail_static_assert<T>(), "external_gc_ptr<T> is not GC-friendly");
  };

  template <typename T>
  struct gc_traits<external_gc_sub_ptr<T>> {
    static_assert(ruts::fail_static_assert<T>(), "external_gc_sub_ptr<T> is not GC-friendly");
  };

  template <typename T>
  struct gc_traits<external_gc_sub_ref<T>> {
    static_assert(ruts::fail_static_assert<T>(), "external_gc_sub_ref<T> is not GC-friendly");
  };
}

namespace ruts {

  template <typename T>
  struct hash1<mpgc::external_gc_ptr<T>> {
    auto operator()(const mpgc::external_gc_ptr<T> &ptr) const {
      return ptr.hash1();
    }
  };
  template <typename T>
  struct hash2<mpgc::external_gc_ptr<T>> {
    auto operator()(const mpgc::external_gc_ptr<T> &ptr) const {
      return ptr.hash2();
    }
  };

}


namespace std {
  template <typename C, typename T, typename X>
  basic_ostream<C,T> &
  operator <<(basic_ostream<C,T> &os, const mpgc::external_gc_ptr<X> &ptr) {
    return os << ptr.value();
  }
  
  template <typename C, typename T, typename X>
  basic_ostream<C,T> &
  operator <<(basic_ostream<C,T> &os, const mpgc::external_gc_sub_ptr<X> &ptr) {
    return os << ptr.value();
  }
  
  template <typename T>
  inline
  void swap(mpgc::external_gc_ptr<T> &lhs, mpgc::external_gc_ptr<T> &rhs) {
    lhs.swap(rhs);
  }

  template <typename T, typename U>
  mpgc::external_gc_ptr<T>
  static_pointer_cast(const mpgc::external_gc_ptr<U> &r) {
    return mpgc::external_gc_ptr<T>(std::static_pointer_cast<T>(r._shared_ptr));
  }

  template <typename T, typename U>
  inline
  mpgc::external_gc_ptr<T>
  dynamic_pointer_cast(const mpgc::external_gc_ptr<U> &r) {
    return mpgc::external_gc_ptr<T>(std::dynamic_pointer_cast<T>(r._shared_ptr));
  }

  template <typename T, typename U>
  inline
  mpgc::external_gc_ptr<T>
  const_pointer_cast(const mpgc::external_gc_ptr<U> &r) {
    return mpgc::external_gc_ptr<T>(std::const_pointer_cast<T>(r._shared_ptr));
  }

  template <typename T>
  struct hash<mpgc::external_gc_ptr<T>> : ruts::delegate_hash<mpgc::external_gc_ptr<T>> {};


}




#endif /* EXTERNAL_GC_PTR_H_ */