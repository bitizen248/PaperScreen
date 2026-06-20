#pragma once

#include <stdint.h>

#include "EpubList/State.h"

#include "sdk/app.h"

class EpubReader;

namespace paper_screen {

class PaperScreenEpubRenderer;

enum class ReaderView : uint8_t {
    Library,
    Reading,
};

struct ReaderBookEntry {
    char path[160] = {};
    char title[64] = {};
};

class ReaderApp final : public PaperApp {
public:
    ~ReaderApp() override;

    const AppDescriptor& descriptor() const override;

    void on_open(AppContext& context) override;
    void on_close(AppContext& context) override;
    AppEventResult on_event(AppContext& context, const AppEvent& event) override;
    AppRenderResult render(AppContext& context, DisplayRenderContext& render_context) override;

private:
    static constexpr int kMaxBooks = 20;

    void scan_library();
    void open_book(int index);
    void close_book();
    void render_library(DisplayRenderContext& ctx);
    void render_reading(DisplayRenderContext& ctx);
    AppEventResult handle_library_touch(int16_t x, int16_t y);
    AppEventResult handle_reading_touch(int16_t x, int16_t y);

    ReaderView view_ = ReaderView::Library;
    ReaderBookEntry books_[kMaxBooks];
    int book_count_ = 0;

    EpubReader* epub_reader_ = nullptr;
    EpubListItem book_state_{};
    PaperScreenEpubRenderer* renderer_ = nullptr;
    int last_width_ = 0;
};

}  // namespace paper_screen
