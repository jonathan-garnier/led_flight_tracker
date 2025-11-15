#include "fireworks_screen.h"
#include "led_matrix.h"
#include "esp_random.h"
#include <math.h>

// Pulling your original constants
#define PANEL_RES_X 64
#define PANEL_RES_Y 32

// ---------------- YOUR ORIGINAL FIREWORKS STRUCTS ----------------
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

static Firework fireworks[5];  // exactly as before

// ---------------- ORIGINAL WHEEL() ----------------
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

// ---------------- ORIGINAL SPAWN FUNCTION ----------------
static void spawnFirework() 
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

// ---------------- ORIGINAL DRAW FUNCTION ----------------
static void drawFireworks(MatrixPanel_I2S_DMA* dma_display, uint16_t frame)
{
    dma_display->fillScreen(0);

    if ((esp_random() & 20) == 0)
        spawnFirework();

    for (auto &fw : fireworks) {
        if (!fw.active) continue;

        if (!fw.exploded) {

            // ---- TRAIL ----
            for (int i = 0; i < 10; i++) {
                float fade = 1.0f - (i / 10.0f);
                uint8_t blue = 80 * fade;

                int tx = (int)fw.trailX[i];
                int ty = (int)fw.trailY[i];

                if (tx >= 0 && tx < PANEL_RES_X &&
                    ty >= 0 && ty < PANEL_RES_Y)
                {
                    dma_display->drawPixelRGB888(
                        tx, ty, 
                        (uint8_t)(255 * fade),
                        (uint8_t)(150 * fade),
                        blue
                    );
                }
            }

            for (int i = 9; i > 0; i--) {
                fw.trailX[i] = fw.trailX[i - 1];
                fw.trailY[i] = fw.trailY[i - 1];
            }
            fw.trailX[0] = fw.x;
            fw.trailY[0] = fw.y;

            // ---- ROCKET ----
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
                    p.color = wheel(dma_display, frame + (esp_random() & 255));
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

// ---------------- SCREEN CLASS ----------------
void FireworksScreen::onEnter()
{
    // Reset fireworks when entering
    for (auto &fw : fireworks)
        fw.active = false;

    frame = 0;
}

void FireworksScreen::update(float dt)
{
    frame++;
}

void FireworksScreen::render(LEDMatrix& matrix)
{
    drawFireworks(matrix.raw(), frame);
}
