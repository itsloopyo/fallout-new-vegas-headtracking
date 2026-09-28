#pragma once

#include <cameraunlock/ads/ads_fade.h>

#include <cmath>

namespace HeadTracking {

struct WeaponView {
    bool valid = false;
    float rotation[9]{};
    // The lean in the clean camera's basis, zero in sights locked.
    float translation[3]{};
    float tanX = 0;
    float tanY = 0;

    // Sights locked keeps the weapon camera at the clean eye, so a lean
    // parallaxes the world past the weapon instead of throwing it off its
    // sights. True free look moves it with the whole collision-clamped world
    // lean, the camera's share plus the rig's, so the weapon stays put in the
    // world while the eye leaves it. The toggle rides AdsFade's transition
    // rather than stepping the weapon by the whole lean in one frame; fed the
    // mode as the aim state, the fade rests at the clean eye in sights locked,
    // so the default mode starts there without a transition.
    void Capture(const float* clean, const float* tracked, const float* lean, bool trueFreeLook,
                 unsigned long long nowMs, float worldTanX, float worldTanY) {
        const float freeLook = 1.0f - m_freeLook.Update(trueFreeLook, nowMs);
        for (int row = 0; row < 3; ++row) {
            translation[row] = 0;
            for (int col = 0; col < 3; ++col) {
                rotation[row * 3 + col] = 0;
                for (int k = 0; k < 3; ++k)
                    rotation[row * 3 + col] += clean[k * 3 + row] * tracked[k * 3 + col];
                translation[row] += clean[col * 3 + row] * lean[col] * freeLook;
            }
        }
        tanX = worldTanX;
        tanY = worldTanY;
        valid = true;
    }

    void Apply(float* transform, float weaponTanX, float weaponTanY) const {
        float relative[9];
        for (int row = 0; row < 3; ++row) {
            relative[row * 3] = rotation[row * 3];
            relative[row * 3 + 1] = rotation[row * 3 + 1] * weaponTanY / tanY;
            relative[row * 3 + 2] = rotation[row * 3 + 2] * weaponTanX / tanX;
        }
        // Match the projected aim direction across the two lenses, then rebuild
        // an orthonormal camera basis so the weapon retains its shape.
        const auto normalize = [](float* v) {
            const float length = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
            for (int k = 0; k < 3; ++k) v[k] /= length;
        };
        normalize(relative);
        const float dot = relative[0]*relative[3] + relative[1]*relative[4] + relative[2]*relative[5];
        for (int k = 0; k < 3; ++k) relative[3 + k] -= dot * relative[k];
        normalize(relative + 3);
        relative[6] = relative[1]*relative[5] - relative[2]*relative[4];
        relative[7] = relative[2]*relative[3] - relative[0]*relative[5];
        relative[8] = relative[0]*relative[4] - relative[1]*relative[3];

        float result[12];
        for (int row = 0; row < 3; ++row) {
            result[9 + row] = transform[9 + row];
            for (int col = 0; col < 3; ++col) {
                result[row * 3 + col] = 0;
                for (int k = 0; k < 3; ++k)
                    result[row * 3 + col] += transform[row * 3 + k] * relative[k * 3 + col];
                result[9 + row] += transform[row * 3 + col] * translation[col];
            }
        }
        for (int i = 0; i < 12; ++i) transform[i] = result[i];
    }

private:
    cameraunlock::ads::AdsFade m_freeLook;
};

namespace D3D9Internal {
extern WeaponView g_weaponView;
}
}
