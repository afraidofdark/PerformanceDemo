/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Game.h"

#include <Scene.h>

#include "PerfProbe.h"
ToolKit::Game Self;

extern "C" TK_PLUGIN_API ToolKit::Game* TK_STDCAL GetInstance() { return &Self; }

namespace ToolKit
{
  namespace
  {
    /** The scene a standalone or web build opens; under PIE the editor brings its own. */
    constexpr const char* StartupScene = "small-scene.scene";

    /**
     * World bounds of the scene. The aabb tree is filled as entities are added, so a scene that was
     * not built through AddEntity has no root bound and the entities are walked instead.
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

    /** Returns the first camera the scene ships with, or null when it has none. */
    CameraPtr SceneCamera(const ScenePtr& scene)
    {
      if (scene == nullptr)
      {
        return nullptr;
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
    // Loading the game scene belongs here, not in the host: the standalone and web builds have no
    // editor to open one, while PIE already runs on the scene the editor has open.
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

    // Free camera: WASD / PageUp / PageDown move, left drag looks, touch drags and pinches. The
    // viewport camera is the one the game render path draws with.
    if (m_viewport == nullptr)
    {
      return;
    }

    // Use the camera the scene ships with rather than the viewport's own: that position is authored
    // content, and the viewport only holds the pointer, so the game renders through the scene camera
    // and the navigator drives it from where the scene put it.
    ScenePtr scene   = sceneMan->GetCurrentScene();
    CameraPtr camera = SceneCamera(scene);
    if (camera != nullptr)
    {
      m_viewport->SetCamera(camera);
    }

    m_navigation.Init(m_viewport->GetCamera());

    // Without a scene camera the viewport sits at the origin looking down -Z, so frame what the
    // scene contains. A camera that came from the scene is left exactly where it is.
    if (camera == nullptr && scene != nullptr)
    {
      m_navigation.FrameBounds(SceneBounds(scene.get()));
    }

    // Device experiments: the probe reads its knobs from system properties and reports the frame's
    // spans to the log. It runs after the scene is up because half of the knobs are scene settings.
    Perf::Configure(scene.get(), m_viewport.get());
#endif
  }

  void Game::Destroy()
  {
    Perf::Shutdown();

    // The navigator holds the camera, so it goes while the engine is still alive.
    m_navigation.Release();
  }

  void Game::Frame(float deltaTime)
  {
    // Input upkeep per frame. The navigator is inert until Init() binds a camera, so PIE pays
    // nothing for it.
#ifdef TK_GAME_STANDALONE
    m_navigation.Update(deltaTime);
#endif
  }

  void Game::OnLoad(XmlDocumentPtr state) {}

  void Game::OnUnload(XmlDocumentPtr state) {}

  void Game::OnPlay() {}

  void Game::OnPause() {}

  void Game::OnResume() {}

  void Game::OnStop() {}

} // namespace ToolKit
