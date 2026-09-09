#pragma once

#include <cameraunlock/ads/ads_blend.h>
#include <cameraunlock/ads/ads_fade.h>

namespace HeadTracking {

class AdsState {
public:
    using Pose = cameraunlock::ads::AdsEntryPose::Pose;
    using Mode = cameraunlock::ads::AdsMode;

    Pose Update(bool suppressed, bool aiming, bool live, const Pose& absolute,
                unsigned long long nowMs) {
        if (suppressed || !live) {
            Reset();
            return {};
        }
        m_aiming = aiming;
        const float scale = m_fade.Update(aiming, nowMs);
        // Keep the entry through the return fade; dropping it on release steps
        // straight back to the absolute pose and bypasses the entire transition.
        const Pose relative = m_entry.Relative(aiming || scale < 1.0f, live, absolute);
        return cameraunlock::ads::BlendAdsPose(m_mode, scale, absolute, relative);
    }

    void Reset() {
        m_entry.Reset();
        m_fade.Reset();
        m_aiming = false;
    }

    void SetMode(Mode mode) {
        if (mode == m_mode) return;
        m_mode = mode;
        Reset();
    }

    Mode GetMode() const { return m_mode; }
    bool IsAiming() const { return m_aiming; }
    bool ShowMarker() const { return m_aiming && m_mode == Mode::Marker; }

private:
    Mode m_mode = cameraunlock::ads::kDefaultAdsMode;
    cameraunlock::ads::AdsEntryPose m_entry;
    cameraunlock::ads::AdsFade m_fade;
    bool m_aiming = false;
};

}
