#pragma once

#include <vector>
#include "base_screen.h"
#include "led_matrix.h"

class ScreenManager {
public:
    ScreenManager(LEDMatrix& matrix);

    void addScreen(BaseScreen* screen);
    void nextScreen();
    void previousScreen();
    BaseScreen* current();

    void update(float dt);
    void render();

private:
    LEDMatrix& _matrix;
    std::vector<BaseScreen*> _screens;
    int _currentIndex = -1;
};
