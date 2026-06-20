#include "apps/reader/reader_app.h"

#include <SD.h>
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "EpubList/Epub.h"
#include "EpubList/EpubReader.h"

#include "apps/reader/epub_renderer.h"
#include "display/drawing.h"
#include "display/fonts/firasans_12.h"
#include "sdk/app_registry.h"

namespace paper_screen {

namespace {

constexpr AppDescriptor kFallbackReaderDescriptor = {
    .id = "reader",
    .name = "Reader",
    .icon = AppIcon::Reader,
    .capabilities = AppCapabilityStorage | AppCapabilityFullscreen,
    .show_on_home = true,
};

// Where readers drop .epub files on the SD card, relative to the Arduino
// SD.h mount point (matches the "import/<app>" convention used elsewhere).
constexpr const char* kLibraryDir = "/paperscreen/import/reader";

constexpr int kLibraryTitleY = 36;
constexpr int kLibraryListTop = 80;
constexpr int kLibraryRowHeight = 48;
constexpr int16_t kBackZoneHeight = 40;

}  // namespace

ReaderApp::~ReaderApp()
{
    close_book();
}

const AppDescriptor& ReaderApp::descriptor() const
{
    const AppDescriptor* descriptor = find_app_by_id("reader");
    return descriptor != nullptr ? *descriptor : kFallbackReaderDescriptor;
}

void ReaderApp::on_open(AppContext& context)
{
    context.storage.ensure_directory();
    view_ = ReaderView::Library;
    scan_library();
    context.log.info("reader opened");
}

void ReaderApp::on_close(AppContext& context)
{
    close_book();
    view_ = ReaderView::Library;
    context.log.info("reader closed");
}

void ReaderApp::scan_library()
{
    book_count_ = 0;

    fs::File dir = SD.open(kLibraryDir);
    if (!dir || !dir.isDirectory()) {
        return;
    }

    for (fs::File entry = dir.openNextFile(); entry && book_count_ < kMaxBooks; entry = dir.openNextFile()) {
        if (!entry.isDirectory()) {
            const char* full_name = entry.name();
            const char* slash = strrchr(full_name, '/');
            const char* base_name = slash != nullptr ? slash + 1 : full_name;
            const size_t name_len = strlen(base_name);
            if (name_len > 5 && strcasecmp(base_name + name_len - 5, ".epub") == 0) {
                ReaderBookEntry& book = books_[book_count_];
                std::snprintf(book.path, sizeof(book.path), "%s/%s", kLibraryDir, base_name);
                std::strncpy(book.title, base_name, sizeof(book.title) - 1);
                book.title[sizeof(book.title) - 1] = '\0';
                ++book_count_;
            }
        }
        entry.close();
    }
    dir.close();
}

void ReaderApp::open_book(int index)
{
    if (index < 0 || index >= book_count_) {
        return;
    }
    close_book();

    char absolute_path[200] = {};
    std::snprintf(absolute_path, sizeof(absolute_path), "/sd%s", books_[index].path);

    book_state_ = EpubListItem{};
    std::strncpy(book_state_.path, absolute_path, sizeof(book_state_.path) - 1);
    std::strncpy(book_state_.title, books_[index].title, sizeof(book_state_.title) - 1);

    renderer_ = new PaperScreenEpubRenderer(&FiraSans_12, &FiraSans_12, &FiraSans_12, &FiraSans_12, nullptr, 0, 0);
    epub_reader_ = new EpubReader(book_state_, renderer_);
    epub_reader_->load();

    view_ = ReaderView::Reading;
}

void ReaderApp::close_book()
{
    delete epub_reader_;
    epub_reader_ = nullptr;
    delete renderer_;
    renderer_ = nullptr;
}

AppEventResult ReaderApp::on_event(AppContext& context, const AppEvent& event)
{
    (void)context;
    if (event.type != AppEventType::TouchUp) {
        return {};
    }

    if (view_ == ReaderView::Library) {
        return handle_library_touch(event.touch.x, event.touch.y);
    }
    return handle_reading_touch(event.touch.x, event.touch.y);
}

AppEventResult ReaderApp::handle_library_touch(int16_t x, int16_t y)
{
    (void)x;
    AppEventResult result;
    result.handled = true;

    if (book_count_ == 0 || y < kLibraryListTop) {
        return result;
    }

    const int index = (y - kLibraryListTop) / kLibraryRowHeight;
    if (index < 0 || index >= book_count_) {
        return result;
    }

    open_book(index);
    result.refresh_hint = AppRefreshHint::Full;
    return result;
}

AppEventResult ReaderApp::handle_reading_touch(int16_t x, int16_t y)
{
    AppEventResult result;
    result.handled = true;

    if (y < kBackZoneHeight) {
        close_book();
        view_ = ReaderView::Library;
        result.refresh_hint = AppRefreshHint::Full;
        return result;
    }

    if (epub_reader_ == nullptr) {
        return result;
    }

    if (last_width_ > 0 && x < last_width_ / 3) {
        epub_reader_->prev();
    } else {
        epub_reader_->next();
    }
    result.refresh_hint = AppRefreshHint::Full;
    return result;
}

void ReaderApp::render_library(DisplayRenderContext& ctx)
{
    draw_text_20(ctx, "Reader", 16, kLibraryTitleY, EPD_DRAW_ALIGN_LEFT);

    if (book_count_ == 0) {
        draw_text_12(ctx, "No books found.", 16, kLibraryListTop, EPD_DRAW_ALIGN_LEFT);
        draw_text_12(ctx, "Copy .epub files to /paperscreen/import/reader on the SD card.",
                     16, kLibraryListTop + 28, EPD_DRAW_ALIGN_LEFT);
        return;
    }

    int y = kLibraryListTop;
    for (int i = 0; i < book_count_; ++i) {
        draw_text_12(ctx, books_[i].title, 16, y, EPD_DRAW_ALIGN_LEFT);
        y += kLibraryRowHeight;
    }
}

void ReaderApp::render_reading(DisplayRenderContext& ctx)
{
    draw_text_12(ctx, "< Library", 8, 4, EPD_DRAW_ALIGN_LEFT);

    if (epub_reader_ == nullptr || renderer_ == nullptr) {
        draw_text_12(ctx, "Failed to load book.", 16, kLibraryListTop, EPD_DRAW_ALIGN_LEFT);
        return;
    }

    renderer_->set_target(ctx);
    epub_reader_->render();
}

AppRenderResult ReaderApp::render(AppContext& context, DisplayRenderContext& render_context)
{
    (void)context;
    last_width_ = render_context.width;

    if (view_ == ReaderView::Library) {
        render_library(render_context);
    } else {
        render_reading(render_context);
    }

    AppRenderResult result;
    result.refresh_hint = AppRefreshHint::Full;
    return result;
}

}  // namespace paper_screen
