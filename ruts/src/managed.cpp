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
 * managed.cpp
 *
 *  Created on: Aug 18, 2014
 *      Author: evank
 */



#include <atomic>
#include <memory>
#include <cassert>

#include "ruts/managed.h"
#include "ruts/runtime_array.h"

using namespace ruts;
using namespace pheap;

namespace {
}

class managed_space::control_block {
public:
  control_block(std::size_t n_slots) : _slots { n_slots }
  {
    for (auto &slot : _slots) {
      slot = nullptr;
    }
  }
  using slot_array_type = runtime_array<std::atomic<void *>, managed_space::allocator<std::atomic<void *>>>;

  slot_array_type _slots;
  std::atomic<void *> &lookup(std::size_t & which) {
    return _slots[which];
  }
};

persistent_heap &
managed_space::init_heap() {
  static persistent_heap heap { name() };


  return heap;
}

managed_space::control_block &
managed_space::find_control_block() {
  persistent_root<control_block> root { heap() };
  control_block *cb = root;
  if (cb == nullptr) {
    cb = root.construct_new(managed_space::n_managed_slots());
  }
  return *cb;

}

std::atomic<void *> &
ruts::managed_space::lookup_slot(std::size_t which) {
  return cblock().lookup(which);
}
