#include "button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

Button::Button(gpio_num_t pin, bool activeLow)
    : _pin(pin), _activeLow(activeLow)
{
}

void Button::begin()
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << _pin);
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = _activeLow ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = !_activeLow ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    gpio_config(&cfg);

    _lastState = false;
}

void Button::update()
{
    bool raw = gpio_get_level(_pin);
    bool active = _activeLow ? !raw : raw;

    uint32_t now = esp_timer_get_time() / 1000; // ms

    // Debouncing
    if (active != _currentState && (now - _lastDebounceTime) > _debounceDelay)
    {
        _currentState = active;
        _lastDebounceTime = now;

        // Detect rising edge = press
        if (_currentState && !_lastState)
            _pressed = true;

        _lastState = _currentState;
    }
}

bool Button::wasPressed()
{
    if (_pressed)
    {
        _pressed = false;   // consume press
        return true;
    }
    return false;
}
