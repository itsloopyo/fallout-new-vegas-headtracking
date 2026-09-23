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

int main() {
    const float clean[9]{-0.6f, 0, 0.8f, 0.8f, 0, 0.6f, 0, 1, 0};
    const float baseline[12]{0, 0, 1, 1, 0, 0, 0, 1, 0, 10, 20, 30};
    HeadTracking::WeaponView view;
    view.Capture(clean, clean, 1.0f, 0.5625f);
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
                view.Capture(clean, tracked, 1.0f, 0.5625f);
                for (float lens : {0.4f, 0.6784f, 1.0f, 1.3f}) {
                    for (int i = 0; i < 12; ++i) result[i] = baseline[i];
                    view.Apply(result, lens, lens*0.5625f);
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
                          "the weapon camera must stay at the clean eye under a lean");
                }
            }
        }
    }
    std::puts("Weapon view tests passed");
}
