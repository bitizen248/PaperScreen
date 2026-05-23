#include "scaffold_app.h"

#include <Arduino.h>

namespace paper_screen {

ScaffoldApp::ScaffoldApp(AppIcon icon)
    : icon_(icon)
{
    Serial.printf("[scaffold] create %s\n", app_icon_label(icon_));
}

ScaffoldApp::~ScaffoldApp()
{
    Serial.printf("[scaffold] destroy %s\n", app_icon_label(icon_));
}

AppIcon ScaffoldApp::icon() const
{
    return icon_;
}

AppScreenViewModel ScaffoldApp::view_model() const
{
    AppScreenViewModel model;
    model.status_bar.title = app_icon_label(icon_);
    model.status_bar.time = "--:--";
    model.status_bar.battery = "--%";
    model.app_name = app_icon_label(icon_);
    return model;
}

}  // namespace paper_screen
