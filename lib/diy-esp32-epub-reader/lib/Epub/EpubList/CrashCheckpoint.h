#pragma once
// USB-CDC drops any printf/fflush that hasn't physically left the TX FIFO
// yet when a hard fault resets the chip - bisection logging based on serial
// output is therefore unreliable for the exact statement that crashes.
// g_crash_checkpoint lives in RTC_NOINIT_ATTR memory, which survives
// esp_restart()/panic resets untouched, so the last value written here is
// readable on the *next* boot regardless of whether anything made it out
// over the wire before the reset. See checkpoint IDs below; app.cpp prints
// the value on abnormal reset.
#include <cstdint>

#ifndef UNIT_TEST
#include <esp_attr.h>
extern uint32_t g_crash_checkpoint;
#define SET_CHECKPOINT(n) (g_crash_checkpoint = (n))
#else
#define SET_CHECKPOINT(n) ((void)0)
#endif

// 1x: ReaderApp::open_book
// 1 = entered, 2 = renderer constructed, 3 = EpubReader constructed, 4 = before epub_reader_->load()
// 2x: EpubReader::load
// 20 = entered, 21 = before new Epub, 22 = before epub->load(), 23 = epub->load() returned
// 3x: Epub::load
// 30 = entered (before ZipFile ctor), 31 = zip opened, 32 = before find_content_opf_file,
// 33 = before parse_content_opf, 34 = before parse_toc_ncx_file, 35 = load() done
// 4x: Epub::parse_content_opf
// 40 = entered, 41 = got metadata, 42 = got title, 43 = got manifest, 44 = manifest loop done,
// 45 = got spine, 46 = spine loop done
// 5x: Epub::parse_toc_ncx_file
// 50 = entered, 51 = ncx read, 52 = parsed, 53 = navMap found, 54+N = Nth navPoint iteration start
