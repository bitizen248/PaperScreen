#include "apps/trmnl/trmnl_view.h"

#include <cstdio>

namespace paper_screen {

namespace {

const char* trmnl_view_status_label(TrmnlFetchStatus status)
{
    switch (status) {
    case TrmnlFetchStatus::Idle:
        return "Idle";
    case TrmnlFetchStatus::ConnectingWifi:
        return "Connecting Wi-Fi";
    case TrmnlFetchStatus::RequestingMetadata:
        return "Requesting";
    case TrmnlFetchStatus::DownloadingImage:
        return "Downloading";
    case TrmnlFetchStatus::Ready:
        return "Ready";
    case TrmnlFetchStatus::Disabled:
        return "Disabled";
    case TrmnlFetchStatus::Offline:
        return "Offline";
    case TrmnlFetchStatus::Unauthorized:
        return "Unauthorized";
    case TrmnlFetchStatus::ServerError:
        return "Server error";
    case TrmnlFetchStatus::DecodeError:
        return "Decode error";
    case TrmnlFetchStatus::NoContent:
        return "No content";
    case TrmnlFetchStatus::MissingApiKey:
        return "Missing API key";
    case TrmnlFetchStatus::DeviceNotFound:
        return "Device not found";
    }
    return "Unknown";
}

}  // namespace

void TrmnlView::set_snapshot(const TrmnlSnapshot& snapshot, const uint8_t* image_data, size_t image_size)
{
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

void TrmnlView::set_exit_prompt(bool show)
{
    show_exit_prompt_ = show;
}

TrmnlViewModel TrmnlView::view_model() const
{
    TrmnlViewModel model;
    model.status_bar.title = "TRMNL";
    model.status_bar.time = "--:--";
    model.status_bar.battery = "--%";
    model.status = snapshot_.status;
    model.message = trmnl_view_status_label(snapshot_.status);
    model.detail = detail_;
    model.image_data = image_data_;
    model.image_size = image_size_;
    model.show_exit_prompt = show_exit_prompt_;
    return model;
}

}  // namespace paper_screen
