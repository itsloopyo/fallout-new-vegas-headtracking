#include "weapon_view.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

static void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

static bool Near(float a, float b) { return std::fabs(a - b) < 0.0001f; }

// With an identity clean basis the weapon camera's translation is the world
// lean times how far the toggle has carried the weapon eye to the leaned eye.
static float FreeLookShare(HeadTracking::WeaponView& view, bool trueFreeLook, unsigned long long nowMs) {
    const float identity[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};
    const float lean[3]{10, 0, 0};
    view.Capture(identity, identity, lean, trueFreeLook, nowMs, 1.0f, 0.5625f);
    return view.translation[0] / 10.0f;
}

static void TheToggleRidesTheTransition() {
    HeadTracking::WeaponView view;
    Check(Near(FreeLookShare(view, false, 1000), 0.0f), "sights locked starts at the clean eye with no transition");
    Check(Near(FreeLookShare(view, true, 2000), 0.0f), "switching on does not step the weapon");
    const float halfway = FreeLookShare(view, true, 2075);
    Check(halfway > 0.1f && halfway < 0.9f, "switching on carries the weapon eye to the lean over the transition");
    Check(Near(FreeLookShare(view, true, 2150), 1.0f), "true free look settles at the leaned eye");
    Check(Near(FreeLookShare(view, false, 3000), 1.0f), "switching off does not step the weapon");
    Check(Near(FreeLookShare(view, false, 3250), 0.0f), "sights locked settles back at the clean eye");

    FreeLookShare(view, true, 4000);
    const float mid = FreeLookShare(view, true, 4075);
    Check(Near(FreeLookShare(view, false, 4075), mid), "a reversal continues from where the transition is");
    Check(FreeLookShare(view, false, 4100) < mid, "and heads back to the clean eye");
}

int main() {
    TheToggleRidesTheTransition();

    const float clean[9]{-0.6f, 0, 0.8f, 0.8f, 0, 0.6f, 0, 1, 0};
    const float offset[3]{4, -2, 3};
    const float baseline[12]{0, 0, 1, 1, 0, 0, 0, 1, 0, 10, 20, 30};
    HeadTracking::WeaponView view;
    view.Capture(clean, clean, offset, false, 0, 1.0f, 0.5625f);
    float result[12];
    for (int i = 0; i < 12; ++i) result[i] = baseline[i];
    view.Apply(result, 0.7f, 0.39375f);
    for (int i = 0; i < 12; ++i)
        Check(Near(result[i], baseline[i]), "centred tracking must preserve the weapon camera");

    for (float yaw : {-1.9f, -0.6f, 0.0f, 0.7f, 1.9f}) {
        for (float pitch : {-0.8f, 0.0f, 0.8f}) {
            for (float roll : {-0.7f, 0.0f, 0.9f}) {
                const float cy = std::cos(yaw), sy = std::sin(yaw);
                const float cp = std::cos(pitch), sp = std::sin(pitch);
                const float cr = std::cos(roll), sr = std::sin(roll);
                const float head[9]{cy*cp, -cy*sp*cr-sy*sr, cy*sp*sr-sy*cr,
                                    sp, cp*cr, -cp*sr,
                                    sy*cp, -sy*sp*cr+cy*sr, sy*sp*sr+cy*cr};
                float tracked[9]{};
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        for (int k = 0; k < 3; ++k)
                            tracked[r*3+c] += clean[r*3+k]*head[k*3+c];
                view.Capture(clean, tracked, offset, false, 0, 1.0f, 0.5625f);
                HeadTracking::WeaponView freeLook;
                freeLook.Capture(clean, tracked, offset, true, 0, 1.0f, 0.5625f);
                freeLook.Capture(clean, tracked, offset, true, 1000, 1.0f, 0.5625f);
                for (float lens : {0.4f, 0.6784f, 1.0f, 1.3f}) {
                    for (int i = 0; i < 12; ++i) result[i] = baseline[i];
                    view.Apply(result, lens, lens*0.5625f);
                    float freeResult[12];
                    for (int i = 0; i < 12; ++i) freeResult[i] = baseline[i];
                    freeLook.Apply(freeResult, lens, lens*0.5625f);
                    for (int i = 0; i < 9; ++i)
                        Check(Near(freeResult[i], result[i]),
                              "true free look must turn the weapon camera exactly as sights locked does");
                    // The lean re-expressed from the clean world camera's basis into the
                    // weapon camera's.
                    Check(Near(freeResult[9], 12) && Near(freeResult[10], 16) && Near(freeResult[11], 33),
                          "in true free look the weapon camera must move with the lean to the tracked eye");
                    // The baseline weapon forward axis is world Y.
                    Check(Near(result[4]*head[0], head[1]*lens*result[3]) &&
                          Near(result[5]*head[0], head[2]*lens*result[3]),
                          "weapon aim and world aim must project to the same screen direction");
                    for (int a = 0; a < 3; ++a) {
                        for (int b = 0; b < 3; ++b) {
                            float dot = 0;
                            for (int k = 0; k < 3; ++k) dot += result[k*3+a]*result[k*3+b];
                            Check(Near(dot, a == b ? 1.0f : 0.0f), "weapon camera must remain orthonormal");
                        }
                    }
                    Check(Near(result[9], 10) && Near(result[10], 20) && Near(result[11], 30),
                          "in sights locked the weapon camera must stay at the clean eye under a lean");
                }
            }
        }
    }
    std::puts("Weapon view tests passed");
}
