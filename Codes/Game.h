/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0
 * (LGPL-3.0). For more information, including options for a more permissive
 * commercial license, please visit [otsoftware.tr] or contact us at
 * [info@otsoftare.tr].
 */

#pragma once

#include "FpsNavigation.h"

#include <Plugin.h>
#include <ToolKit.h>

namespace ToolKit
{

  class Game : public GamePlugin
  {
   public:
    void Init(Main* master) override;
    void Destroy() override;
    void Frame(float deltaTime) override;
    void OnLoad(XmlDocumentPtr state) override;
    void OnUnload(XmlDocumentPtr state) override;
    void OnPlay() override;
    void OnPause() override;
    void OnResume() override;
    void OnStop() override;

   private:
    /**
     * Free camera for the standalone and web hosts. Under PIE the editor owns the
     * viewport camera, so the navigator is only driven when the game hosts the
     * engine itself (TK_GAME_STANDALONE).
     */
    FpsNavigation m_navigation;

    /** Base position the oscillation moves around, captured once the camera is
     * placed. */
    Vec3 m_oscOrigin                       = Vec3(0.0f);
    bool m_oscOriginValid                  = false;
    int m_oscSign                          = 1;

    /** Pose A and pose B the camera alternates between when the ab knob is set.
     */
    Vec3 m_poseAEye                        = Vec3(0.0f);
    Vec3 m_poseATarget                     = Vec3(0.0f);
    Vec3 m_poseBTarget                     = Vec3(0.0f);
    bool m_alternateValid                  = false;
    bool m_alternateA                      = true;

    /**
     * The two views the two camera presentation test draws through, see
     * Perf::TwoCameras(). Both are scene entities: A is the camera the scene
     * ships with, B stands TwoCameraOffset world units along A's local X axis and
     * is otherwise its copy.
     */
    CameraPtr m_cameraA                    = nullptr;
    CameraPtr m_cameraB                    = nullptr;

    /** Frames the two camera test has drawn, so the first one can be told apart
     * from the rest. */
    uint m_twoCameraFrame                  = 0;

    /** World units along A's local X axis that camera B stands away from it. */
    static constexpr float TwoCameraOffset = 2.0f;
  };

} // namespace ToolKit

extern "C" TK_PLUGIN_API ToolKit::Game* TK_STDCAL GetInstance();
