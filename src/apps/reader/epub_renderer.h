#pragma once

#include "display/render_context.h"

#include "Renderer/EpdiyFrameBufferRenderer.h"

namespace paper_screen {

// Bridges the diy-esp32-epub-reader layout/paint engine onto our own
// epdiy-format framebuffer instead of letting it own/flush the hardware
// itself - Display::begin_app_frame()/end_app_frame() do that for us.
class PaperScreenEpubRenderer : public EpdiyFrameBufferRenderer {
public:
    using EpdiyFrameBufferRenderer::EpdiyFrameBufferRenderer;

    void set_target(const DisplayRenderContext& ctx)
    {
        m_frame_buffer = ctx.framebuffer;
        target_width_ = ctx.width;
        target_height_ = ctx.height;
    }

    void show_busy() override {}
    void flush_display() override {}
    void flush_area(int, int, int, int) override {}
    void reset() override {}

    int get_page_width() override { return target_width_ - (margin_left + margin_right); }
    int get_page_height() override { return target_height_ - (margin_top + margin_bottom); }

private:
    int target_width_ = 0;
    int target_height_ = 0;
};

}  // namespace paper_screen
