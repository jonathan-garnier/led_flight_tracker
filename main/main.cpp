#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Arduino.h"

#include "led_matrix.h"
#include "screen_manager.h"
#include "button.h"
#include "info_screen.h"
#include "spectrum_screen.h"
#include "fireworks_screen.h"
#include "clock_screen.h"
#include "info_screen.h"

#define BUTTON_PIN GPIO_NUM_38   // your button pin

static const int FRAME_RATE = 60;
static const TickType_t FRAME_DELAY = pdMS_TO_TICKS(1000 / FRAME_RATE);

extern "C" void app_main(void)
{
    initArduino();

    LEDMatrix matrix(64, 32, 1);
    matrix.begin();

    // Screen Manager
    ScreenManager manager(matrix);
    manager.addScreen(new InfoScreen());
    manager.addScreen(new SpectrumScreen());
    manager.addScreen(new FireworksScreen());
    manager.addScreen(new ClockScreen());
    manager.addScreen(new InfoScreen());

    // Button
    Button button(BUTTON_PIN, true); // active low with pull-up
    button.begin();

    uint32_t lastTick = xTaskGetTickCount();

    while (true)
    {
        uint32_t now = xTaskGetTickCount();
        float dt = (now - lastTick) / 1000.0f;
        lastTick = now;

        // Update button
        button.update();
        if (button.wasPressed())
        {
            printf("BUTTON PRESSED!\n");
            manager.nextScreen();
        }

        // Update + render active screen
        manager.update(dt);
        manager.render();
        matrix.show();

        vTaskDelay(FRAME_DELAY);
    }
}
