/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file FpsNavigation.h Header for FpsNavigation.
 */

#include <SDL.h>

#include <Camera.h>
#include <ToolKit.h>

namespace ToolKit
{

  /**
   * Free camera navigation for a standalone or web game.
   *
   * The desktop controls are the ones the editor viewport ships
   * (EditorViewport::FpsNavigationMod), with the same defaults: WASD moves along the view,
   * PageUp / PageDown along the camera's own up vector, and the look goes through
   * DirectionComponent so the camera keeps one orientation model. The editor reads that input
   * through imgui, which a game does not link, so this class polls SDL instead.
   *
   * Touch has no editor counterpart and is added here: one finger drags the look, two fingers pinch
   * to walk the camera along its view -- apart walks forward, together walks back. The two gestures
   * never bleed into each other: the second finger switches the gesture to a pinch without applying
   * any look on that frame, and a finger left on the glass after a pinch is ignored until every
   * finger is lifted, so finishing a pinch can not turn the view. Touch is read from SDL's finger
   * state rather than from events, and while any finger is down the mouse path is skipped -- a
   * browser reports a touch as a mouse drag too, and applying both would double the motion.
   */
  class FpsNavigation
  {
   public:
    /**
     * Binds the camera to drive and takes over its orientation.
     * @param camera is the camera to navigate. Not owned.
     */
    void Init(const CameraPtr& camera);

    /** Drops the camera reference. Call before the engine tears down, it owns the camera. */
    void Release();

    /**
     * Places the camera so the given bounds fill the view, looking at their center.
     * @param bounds is the world space bounding box to frame.
     */
    void FrameBounds(const BoundingBox& bounds);

    /**
     * Reads the input devices and moves the camera.
     * @param deltaTime is the frame time in milliseconds, like every engine delta.
     */
    void Update(float deltaTime);

   private:
    /** The gesture the fingers are currently making. */
    enum class TouchState
    {
      None,
      Look,
      Pinch
    };

    /** Applies a relative look. Angles are in degrees, the same units the editor feeds. */
    void Look(float yawDegrees, float pitchDegrees);

    /** Translates the camera along its own axis from the held keys. */
    void Move(float deltaTime);

    /**
     * Reads the finger state and drives the look / pinch walk.
     * @return True when a finger was down, in which case the mouse path is skipped.
     */
    bool UpdateTouch();

   public:
    /** Movement speed in meters per second, the editor's App::m_camSpeed default. */
    float m_camSpeed = 8.0f;

    /** Mouse look in degrees per pixel, the editor's App::m_mouseSensitivity default. */
    float m_mouseSensitivity = 0.08f;

    /** Touch look in degrees per full screen width / height. */
    float m_touchSensitivity = 120.0f;

    /**
     * Meters the pinch walks the camera per unit of finger separation. A spread is measured in
     * screen fractions, so a fully open pinch is roughly one unit.
     */
    float m_pinchSpeed = 40.0f;

    /** Field of view limits, in radians, used by the fallback framing to pick one. */
    float m_minFov = 0.35f;
    float m_maxFov = 1.4f;

   private:
    CameraPtr m_camera = nullptr;

    // Look state. Shared by the mouse and the touch path, they are mutually exclusive.
    bool m_looking     = false;
    Vec2 m_lastPointer = Vec2(0.0f);

    // Touch gesture state.
    TouchState m_touchState = TouchState::None;
    float m_lastPinch       = 0.0f;
  };

} // namespace ToolKit
