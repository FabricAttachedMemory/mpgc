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
 * gc.cpp
 *
 *  Created on: June 2, 2015
 *      Author: gidra
 */

#include <mutex>
#include <cassert>
#include <cstdlib>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "ruts/managed.h"
#include "mpgc/gc.h"

namespace {
  
  std::string heaps_dir() {
    static std::string from_env = ruts::env_string("MPGC_HEAPS_DIR");
    static std::string dir = from_env.empty() ? std::string("heaps") : from_env;
    return dir;
  }

  std::string heap_file(const char *evar, const char *def) {
    std::string from_env = ruts::env_string(evar);
    if (from_env.empty()) {
      return heaps_dir()+"/"+def;
    } else {
      return from_env;
    }
  }

  std::string gc_heap_file() {
    return heap_file("MPGC_GC_HEAP", "gc_heap");
  }

  std::string control_heap_file() {
    return heap_file("MPGC_CONTROL_HEAP", "managed_heap");
  }
}

std::string ruts::managed_space::name()
{
  return control_heap_file();
}

std::size_t ruts::managed_space::n_managed_slots() {
  return 100;
}


namespace mpgc {

  uint8_t* base_offset_ptr::_real_base = nullptr;
  uint8_t* base_offset_ptr::_signed_base = nullptr;
  uint8_t* base_offset_ptr::_heap_end = nullptr;
  std::size_t base_offset_ptr::_heap_size = 0;

  static gc_control_block *cblock = nullptr;

  void initialize() {
    static std::once_flag done;
    std::call_once(done, [] {
      int fd = open(gc_heap_file().data(), O_RDWR, S_IRUSR | S_IWUSR);
      assert(fd != -1);
      struct stat st;
      int ret = fstat(fd, &st);
      assert(ret == 0);

      uint8_t* p = static_cast<uint8_t*>(mmap(nullptr, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
      close(fd);
      if (p == MAP_FAILED)
        std::abort();

      base_offset_ptr::initialize(p, st.st_size);
      gc_control_block &block = ruts::managed_space::find_or_construct<gc_control_block>(42, p, st.st_size);
      gc_allocator::initialize(st.st_size, block.global_free_list);
      cblock = &block;
      gc_handshake::initialize1();
    });
    gc_handshake::initialize2();
  }

  gc_control_block &control_block() {
    static gc_control_block &b = *cblock;
    assert(&b);
    return b;
  }


}
