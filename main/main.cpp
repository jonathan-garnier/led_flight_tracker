#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Arduino.h"
#include "driver/i2s.h"
#include "esp_random.h"

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =====================================================
// Panel / hardware config
// =====================================================

#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1

#define BUTTON_PIN 40

// Microphone pins (ICS-43434)
#define MIC_BCLK 16
#define MIC_LRCL 17
#define MIC_DOUT 18

HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
MatrixPanel_I2S_DMA* dma_display = nullptr;


// =====================================================
// Colour helper
// =====================================================

uint16_t wheel(uint8_t pos) {
    pos = 255 - pos;
    if (pos < 85) return dma_display->color565(255 - pos * 3, 0, pos * 3);
    if (pos < 170) {
        pos -= 85;
        return dma_display->color565(0, pos * 3, 255 - pos * 3);
    }
    pos -= 170;
    return dma_display->color565(pos * 3, 255 - pos * 3, 0);
}


// =====================================================
// MODE 0: MICROPHONE FFT SPECTRUM (8 bands)
// =====================================================

#define NUM_FFT_SAMPLES 256   // must be power of 2
#define NUM_BANDS       16

static float fftReal[NUM_FFT_SAMPLES];
static float fftImag[NUM_FFT_SAMPLES];
static float fftWindow[NUM_FFT_SAMPLES];
static bool  fftWindowInited = false;
static float bandLevels[NUM_BANDS] = {0};

// Legacy I2S driver init for ICS-43434
void initMicrophone()
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
        .bck_io_num = MIC_BCLK,
        .ws_io_num  = MIC_LRCL,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = MIC_DOUT
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, &pin_config);

    printf("Microphone initialized (legacy I2S).\n");
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

void drawAudioFFT()
{
    // Init Hann window once
    if (!fftWindowInited) {
        for (int i = 0; i < NUM_FFT_SAMPLES; ++i) {
            fftWindow[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (NUM_FFT_SAMPLES - 1)));
        }
        fftWindowInited = true;
    }

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
        int32_t s = samples[i] >> 14; // downscale 24-bit data
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

    // Group into NUM_BANDS bands
    int binsPerBand = half / NUM_BANDS; // 128/8 = 16
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

    // Draw bars
    int bandWidth = PANEL_RES_X / NUM_BANDS; // 64/8=8

    for (int b = 0; b < NUM_BANDS; ++b) {
        float lvl = bandLevels[b];
        if (lvl < 0.0f) lvl = 0.0f;
        if (lvl > 1.0f) lvl = 1.0f;

        int barHeight = (int)(lvl * (PANEL_RES_Y - 1));

        int x0 = b * bandWidth;
        int x1 = x0 + bandWidth - 1;
        if (x1 >= PANEL_RES_X) x1 = PANEL_RES_X - 1;

        uint16_t col = wheel(b * 32 + (int)(lvl * 64));
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


// =====================================================
// MODE 1: HI EM + COMET
// =====================================================

void drawCometWithText(int &cometPos, int tailLength, uint16_t frame)
{
    dma_display->fillScreen(0);

    int cometY = 22;

    for (int i = 0; i < tailLength; i++) {
        int x = cometPos - i;
        if (x < 0 || x >= PANEL_RES_X) continue;

        uint8_t idx = (frame + i * 8) & 0xFF;
        uint16_t col = wheel(idx);

        float fade = float(tailLength - i) / tailLength;
        uint8_t r = ((col >> 11) & 0x1F) * 8 * fade;
        uint8_t g = ((col >> 5)  & 0x3F) * 4 * fade;
        uint8_t bb = (col & 0x1F) * 8 * fade;

        dma_display->drawPixelRGB888(x, cometY,     r, g, bb);
        dma_display->drawPixelRGB888(x, cometY + 1, r, g, bb);
        dma_display->drawPixelRGB888(x, cometY - 1, r, g, bb);
    }

    cometPos++;
    if (cometPos - tailLength > PANEL_RES_X)
        cometPos = -20;

    dma_display->setTextWrap(false);
    dma_display->setTextSize(1);
    dma_display->setCursor(8, 4);
    dma_display->setTextColor(wheel(frame));
    dma_display->print("Hi Em!");
}


// =====================================================
// MODE 2: RAIN RIPPLES
// =====================================================

struct Drop {
    int x, y;
    float radius;
    bool active;
};

Drop drops[8];

void waterMap(float v, uint8_t &r, uint8_t &g, uint8_t &b)
{
    r = (uint8_t)(v * 40);
    g = (uint8_t)(50 + v * 120);
    b = (uint8_t)(80 + v * 160);
}

void drawRainRipple()
{
    dma_display->fillScreen(0);

    if ((esp_random() & 31) == 0) {
        for (auto &d : drops) {
            if (!d.active) {
                d.active = true;
                d.radius = 0;
                d.x = esp_random() % PANEL_RES_X;
                d.y = esp_random() % PANEL_RES_Y;
                break;
            }
        }
    }

    for (auto &d : drops) {
        if (!d.active) continue;

        for (int y = 0; y < PANEL_RES_Y; y++) {
            for (int x = 0; x < PANEL_RES_X; x++) {
                float dx = x - d.x;
                float dy = y - d.y;
                float dist = sqrtf(dx * dx + dy * dy);
                float diff = fabsf(dist - d.radius);

                if (diff < 0.8f) {
                    float v = 1.0f - (diff / 0.8f);
                    uint8_t r, g, b;
                    waterMap(v, r, g, b);
                    dma_display->drawPixelRGB888(x, y, r, g, b);
                }
            }
        }

        d.radius += 0.35f;
        if (d.radius > 40) d.active = false;
    }
}


// =====================================================
// MODE 3: FIREWORKS (trails + compact bursts)
// =====================================================

struct Particle {
    float x, y;
    float vx, vy;
    float life;
    uint16_t color;
    bool active;
};

struct Firework {
    float x, y;
    float vy;
    bool exploded;
    bool active;
    Particle particles[50];
    float trailX[10];
    float trailY[10];
};

Firework fireworks[5];

void spawnFirework()
{
    for (auto &fw : fireworks) {
        if (!fw.active) {
            fw.active = true;
            fw.exploded = false;

            fw.x = esp_random() % PANEL_RES_X;
            fw.y = PANEL_RES_Y - 1;
            fw.vy = -((esp_random() % 10) / 15.0f + 0.9f);

            for (int i = 0; i < 10; i++) {
                fw.trailX[i] = fw.x;
                fw.trailY[i] = fw.y;
            }

            for (auto &p : fw.particles)
                p.active = false;

            return;
        }
    }
}

void drawFireworks(uint16_t frame)
{
    dma_display->fillScreen(0);

    if ((esp_random() & 20) == 0)
        spawnFirework();

    for (auto &fw : fireworks) {
        if (!fw.active) continue;

        if (!fw.exploded) {

            // Trail
            for (int i = 0; i < 10; i++) {
                float fade = 1.0f - (i / 10.0f);
                uint8_t blue = 80 * fade;

                int tx = (int)fw.trailX[i];
                int ty = (int)fw.trailY[i];

                if (tx >= 0 && tx < PANEL_RES_X &&
                    ty >= 0 && ty < PANEL_RES_Y)
                {
                    dma_display->drawPixelRGB888(tx, ty,
                        (uint8_t)(255 * fade),
                        (uint8_t)(150 * fade),
                        blue);
                }
            }

            for (int i = 9; i > 0; i--) {
                fw.trailX[i] = fw.trailX[i - 1];
                fw.trailY[i] = fw.trailY[i - 1];
            }
            fw.trailX[0] = fw.x;
            fw.trailY[0] = fw.y;

            // Rocket head
            dma_display->drawPixelRGB888((int)fw.x, (int)fw.y, 255, 255, 255);

            fw.y += fw.vy;
            fw.vy += 0.035f;

            if (fw.y < 8 || fw.vy > -0.15f)
                fw.exploded = true;

            if (fw.exploded) {
                for (auto &p : fw.particles) {
                    p.active = true;

                    float angle = (esp_random() % 360) * 0.01745f;
                    float speed = (esp_random() % 100) / 120.0f + 0.4f;

                    p.x = fw.x;
                    p.y = fw.y;
                    p.vx = cosf(angle) * speed;
                    p.vy = sinf(angle) * speed;

                    p.life = (esp_random() % 20) / 10.0f + 0.8f;
                    p.color = wheel(frame + (esp_random() & 255));
                }
            }
        } else {
            bool alive = false;

            for (auto &p : fw.particles) {
                if (!p.active) continue;
                alive = true;

                float fade = p.life / 1.2f;
                if (fade < 0) fade = 0;

                uint8_t r = ((p.color >> 11) & 0x1F) * 8 * fade;
                uint8_t g = ((p.color >> 5)  & 0x3F) * 4 * fade;
                uint8_t b = (p.color & 0x1F) * 8 * fade;

                int px = (int)p.x;
                int py = (int)p.y;

                if (px >= 0 && px < PANEL_RES_X &&
                    py >= 0 && py < PANEL_RES_Y)
                {
                    dma_display->drawPixelRGB888(px, py, r, g, b);
                }

                p.x += p.vx;
                p.y += p.vy;
                p.vy += 0.02f;
                p.life -= 0.05f;

                if (p.life <= 0)
                    p.active = false;
            }

            if (!alive)
                fw.active = false;
        }
    }
}


// =====================================================
// MODE 4: PLASMA / NEBULA
// =====================================================

void drawPlasma(uint16_t frame)
{
    for (int y = 0; y < PANEL_RES_Y; y++) {
        for (int x = 0; x < PANEL_RES_X; x++) {

            float v = sinf(x * 0.15f + frame * 0.04f)
                    + sinf(y * 0.25f + frame * 0.03f)
                    + sinf((x + y) * 0.12f + frame * 0.05f);

            uint8_t idx = (uint8_t)((v + 3.0f) * 42.5f);
            uint16_t col = wheel(idx);

            uint8_t r = ((col >> 11) & 0x1F) * 8;
            uint8_t g = ((col >> 5)  & 0x3F) * 4;
            uint8_t b = (col & 0x1F) * 8;

            dma_display->drawPixelRGB888(x, y, r, g, b);
        }
    }
}


// =====================================================
// MAIN APP
// =====================================================

extern "C" void app_main(void)
{
    initArduino();
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    initMicrophone();

    // Panel pin mapping
    mxconfig.gpio.r1 = 2;
    mxconfig.gpio.g1 = 15;
    mxconfig.gpio.b1 = 4;
    mxconfig.gpio.r2 = 5;
    mxconfig.gpio.g2 = 6;
    mxconfig.gpio.b2 = 7;
    mxconfig.gpio.a  = 8;
    mxconfig.gpio.b  = 9;
    mxconfig.gpio.c  = 10;
    mxconfig.gpio.d  = 11;
    mxconfig.gpio.clk = 12;
    mxconfig.gpio.lat = 13;
    mxconfig.gpio.oe  = 14;

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->begin();
    dma_display->setBrightness8(60);

    for (auto &d : drops)
        d.active = false;

    for (auto &fw : fireworks)
        fw.active = false;

    int mode = 0;
    bool lastButton = HIGH;

    int cometPos = -20;
    uint16_t frame = 0;

    printf("Starting 5-mode system (0=FFT, 1=HiEm, 2=Rain, 3=Fireworks, 4=Plasma)\n");

    while (true)
    {
        bool state = digitalRead(BUTTON_PIN);
        if (state != lastButton) {
            if (state == LOW) {
                mode = (mode + 1) % 5;
                printf("Mode changed to %d\n", mode);
            }
            lastButton = state;
        }

        switch (mode) {
            case 0: drawAudioFFT();                     break;
            case 1: drawCometWithText(cometPos, 25, frame); break;
            case 2: drawRainRipple();                   break;
            case 3: drawFireworks(frame);               break;
            case 4: drawPlasma(frame);                  break;
        }

        frame++;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
