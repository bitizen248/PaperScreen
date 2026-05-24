#include "trmnl_screen.h"

#include <cstdio>

namespace paper_screen {
    void TrmnlScreen::set_snapshot(TrmnlSnapshot snapshot, const uint8_t *image_data, size_t image_size) {
        snapshot_ = snapshot;
        image_data_ = image_data;
        image_size_ = image_size;

        if (snapshot_.status == TrmnlFetchStatus::Ready && image_size_ == 0) {
            std::snprintf(detail_, sizeof(detail_), "No image bytes");
        } else if (snapshot_.status == TrmnlFetchStatus::Ready && image_data_ == nullptr) {
            std::snprintf(detail_, sizeof(detail_), "Image buffer missing");
        } else if (snapshot_.status == TrmnlFetchStatus::Ready) {
            std::snprintf(detail_, sizeof(detail_), "%lu bytes", static_cast<unsigned long>(snapshot_.image_bytes));
        } else if (snapshot_.response.refresh_seconds > 0) {
            std::snprintf(detail_, sizeof(detail_), "Refresh %lu sec",
                          static_cast<unsigned long>(snapshot_.response.refresh_seconds));
        } else {
            detail_[0] = '\0';
        }
    }

    void TrmnlScreen::set_exit_prompt(bool show) {
        show_exit_prompt_ = show;
    }

    TrmnlViewModel TrmnlScreen::view_model() const {
        TrmnlViewModel model;
        model.status_bar.title = "TRMNL";
        model.status_bar.time = "--:--";
        model.status_bar.battery = "--%";
        model.status = snapshot_.status;
        model.message = trmnl_status_label(snapshot_.status);
        model.detail = detail_;
        model.image_data = image_data_;
        model.image_size = image_size_;
        model.show_exit_prompt = show_exit_prompt_;
        return model;
    }
} // namespace paper_screen
