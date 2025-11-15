#include "spectrum_screen.h"
#include "led_matrix.h"
#include "microphone.h"   // NEW: our microphone/FFT module

void SpectrumScreen::onEnter()
{
    mic_start();   // initialise I2S mic (implemented below)
}

void SpectrumScreen::render(LEDMatrix& matrix)
{
    draw_fft_visual(matrix);   // uses your exact FFT function
}
