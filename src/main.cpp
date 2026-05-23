#include <Arduino.h>

#include "main/app.h"

paper_screen::App app;

void setup()
{
    app.setup();
}

void loop()
{
    app.loop();
}
