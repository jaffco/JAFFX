#include "../../Jaffx.hpp"
#include "dev/oled_ssd1312.h"
#include "util/oled_fonts.h"
#include <cmath>

using DisplayType = daisy::OledDisplay<daisy::SSD13124WireSpi128x32Driver>;

// ─── Layout ──────────────────────────────────────────────────────────────────
// 4 displays side-by-side: 128×32 each → 512×32 panorama
// 20 fps, 6 scenes × 20 s = 120 s loop
static constexpr int kW  = 128;         // single display width
static constexpr int kH  = 32;          // single display height
static constexpr int kN  = 4;           // number of displays
static constexpr int kPW = kW * kN;     // panorama width = 512

static constexpr int kFps           = 20;
static constexpr int kSamplesPerFrm = 48000 / kFps;  // 2400 samples
static constexpr int kSceneLen      = 3 * kFps;      // 400 frames = 20 s
static constexpr int kTotalFrames   = 6 * kSceneLen;  // 2400 frames = 2 min

// ─────────────────────────────────────────────────────────────────────────────

class DisplayParty : public Jaffx::Firmware {
    DisplayType mDisplay;
    GPIO        mCS[kN];

    volatile bool mDoUpdate  = false;
    int           mSampleAcc = 0;
    int           mFrame     = 0;   // 0 .. kTotalFrames-1

    // ── Hardware helpers ─────────────────────────────────────────────────────

    void selectDisplay(int d) {
        for (int i = 0; i < kN; i++) mCS[i].Write(true);
        mCS[d].Write(false);
    }

    // Render all 4 displays for the current frame
    void renderAllDisplays() {
        for (int d = 0; d < kN; d++) {
            selectDisplay(d);
            mDisplay.Fill(false);
            drawScene(d, mFrame);
            mDisplay.Update();
        }
    }

    // Draw a pixel at global panorama coordinates onto display d
    void pset(int d, int gx, int gy) {
        int lx = gx - d * kW;
        if (lx >= 0 && lx < kW && gy >= 0 && gy < kH)
            mDisplay.DrawPixel(lx, gy, true);
    }

    // Fill a global-coord rectangle onto display d (clipped)
    void prect(int d, int gx, int gy, int w, int h) {
        for (int dx = 0; dx < w; dx++)
            for (int dy = 0; dy < h; dy++)
                pset(d, gx + dx, gy + dy);
    }

    // ── Scene dispatch ───────────────────────────────────────────────────────

    void drawScene(int d, int frame) {
        int scene = (frame / kSceneLen) % 6;
        int f     = frame % kSceneLen;   // local frame 0..399

        // 10-frame blackout between scenes
        if (f < 10) return;

        switch (scene) {
            case 0: sceneSineRivers  (d, f); break;
            case 1: sceneBouncingBall(d, f); break;
            case 2: sceneSpectrum    (d, f); break;
            case 3: sceneRain        (d, f); break;
            case 4: sceneRipple      (d, f); break;
            case 5: sceneSonar       (d, f); break;
        }
    }

    // ── Scene 0 · Sine Rivers (0–20 s) ───────────────────────────────────────
    // Two sine waves of different frequency scroll in opposite directions.
    // They cross each other creating interference, like ripples on water.
    void sceneSineRivers(int d, int f) {
        float t = f * 0.15f;
        for (int lx = 0; lx < kW; lx++) {
            float gx = d * kW + lx;
            // Wave 1: faster, upper half
            float p1 = gx * 0.05f + t;
            int   y1 = 9 + (int)(8.0f * arm_sin_f32(p1));
            // Wave 2: slower, lower half, opposite direction
            float p2 = gx * 0.03f - t * 0.7f;
            int   y2 = 22 + (int)(7.0f * arm_sin_f32(p2));

            // Draw each wave 3 px wide for visibility
            for (int dy = -1; dy <= 1; dy++) {
                if (y1 + dy >= 0 && y1 + dy < kH) mDisplay.DrawPixel(lx, y1 + dy, true);
                if (y2 + dy >= 0 && y2 + dy < kH) mDisplay.DrawPixel(lx, y2 + dy, true);
            }
        }
    }

    // ── Scene 1 · Bouncing Ball (20–40 s) ────────────────────────────────────
    // A solid ball bounces across the full 512-wide panorama.
    // Horizontal motion is a triangle wave; vertical is a sine wave.
    // A comet trail follows behind it.
    void sceneBouncingBall(int d, int f) {
        // Horizontal: triangle wave → bounces off walls ~3× in 20 s
        float hRaw = fmodf(f * 3.5f, (float)(kPW * 2));
        int   bgx  = (hRaw < kPW) ? (int)hRaw : (int)(kPW * 2 - hRaw);
        // Vertical: slower sine
        float bgy  = 15.5f + 12.0f * arm_sin_f32(f * 0.09f);
        int   by   = (int)bgy;

        // Comet trail (10 steps back, each step = 0.5 raw frames ago)
        for (int t = 10; t >= 1; t--) {
            float hr = fmodf((f - t * 0.5f) * 3.5f, (float)(kPW * 2));
            int tgx = (hr < kPW) ? (int)hr : (int)(kPW * 2 - hr);
            float tgy = 15.5f + 12.0f * arm_sin_f32((f - t * 0.5f) * 0.09f);
            // Trail is a single pixel, skip every other for a dotted look
            if (t % 2 == 0) pset(d, tgx, (int)tgy);
        }

        // Ball: filled circle, radius 4
        for (int dx = -4; dx <= 4; dx++)
            for (int dy = -4; dy <= 4; dy++)
                if (dx * dx + dy * dy <= 16)
                    pset(d, bgx + dx, by + dy);
    }

    // ── Scene 2 · Spectrum Bars (40–60 s) ────────────────────────────────────
    // 64 bars spanning all 4 screens. Heights driven by two overlapping sine
    // waves that travel left-to-right, making the whole panorama pulse.
    void sceneSpectrum(int d, int f) {
        const int barW = 8; // 512 / 8 = 64 bars
        float     wave = f * 0.1f;
        for (int lx = 0; lx < kW; lx += barW) {
            float gx      = d * kW + lx;
            float barIdx  = gx / (float)barW;
            float h = 14.0f
                    + 10.0f * arm_sin_f32(barIdx * 0.25f - wave)
                    +  6.0f * arm_sin_f32(barIdx * 0.5f  - wave * 1.7f);
            int height = (int)(h < 2.0f ? 2.0f : h > kH - 2.0f ? kH - 2.0f : h);

            // Solid bar from bottom
            for (int y = kH - 1; y >= kH - height; y--)
                for (int bx = 0; bx < barW - 1; bx++)
                    mDisplay.DrawPixel(lx + bx, y, true);

            // Bright "bounce dot" 2 px above bar
            int peakY = kH - height - 2;
            if (peakY >= 0)
                for (int bx = 0; bx < barW - 1; bx++)
                    mDisplay.DrawPixel(lx + bx, peakY, true);
        }
    }

    // ── Scene 3 · Matrix Rain (60–80 s) ──────────────────────────────────────
    // Each column drops a lit streak. A density wave sweeps left-to-right,
    // making the rain heavier in a band that travels across the screens.
    void sceneRain(int d, int f) {
        for (int lx = 0; lx < kW; lx += 2) {
            int gx = d * kW + lx;

            // Density wave: columns near the wave peak fall; others skip
            float densityWave = arm_sin_f32(gx * 0.025f - f * 0.04f);
            if (densityWave < -0.1f) continue;

            // Each column has a unique period and starting offset
            int period = 20 + (gx * 7) % 16;   // 20–35 frames per drop
            int offset = (gx * 13) % period;
            int headY  = ((f + offset) % period) * kH / period;

            // Draw 7-pixel streak, sparser at the tail
            for (int len = 0; len < 7; len++) {
                int y = headY - len;
                if (y < 0 || y >= kH) continue;
                if (len < 3 || len % 2 == 0)
                    mDisplay.DrawPixel(lx, y, true);
            }
        }
    }

    // ── Scene 4 · Ripple Rings (80–100 s) ────────────────────────────────────
    // Concentric oval rings expand outward from the centre of the panorama
    // (the seam between display 1 and 2). 5 rings staggered in time.
    void sceneRipple(int d, int f) {
        const int cx = kPW / 2;   // 256: panorama centre
        const int cy = kH  / 2;   // 16

        for (int ring = 0; ring < 5; ring++) {
            int age = (f - ring * 80 + kSceneLen) % kSceneLen;
            if (age > 200) continue;   // ring has left the screen

            float r  = age * 1.4f;
            int   rx = (int)(r * 3.5f);   // wide horizontal radius
            int   ry = (int)(r);           // shorter vertical radius
            if (ry <= 0) continue;

            // Parametric ellipse outline: step through y, compute x at each row
            for (int dy = -ry; dy <= ry; dy++) {
                int y = cy + dy;
                if (y < 0 || y >= kH) continue;
                float ratio = 1.0f - (float)(dy * dy) / (float)(ry * ry);
                if (ratio < 0.0f) continue;
                int xOff = (int)(rx * sqrtf(ratio));
                pset(d, cx - xOff, y);
                pset(d, cx + xOff, y);
            }
        }
    }

    // ── Scene 5 · Sonar Sweep (100–120 s) ────────────────────────────────────
    // A bright vertical line sweeps left-to-right across the panorama like a
    // radar sweep. It leaves a decaying echo, and "contact blips" light up
    // at fixed positions on the panorama just after the sweep passes them.
    void sceneSonar(int d, int f) {
        // Sweep: ~2.5 sweeps in 20 s (one full crossing every ~80 frames)
        int sweepGX = (f * 7) % kPW;

        // Draw sweep head and decaying trail for each column
        for (int lx = 0; lx < kW; lx++) {
            int gx  = d * kW + lx;
            int age = (sweepGX - gx + kPW) % kPW;  // frames since sweep passed

            if (age > 55) continue;

            // Density: bright head, increasingly sparse trail
            bool draw;
            if      (age == 0)              draw = true;
            else if (age < 6)               draw = true;
            else if (age < 20 && lx % 2 == 0) draw = true;
            else if (age < 55 && lx % 4 == 0) draw = true;
            else                            draw = false;

            if (!draw) continue;

            // Full-height column, but skip rows for a scan-line look at distance
            int step = age / 12 + 1;
            for (int y = 0; y < kH; y += step)
                mDisplay.DrawPixel(lx, y, true);
        }

        // Contact blips: small blobs that flare up just after the sweep passes
        static const int kBlipGX[4]  = { 70, 195, 317, 445 };
        static const int kBlipY[4]   = { 10, 22,  8,   18  };
        for (int b = 0; b < 4; b++) {
            int age = (sweepGX - kBlipGX[b] + kPW) % kPW;
            if (age > 40) continue;
            int sz = (age < 10) ? 3 : 2;
            for (int dx = -sz; dx <= sz; dx++)
                for (int dy = -sz; dy <= sz; dy++)
                    if (dx * dx + dy * dy <= sz * sz)
                        pset(d, kBlipGX[b] + dx, kBlipY[b] + dy);
        }
    }

    // ── Jaffx overrides ──────────────────────────────────────────────────────

    void init() override {
        static const daisy::Pin kCsPins[kN] = {
            seed::D0, seed::D1, seed::D2, seed::D3
        };
        // Select ALL displays before Init() so every display receives
        // the SSD1312 initialization sequence over SPI simultaneously.
        for (int i = 0; i < kN; i++) {
            mCS[i].Init(kCsPins[i], GPIO::Mode::OUTPUT);
            mCS[i].Write(false);  // assert CS (active low) = selected
        }

        DisplayType::Config cfg;
        cfg.driver_config.transport_config.pin_config.dc    = seed::D9;
        cfg.driver_config.transport_config.pin_config.reset = seed::D11;
        mDisplay.Init(cfg);   // init sequence broadcast to all 4 displays

        // Blank each display individually
        for (int d = 0; d < kN; d++) {
            selectDisplay(d);
            mDisplay.Fill(false);
            mDisplay.Update();
        }
    }

    float processAudio(float in) override {
        if (++mSampleAcc >= kSamplesPerFrm) {
            mSampleAcc = 0;
            mDoUpdate  = true;
        }
        return in;
    }

    void loop() override {
        if (mDoUpdate) {
            mDoUpdate = false;
            renderAllDisplays();
            mFrame = (mFrame + 1) % kTotalFrames;
        }
    }
};

int main() {
    DisplayParty dp;
    dp.start();
    return 0;
}
