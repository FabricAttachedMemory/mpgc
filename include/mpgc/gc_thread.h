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
 * gc_thread.h
 *
 *  Created on: Jul 22, 2015
 *      Author: gidra
 */

#ifndef GC_GC_THREAD_H_
#define GC_GC_THREAD_H_

#include <sys/types.h>
#include <unistd.h>

#include<deque>
#include<cassert>
#include<cstdio>
#include<iomanip>
#include<cstring>
#include<string>

#include "ruts/util.h"
#include "ruts/runtime_array.h"
#include "ruts/managed.h"
#include "ruts/collections.h"
#include "ruts/atomic16B.h"
#include "mpgc/work_stealing_wq.h"
#include "mpgc/gc_allocated.h"
#include "mpgc/gc_desc.h"
#include "mpgc/mark_buffer.h"
#include "mpgc/offset_ptr.h"
#include "mpgc/gc_allocator.h"
/*
 * This class contains all the per-process structures.
 */
namespace mpgc {
  namespace gc_handshake {
    enum class Signum : char;
  }
  enum class Stage : unsigned char;

  struct status_global_list_idx {
    gc_handshake::Signum status;
    uint8_t idx;
    status_global_list_idx(gc_handshake::Signum s, uint8_t idx = 0) : status(s), idx(idx) {}
  };
  union gc_status {
    status_global_list_idx status_idx;
    uint16_t data;
    gc_status(gc_handshake::Signum s) : status_idx(status_global_list_idx(s)) {}
    gc_status(gc_handshake::Signum s, uint8_t idx) : status_idx(status_global_list_idx(s, idx)) {}
    gc_status(uint16_t d) : data(d) {}
    gc_status() : data(0) {}

    gc_handshake::Signum status() { return status_idx.status; }
    uint8_t index() { return status_idx.idx; }
  };

  //The enuruts below are intentionally kept bigger than uint8_t to keep the atomics word aligned
  enum Barrier_indices : uint16_t {
    marking2 = 0,
    sync,
    preMarking,
    preSweep,
    sweep1,
    sweep2,
    postSweep,
    arraysize,
    marking1 //must be the last one
  };

  using Mbuf = mark_buffer<offset_ptr<const gc_allocated>>;
  using Mark_buffer_list = ruts::sequential_lazy_delete_collection<Mbuf, ruts::managed_space::allocator<Mbuf>>;
  using Traversal_queue = work_stealing_wq<offset_ptr<const gc_allocated>>;
  using Pre_sweep_list = std::deque<std::size_t, ruts::managed_space::allocator<std::size_t>>;

  using pcount_t = uint16_t;//any variable which needs to hold process count must use this.

  struct marking1_barrier_t {
    pcount_t barrier;
    pcount_t version;
    marking1_barrier_t() noexcept : barrier(0), version(0) {}
  };

  class per_process_struct {
  public:
    enum class Alive : uint32_t {
      Live,
      Dead
    };

    enum class Barrier_stage : uint16_t {
      unincremented = 0,
      incrementing,
      incremented
    };

    struct alignas(16) liveness {
      unsigned long long creation_time;
      pid_t pid;
      Alive is_live;

      liveness() : creation_time(0), pid(0), is_live(Alive::Live) {}
      liveness(pid_t p) : pid(p), is_live(Alive::Live) {
        creation_time = get_creation_time(pid);
      }
      liveness(pid_t p, unsigned long long t) : creation_time(t), pid(p), is_live(Alive::Dead) {}
      liveness(pid_t p, unsigned long long t, Alive live) : creation_time(t), pid(p), is_live(live) {}
    };

    union Barrier_info {
      struct {
        marking1_barrier_t _barrier;//Will work for other barriers too
        Barrier_indices _barrier_idx;
        Barrier_stage _bstage;
      } _info;
      uint64_t _data;
      Barrier_info() : _data(0) {}
    };

    struct buffer_pair {
      Mbuf::buffer *begin;
      Mbuf::buffer *end;
      buffer_pair() : begin(nullptr), end(nullptr) {}
    };
  private:
    union {
      std::size_t sweep_nr_chunk = 0;
      offset_ptr<const gc_allocated> marking_obj_ref;
    };

    ruts::atomic16B<liveness> _liveness;
    Barrier_info _binfo;
    Mark_buffer_list _mark_buffer_list;

    Traversal_queue _tqueue;
    Pre_sweep_list  _pre_sweep_list;

    volatile gc_status _status;

  public:
   per_process_struct () :
      _liveness(liveness(getpid())),
      _tqueue(),
      _pre_sweep_list()
    {
      assert(sizeof(liveness) <= 16);
    }

    ~per_process_struct() {
      _tqueue.~Traversal_queue();
      _pre_sweep_list.~Pre_sweep_list();
      _mark_buffer_list.~Mark_buffer_list();
    }

    void mark_dead() {
      liveness temp = _liveness;
      temp.is_live = Alive::Dead;
      _liveness = temp;
    }

    bool mark_dead(liveness expected) {
      liveness desired = expected;
      desired.is_live = Alive::Dead;
      //false may be returned only if someone has taken ownership and is working on it (or has died while doing so).
      return _liveness.compare_exchange_strong(expected, desired) || expected.is_live == Alive::Dead;
    }

    static bool is_marked(per_process_struct *p) {
      return p->_liveness.load().is_live == Alive::Dead;
    }
    bool steal(Traversal_queue &other) {
      return _tqueue.steal(other);
    }

    void set_marking_ref(offset_ptr<const gc_allocated> ref) {
      assert(!marking_obj_ref);
      marking_obj_ref = ref;
    }
    offset_ptr<const gc_allocated>& marking_ref() {
      return marking_obj_ref;
    }
    void clear_marking_ref() {
      marking_obj_ref = nullptr;
    }

    void reset_tolerate_sweep_chunk() {
      sweep_nr_chunk = 0;
    }

    std::size_t& get_tolerate_sweep_chunk() {
      return sweep_nr_chunk;
    }

    Barrier_info get_barrier_info() {
      return _binfo;
    }

    pcount_t &barrier_id_ref() {
      return _binfo._info._barrier.barrier;
    }

    pcount_t get_barrier_id() {
      return _binfo._info._barrier.barrier;
    }

    pcount_t get_barrier_version() {
      return _binfo._info._barrier.version;
    }

    pcount_t &barrier_version_ref() {
      return _binfo._info._barrier.version;
    }

    marking1_barrier_t &marking1_barrier_ref() {
      return _binfo._info._barrier;
    }

    marking1_barrier_t get_marking1_barrier() {
      return _binfo._info._barrier;
    }

    void set_barrier_incrementing() {
      _binfo._info._bstage = Barrier_stage::incrementing;
    }

    void set_barrier_incremented() {
      _binfo._info._bstage = Barrier_stage::incremented;
    }

    Barrier_stage get_barrier_stage() {
      return _binfo._info._bstage;
    }

    Barrier_indices get_barrier_index() {
      return _binfo._info._barrier_idx;
    }
    void reset_barrier_info(Barrier_indices barrier_idx) {
      Barrier_info temp;
      temp._info._barrier_idx = barrier_idx;
      if (barrier_idx == Barrier_indices::marking2 || barrier_idx == Barrier_indices::marking1) {
        temp._info._barrier.version = _binfo._info._barrier.version;
      }
      _binfo._data = temp._data;
    }

    const uint8_t global_list_index() { return _status.status_idx.idx;}
    const gc_handshake::Signum status() { return _status.status_idx.status;}
    const uint16_t get_gc_status() { return _status.data;}

    void set_gc_status(uint16_t d) { _status.data = d;}
    void set_status(gc_handshake::Signum s) { _status.status_idx.status = s;}

    Mark_buffer_list &mark_buffer_list() {
      return _mark_buffer_list;
    }

    Traversal_queue &traversal_queue() {
      return _tqueue;
    }

    Pre_sweep_list &pre_sweep_list() {
      return _pre_sweep_list;
    }

    void clear() {
      _mark_buffer_list.deletion(Mbuf::is_marked);
    }

    liveness get_liveness() { return _liveness.load();}

    bool set_liveness(liveness expected, liveness desired) {
      return _liveness.compare_exchange_strong(expected, desired);
    }

    static unsigned long long get_creation_time(pid_t pid) {
      std::FILE *file = std::fopen(("/proc/" + std::to_string(pid) + "/stat").c_str(), "r");
      if (file) {
        unsigned long long start_time;
        std::fscanf(file, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %llu", &start_time);
        std::fclose(file);
        return start_time;
      }
      return -1;
    }
  };

  class mark_bitmap {
    using rep_t = std::size_t;
    using atomic_rep_t = std::atomic<rep_t>;
    using bitmap_idx_t = std::size_t;
    using bit_number_t = std::size_t;
    using bitmap_type = ruts::runtime_array<atomic_rep_t, ruts::managed_space::allocator<atomic_rep_t>>;
    using Allocator = ruts::managed_space::allocator<atomic_rep_t>;

    static constexpr uint8_t bits_per_value = sizeof(rep_t) * 8;
    static constexpr uint8_t value_log_bits = 6;
    static constexpr uint8_t chunk_size_log_bits = 10;

    const std::size_t _size;
    const std::size_t _total_logical_chunks;
    const std::size_t _sweep_bitmap_size;
    Allocator _alloc;

    atomic_rep_t * const _begin;
    atomic_rep_t * const _end;

    /* Sweep bitmap is a bitmap over the mark-bitmap. It is required
     * in order to have a lock-free sweep function. There is a bit
     * field for each logical chunk of the mark-bitmap, both in begin
     * and end. A logical chunk is a single entity swept by a GC thread
     * at a time. In other words, it is the smallest entity that a GC
     * thread sweeps in one instance.
     *
     * The meaning of 0s and 1s is toggled after every GC cycle.
     */
    atomic_rep_t * const _sweep_bitmap_begin;
    atomic_rep_t * const _sweep_bitmap_end;

    std::atomic<std::size_t> _logical_chunks;
    std::atomic<std::size_t> _sweep_bitmap_words;

    void fetch_logical_chunk_to_process(std::size_t &i) {
      i = _logical_chunks;
      while (!_logical_chunks.compare_exchange_weak(i, i + 1));
    }

    void fetch_sweep_bitmap_word_to_process(std::size_t &i) {
      i = _sweep_bitmap_words;
      while(!_sweep_bitmap_words.compare_exchange_weak(i, i + 1));
    }

    static constexpr rep_t construct_left_mask(const bit_number_t bit) {
      return rep_t(-1) >> bit;
    }

    static constexpr rep_t construct_right_mask(const bit_number_t bit) {
      return rep_t(-1) << ((bits_per_value - 1) - bit);
    }

    static constexpr rep_t construct_bitmap_word(const bit_number_t bit) {
      return rep_t(1) << ((bits_per_value - 1) - bit);
    }

    static constexpr bitmap_idx_t compute_bitmap_index(const std::size_t byte_offset) {
      return byte_offset >> (value_log_bits + 3);//3 for word size
    }

    static bit_number_t compute_bit_number(const std::size_t byte_offset) {
      return (byte_offset >> 3) & (bits_per_value - 1);//3 for word size
    }

    void set_sweep_bitmap(atomic_rep_t &word, const rep_t desired, const bool set) {
      set ? word.fetch_or(desired) : word.fetch_and(~desired);
    }

    void set_sweep_bitmap_begin(const std::size_t nr_chunk, const bool set) {
      set_sweep_bitmap(_sweep_bitmap_begin[nr_chunk >> value_log_bits],
                       construct_bitmap_word(nr_chunk & (bits_per_value - 1)),
                       set);
    }

    void set_sweep_bitmap_end(const std::size_t nr_chunk, const bool set) {
      set_sweep_bitmap(_sweep_bitmap_end[nr_chunk >> value_log_bits],
                       construct_bitmap_word(nr_chunk & (bits_per_value - 1)),
                       set);
    }

    void set_sweep_bitmap_both(const std::size_t nr_chunk, const bool set) {
      const std::size_t index = nr_chunk >> value_log_bits;
      const std::size_t desired = construct_bitmap_word(nr_chunk & (bits_per_value - 1));
      set_sweep_bitmap(_sweep_bitmap_end[index], desired, set);
      set_sweep_bitmap(_sweep_bitmap_begin[index], desired, set);
    }

    bool is_end_sweep_bitmap_set(const std::size_t nr_chunk, const bool set) {
      const rep_t val = _sweep_bitmap_end[nr_chunk >> value_log_bits];
      const rep_t expected = construct_bitmap_word(nr_chunk & (bits_per_value - 1));
      return set ? val & expected : ~val & expected;
    }

    void _post_sweep_clear(atomic_rep_t &, atomic_rep_t *, const bool);
    void _set_sweep_bitmap_range(const std::size_t, const std::size_t, const bool);

    static std::size_t compute_bitmap_size(std::size_t heap_size) {
      //The count we receive is in bytes.
      //3 bits for bytes per word, and n bits for bits per rep_t.
      //Each bit represents a word in the heap.
      assert(!(heap_size & ((rep_t(1) << (value_log_bits + 3)) - 1)));
      return compute_bitmap_index(heap_size);
    }

    atomic_rep_t &lookup_begin(const bitmap_idx_t idx) {
      assert(idx < _size);
      return _begin[idx];
    }

    atomic_rep_t &lookup_end(const bitmap_idx_t idx) {
      assert(idx < _size);
      return _end[idx];
    }

    bool mark_begin(const bitmap_idx_t idx, const bit_number_t bit) {
      atomic_rep_t &B = lookup_begin(idx);
      rep_t desired = construct_bitmap_word(bit);
      const rep_t res = B.fetch_or(desired);
      return !(res & desired);
    }

    void mark_end(const bitmap_idx_t idx, const bit_number_t bit) {
      atomic_rep_t &B = lookup_end(idx);
      rep_t desired = construct_bitmap_word(bit);
      B.fetch_or(desired);
    }

    bool is_end_marked(const bitmap_idx_t idx, const bit_number_t bit) {
      return lookup_end(idx) & construct_bitmap_word(bit) ? true : false;
    }

    bool _mark_end_first(const std::size_t beg_byte, const std::size_t end_byte) {
      mark_end(compute_bitmap_index(end_byte), compute_bit_number(end_byte));
      if (mark_begin(compute_bitmap_index(beg_byte), compute_bit_number(beg_byte))) {
        return true;
      }
      return false;
    }

    bool _mark_begin_first(const std::size_t beg_byte, const std::size_t end_byte) {
      if (mark_begin(compute_bitmap_index(beg_byte), compute_bit_number(beg_byte))) {
        mark_end(compute_bitmap_index(end_byte), compute_bit_number(end_byte));
        return true;
      }
      return false;
    }

    bool is_marked(const bitmap_idx_t idx, const bit_number_t bit) {
      return lookup_begin(idx) & construct_bitmap_word(bit) ? true : false;
    }

    /* Let us keep it simple for now. We want every entry in the sweep_bitmap
     * to cover 1K of rep_t mark_bitmap entries (8K memory).
     * TODO: This function needs to be more sophisticated in the way we deal
     * with smaller heap sizes etc.
     */
    static constexpr std::size_t compute_logical_chunk_count(std::size_t s) {
      return s >> chunk_size_log_bits;
    }

    // Input is number of logical chunks of mark-bitmap.
    static constexpr std::size_t compute_sweep_bitmap_size(std::size_t s) {
      return s >> value_log_bits;
    }

    void clear_chunk_begin(const std::size_t nr_chunk) {
      std::memset(_begin + (nr_chunk << chunk_size_log_bits), 0x0, sizeof(atomic_rep_t) << chunk_size_log_bits);
    }

    void clear_chunk_end(const std::size_t nr_chunk) {
      std::memset(_end + (nr_chunk << chunk_size_log_bits), 0x0, sizeof(atomic_rep_t) << chunk_size_log_bits);
    }

  public:

    static std::size_t compute_total_bitmap_size(const std::size_t heap_size) {
      std::size_t bitmap_size = compute_bitmap_size(heap_size);
      bitmap_size += compute_sweep_bitmap_size(compute_logical_chunk_count(bitmap_size));
      return bitmap_size * sizeof(atomic_rep_t) * 2;
    }

    mark_bitmap(std::size_t heap_size, const Allocator &alloc = Allocator()) :
                                         _size(compute_bitmap_size(heap_size)),
                                         _total_logical_chunks(compute_logical_chunk_count(_size)),
                                         _sweep_bitmap_size(compute_sweep_bitmap_size(_total_logical_chunks)),
                                         _alloc(alloc),
                                         _begin(_alloc.allocate((_size + _sweep_bitmap_size) * 2)),
                                         _end(_begin + _size),
                                         _sweep_bitmap_begin(_end + _size),
                                         _sweep_bitmap_end(_sweep_bitmap_begin + _sweep_bitmap_size),
                                         _logical_chunks(0),
                                         _sweep_bitmap_words(0)
  {
      /* TODO: This is too much of work. We have to come up with a way where we don't need to
       * clear all the bitmaps because we can come up with a solution where the bitmaps are
       * allocated on a new files which are already zero-initialized.
       */
      std::memset(_begin, 0x0, 2 * sizeof(atomic_rep_t) * (_size + _sweep_bitmap_size));
  }

    ~mark_bitmap() {
      _alloc.deallocate(_begin, 1);
    }

    void clear() {
      std::memset(_begin, 0x0, _size * sizeof(atomic_rep_t));
      std::memset(_end, 0x0, _size * sizeof(atomic_rep_t));
    }

    void print() {
      ruts::reset_flags_on_exit reset(std::cout);
      std::cout << std::setfill('0') << std::hex;
      for (bitmap_idx_t i = 0; i < _size;) {
        std::cout << "[" << std::setw(3) << i << "]";
        std::cout << std::setw(16) << _begin[i] << ":" << std::setw(16) << _end[i];
        std::cout << "\t";
        i++;
        if (i % 4 == 0) {
          std::cout << "\n";
        }
      }
    }

    bool is_marked(const offset_ptr<const gc_allocated> &p) {
      const std::size_t beg_byte = p.offset();
      return is_marked(compute_bitmap_index(beg_byte), compute_bit_number(beg_byte));
    }

    bool mark_end_first(const offset_ptr<const gc_allocated> &p) {
      assert(p->get_gc_descriptor().is_valid());
      const std::size_t beg_byte = p.offset();
      const std::size_t end_byte = beg_byte + ((p->get_gc_descriptor().object_size() - 1) << 3);
      return _mark_end_first(beg_byte, end_byte);
    }

    bool mark_begin_first(const offset_ptr<const gc_allocated> &p) {
      assert(p->get_gc_descriptor().is_valid());
      const std::size_t beg_byte = p.offset();
      const std::size_t end_byte = beg_byte + ((p->get_gc_descriptor().object_size() - 1) << 3);
      return _mark_begin_first(beg_byte, end_byte);
    }

    std::size_t find_next_free_word(std::size_t word, std::size_t end, bool &found_set_bit) const {
      bit_number_t bit = compute_bit_number(word << 3);
      bitmap_idx_t idx = compute_bitmap_index(word << 3);
      bitmap_idx_t end_idx = compute_bitmap_index(end << 3);
      do {
        rep_t B = _end[idx] & construct_left_mask(bit);
        while (B == 0) {
          idx++;
          if (idx == end_idx) {
            return idx << value_log_bits;
          }
          B = _end[idx];
        }
        found_set_bit = true;
        if (B == 1) {
          idx++;
          if (idx == end_idx) {
            return idx << value_log_bits;
          }
          bit = 0;
        } else {
          bit = __builtin_clzl(B) + 1;
        }
      } while (_begin[idx] & construct_bitmap_word(bit));
      return (idx << value_log_bits) + bit;
    }

    std::size_t find_next_used_word(std::size_t word, std::size_t end) const {
      bit_number_t bit = compute_bit_number(word << 3);
      bitmap_idx_t idx = compute_bitmap_index(word << 3);
      bitmap_idx_t end_idx = compute_bitmap_index(end << 3);
      if (idx >= end_idx) {
        return idx << value_log_bits;
      }
      rep_t B = _begin[idx] & construct_left_mask(bit);
      while (B == 0) {
        idx++;
        if (idx == end_idx) {
          return idx << value_log_bits;
        }
        B = _begin[idx];
      }
      return (idx << value_log_bits) + __builtin_clzl(B);
    }

    //Actually, this function returns the next used word + 1.
    std::size_t find_prev_used_word(std::size_t word) const {
      bit_number_t bit = compute_bit_number(word << 3);
      bitmap_idx_t idx = compute_bitmap_index(word << 3);
      rep_t B = _end[idx];
      B &= construct_right_mask(bit);
      while (B == 0) {
        if (idx == 0) {
          return 0;
        }
        B = _end[--idx];
      }
      return (idx << value_log_bits) + (bits_per_value - __builtin_ctzl(B));
    }

    void reset_logical_chunk_count() {
      _logical_chunks = 0;
      _sweep_bitmap_words = 0;
    }

    //nr_chunk: chunk number from where to start.
    std::size_t process_next_chunk_begin(std::size_t nr_chunk, const bool set_bit) {
      constexpr std::size_t bits_to_shift = chunk_size_log_bits + value_log_bits;
      std::size_t start = nr_chunk << bits_to_shift;
      std::size_t end = (nr_chunk + 1) << bits_to_shift;
      while (start < (_size << value_log_bits)) {
        start = find_next_used_word(start, end);
        if (start < end) {
          /* TODO: We can have an optimization here. If the set bit is the
           * last bit of the _begin chunk, then we can set the _end bitmap
           * to be clear. Also, by just resetting the last bit of _begin
           * bitmap, even it can be set to be clear.
           */
          break;
        }
        assert(start == end);
        end += 1 << bits_to_shift;
        //atomically set bits. No need to clear _begin as we are here coz its all cleared
        set_sweep_bitmap_both(nr_chunk, set_bit);
        nr_chunk++;
      }
      return start;
    }

    void test_bitmaps(const bool set_bitmap) {
      std::size_t i;
      for(i = 0; i < _sweep_bitmap_size; i++) {
        set_bitmap ? assert(_sweep_bitmap_begin[i] == rep_t(-1) && _sweep_bitmap_end[i] == rep_t(-1)) :
                     assert(_sweep_bitmap_begin[i] == 0 && _sweep_bitmap_end[i] == 0);
      }

      for(i = 0; i < _size; i++) {
        assert(_begin[i] == 0 && _end[i] == 0);
      }
    }

    bool expand_free_chunk(offset_ptr<gc_allocator::global_chunk> c,
                           std::size_t size,
                           std::size_t &beg_word,
                           std::size_t &end_word) {
      beg_word = c.offset() >> 3;
      end_word = beg_word + (size >> 3);

      beg_word = find_prev_used_word(beg_word);
      end_word = find_next_used_word(end_word, _size << value_log_bits);
      //find_next_used_word returns the next used word. So we must decrement by 1.
      return _mark_begin_first(beg_word << 3, (end_word - 1) << 3);
    }

    void post_sweep_phase(per_process_struct*, const bool);
    bool post_sweep_phase_without_load_balancing(per_process_struct*, const bool);
    void post_sweep_clear(const std::size_t, const bool);
    void process_logical_chunk(gc_allocator::globalListType&, const std::size_t, const bool);
    void set_sweep_bitmap_range(const std::size_t, const std::size_t, const bool);
    void sweep2_phase(const bool);
  };
}

#endif /* GC_GC_THREAD_H_ */
