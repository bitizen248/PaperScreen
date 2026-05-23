# BOARD.md

Board reference for the LilyGO T5 E-Paper S3 Pro productivity tool.

## Baseline board

This repository targets the LilyGO T5 E-Paper S3 Pro family, documented by LilyGO as a 4.7-inch ESP32-S3 e-paper device with touch, RTC, battery support, TF card, and optional LoRa support.[cite:17][cite:18]

## Verified board-level assumptions

- Display: 4.7-inch E-Paper, 960×540, ED047TC1 family in official materials.[cite:17][cite:18]
- Touch controller: GT911.[cite:17]
- RTC: PCF85063.[cite:17]
- MCU family: ESP32-S3.[cite:17][cite:18]
- Storage: TF / microSD support is present in official docs and examples.[cite:17][cite:18]
- LoRa: SX1262 appears in official descriptions and examples for supported variants.[cite:17][cite:18]

## Bring-up baseline

Use the official LilyGO T5S3 Pro repository as the reference implementation for:
- display initialization
- touch initialization
- RTC access
- SD card access
- LoRa bring-up
- example project layout and board-specific build settings

Reference examples mentioned in the official repository include display tests, touch, LVGL tests, SD card, RTC, and LoRa send/receive.[cite:18]

## Rules

- Do not replace the display path with a random generic e-paper library without a clear reason.
- Do not guess pin maps from other LilyGO boards.
- Do not assume TFT semantics for refresh or UI loops.
- Do not make the render path depend on high-frequency redraws.
- Always preserve a path back to a known-good bring-up example.

## Revision caution

The board family has multiple revisions and official material mentions newer hardware support details such as direct-drive `epdiy` support and partial refresh behavior on newer revisions.[cite:17] Any low-level display or power changes should be reviewed against the exact board revision in hand before merging.
