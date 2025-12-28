# Meshtastic Firmware for T5S3-4.7-e-paper-PRO

Meshtastic is an open-source, off-grid mesh networking platform that uses LoRa radios for long-range, low-power communication without cellular or WiFi infrastructure.

## Status: WORKING

The T5S3-4.7-e-paper-PRO V2 (H752-01/H752-02) is now fully supported by Meshtastic!

## Features

- Full 4.7" e-paper display support (960x540)
- Touch screen navigation
- SX1262 LoRa radio communication
- Bluetooth connectivity for mobile app
- GPS location sharing
- WiFi for web interface
- Long battery life with e-paper display

## Download Firmware

**Pre-built firmware:** https://github.com/Ringmast4r/t5-epaper-meshtastic/releases

**Official Meshtastic (pending merge):** https://github.com/meshtastic/firmware/pull/9096

## How to Flash

1. Download `firmware.factory.bin` from the releases page
2. Put device in bootloader mode:
   - Hold BOOT button
   - Press and release RESET
   - Release BOOT
3. Flash using esptool:
   ```
   esptool.py --chip esp32s3 write_flash 0x0 firmware.factory.bin
   ```
   Or use the web flasher at https://flasher.meshtastic.org

## Connect to Your Device

1. Download the Meshtastic app (iOS/Android)
2. Enable Bluetooth on your phone
3. Open the app and scan for devices
4. Connect to "Meshtastic_XXXX"

## Hardware Compatibility

| Model | Status |
|-------|--------|
| H752-01 (915MHz) | ✅ Working |
| H752-02 (915MHz) | ✅ Working |
| H752-XX (868MHz) | Should work (untested) |

## Technical Notes

The key fix for this hardware was properly initializing all 8 pins on the PCA9535 IO expander's Port 0. The stock Meshtastic code only initialized pin 0, but the hardware requires all pins to be set as outputs with HIGH values to match the stock firmware behavior.

## Credits

- First working implementation by [@Ringmast4r](https://github.com/Ringmast4r)
- Based on Meshtastic firmware: https://meshtastic.org
- Hardware by LilyGO: https://lilygo.cc

## Links

- Meshtastic Project: https://meshtastic.org
- Meshtastic Firmware: https://github.com/meshtastic/firmware
- T5 E-Paper Meshtastic Fork: https://github.com/Ringmast4r/t5-epaper-meshtastic
