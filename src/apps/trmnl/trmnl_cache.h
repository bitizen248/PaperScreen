#pragma once

#include <cstddef>
#include <cstdint>

#include "sdk/app_context.h"
#include "services/trmnl_service.h"

namespace paper_screen {

class TrmnlCache {
public:
    bool save_last_good(AppContext& context,
                        const TrmnlSnapshot& snapshot,
                        const uint8_t* image_data,
                        size_t image_size) const;
    bool load_metadata(AppContext& context, TrmnlSnapshot* snapshot) const;
    size_t load_recent_metadata(AppContext& context, TrmnlSnapshot* snapshots, size_t capacity) const;
    bool load_matching(AppContext& context,
                       const TrmnlSnapshot& probe,
                       TrmnlSnapshot* snapshot,
                       uint8_t** image_data,
                       size_t* image_size) const;
    bool load_last_good(AppContext& context,
                        TrmnlSnapshot* snapshot,
                        uint8_t** image_data,
                        size_t* image_size) const;
    bool clear(AppContext& context) const;
    bool cleanup(AppContext& context) const;
    void release_image(uint8_t* image_data) const;
};

}  // namespace paper_screen
