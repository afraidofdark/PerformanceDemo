/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0
 * (LGPL-3.0). For more information, including options for a more permissive
 * commercial license, please visit [otsoftware.tr] or contact us at
 * [info@otsoftare.tr].
 */

#include "Game.h"

#include "Camera.h"
#include "AnimationControllerComponent.h"
#include "DirectionComponent.h"
#include "PerfProbe.h"

#include <SDL.h>
#include <Scene.h>

#include <cmath>
ToolKit::Game Self;

extern "C" TK_PLUGIN_API ToolKit::Game* TK_STDCAL GetInstance() { return &Self; }

namespace ToolKit
{
  namespace
  {
    /** The scene a standalone or web build opens; under PIE the editor brings its
     * own. */
    constexpr const char* StartupScene = "unity-city.scene";

    /**
     * World bounds of the scene. The aabb tree is filled as entities are added, so
     * a scene that was not built through AddEntity has no root bound and the
     * entities are walked instead.
     */
    BoundingBox SceneBounds(Scene* scene)
    {
      BoundingBox bounds = scene->GetSceneBoundary();
      if (bounds.IsValid())
      {
        return bounds;
      }

      for (const EntityPtr& entity : scene->GetEntities())
      {
        if (entity != nullptr)
        {
          bounds.UpdateBoundary(entity->GetBoundingBox(true));
        }
      }

      return bounds;
    }

    /**
     * Returns the camera the scene was authored around, or null when it has none.
     *
     * Scenes name that camera Camera. The first camera in the scene is not necessarily it - a scene with a
     * second camera, or one whose cameras were added in another order, opened looking at a corner of nothing,
     * which is exactly how the reflective scene came up black on the device.
     */
    CameraPtr SceneCamera(const ScenePtr& scene)
    {
      if (scene == nullptr)
      {
        return nullptr;
      }

      if (EntityPtr named = scene->GetFirstByName("Camera"))
      {
        if (named->IsA<Camera>())
        {
          return std::static_pointer_cast<Camera>(named);
        }
      }

      for (const EntityPtr& entity : scene->GetEntities())
      {
        if (entity != nullptr && entity->IsA<Camera>())
        {
          return std::static_pointer_cast<Camera>(entity);
        }
      }

      return nullptr;
    }
  } // namespace

  void Game::Init(Main* master)
  {
    Main::SetProxy(master);

#ifdef TK_GAME_STANDALONE
    // Loading the game scene belongs here, not in the host: the standalone and
    // web builds have no editor to open one, while PIE already runs on the scene
    // the editor has open.
    SceneManager* sceneMan = GetSceneManager();
    if (sceneMan->GetCurrentScene() == nullptr)
    {
      ScenePtr scene = sceneMan->Create<Scene>(ScenePath(StartupScene));
      if (scene != nullptr)
      {
        sceneMan->SetCurrentScene(scene);
      }
      else
      {
        TK_ERR("Startup scene %s could not be loaded.", StartupScene);
      }
    }

    // Free camera: WASD / PageUp / PageDown move, left drag looks, touch drags
    // and pinches. The viewport camera is the one the game render path draws
    // with.
    if (m_viewport == nullptr)
    {
      return;
    }

    // Use the camera the scene ships with rather than the viewport's own: that
    // position is authored content, and the viewport only holds the pointer, so
    // the game renders through the scene camera and the navigator drives it from
    // where the scene put it.
    ScenePtr scene   = sceneMan->GetCurrentScene();
    CameraPtr camera = SceneCamera(scene);
    if (camera != nullptr)
    {
      m_viewport->SetCamera(camera);
    }

    m_navigation.Init(m_viewport->GetCamera());

    // Without a scene camera the viewport sits at the origin looking down -Z, so
    // frame what the scene contains. A camera that came from the scene is left
    // exactly where it is.
    if (camera == nullptr && scene != nullptr)
    {
      m_navigation.FrameBounds(SceneBounds(scene.get()));
    }

    // Device experiments: the probe reads its knobs from system properties and
    // reports the frame's spans to the log. It runs after the scene is up because
    // half of the knobs are scene settings.
    Perf::Configure(scene.get(), m_viewport.get());

    // debug.tk.camanim: play the first track the camera's animation controller carries, looping, so a
    // camera that moves on its own can be measured. The component and its animation are authored in the scene;
    // the track only has to exist. A camera that moves is the only way to see what the renderer does when the
    // set of visible objects changes every frame, which the fixed measurements never exercise.
    if (Perf::ReadIntProperty("debug.tk.camanim", 0) != 0)
    {
      if (CameraPtr animated = m_viewport->GetCamera())
      {
        if (AnimControllerComponentPtr controller = animated->GetComponent<AnimControllerComponent>())
        {
          AnimRecordPtrMap& records = controller->ParamRecords().GetVar<AnimRecordPtrMap>();
          if (records.empty())
          {
            TK_ERR("Camera animation: the controller on %s carries no track.", animated->GetNameVal().c_str());
          }
          else
          {
            const String signal = records.begin()->first;
            if (records.begin()->second != nullptr)
            {
              records.begin()->second->m_loop = true;
            }
            controller->Play(signal);
            TK_LOG("Camera animation: playing '%s' on %s.", signal.c_str(), animated->GetNameVal().c_str());
          }
        }
        else
        {
          TK_ERR("Camera animation: %s has no animation controller.", animated->GetNameVal().c_str());
        }
      }
    }

    if (CameraPtr camera = m_viewport->GetCamera())
    {
      if (camera->m_node != nullptr)
      {
        m_oscOrigin      = camera->m_node->GetTranslation(TransformationSpace::TS_WORLD);
        m_oscOriginValid = true;

        // Two views, 80 degrees apart, for the ab test. The targets are absolute
        // points, so each frame aims the camera at one or the other instead of
        // rotating it from wherever it was.
        if (DirectionComponentPtr direction = camera->GetComponent<DirectionComponent>())
        {
          const Vec3 forward = direction->GetDirection();
          const Vec3 up      = Vec3(0.0f, 1.0f, 0.0f);
          const auto aim     = [&](float degrees) -> Vec3
          {
            const float radians = glm::radians(degrees);
            const float cs      = std::cos(radians);
            const float sn      = std::sin(radians);
            const Vec3 yawed    = Vec3(forward.x * cs + forward.z * sn, forward.y, -forward.x * sn + forward.z * cs);
            return m_oscOrigin + yawed * 10.0f;
          };

          m_poseAEye       = m_oscOrigin;
          m_poseATarget    = aim(30.0f);
          m_poseBTarget    = aim(-50.0f);
          m_alternateValid = true;
        }
      }
    }

    // The test drives the viewport camera by itself and freezes the navigator, so a build that has it
    // on cannot be used to fly around the scene. It says so, loudly, and names the knob that turns it
    // off - a measurement mode that leaves a user wondering why the camera is stuck is worse than no
    // measurement mode at all.
    if (Perf::TwoCameras())
    {
      TK_WRN("Two camera presentation test is ON (debug.tk.twocam=%i): the viewport camera is driven by "
             "the test and the navigator is frozen. Set debug.tk.twocam -1 and restart to use the app.",
             Perf::TwoCameraMode());
    }

    // Two camera presentation test, see Perf::TwoCameras(). Camera B is the scene
    // camera's own copy standing TwoCameraOffset world units along its local X
    // axis, so the two views differ by nothing but where the camera is - no
    // rotation, no lens change, and both in the same scene. It is a scene entity
    // like any other, and the renderer draws through whichever one the viewport
    // holds.
    if (Perf::TwoCameras() && camera != nullptr && camera->m_node != nullptr)
    {
      const Mat4 worldA = camera->m_node->GetTransform(TransformationSpace::TS_WORLD);
      const Vec3 posA   = camera->m_node->GetTranslation(TransformationSpace::TS_WORLD);

      // Local X of A, in world space. A uniform scale of 1 leaves this a unit
      // vector, but the normalize is what makes the offset two world units
      // whatever the scene did to the node.
      const Vec3 localX = glm::normalize(Vec3(worldA[0]));

      CameraPtr camB    = MakeNewPtr<Camera>();
      camB->SetNameVal("CameraB");
      camB->NativeConstruct();
      camB->SetLens(camera->Fov(), camera->Aspect(), camera->Near(), camera->Far());
      camB->m_node->SetTransform(worldA, TransformationSpace::TS_WORLD);
      camB->m_node->SetTranslation(posA + localX * TwoCameraOffset, TransformationSpace::TS_WORLD);

      m_cameraA = camera;
      m_cameraB = camB;

      if (scene != nullptr)
      {
        scene->AddEntity(camB);
      }

      TK_LOG("TWOCAM setup A=%s at %.3f,%.3f,%.3f B=%s at %.3f,%.3f,%.3f "
             "offset=%.3f along %.3f,%.3f,%.3f",
             camera->GetNameVal().c_str(),
             posA.x,
             posA.y,
             posA.z,
             camB->GetNameVal().c_str(),
             posA.x + localX.x * TwoCameraOffset,
             posA.y + localX.y * TwoCameraOffset,
             posA.z + localX.z * TwoCameraOffset,
             TwoCameraOffset,
             localX.x,
             localX.y,
             localX.z);
    }
#endif
  }

  void Game::Destroy()
  {
    Perf::Shutdown();

    // The navigator holds the camera, so it goes while the engine is still alive.
    m_navigation.Release();

    // Camera B belongs to the scene, which outlives this game, so the game holds
    // only a reference. Dropping both here keeps the game from reaching into a
    // scene that is already gone.
    m_cameraA = nullptr;
    m_cameraB = nullptr;
  }

  void Game::Frame(float deltaTime)
  {
    // Input upkeep per frame. The navigator is inert until Init() binds a camera,
    // so PIE pays nothing for it.
#ifdef TK_GAME_STANDALONE
    // A finger on the screen drags the camera, which makes every frame a
    // different view. Measurements freeze it so the only thing that can change
    // the picture is the render path.
    if (Perf::NavigationEnabled() && !Perf::TwoCameras())
    {
      m_navigation.Update(deltaTime);
    }

    // Per frame camera trace, for chasing a view that changes on its own.
    if (Perf::FrameTrace())
    {
      static unsigned s_traceFrame = 0;
      ++s_traceFrame;
      if (CameraPtr camera = m_viewport != nullptr ? m_viewport->GetCamera() : nullptr)
      {
        const Vec3 p = camera->m_node->GetTranslation(TransformationSpace::TS_WORLD);
        Vec3 d(0.0f, 0.0f, -1.0f);
        if (DirectionComponentPtr direction = camera->GetComponent<DirectionComponent>())
        {
          d = direction->GetDirection();
        }
        int fingers = 0;
        for (int i = 0; i < SDL_GetNumTouchDevices(); ++i)
        {
          fingers += SDL_GetNumTouchFingers(SDL_GetTouchDevice(i));
        }

        int mx               = 0;
        int my               = 0;
        const Uint32 buttons = SDL_GetMouseState(&mx, &my);
        TK_LOG("TRACE %u cam=%.4f,%.4f,%.4f dir=%.4f,%.4f,%.4f fingers=%i "
               "buttons=%u mouse=%i,%i",
               s_traceFrame,
               p.x,
               p.y,
               p.z,
               d.x,
               d.y,
               d.z,
               fingers,
               (unsigned) buttons,
               mx,
               my);
      }
    }

    // A frame for the two camera test: the first frames the game renders draw
    // through camera A and every frame after the switch frame through camera B,
    // with nothing else in the scene or the input moving. The picture therefore
    // has exactly one change in it, at the switch, and every frame after that has
    // to repeat the previous one until the app is closed.
    //
    // This runs before the frame is rendered, because the render task the host
    // queues reads the viewport's camera when it executes, further down this same
    // frame.
    if (Perf::TwoCameras() && m_cameraA != nullptr && m_cameraB != nullptr && m_viewport != nullptr &&
        !Perf::TwoCameraKeepsA())
    {
      const uint frame = m_twoCameraFrame++;
      CameraPtr use    = frame < Perf::TwoCameraSwitchFrame() ? m_cameraA : m_cameraB;

      if (m_viewport->GetCamera() != use)
      {
        m_viewport->SetCamera(use);
        TK_LOG("TWOCAM frame=%u viewport draws through %s", frame, use == m_cameraA ? "A" : "B");
      }
    }

    // A frame at pose A, the next at pose B, so two frames that differ is the
    // only correct output.
    if (Perf::AlternatePoses() && m_alternateValid && m_viewport != nullptr)
    {
      if (CameraPtr camera = m_viewport->GetCamera())
      {
        if (DirectionComponentPtr direction = camera->GetComponent<DirectionComponent>())
        {
          m_alternateA = !m_alternateA;
          direction->LookAt(m_poseAEye, m_alternateA ? m_poseATarget : m_poseBTarget);
        }
      }
    }

    // Alternates the camera along its own X axis, one sign per frame, so the only
    // thing that changes between two frames is where the camera stands. A render
    // path that presents a frame late or twice shows the same position twice in a
    // row, which is visible as a stutter in this motion.
    if (const int amplitude = Perf::OscillationAmplitude(); amplitude > 0 && m_oscOriginValid)
    {
      CameraPtr camera = m_viewport != nullptr ? m_viewport->GetCamera() : nullptr;
      if (camera != nullptr && camera->m_node != nullptr)
      {
        Vec3 right = Vec3(1.0f, 0.0f, 0.0f);
        if (DirectionComponentPtr direction = camera->GetComponent<DirectionComponent>())
        {
          const Vec3 forward = direction->GetDirection();
          const Vec3 side    = glm::cross(forward, Vec3(0.0f, 1.0f, 0.0f));
          if (glm::length(side) > 0.0001f)
          {
            right = glm::normalize(side);
          }
        }

        m_oscSign = -m_oscSign;
        camera->m_node->SetTranslation(m_oscOrigin + right * (float) (amplitude * m_oscSign),
                                       TransformationSpace::TS_WORLD);
      }
    }
#endif
  }

  void Game::OnLoad(XmlDocumentPtr state) {}

  void Game::OnUnload(XmlDocumentPtr state) {}

  void Game::OnPlay() {}

  void Game::OnPause() {}

  void Game::OnResume() {}

  void Game::OnStop() {}

} // namespace ToolKit
