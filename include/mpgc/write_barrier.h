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
 * write_barrier.h
 *
 *  Created on: June 27, 2016
 *      Author: gidra
 */

#ifndef GC_WRITE_BARRIER_H_
#define GC_WRITE_BARRIER_H_

#include "mpgc/gc_handshake.h"

namespace mpgc {
  /*
   * The function is used to mark gray an object. Marking gray
   * means adding the object reference in the mark buffer.
   * Called by write barrier and stack scanning function.
   */
  inline void mark_gray(const offset_ptr<const gc_allocated> p, gc_handshake::in_memory_thread_struct &thread_struct) {
    if (p.is_valid() && !thread_struct.bitmap->is_marked(p)) {
      thread_struct.mbuffer->add_element(p);
    }
  }

  /*
   * The write barrier function that is called on reference update.
   * The barrier, in order to be atomic wrt. sync and async phases,
   * defers handshake in the beginning, does the barrier and reference
   * update and then checks if a handshake is pending.
   */
  template <typename Fn>
  inline void write_barrier(const offset_ptr<const gc_allocated> &lhs,
                     const offset_ptr<const gc_allocated> &rhs,
                     Fn&& func) {
    const offset_ptr<const gc_allocated> l = lhs;
    assert(!l.is_valid() || l->get_gc_descriptor().is_valid());
    assert(!rhs.is_valid() || rhs->get_gc_descriptor().is_valid());

    // If lhs and rhs are both same, then we don't need to trigger the barrier.
    if (lhs == rhs) {
      std::forward<Fn>(func)();
      return;
    }

    //Is there a way to avoid the function call to fetch the thread_local?
    gc_handshake::in_memory_thread_struct &thread_struct = *gc_handshake::thread_struct_handles.handle;
    thread_struct.mark_signal_disabled = true;

    /* The following signal_fence because the sync/async disabling above
     * *must* happen before the pointer is added to the mark_buffer.
     */
    std::atomic_signal_fence(std::memory_order_release);

    switch (thread_struct.status_idx.load().status()) {
    case gc_handshake::Signum::sigSync1:
    case gc_handshake::Signum::sigSync2:
      mark_gray(rhs, thread_struct);
    case gc_handshake::Signum::sigAsync:
      mark_gray(lhs, thread_struct);
    default: break;
    }

    //Perform the reference update operation
    std::forward<Fn>(func)();

    /* The following signal_fence because the reference update above
     * *must* happen before the handshake is enabled below.
     */
    std::atomic_signal_fence(std::memory_order_release);

    thread_struct.mark_signal_disabled = false;
    switch (thread_struct.mark_signal_requested) {
    case gc_handshake::Signum::sigSync1:
    case gc_handshake::Signum::sigSync2:
      thread_struct.status_idx = gc_status(thread_struct.mark_signal_requested, thread_struct.status_idx.load().index());
      break;
    case gc_handshake::Signum::sigAsync:
      gc_handshake::do_deferred_async_signal(thread_struct);
    default: break;
    }
    thread_struct.mark_signal_requested = gc_handshake::Signum::sigInit;
  }
}

#endif /* GC_WRITE_BARRIER_H_ */
