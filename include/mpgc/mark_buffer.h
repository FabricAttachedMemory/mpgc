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
 * mark_buffer.h
 *
 *  Created on: Jul 22, 2015
 *      Author: gidra
 */

#ifndef GC_MARK_BUFFER_H_
#define GC_MARK_BUFFER_H_

#include<cassert>
#include<atomic>
#include<algorithm>

#include "ruts/sesd_queue.h"
#include "ruts/managed.h"

namespace mpgc {
  template <typename T>
  class mark_buffer {
  public:
    static constexpr int32_t buffer_size = 254;

    struct buffer {
      volatile int32_t read_idx;
      volatile int32_t write_idx;
      T buf[buffer_size];
      buffer() : read_idx(-1), write_idx(0) {
        std::uninitialized_fill_n(buf, buffer_size, T());
      }
    };

    enum class Alive : unsigned char {
      Live,
      Dead
    };
  private:
    ruts::sesd_queue<buffer, ruts::managed_space::allocator<buffer>> _queue;
    Alive live;
  public:
    mark_buffer() : live(Alive::Live) {}

    void clear() {
      _queue.clear();
    }

    static bool is_marked(mark_buffer *b) {
      return b->live == Alive::Dead;
    }

    void mark_dead() { live = Alive::Dead; }

    constexpr bool is_empty() const {
      buffer *h = _queue.head();
      return h != _queue.tail() || (h && (h->write_idx - h->read_idx) > 1) ? false : true;
    }

    void add_element(const T &e) {
      buffer *b = _queue.tail();
      if (!b || b->write_idx == buffer_size) {
        b = _queue.enqueue();
      }
      assert(b->write_idx < buffer_size);
      b->buf[b->write_idx] = e;
      /* Following fence is required to avoid reordering of above store to buf
       * with the following store to write_idx.
       */
      std::atomic_thread_fence(std::memory_order_release);
      b->write_idx++;
    }

    template <typename Fn, typename ...Args>
    void process_element(Fn&& func, Args&& ...args) {
      buffer *b = _queue.head();
      //We do not verify things because this function is supposed to be called
      //after ensuring all that.
      assert(b && (b->write_idx - b->read_idx) > 1);
      std::forward<Fn>(func)(b->buf[b->read_idx + 1], std::forward<Args>(args)...);
      /* Following fence is required to avoid reordering of above load of buf
       * with the following store to read_idx.
       */
      std::atomic_thread_fence(std::memory_order_release);
      b->read_idx++;
      if (b->read_idx == buffer_size - 1) {
        buffer *temp = _queue.dequeue();
        assert(temp == b);
        _queue.destroy_entry(b);
      }
    }

  };
}



#endif /* GC_MARK_BUFFER_H_ */