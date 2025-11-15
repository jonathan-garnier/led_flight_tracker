#pragma once

#include "base_screen.h"

class SpectrumScreen : public BaseScreen {
public:
    void onEnter() override;
    void update(float dt) override {}
    void render(LEDMatrix& matrix) override;
};
