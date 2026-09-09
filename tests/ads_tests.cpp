#include "ads.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using HeadTracking::AdsState;
using Mode = AdsState::Mode;

static void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

static bool Near(float a, float b) { return std::fabs(a - b) < 0.001f; }

int main() {
    const AdsState::Pose head{10, 30, 7, 0.1f, 0.2f, 0.3f};
    AdsState ads;
    auto pose = ads.Update(false, false, true, head, 0);
    Check(Near(pose.yaw, 30) && Near(pose.x, 0.1f), "hip pose must pass through");
    pose = ads.Update(false, true, true, head, 10);
    Check(Near(pose.yaw, 30), "ADS entry must not cut the pose");
    pose = ads.Update(false, true, true, head, 85);
    Check(Near(pose.yaw, 15), "paused ADS must fade during entry");
    pose = ads.Update(false, true, true, head, 160);
    Check(ads.IsAiming() && Near(pose.yaw, 0) && Near(pose.x, 0), "paused ADS must remove aim axes and lean");
    Check(Near(pose.roll, 7), "roll must remain absolute");
    pose = ads.Update(false, false, true, head, 170);
    Check(!ads.IsAiming() && Near(pose.yaw, 0), "release must start at the settled pose");
    pose = ads.Update(false, false, true, head, 420);
    Check(Near(pose.yaw, 30), "paused return must reach the absolute pose");

    for (auto mode : {Mode::Marker, Mode::Tracked}) {
        ads.SetMode(mode);
        ads.Update(false, true, false, head, 500);
        const AdsState::Pose entry{12, 175, 9, 1, 2, 3};
        const AdsState::Pose moved{15, -175, 11, 2, 4, 6};
        ads.Update(false, true, true, entry, 510);
        pose = ads.Update(false, true, true, moved, 660);
        Check(Near(pose.yaw, 10) && Near(pose.pitch, 3), "entry must use a live pose and cross the yaw seam the short way");
        Check(Near(pose.roll, 11) && Near(pose.x, 1) && Near(pose.y, 2) && Near(pose.z, 3), "roll absolute, position relative");
        Check(ads.ShowMarker() == (mode == Mode::Marker), "marker follows the selected mode");
        pose = ads.Update(false, false, true, moved, 670);
        Check(Near(pose.yaw, 10), "entry must survive weapon release");
        pose = ads.Update(false, false, true, moved, 920);
        Check(Near(pose.yaw, -175), "return must restore the absolute pose");
        ads.Update(false, true, true, head, 1000);
        pose = ads.Update(true, true, true, head, 1010);
        Check(!ads.IsAiming() && !ads.ShowMarker() && Near(pose.roll, 0), "menu suppression must outrank ADS and clear its flag");
        ads.Update(false, true, false, head, 1020);
        ads.Update(false, true, true, moved, 1030);
        pose = ads.Update(false, true, true, moved, 1180);
        Check(Near(pose.yaw, 0), "resume during ADS must capture a fresh entry");
        ads.Reset();
    }

    ads.SetMode(Mode::Paused);
    ads.Update(false, true, true, head, 2000);
    const auto before = ads.Update(false, true, true, head, 2030);
    const auto reversal = ads.Update(false, false, true, head, 2030);
    Check(Near(before.yaw, reversal.yaw), "rapid aim reversal must be continuous");
    Check(cameraunlock::ads::NextAdsMode(Mode::Paused) == Mode::Marker &&
          cameraunlock::ads::NextAdsMode(Mode::Marker) == Mode::Tracked &&
          cameraunlock::ads::NextAdsMode(Mode::Tracked) == Mode::Paused, "three-slot cycle order");
    Check(cameraunlock::ads::ParseAdsMode("garbage") == Mode::Paused, "invalid mode must use the default");
    std::puts("ADS entry, fade, suppression and cycle tests passed");
}
