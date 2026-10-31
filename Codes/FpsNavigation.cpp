/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "FpsNavigation.h"

#include <DirectionComponent.h>
#include <MathUtil.h>
#include <Scene.h>

namespace ToolKit
{

  void FpsNavigation::Init(const CameraPtr& camera)
  {
    m_camera     = camera;
    m_looking    = false;
    m_touchState = TouchState::None;
    m_lastPinch  = 0.0f;

    // The camera starts wherever the scene put it; the controller drives it from here on.
    m_lastPointer = Vec2(0.0f);
  }

  void FpsNavigation::Release() { m_camera = nullptr; }

  void FpsNavigation::Update(float deltaTime)
  {
    if (m_camera == nullptr)
    {
      return;
    }

    // Touch first: a browser reports a touch as a mouse drag as well, and both would double the
    // rotation.
    if (!UpdateTouch())
    {
      IVec2 pointer;
      const Uint32 buttons = SDL_GetMouseState(&pointer.x, &pointer.y);
      const bool pressed   = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
      const Vec2 current((float) pointer.x, (float) pointer.y);

      if (pressed)
      {
        if (m_looking)
        {
          const Vec2 delta = current - m_lastPointer;
          Look(delta.x * m_mouseSensitivity, delta.y * m_mouseSensitivity);
        }
        m_lastPointer = current;
        m_looking     = true;
      }
      else
      {
        m_looking = false;
      }
    }

    Move(deltaTime);
  }

  void FpsNavigation::Look(float yawDegrees, float pitchDegrees)
  {
    DirectionComponent* dc = m_camera->GetComponentFast<DirectionComponent>();
    if (dc == nullptr)
    {
      return;
    }

    // Mirror of EditorViewport::FpsNavigationMod: pitch on the local X axis, yaw around the up
    // vector, both negated so that dragging right looks right and dragging down looks down.
    dc->Pitch(-glm::radians(pitchDegrees));
    dc->RotateOnUpVector(-glm::radians(yawDegrees));
  }

  void FpsNavigation::Move(float deltaTime)
  {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (keys == nullptr)
    {
      return;
    }

    Vec3 move(0.0f);
    if (keys[SDL_SCANCODE_A])
    {
      move -= X_AXIS;
    }

    if (keys[SDL_SCANCODE_D])
    {
      move += X_AXIS;
    }

    if (keys[SDL_SCANCODE_W])
    {
      move -= Z_AXIS;
    }

    if (keys[SDL_SCANCODE_S])
    {
      move += Z_AXIS;
    }

    if (keys[SDL_SCANCODE_PAGEUP])
    {
      move += Y_AXIS;
    }

    if (keys[SDL_SCANCODE_PAGEDOWN])
    {
      move -= Y_AXIS;
    }

    if (glm::length(move) <= 0.0f)
    {
      return;
    }

    float speed = m_camSpeed;
    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
    {
      speed *= 4.0f;
    }

    // Local space, like the editor: the camera flies where it looks, and PageUp pushes along its
    // own up vector rather than the world's.
    move = glm::normalize(move);
    m_camera->m_node->Translate(move * (speed * MillisecToSec(deltaTime)),
                               TransformationSpace::TS_LOCAL);
  }

  bool FpsNavigation::UpdateTouch()
  {
    // SDL keeps the live finger state, so the gestures can be read without consuming events.
    Vec2 points[2];
    int fingerCount = 0;
    Vec2 center(0.0f);

    const int deviceCount = SDL_GetNumTouchDevices();
    for (int i = 0; i < deviceCount; i++)
    {
      const SDL_TouchID device      = SDL_GetTouchDevice(i);
      const int deviceFingerCount   = SDL_GetNumTouchFingers(device);
      for (int f = 0; f < deviceFingerCount; f++)
      {
        const SDL_Finger* finger = SDL_GetTouchFinger(device, f);
        if (finger == nullptr)
        {
          continue;
        }

        const Vec2 position((float) finger->x, (float) finger->y);
        if (fingerCount < 2)
        {
          points[fingerCount] = position;
        }

        center += position;
        fingerCount++;
      }
    }

    if (fingerCount == 0)
    {
      // Nothing left on the glass: the next contact starts a fresh gesture.
      m_touchState = TouchState::None;
      m_lastPinch  = 0.0f;
      return false;
    }

    if (fingerCount >= 2)
    {
      const float distance = glm::length(points[1] - points[0]);

      // The second finger taking part switches the gesture to a pinch. That frame applies no look,
      // and the separation is only seeded here, so neither the arriving finger nor the frames before
      // it turn the view.
      if (m_touchState != TouchState::Pinch)
      {
        m_touchState = TouchState::Pinch;
        m_lastPinch  = distance;
        return true;
      }

      if (m_lastPinch > 0.0f && distance > 0.0f)
      {
        // Fingers apart walks the camera forward along its own view, together walks it back. This is
        // a walk, not a lens change: the field of view stays where the scene put it.
        const float travel = (distance - m_lastPinch) * m_pinchSpeed;
        if (travel != 0.0f)
        {
          Vec3 forward = m_camera->Direction();
          if (glm::length(forward) > 0.0f)
          {
            forward = glm::normalize(forward);
            m_camera->m_node->Translate(forward * travel, TransformationSpace::TS_WORLD);
          }
        }
      }

      m_lastPinch = distance;
      return true;
    }

    // A single finger left over from a pinch keeps the gesture latched until the hand is lifted. It
    // must not start looking, or letting go of a pinch would turn the view.
    if (m_touchState == TouchState::Pinch)
    {
      return true;
    }

    center /= (float) fingerCount;

    if (m_touchState != TouchState::Look)
    {
      m_touchState  = TouchState::Look;
      m_lastPointer = center;
      return true;
    }

    // One finger: the finger position is normalized to the window, so the sensitivity is per
    // screen rather than per pixel.
    const Vec2 delta = center - m_lastPointer;
    m_lastPointer    = center;
    Look(delta.x * m_touchSensitivity, delta.y * m_touchSensitivity);
    return true;
  }

  void FpsNavigation::FrameBounds(const BoundingBox& bounds)
  {
    if (m_camera == nullptr || !bounds.IsValid())
    {
      return;
    }

    const Vec3 center  = bounds.GetCenter();
    const Vec3 extents = bounds.max - bounds.min;
    const float radius = glm::length(extents) * 0.5f;
    if (radius <= 0.0f)
    {
      return;
    }

    // Clip planes follow the scene's size. The engine default far plane (1000) is shorter than a
    // city block and a default near plane of 0.01 costs depth precision at that range.
    const float nearClip = glm::clamp(radius * 0.01f, 0.05f, 2.0f);
    const float farClip  = glm::max(radius * 8.0f, 1000.0f);

    // A three quarter view from above reads better than a straight on one.
    const Quaternion rotation =
        glm::quat(Vec3(glm::radians(-25.0f), glm::radians(35.0f), 0.0f));
    const Vec3 forward = rotation * (-Z_AXIS);

    const float fov      = glm::clamp(m_camera->Fov(), m_minFov, m_maxFov);
    const float distance = (radius / glm::sin(fov * 0.5f)) * 1.1f;

    m_camera->SetLens(fov, m_camera->Aspect(), nearClip, farClip);
    m_camera->m_node->SetOrientation(rotation, TransformationSpace::TS_WORLD);
    m_camera->m_node->SetTranslation(center - forward * distance,
                                     TransformationSpace::TS_WORLD);
  }

} // namespace ToolKit
