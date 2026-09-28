// The lean split between the camera and the first-person skeleton (RigLean),
// driven frame by frame the way the game drives it: skeleton update (TakeRig),
// camera placed on the shifted Camera1st bone, culling hook (clamp, Split),
// Present (EndFrame).

#include "rig_lean.h"

#include <cameraunlock/camera/lean_clamp.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using cameraunlock::math::Vec3;
using HeadTracking::RigLean;

static int g_failures = 0;

static void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

static bool Near(const Vec3& a, const Vec3& b) {
    return std::fabs(a.x - b.x) < 0.001f && std::fabs(a.y - b.y) < 0.001f && std::fabs(a.z - b.z) < 0.001f;
}

static bool IsZero(const Vec3& v) { return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f; }

// A lean given in a view pitched 30 degrees down and yawed, in world axes.
static Vec3 PitchedLean(float right, float up, float forward) {
    const float pitch = -0.5236f, yaw = 0.9f;
    const Vec3 f{std::cos(pitch) * std::sin(yaw), std::cos(pitch) * std::cos(yaw), std::sin(pitch)};
    const Vec3 r{std::cos(yaw), -std::sin(yaw), 0.0f};
    const Vec3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
    return r * right + u * up + f * forward;
}

struct Frame {
    Vec3 rig;     // what the skeleton carried this frame
    Vec3 camera;  // what the camera added, the share the aim hook and reticle get
    Vec3 eye;     // where the rendered eye ended up
};

struct Game {
    RigLean rig;
    Vec3 clean{100.0f, -50.0f, 120.0f};
    unsigned long long now = 1000;

    Frame Step(const Vec3& lean, bool aiming, bool trueFreeLook = false, bool firstPerson = true) {
        Frame f;
        f.rig = rig.TakeRig();
        const Vec3 cameraEye = clean + f.rig;
        f.camera = rig.Split(lean, aiming, trueFreeLook, firstPerson, now);
        f.eye = cameraEye + f.camera;
        rig.EndFrame();
        now += 16;
        return f;
    }
};

static void HipKeepsTheLeanOnTheCamera() {
    Game g;
    const Vec3 lean = PitchedLean(15.4f, 3.0f, -2.0f);
    for (int i = 0; i < 60; ++i) {
        const Frame f = g.Step(lean, false);
        Check(IsZero(f.rig), "at the hip the rig is never written");
        Check(Near(f.camera, lean), "at the hip the camera carries the whole lean");
    }
}

static void SightsUpPutTheLeanOnTheRig() {
    Game g;
    const Vec3 lean = PitchedLean(15.4f, 3.0f, -2.0f);
    for (int i = 0; i < 40; ++i) g.Step(lean, true);
    const Frame f = g.Step(lean, true);
    Check(Near(f.rig, lean), "with the sights up the rig carries the whole lean");
    Check(Near(f.camera, Vec3()), "with the sights up the camera share handed to the aim and reticle is zero");
}

static void EveryCarrierPutsTheEyeInTheSamePlace() {
    Game g;
    bool sawSplit = false;
    for (int i = 0; i < 200; ++i) {
        const float t = static_cast<float>(i);
        const Vec3 lean = PitchedLean(15.0f * std::sin(t * 0.13f), 4.0f * std::cos(t * 0.07f), 3.0f * std::sin(t * 0.05f));
        const bool aiming = (i / 9) % 2 == 1;
        const Frame f = g.Step(lean, aiming);
        Check(Near(f.eye, g.clean + lean), "the eye is the clean eye plus the lean whichever carrier holds it");
        sawSplit = sawSplit || (!IsZero(f.rig) && !Near(f.camera, Vec3()) && !Near(f.rig, lean));
    }
    Check(sawSplit, "the walk covered frames with the lean split between both carriers");
}

static void EveryStopReleasesTheRig() {
    const Vec3 lean = PitchedLean(15.4f, 0.0f, 0.0f);
    {
        Game g;
        for (int i = 0; i < 40; ++i) g.Step(lean, true);
        for (int i = 0; i < 40; ++i) g.Step(lean, false);
        Check(IsZero(g.Step(lean, false).rig), "sights down puts the rig back at its origin");
    }
    {
        Game g;
        for (int i = 0; i < 40; ++i) g.Step(lean, true);
        g.Step(Vec3(), true);
        Check(IsZero(g.Step(Vec3(), true).rig), "position off puts the rig back at its origin");
    }
    {
        Game g;
        for (int i = 0; i < 40; ++i) g.Step(lean, true);
        g.rig.Stop();
        Check(IsZero(g.rig.TakeRig()), "a suspend puts the rig back at its origin");
    }
    {
        Game g;
        for (int i = 0; i < 40; ++i) g.Step(lean, true);
        g.rig.TakeRig();
        g.rig.EndFrame();
        // A frame whose camera never ran: the next skeleton update writes nothing.
        Check(IsZero(g.rig.TakeRig()), "a frame without a camera update leaves the rig alone");
    }
}

static void TrueFreeLookAndThirdPersonKeepTheLeanOnTheCamera() {
    const Vec3 lean = PitchedLean(15.4f, 3.0f, 0.0f);
    Game g;
    for (int i = 0; i < 40; ++i) {
        const Frame f = g.Step(lean, true, true);
        Check(IsZero(f.rig), "true free look never moves the rig");
        Check(Near(f.camera, lean), "true free look keeps the whole lean on the camera");
    }
    Game third;
    for (int i = 0; i < 40; ++i) Check(IsZero(third.Step(lean, true, false, false).rig), "the third-person camera never moves the rig");
}

// A wall 20 units along +x from the clean eye.
static cameraunlock::camera::LeanObstruction Wall(void* context, const Vec3& start, const Vec3& direction, float max) {
    const float wallX = *static_cast<const float*>(context);
    cameraunlock::camera::LeanObstruction hit;
    hit.queried = true;
    if (direction.x <= 0.0f) return hit;
    const float distance = (wallX - start.x) / direction.x;
    hit.blocked = distance <= max;
    hit.distance = distance;
    return hit;
}

static void TheClampHoldsTheSameStandoffWhicheverCarrier() {
    for (const bool aiming : {false, true}) {
        Game g;
        float wallX = g.clean.x + 20.0f;
        cameraunlock::camera::LeanClamp clamp;
        cameraunlock::camera::LeanClampSettings settings;
        settings.skin = 5.0f;
        clamp.SetSettings(settings);
        const Vec3 desired{40.0f, 0.0f, 0.0f};
        Frame f;
        for (int i = 0; i < 40; ++i) {
            f.rig = g.rig.TakeRig();
            const Vec3 cameraEye = g.clean + f.rig;
            // As the culling hook does: clamp from the camera's eye minus the rig's share.
            const Vec3 lean = clamp.Apply(cameraEye - g.rig.Applied(), desired, 0.016f, &Wall, &wallX);
            f.camera = g.rig.Split(lean, aiming, false, true, g.now);
            f.eye = cameraEye + f.camera;
            g.rig.EndFrame();
            g.now += 16;
        }
        Check(std::fabs(f.eye.x - (wallX - 5.0f)) < 0.01f, "the eye stops the standoff short of the wall");
        if (aiming) Check(Near(f.rig, Vec3{15.0f, 0.0f, 0.0f}), "with the sights up the rig holds the clamped lean");
    }
}

int main() {
    HipKeepsTheLeanOnTheCamera();
    SightsUpPutTheLeanOnTheRig();
    EveryCarrierPutsTheEyeInTheSamePlace();
    EveryStopReleasesTheRig();
    TrueFreeLookAndThirdPersonKeepTheLeanOnTheCamera();
    TheClampHoldsTheSameStandoffWhicheverCarrier();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("Rig lean tests passed");
    return 0;
}
