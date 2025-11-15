#pragma once

#include "base_screen.h"

class FireworksScreen : public BaseScreen {
public:
    void onEnter() override;
    void update(float dt) override;
    void render(LEDMatrix& matrix) override;

private:
    uint16_t frame = 0;
};
