//#include "cache.h"

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

#include "cache.h"

int PREFETCH_DEGREE = 2; // Initialize with a default degree

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

constexpr std::size_t TRACKER_SETS = 256;
constexpr std::size_t TRACKER_WAYS = 4;
std::map<CACHE*, lookahead_entry> lookahead;
std::map<CACHE*, std::array<tracker_entry, TRACKER_SETS * TRACKER_WAYS>> trackers;

// Track prefetch accuracy
uint64_t pf_useful = 0;
uint64_t pf_issued = 0;

void CACHE::prefetcher_initialize() {
  std::cout << NAME << " IP-based stride prefetcher" << std::endl;
}

void CACHE::prefetcher_cycle_operate() {
  // Calculate prefetch accuracy
  double prefetch_accuracy = static_cast<double>(pf_useful) / pf_issued;

  // Adjust prefetch parameters based on accuracy
  int new_prefetch_degree = 2; // Default value

  if (prefetch_accuracy < 0.5) {
    new_prefetch_degree = std::max(1, PREFETCH_DEGREE / 2); // Reduce degree on low accuracy
  } else if (prefetch_accuracy > 0.8) {
    new_prefetch_degree = std::min(2 * PREFETCH_DEGREE, int(TRACKER_SETS * TRACKER_WAYS)); // Increase degree on high accuracy
  }

  // Update the prefetch degree
  PREFETCH_DEGREE = new_prefetch_degree;

  // If a lookahead is active
  if (auto [old_pf_address, stride, degree] = lookahead[this]; degree > 0) {
    auto pf_address = old_pf_address + (stride << LOG2_BLOCK_SIZE);

    // If the next step would exceed the degree or run off the page, stop
    if (virtual_prefetch || (pf_address >> LOG2_PAGE_SIZE) == (old_pf_address >> LOG2_PAGE_SIZE)) {
      // Check the MSHR occupancy to decide if we're going to prefetch to this level or not
      bool success = prefetch_line(0, 0, pf_address, (get_occupancy(0, pf_address) < get_size(0, pf_address) / 2), 0);
      if (success)
        lookahead[this] = {pf_address, stride, degree - 1};
      // If we fail, try again next cycle
    } else {
      lookahead[this] = {};
    }
  }
}

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in) {
  uint64_t cl_addr = addr >> LOG2_BLOCK_SIZE;
  int64_t stride = 0;

  // Get boundaries of tracking set
  auto set_begin = std::next(std::begin(trackers[this]), ip % TRACKER_SETS);
  auto set_end = std::next(set_begin, TRACKER_WAYS);

  // Find the current IP within the set
  auto found = std::find_if(set_begin, set_end, [ip](tracker_entry x) { return x.ip == ip; });

  // If we found a matching entry
  if (found != set_end) {
    // Calculate the stride between the current address and the last address
    // No need to check for overflow since these values are downshifted
    stride = static_cast<int64_t>(cl_addr) - static_cast<int64_t>(found->last_cl_addr);

    // Initialize prefetch state unless we somehow saw the same address twice in
    // a row or if this is the first time we've seen this stride
    if (stride != 0 && stride == found->last_stride)
      lookahead[this] = {cl_addr, stride, PREFETCH_DEGREE};
  } else {
    // Replace by LRU
    found = std::min_element(set_begin, set_end, [](tracker_entry x, tracker_entry y) { return x.last_used_cycle < y.last_used_cycle; });
  }

  // Update tracking set
  *found = {ip, cl_addr, stride, current_cycle};

  // Update prefetch accuracy counters
  pf_issued++;
  if (cache_hit == 0) {
    pf_useful++;
  }

  return metadata_in;
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in) {
  return metadata_in;
}

void CACHE::prefetcher_final_stats() {}