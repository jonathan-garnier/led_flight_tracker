#include "microphone.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

#include "driver/i2s.h"
#include "led_matrix.h"
#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

// =====================================================
// MODE 0: MICROPHONE FFT SPECTRUM (16 bands)
// =====================================================

#define NUM_FFT_SAMPLES 256   // must be power of 2
#define NUM_BANDS       16

static float fftReal[NUM_FFT_SAMPLES];
static float fftImag[NUM_FFT_SAMPLES];
static float fftWindow[NUM_FFT_SAMPLES];
static bool  fftWindowInited = false;
static float bandLevels[NUM_BANDS] = {0};

#define MIC_BCLK 16
#define MIC_LRCL 17
#define MIC_DOUT 18

// Legacy I2S driver init for ICS-43434 (your original code)
static void initMicrophone()
{
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num   = MIC_BCLK,
        .ws_io_num    = MIC_LRCL,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = MIC_DOUT
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, &pin_config);

    printf("Microphone initialized (legacy I2S).\n");
}

// public wrapper so we can call from SpectrumScreen
void mic_start()
{
    static bool inited = false;
    if (!inited) {
        initMicrophone();
        inited = true;
    }
}

// Bit-reversal helper for FFT
static void fftBitReverse(float *real, float *imag, int n) {
    int j = 0;
    for (int i = 0; i < n; ++i) {
        if (i < j) {
            float tr = real[i]; real[i] = real[j]; real[j] = tr;
            float ti = imag[i]; imag[i] = imag[j]; imag[j] = ti;
        }
        int m = n >> 1;
        while (m >= 1 && j >= m) {
            j -= m;
            m >>= 1;
        }
        j += m;
    }
}

// In-place radix-2 FFT
static void fftCompute(float *real, float *imag, int n) {
    fftBitReverse(real, imag, n);

    for (int s = 1; (1 << s) <= n; ++s) {
        int m = 1 << s;
        int m2 = m >> 1;
        float theta = -2.0f * (float)M_PI / (float)m;
        float wpr = cosf(theta);
        float wpi = sinf(theta);

        for (int k = 0; k < n; k += m) {
            float wr = 1.0f;
            float wi = 0.0f;
            for (int j = 0; j < m2; ++j) {
                int t = k + j + m2;
                int u = k + j;

                float tr = wr * real[t] - wi * imag[t];
                float ti = wr * imag[t] + wi * real[t];

                real[t] = real[u] - tr;
                imag[t] = imag[u] - ti;
                real[u] += tr;
                imag[u] += ti;

                float tmp = wr;
                wr = tmp * wpr - wi * wpi;
                wi = tmp * wpi + wi * wpr;
            }
        }
    }
}

// same wheel() you had before, just local here
static uint16_t wheel(MatrixPanel_I2S_DMA* dma_display, uint8_t pos) {
    pos = 255 - pos;
    if (pos < 85) return dma_display->color565(255 - pos * 3, 0, pos * 3);
    if (pos < 170) {
        pos -= 85;
        return dma_display->color565(0, pos * 3, 255 - pos * 3);
    }
    pos -= 170;
    return dma_display->color565(pos * 3, 255 - pos * 3, 0);
}

// This is your original drawAudioFFT, adapted to use LEDMatrix
void draw_fft_visual(LEDMatrix& matrix)
{
    // Init Hann window once
    if (!fftWindowInited) {
        for (int i = 0; i < NUM_FFT_SAMPLES; ++i) {
            fftWindow[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (NUM_FFT_SAMPLES - 1)));
        }
        fftWindowInited = true;
    }

    MatrixPanel_I2S_DMA* dma_display = matrix.raw();
    if (!dma_display) return;

    dma_display->fillScreen(0);

    // Read raw samples from I2S
    int32_t samples[NUM_FFT_SAMPLES];
    size_t bytes_read = 0;
    i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytes_read, portMAX_DELAY);

    int count = bytes_read / sizeof(int32_t);
    if (count < NUM_FFT_SAMPLES) {
        for (int i = count; i < NUM_FFT_SAMPLES; ++i) {
            samples[i] = 0;
        }
        count = NUM_FFT_SAMPLES;
    }

    // Convert to floats + apply window
    for (int i = 0; i < NUM_FFT_SAMPLES; ++i) {
        int32_t s = samples[i] >> 14; // your original downscale
        fftReal[i] = (float)s * fftWindow[i];
        fftImag[i] = 0.0f;
    }

    // Run FFT
    fftCompute(fftReal, fftImag, NUM_FFT_SAMPLES);

    const int half = NUM_FFT_SAMPLES / 2;
    static float mags[NUM_FFT_SAMPLES / 2];

    float maxMag = 1.0f;
    for (int k = 0; k < half; ++k) {
        float re = fftReal[k];
        float im = fftImag[k];
        float m = sqrtf(re * re + im * im);
        mags[k] = m;
        if (m > maxMag) maxMag = m;
    }

    // Use matrix dimensions instead of PANEL_RES_X/Y
    int PANEL_RES_X = matrix.width();
    int PANEL_RES_Y = matrix.height();

    // Group into NUM_BANDS bands (same as before)
    int binsPerBand = half / NUM_BANDS;
    for (int b = 0; b < NUM_BANDS; ++b) {
        float sum = 0.0f;
        int start = b * binsPerBand;
        int end   = start + binsPerBand;
        for (int k = start; k < end; ++k) {
            sum += mags[k];
        }
        float avg = sum / binsPerBand;

        float gain = 2.0f;                      // boost overall level (try 4–10)
        float level = avg * gain / maxMag;
        if (b == 0 || b == 1) level *= 0.25f; 

        level = log10f(1.0f + level * 9.0f);

        // smooth: faster rise, slower decay
        float prev = bandLevels[b];
        float target = level;
        float smoothed = (target > prev) ?
                         (prev * 0.3f + target * 0.7f) :
                         (prev * 0.8f + target * 0.2f);
        bandLevels[b] = smoothed;
    }

    // Draw bars (same as before, just using PANEL_RES_X/Y from matrix)
    int bandWidth = PANEL_RES_X / NUM_BANDS;

    for (int b = 0; b < NUM_BANDS; ++b) {
        float lvl = bandLevels[b];
        if (lvl < 0.0f) lvl = 0.0f;
        if (lvl > 1.0f) lvl = 1.0f;

        int barHeight = (int)(lvl * (PANEL_RES_Y - 1));

        int x0 = b * bandWidth;
        int x1 = x0 + bandWidth - 1;
        if (x1 >= PANEL_RES_X) x1 = PANEL_RES_X - 1;

        uint16_t col = wheel(dma_display, b * 32 + (int)(lvl * 64));
        uint8_t r = ((col >> 11) & 0x1F) * 8;
        uint8_t g = ((col >> 5)  & 0x3F) * 4;
        uint8_t bb = (col & 0x1F) * 8;

        for (int x = x0; x <= x1; ++x) {
            for (int y = PANEL_RES_Y - 1; y >= PANEL_RES_Y - barHeight; --y) {
                dma_display->drawPixelRGB888(x, y, r, g, bb);
            }
        }
    }
}
