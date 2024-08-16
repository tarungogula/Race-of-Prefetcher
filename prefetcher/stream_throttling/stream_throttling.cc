// #include "cache.h"

// void CACHE::prefetcher_initialize() {}

// uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in) { return metadata_in; }

// uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
// {
//   return metadata_in;
// }

// void CACHE::prefetcher_cycle_operate() {}

// void CACHE::prefetcher_final_stats() {}

// #include <algorithm>
// #include <array>
// #include <map>
// #include <vector>

// #include "cache.h"

// int PREFETCH_DEGREE = 2;      // Number of blocks to prefetch in advance (non-constexpr)
// int PREFETCH_DISTANCE = 4;    // Distance between prefetched blocks (non-constexpr)

// struct stream_entry {
//   uint64_t last_address = 0;  // the last address accessed in the stream
//   int degree = 0;            // degree remaining
// };

// constexpr std::size_t STREAM_SETS = 256;
// constexpr std::size_t STREAM_WAYS = 4;
// std::map<CACHE*, stream_entry> streams;

// // Track prefetch accuracy
// uint64_t pf_useful = 0;
// uint64_t pf_issued = 0;

// void CACHE::prefetcher_initialize() {
//   std::cout << NAME << " Stream prefetcher" << std::endl;
// }

// void CACHE::prefetcher_cycle_operate() {
//   // Calculate prefetch accuracy
//   double prefetch_accuracy = static_cast<double>(pf_useful) / pf_issued;

//   // Adjust prefetch parameters based on accuracy
//   int new_prefetch_degree = 3;  // Default value
//   int new_prefetch_distance = 4;  // Default value

//   if (prefetch_accuracy < 0.5) {
//     new_prefetch_degree = std::max(1, PREFETCH_DEGREE / 2);  // Reduce degree on low accuracy
//   } else if (prefetch_accuracy > 0.8) {
//     new_prefetch_degree = std::min(2 * PREFETCH_DEGREE, static_cast<int>(STREAM_SETS));  // Increase degree on high accuracy
//   }

//   // Update the prefetch parameters
//   PREFETCH_DEGREE = new_prefetch_degree;
//   PREFETCH_DISTANCE = new_prefetch_distance;

//   // If a stream is active
//   if (auto [last_address, degree] = streams[this]; degree > 0) {
//     auto pf_address = last_address + (PREFETCH_DISTANCE << LOG2_BLOCK_SIZE);

//     // Check the MSHR occupancy to decide if we're going to prefetch to this
//     // level or not
//     bool success = prefetch_line(0, 0, pf_address, (get_occupancy(0, pf_address) < get_size(0, pf_address) / 2), 0);
//     if (success)
//       streams[this] = {pf_address, degree - 1};
//     // If we fail, try again next cycle
//   }
// }

// uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in) {
//   uint64_t cl_addr = addr >> LOG2_BLOCK_SIZE;

//   // Get the current stream entry
//   auto& entry = streams[this];

//   // If we are not currently tracking a stream or the address is not contiguous, start a new stream
//   if (entry.degree == 0 || cl_addr != entry.last_address + 1) {
//     entry = {cl_addr, PREFETCH_DEGREE};
//   }

//   // Update prefetch accuracy counters
//   pf_issued++;
//   if (cache_hit == 0) {
//     pf_useful++;
//   }

//   return metadata_in;
// }

// uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in) {
//   return metadata_in;
// }

// void CACHE::prefetcher_final_stats() {}

#include <algorithm>
#include <array>
#include <map>
#include <deque>

#include "cache.h"

 int PREFETCH_DEGREE = 2;
constexpr int STREAM_SIZE = 64; // Maximum number of addresses to track in a stream
 int PREFETCH_DISTANCE = 4; // Maximum distance ahead to prefetch
// Track prefetch accuracy
uint64_t pf_useful = 0;
uint64_t pf_issued = 0;

enum StreamState {
  INVALID,
  ALLOCATED,
  TRAINING,
  MONITOR_AND_REQUEST
};

struct tracker_entry {
  uint64_t ip = 0;               // the IP we're tracking
  std::deque<uint64_t> stream_history; // Recently accessed addresses in the stream
  StreamState state = INVALID;   // Current state of the tracking entry
  bool direction = true;        // Direction of the stream (true for ascending, false for descending)
  uint64_t start_pointer = 0;   // Start pointer for the monitored memory region
  uint64_t end_pointer = 0;     // End pointer for the monitored memory region
};

std::map<CACHE*, tracker_entry> stream_trackers;

void CACHE::prefetcher_initialize() {
  std::cout << NAME << " Stream Throttling prefetcher" << std::endl;
}

void CACHE::prefetcher_cycle_operate() {

    // Calculate prefetch accuracy
  double prefetch_accuracy = static_cast<double>(pf_useful) / pf_issued;

  // Adjust prefetch parameters based on accuracy
  int new_prefetch_degree = 3;  // Default value
  int new_prefetch_distance = 4;  // Default value

  if (prefetch_accuracy < 0.5) {
    new_prefetch_degree = std::max(1, PREFETCH_DEGREE / 2);  // Reduce degree on low accuracy
  } else if (prefetch_accuracy > 0.8) {
    new_prefetch_degree=2*PREFETCH_DEGREE;  // Increase degree on high accuracy
  }

  // Update the prefetch parameters
  PREFETCH_DEGREE = new_prefetch_degree;
  PREFETCH_DISTANCE = new_prefetch_distance;

  // Iterate through stream trackers
  for (auto& [cache, entry] : stream_trackers) {
    switch (entry.state) {
      case INVALID:
        // Not allocated, do nothing
        break;
        
      case ALLOCATED:
        // Transition to TRAINING state if there are enough addresses in the stream history
        if (entry.stream_history.size() >= 2) {
          entry.state = TRAINING;
          entry.direction = true; // Assume ascending direction initially
        }
        break;

      case TRAINING:
        // Check for direction change
        if (entry.stream_history.size() >= 3) {
          bool ascending = true;
          bool descending = true;

          // Check the next two accesses in the stream
          for (size_t i = 1; i <= 2; i++) {
            uint64_t addr1 = entry.stream_history[i - 1];
            uint64_t addr2 = entry.stream_history[i];

            if (addr1 > addr2) {
              ascending = false;
            } else if (addr1 < addr2) {
              descending = false;
            }
          }

          if (ascending || descending) {
            entry.direction = ascending;
            entry.state = MONITOR_AND_REQUEST;
            entry.start_pointer = entry.stream_history[0];
            entry.end_pointer = (entry.start_pointer + PREFETCH_DISTANCE) << LOG2_BLOCK_SIZE;
          }
        }
        break;

      case MONITOR_AND_REQUEST:
        // Monitor the memory region
        for (auto it = entry.stream_history.rbegin(); it != entry.stream_history.rend(); ++it) {
          if (*it >= entry.start_pointer && *it <= entry.end_pointer) {
            // Demand access to a monitored cache block, prefetch next blocks
            uint64_t pf_address = entry.direction ? entry.end_pointer + 1 : entry.start_pointer - 1;
            for (int i = 0; i < PREFETCH_DEGREE; i++) {
              bool success = prefetch_line(0, 0, pf_address, (get_occupancy(0, pf_address) < get_size(0, pf_address) / 2), 0);
              if (success) {
                entry.end_pointer = entry.direction ? pf_address : entry.end_pointer;
                entry.start_pointer = entry.direction ? entry.start_pointer : pf_address;
                pf_address = entry.direction ? entry.end_pointer + 1 : entry.start_pointer - 1;
              } else {
                break; // Stop prefetching if MSHR occupancy is high
              }
            }
            break;
          }
        }
        break;
    }
  }
}

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in) {
  // Check if a stream for this IP is already being tracked
  if (auto found = stream_trackers.find(this); found != stream_trackers.end() && found->second.ip == ip) {
    // Update the stream history for the existing IP tracker
    auto& entry = found->second;
    entry.stream_history.push_front((addr) );
    
    // Trim the stream history to a maximum length
    while (entry.stream_history.size() > STREAM_SIZE) {
      entry.stream_history.pop_back();
    }
  } else {
    // Create a new stream tracker for this IP
    tracker_entry new_entry;
    new_entry.ip = ip;
    new_entry.stream_history.push_front((addr) );
    new_entry.state = ALLOCATED;
    
    stream_trackers[this] = new_entry;
  }
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

