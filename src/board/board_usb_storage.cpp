#include "board/board_usb_storage.h"

#include <Arduino.h>
#include <SD.h>
#include <USB.h>
#include <USBMSC.h>

#include "board/board_storage.h"

namespace paper_screen {

namespace {

constexpr uint32_t kSectorSize = 512;

USBMSC g_msc;

int32_t msc_on_write(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
    (void)offset;
    const uint32_t sectors = bufsize / kSectorSize;
    for (uint32_t i = 0; i < sectors; ++i) {
        if (!SD.writeRAW(buffer + i * kSectorSize, lba + i)) {
            return -1;
        }
    }
    return static_cast<int32_t>(bufsize);
}

int32_t msc_on_read(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
    (void)offset;
    uint8_t* out = static_cast<uint8_t*>(buffer);
    const uint32_t sectors = bufsize / kSectorSize;
    for (uint32_t i = 0; i < sectors; ++i) {
        if (!SD.readRAW(out + i * kSectorSize, lba + i)) {
            return -1;
        }
    }
    return static_cast<int32_t>(bufsize);
}

bool msc_on_start_stop(uint8_t power_condition, bool start, bool load_eject)
{
    (void)power_condition;
    (void)start;
    (void)load_eject;
    return true;
}

}  // namespace

bool BoardUsbStorage::begin(BoardStorage& board_storage)
{
    if (!board_storage.mounted()) {
        return false;
    }

    g_msc.vendorID("PaperScr");
    g_msc.productID("Reader SD Card");
    g_msc.productRevision("1.0");
    g_msc.onRead(msc_on_read);
    g_msc.onWrite(msc_on_write);
    g_msc.onStartStop(msc_on_start_stop);
    g_msc.mediaPresent(true);
    g_msc.begin(SD.numSectors(), SD.cardSize() / SD.numSectors());

    USB.begin();
    return true;
}

}  // namespace paper_screen
