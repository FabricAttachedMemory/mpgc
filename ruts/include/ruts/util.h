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
 * util.h
 *
 *  Created on: Sep 21, 2014
 *      Author: evank
 */

#ifndef UTIL_H_
#define UTIL_H_

#include <iostream>
#include <limits>
#include <string>
#include <sstream>
#include "ruts/meta.h"

namespace ruts {
  
  bool env_flag(const char *var);
  std::string env_string(const char *var);

  class reset_flags_on_exit {
    std::ios_base &_stream;
    decltype(_stream.flags()) _flags = _stream.flags();
  public:
    explicit reset_flags_on_exit(std::ios_base &stream) : _stream(stream) {}
    ~reset_flags_on_exit() {
      _stream.flags(_flags);
    }
  };


  // I get a redefinition error if I try to overload based on is_signed and is_unsigned
  template <typename T, typename = std::enable_if_t<std::is_integral<T>::value>
  >
  inline
  constexpr
  std::remove_reference_t<T> rotate(T val) {
    using ut = std::make_unsigned_t<T>;
    return std::is_signed<T>::value ?
        reinterpret_cast<T>(rotate(reinterpret_cast<ut>(val)))
        :
        ((val & T{1}) << (std::numeric_limits<T>::digits-1))
                |
                (val >> 1)
                ;
  }

  template <typename Iter>
  class range {
    Iter _from;
    Iter _to;
  public:
    range(const Iter &from, const Iter &to) : _from{from}, _to{to} {}
    Iter begin() const {
      return _from;
    }

    Iter end() const {
      return _to;
    }

    std::size_t size() const {
      return _to - _from;
    }
  };

  template <typename I>
  inline range<I> range_over(const I &from, const I &to) {
    return range<I>{from,to};
  }

  template <typename T>
  inline
  range<typename T::iterator>
  range_of(T &obj) {
    return range_over(obj.begin(), obj.end());
  }

  template <typename T>
  inline
  range<typename T::const_iterator>
  range_of(const T &obj) {
    return range_over(obj.cbegin(), obj.cend());
  }


  /*
   * GCC doesn't do codecvt as of 4.8.3
   */

  template <typename C, typename T, typename A, typename = std::enable_if_t<sizeof(C)>=sizeof(char16_t)>>
  std::string to_utf8(const std::basic_string<C,T,A> &s) {
    using string_type = std::basic_string<C,T,A>;
    using char_type = typename string_type::value_type;
    std::size_t size = 0;
    for (char_type c : s) {
      if ((c & ~0x7F) == 0) {
        size++;
      } else if ((c & ~0x7FF) == 0) {
        size+=2;
      } else if ((c & ~0xFFFF) == 0) {
        size+=3;
      } else if ((c & ~0x1FFFFF) == 0) {
        size+=4;
      } else if ((c & ~0x3FFFFFF) == 0) {
        size+=5;
      } else {
        size += 6;
      }
    }
    std::string utf8;
    utf8.reserve(size);
    for (char_type c : s) {
      if ((c & ~0x7F) == 0) {
        utf8.push_back(static_cast<char>(c & 0x7F));
      } else if ((c & ~0x7FF) == 0) {
        utf8.push_back(static_cast<char>(0xC0 | ((c >> 6) & 0x1F)));
        utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
      } else if ((c & ~0xFFFF) == 0) {
        utf8.push_back(static_cast<char>(0xE0 | ((c >> 12) & 0x0F)));
        utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
      } else if ((c & ~0x1FFFFF) == 0) {
        utf8.push_back(static_cast<char>(0xF0 | ((c >> 18) & 0x07)));
        utf8.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
      } else if ((c & ~0x3FFFFFF) == 0) {
        utf8.push_back(static_cast<char>(0xF8 | ((c >> 24) & 0x03)));
        utf8.push_back(static_cast<char>(0x80 | ((c >> 18) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
      } else {
        utf8.push_back(static_cast<char>(0xFC | ((c >> 30) & 0x03)));
        utf8.push_back(static_cast<char>(0x80 | ((c >> 24) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | ((c >> 18) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
      }
    }
    return utf8;
  }

  template <typename E, typename = std::enable_if_t<std::is_enum<E>::value> >
  inline constexpr E enum_plus(E base, std::size_t index) {
    return static_cast<E>(static_cast<std::size_t>(base)+index);
  }

  template <typename E, typename RT = std::size_t, typename = std::enable_if_t<std::is_enum<E>::value> >
  inline constexpr RT index(E e) {
    return static_cast<RT>(e);
  }

  template <typename Fn>
  inline std::string format(Fn &&fn) {
    std::stringstream stream;
    std::forward<Fn>(fn)(stream);
    return stream.str();
  }

  template <typename T>
  inline std::string to_string(T&& val) {
    std::stringstream stream;
    stream << std::forward<T>(val);
    return stream.str();
  }


  template <typename Fn>
  class in_dtor {
    const Fn _func;
  public:
    explicit in_dtor(const Fn &fn) : _func{fn} {}
    ~in_dtor() {
      _func();
    }
  };

  template <typename Fn>
  in_dtor<Fn> cleanup(const Fn &fn) {
    return in_dtor<Fn>{fn};
  }
  

  template <typename...Args>
  constexpr bool fail(Args&&...) {
    return false;
  }

  template <typename T>
  constexpr bool fail_static_assert() {
    return false;
  }

}




#endif /* UTIL_H_ */
