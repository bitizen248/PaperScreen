#pragma once

#include "display/render_context.h"
#include "sdk/app_context.h"
#include "sdk/app_descriptor.h"
#include "sdk/app_event.h"
#include "sdk/app_result.h"

namespace paper_screen {

class PaperApp {
public:
    virtual ~PaperApp() = default;

    virtual const AppDescriptor& descriptor() const = 0;

    virtual void on_open(AppContext&) {}
    virtual void on_close(AppContext&) {}
    virtual void on_suspend(AppContext&) {}
    virtual void on_resume(AppContext&) {}

    virtual AppEventResult on_event(AppContext& context, const AppEvent& event) = 0;
    virtual AppRenderResult render(AppContext& context, DisplayRenderContext& render_context) = 0;
};

}  // namespace paper_screen
