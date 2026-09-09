#pragma once

#include <cmath>

namespace HeadTracking {

struct WeaponView {
    bool valid = false;
    float rotation[9]{};
    float translation[3]{};
    float tanX = 0;
    float tanY = 0;

    void Capture(const float* clean, const float* tracked, const float* offset,
                 float worldTanX, float worldTanY) {
        for (int row = 0; row < 3; ++row) {
            translation[row] = 0;
            for (int col = 0; col < 3; ++col) {
                rotation[row * 3 + col] = 0;
                for (int k = 0; k < 3; ++k)
                    rotation[row * 3 + col] += clean[k * 3 + row] * tracked[k * 3 + col];
                translation[row] += clean[col * 3 + row] * offset[col];
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
};

namespace D3D9Internal {
extern WeaponView g_weaponView;
}
}
