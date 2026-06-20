#pragma once

namespace paper_screen {

class BoardStorage;

// USB mass-storage exposure of the already-mounted SD card. This is a
// deliberately one-way trip: once begin() hands the card's raw sectors to
// the host over USB, the firmware leaves USB-Drive mode by rebooting
// (esp_restart()) rather than trying to hand the SD bus back to BoardStorage
// at runtime, since TinyUSB's MSC class has no supported teardown path.
class BoardUsbStorage {
public:
    bool begin(BoardStorage& board_storage);
};

}  // namespace paper_screen
