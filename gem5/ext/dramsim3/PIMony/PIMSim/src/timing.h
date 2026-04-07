#ifndef __TIMING_H
#define __TIMING_H

#include <vector>
#include "common.h"
#include "configuration.h"

namespace dramsim3 {

class Timing {
   public:
    Timing(const Config &config);
    std::vector<std::vector<std::pair<CommandType, int>>>
        same_bank;  // it is used as same_subarray when bankmode is DPSA
    std::vector<std::vector<std::pair<CommandType, int>>>
        other_subarrays_same_bank;  // only appling between pim and normal
                                    // access operations
    std::vector<std::vector<std::pair<CommandType, int>>>
        other_banks_same_bankgroup;
    std::vector<std::vector<std::pair<CommandType, int>>>
        other_bankgroups_same_rank;
    std::vector<std::vector<std::pair<CommandType, int>>> other_ranks;
    std::vector<std::vector<std::pair<CommandType, int>>> same_rank;
};

}  // namespace dramsim3
#endif
