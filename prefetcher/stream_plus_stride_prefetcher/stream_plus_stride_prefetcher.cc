// #include "cache.h"

// void CACHE::prefetcher_initialize() {}

// uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in) { return metadata_in; }

// uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
// {
//   return metadata_in;
// }

// void CACHE::prefetcher_cycle_operate() {}

// void CACHE::prefetcher_final_stats() {}

#include <algorithm>
#include <array>
#include <map>
#include <vector>

#include "cache.h"

constexpr int PREFETCH_DEGREE=2;       // Number of blocks to prefetch in advance
constexpr int PREFETCH_DISTANCE=4;     // Distance between prefetched blocks

struct stream_entry {
  uint64_t last_address = 0;  // the last address accessed in the stream
  int degree = 0;            // degree remaining
};

struct tracker_entry {
  uint64_t ip = 0;              // the IP we're tracking
  uint64_t last_cl_addr = 0;    // the last address accessed by this IP
  int64_t last_stride = 0;      // the stride between the last two addresses accessed by this IP
  uint64_t last_used_cycle = 0; // use LRU to evict old IP trackers
};

struct lookahead_entry {
  uint64_t address = 0;
  int64_t stride = 0;
  int degree = 0; // degree remaining
};

constexpr std::size_t STREAM_SETS = 256;
constexpr std::size_t STREAM_WAYS = 4;
constexpr std::size_t TRACKER_SETS = 256;
constexpr std::size_t TRACKER_WAYS = 4;

std::map<CACHE*, stream_entry> streams;
std::map<CACHE*, lookahead_entry> lookahead;
std::map<CACHE*, std::array<tracker_entry, TRACKER_SETS * TRACKER_WAYS>> trackers;

void CACHE::prefetcher_initialize() {
  std::cout << NAME << " Hybrid Stream + IP-based stride prefetcher" << std::endl;
}

void CACHE::prefetcher_cycle_operate() {
  // If a stream is active
  if (auto [last_stream_address, stream_degree] = streams[this]; stream_degree > 0) {
    auto stream_pf_address = last_stream_address + (PREFETCH_DISTANCE << LOG2_BLOCK_SIZE);

    // Check the MSHR occupancy to decide if we're going to prefetch to this
    // level or not
    bool stream_success = prefetch_line(0, 0, stream_pf_address, (get_occupancy(0, stream_pf_address) < get_size(0, stream_pf_address) / 2), 0);

    if (stream_success) {
      streams[this] = {stream_pf_address, stream_degree - 1};
      lookahead[this] = {}; // Clear any active lookahead
    }
  } else {
    // If a lookahead is active
    if (auto [lookahead_address, lookahead_stride, lookahead_degree] = lookahead[this]; lookahead_degree > 0) {
      auto lookahead_pf_address = lookahead_address + (lookahead_stride << LOG2_BLOCK_SIZE);

      // If the next step would exceed the degree or run off the page, stop
      if (virtual_prefetch || (lookahead_pf_address >> LOG2_PAGE_SIZE) == (lookahead_address >> LOG2_PAGE_SIZE)) {
        // Check the MSHR occupancy to decide if we're going to prefetch to this
        // level or not
        bool lookahead_success = prefetch_line(0, 0, lookahead_pf_address, (get_occupancy(0, lookahead_pf_address) < get_size(0, lookahead_pf_address) / 2), 0);

        if (lookahead_success) {
          lookahead[this] = {lookahead_pf_address, lookahead_stride, lookahead_degree - 1};
          streams[this] = {}; // Clear any active stream
        }
      } else {
        lookahead[this] = {}; // Clear the lookahead if it's going off-page
      }
    }
  }
}

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in) {
  uint64_t cl_addr = addr >> LOG2_BLOCK_SIZE;
  int64_t stride = 0;

  // Get the current stream entry
  auto& stream_entry = streams[this];

  // If we are not currently tracking a stream or the address is not contiguous, start a new stream
  if (stream_entry.degree == 0 || cl_addr != stream_entry.last_address + 1) {
    stream_entry = {cl_addr, PREFETCH_DEGREE};
    lookahead[this] = {}; // Clear any active lookahead
  }

  // Get boundaries of tracking set
  auto set_begin = std::next(std::begin(trackers[this]), ip % TRACKER_SETS);
  auto set_end = std::next(set_begin, TRACKER_WAYS);

  // Find the current IP within the set
  auto found = std::find_if(set_begin, set_end, [ip](tracker_entry x) { return x.ip == ip; });

  // If we found a matching entry in the tracker
  if (found != set_end) {
    // Calculate the stride between the current address and the last address
    // No need to check for overflow since these values are downshifted
    stride = (int64_t)cl_addr - (int64_t)found->last_cl_addr;

    // Initialize prefetch state unless we somehow saw the same address twice in
    // a row or if this is the first time we've seen this stride
    if (stride != 0 && stride == found->last_stride) {
      lookahead[this] = {cl_addr, stride, PREFETCH_DEGREE};
      streams[this] = {}; // Clear any active stream
    }
  } else {
    // Replace by LRU
    found = std::min_element(set_begin, set_end, [](tracker_entry x, tracker_entry y) { return x.last_used_cycle < y.last_used_cycle; });
  }

  // Update tracking set
  *found = {ip, cl_addr, stride, current_cycle};

  return metadata_in;
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in) {
  return metadata_in;
}

void CACHE::prefetcher_final_stats() {}