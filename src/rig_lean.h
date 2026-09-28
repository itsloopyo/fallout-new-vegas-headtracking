#pragma once

#include <cameraunlock/ads/lean_handover.h>
#include <cameraunlock/math/vec3.h>

namespace HeadTracking {

// The lean split between the world camera and the first-person skeleton root,
// the rig the camera bone (Camera1st), the arms, the weapon and its
// ProjectileNode all hang off. The game updates that skeleton early in the
// frame, fires from it during the same update, and only later places the
// camera, so the rig share decided at this frame's camera is written at the
// next frame's skeleton update. The camera makes up the difference: the eye is
// always the clean eye plus the whole lean, whichever carrier holds it.
class RigLean {
public:
    using Vec3 = cameraunlock::math::Vec3;

    // At the skeleton update: the world offset to add to the root this frame.
    // Taken once, so a frame whose camera never ran writes nothing next frame.
    // The third-person camera does not sit on the skeleton, so there nothing is
    // written and the camera carries the whole lean, sights up or not.
    Vec3 TakeRig(bool firstPerson) {
        m_applied = firstPerson ? m_next : Vec3();
        m_next = Vec3();
        return m_applied;
    }

    // The rig share inside the eye the camera reads this frame.
    const Vec3& Applied() const { return m_applied; }

    // At the camera, with the whole lean already clamped from the clean eye
    // (the camera's eye minus Applied()) and the clean camera's unit forward
    // axis, both in world axes. Returns the offset to add to the camera's eye:
    // the camera's share of the lean, the only share that opens a gap between
    // the eye and the round. Only the lean across the aim goes to the rig.
    Vec3 Split(const Vec3& lean, const Vec3& aimForward, bool aiming, bool trueFreeLook, bool rigAvailable,
               unsigned long long nowMs) {
        m_next = m_handover.Update(lean, aimForward, aiming, trueFreeLook, rigAvailable, nowMs).rig;
        return lean - m_applied;
    }

    // End of the displayed frame: the game rewrites the root before its next
    // update, so nothing carries over.
    void EndFrame() { m_applied = Vec3(); }

    // Every path that stops the lean. The game rewrites the root every frame,
    // so writing nothing more is the release.
    void Stop() {
        m_handover.Stop();
        m_next = Vec3();
        m_applied = Vec3();
    }

private:
    cameraunlock::ads::LeanHandover m_handover;
    Vec3 m_next;
    Vec3 m_applied;
};

namespace D3D9Internal {
extern RigLean g_rigLean;
}

}  // namespace HeadTracking
