#pragma once

#include <stdbool.h>

#define BUTTON_GPIO 38

// Initialize the button GPIO
void button_init(void);

// Returns true if the button is currently held down
bool button_is_pressed(void);
