#pragma once

class LEDMatrix;

// Initialise mic (will just ensure I2S is set up)
void mic_start();

// Draw the FFT spectrum into the given matrix
void draw_fft_visual(LEDMatrix& matrix);
