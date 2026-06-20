#include "scaffold_app.h"

#include <Arduino.h>

namespace paper_screen {

namespace {

const char* placeholder_headline(AppIcon icon)
{
    switch (icon) {
    case AppIcon::Tasks:
        return "Inbox empty";
    case AppIcon::Reader:
        return "No books";
    case AppIcon::Focus:
        return "Timer ready";
    case AppIcon::Sync:
        return "Not paired";
    case AppIcon::Terrain:
        return "Contour prototype";
    case AppIcon::Notes:
        return "No notes";
    case AppIcon::Timer:
        return "Timer ready";
    case AppIcon::Trmnl:
    case AppIcon::Settings:
        return "";
    }
    return "";
}

const char* placeholder_detail(AppIcon icon)
{
    switch (icon) {
    case AppIcon::Tasks:
        return "Local task list placeholder";
    case AppIcon::Reader:
        return "Import TXT or Markdown from microSD later";
    case AppIcon::Focus:
        return "Focus session controls will live here";
    case AppIcon::Sync:
        return "Backend sync setup will live here";
    case AppIcon::Terrain:
        return "Generated contour lines";
    case AppIcon::Notes:
        return "Notes are not part of the primary v1 scope";
    case AppIcon::Timer:
        return "Use Focus for the product timer flow";
    case AppIcon::Trmnl:
    case AppIcon::Settings:
        return "";
    }
    return "";
}

const char* placeholder_footer(AppIcon icon)
{
    switch (icon) {
    case AppIcon::Tasks:
        return "Next: local Tasks v0";
    case AppIcon::Reader:
        return "Next: reader import";
    case AppIcon::Focus:
        return "Next: start / pause / reset";
    case AppIcon::Sync:
        return "Next: device pairing state";
    case AppIcon::Terrain:
        return "Seeded height map, contour only";
    case AppIcon::Notes:
    case AppIcon::Timer:
    case AppIcon::Trmnl:
    case AppIcon::Settings:
        return "";
    }
    return "";
}

}  // namespace

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
    model.icon = icon_;
    model.status_bar.title = app_icon_label(icon_);
    model.status_bar.time = "--:--";
    model.status_bar.battery = "--%";
    model.app_name = app_icon_label(icon_);
    model.headline = placeholder_headline(icon_);
    model.detail = placeholder_detail(icon_);
    model.footer = placeholder_footer(icon_);
    return model;
}

}  // namespace paper_screen
