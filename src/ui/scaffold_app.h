#pragma once

#include "ui/home_screen.h"

namespace paper_screen {

class ScaffoldApp {
public:
    explicit ScaffoldApp(AppIcon icon);
    ~ScaffoldApp();

    AppIcon icon() const;
    AppScreenViewModel view_model() const;

private:
    AppIcon icon_;
};

}  // namespace paper_screen
