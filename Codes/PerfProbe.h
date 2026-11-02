/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0
 * (LGPL-3.0). For more information, including options for a more permissive
 * commercial license, please visit [otsoftware.tr] or contact us at
 * [info@otsoftare.tr].
 */

#pragma once

/**
 * @file PerfProbe.h Frame timing for a device run of the game build.
 *
 * Stats::GetRenderTime() reports one render span, which cannot answer the
 * question a slow phone asks. This probe splits a frame into the three spans
 * that any platform can measure without a driver feature:
 *
 *   work   - wall clock spent inside one frame body, i.e. the cpu time the
 * frame needs to build and submit all of its gpu commands. swap   - wall clock
 * the present call blocks, i.e. the cpu waiting for a buffer the gpu is still
 *            reading from (or for the display to take one).
 *   period - wall clock from one frame body to the next, i.e. what the frame
 * rate actually is.
 *
 * work close to period means the cpu is the limit. work far below period, with
 * swap taking up the difference, means the gpu is. The knobs below then
 * attribute a gpu bound frame: each one turns a single feature off, and the per
 * pass snapshot says where the cpu time (and the stalls) land.
 *
 * The same spans are also reported by the engine once
 * GL_EXT_disjoint_timer_query is available, as Stats::GetRenderTime(cpu, gpu).
 * Both are logged here so a device that answers and a device that does not can
 * be read the same way; gpu reads 1.00 when the driver has no timer query.
 *
 * The knobs are system properties, so an experiment needs no rebuild. They are
 * read once, when the scene comes up; restart the app to apply a new set:
 *
 *   adb shell setprop debug.tk.res 50      render resolution percentage, 10 to 200
 *   adb shell setprop debug.tk.msaa 0      0 disables msaa, 2, 4 and 8 select a sample count
 *   adb shell setprop debug.tk.hdr 0       0 renders into rgba8 instead of rgba16f
 *   adb shell setprop debug.tk.ssao 0      0 disables screen space ambient occlusion
 *   adb shell setprop debug.tk.bloom 0     0 disables bloom, otherwise the iteration count
 *   adb shell setprop debug.tk.fxaa 1      trade msaa for the post pass antialiasing instead
 *   adb shell setprop debug.tk.stableshadow 1  sphere cascade fit, snapped to shadow texels
 *   adb shell setprop debug.tk.tm 0        tonemapping
 *   adb shell setprop debug.tk.cull 0      depth grid occlusion culling
 *   adb shell setprop debug.tk.batch 0     instanced batching
 *   adb shell setprop debug.tk.perf 0      silence the frame log
 *   adb shell setprop debug.tk.prof 0      silence the per pass breakdown
 *   adb shell setprop debug.tk.vsync 0     0 unpaced (a performance number), 1 paced (a picture)
 *   adb shell setprop debug.tk.twocam 1    the two camera presentation test, see TwoCameras()
 *   adb shell setprop debug.tk.twocamframe 60  frames of view A before the test switches to B
 *
 * To clear a knob again: adb shell setprop debug.tk.res 100, or `-1` for every knob, which reads as
 * "not set" everywhere.
 */

#include "Camera.h"
#include "DirectionComponent.h"
#include "EngineSettings.h"
#include "ForwardSceneRenderPath.h"
#include "Framebuffer.h"
#include "GameRenderer.h"
#include "Image.h"
#include "Light.h"
#include "MeshComponent.h"
#include "RenderSystem.h"
#include "Scene.h"
#include "SsaoPass.h"
#include "Stats.h"
#include "Viewport.h"

#include <ToolKit.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(TK_ANDROID)
  #include <sys/system_properties.h>
#endif

namespace ToolKit
{
  namespace Perf
  {
    /** One profiler scope, accumulated between two log windows. */
    struct ProfilerEntry
    {
      double m_inclusiveMs = 0.0;
      double m_exclusiveMs = 0.0;
      uint m_depth         = 0;
    };

    /** The process's own memory figures, read from /proc/self. */
    struct SelfMemory
    {
      uint64 m_rssBytes    = 0;
      uint64 m_virtualBytes = 0;
      uint64 m_minorFaults = 0;
      uint64 m_majorFaults = 0;
    };

    /**
     * Reads resident set size and the fault counters from the process's own /proc entries.
     *
     * The phone mounts /proc with hidepid=2, so nothing outside the process can read these - which
     * is why Streamline's Linux_proc_statm_* counters come back empty on this device unless gatord
     * runs as root. The app reading /proc/self is always allowed, and the two numbers that matter
     * for a frame budget are here: how much is resident, and how many pages are being touched.
     */
    inline SelfMemory ReadSelfMemory()
    {
      SelfMemory mem;

#if defined(TK_ANDROID) || defined(TK_LINUX)
      if (FILE* statm = fopen("/proc/self/statm", "r"))
      {
        unsigned long long size = 0, resident = 0;
        if (fscanf(statm, "%llu %llu", &size, &resident) == 2)
        {
          mem.m_virtualBytes = size * 4096ull;
          mem.m_rssBytes     = resident * 4096ull;
        }
        fclose(statm);
      }

      if (FILE* stat = fopen("/proc/self/stat", "r"))
      {
        char line[512] = {};
        if (fgets(line, sizeof(line), stat) != nullptr)
        {
          // The command name is in parentheses and may contain spaces, so the fields start
          // after the last ')'.
          if (char* tail = strrchr(line, ')'))
          {
            unsigned long long ignored = 0;
            // After the ')': state, ppid, pgrp, session, tty_nr, tpgid, flags, minflt, cminflt,
            // majflt, cmajflt. minflt is the eighth field, majflt the tenth, and everything that is
            // not one of those two is suppressed - a format with more conversions than arguments
            // makes sscanf read past the argument list.
            const int matched = sscanf(tail + 1,
                                       " %*c %*u %*u %*u %*u %*u %*u %llu %*u %llu",
                                       &mem.m_minorFaults,
                                       &mem.m_majorFaults);
            (void) matched;
          }
        }
        fclose(stat);
      }
#endif

      return mem;
    }

    /** Everything the probe holds. Plain values only: no engine object survives a
     * frame in here. */
    struct ProbeState
    {
      bool m_enabled       = false;
      bool m_profiler      = false;

      uint m_warmupFrames  = 20;
      uint m_frameCount    = 0;
      double m_windowStart = 0.0;
      double m_periodSum   = 0.0;
      double m_periodMax   = 0.0;
      double m_workSum     = 0.0;
      double m_swapSum     = 0.0;
      double m_renderSum   = 0.0;

      float m_renderCpuMs  = 0.0f;
      float m_swapMs       = 0.0f;

      // Applied configuration, so a log line says what produced the numbers.
      int m_resPercent     = 100;
      int m_msaa           = -1;
      int m_hdr            = -1;
      int m_ssao           = -1;
      int m_bloom          = -1;
      int m_fxaa           = -1;
      int m_stableShadows  = -1;
      int m_tonemap        = -1;
      int m_cull           = -1;
      int m_batch          = -1;
      int m_shadow         = -1;
      int m_mesh           = -1;
      int m_pose           = 0;
      int m_oscillation    = 0;
      int m_alternatePoses = 0;
      CameraPtr m_camera   = nullptr;
      int m_trace          = 0;
      int m_navigation     = 1;
      int m_twoCameras     = 0;
      int m_twoCamerasAt   = 1;

      /** Remaining frames of ambient occlusion map to write, see DumpSsaoMaps. */
      int m_ssaoDump       = 0;

      uint m_renderWidth   = 0;
      uint m_renderHeight  = 0;

      std::unordered_map<String, ProfilerEntry> m_profilerPrev;
    };

    inline ProbeState& State()
    {
      static ProbeState state;
      return state;
    }

    /** Milliseconds from an arbitrary origin, monotonic. */
    inline double NowMs()
    {
      using Clock = std::chrono::steady_clock;
      return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
    }

    /** Reads an integer system property, or the fallback off Android. */
    inline int ReadIntProperty(const char* name, int fallback)
    {
#if defined(TK_ANDROID)
      char buffer[PROP_VALUE_MAX] = {};
      if (__system_property_get(name, buffer) <= 0 || buffer[0] == '\0')
      {
        return fallback;
      }
      return std::atoi(buffer);
#else
      (void) name;
      return fallback;
#endif
    }

    /** Reads an integer from the environment, for the knobs a desktop run needs to
     * set. */
    inline int ReadEnvInt(const char* name)
    {
      const char* value = std::getenv(name);
      return value != nullptr ? std::atoi(value) : 0;
    }

    /**
     * Reads one render target back and writes it as a single channel PNG into the app's data
     * folder, next to the pak. A phone has no frame debugger attached by default, so a file is the
     * only way to look at what a pass actually produced.
     */
    inline void DumpRenderTarget(Renderer* renderer, const RenderTargetPtr& target, const String& name)
    {
      if (renderer == nullptr || target == nullptr || target->m_width <= 0 || target->m_height <= 0)
      {
        return;
      }

      const int width  = target->m_width;
      const int height = target->m_height;

      FramebufferSettings settings;
      settings.width           = width;
      settings.height          = height;
      settings.useDefaultDepth = false;

      FramebufferPtr readBuffer = MakeNewPtr<Framebuffer>(settings, "DumpReadFB");
      readBuffer->Init();
      readBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, target);

      FramebufferPtr previous = renderer->GetFrameBuffer();
      renderer->SetFramebuffer(readBuffer, GraphicBitFields::None);

      std::vector<ubyte> pixels(size_t(width) * size_t(height), 0);
      renderer->GetBackend()->ReadPixels(0,
                                         0,
                                         width,
                                         height,
                                         GraphicTypes::FormatRed,
                                         GraphicTypes::TypeUnsignedByte,
                                         pixels.data());

      renderer->SetFramebuffer(previous, GraphicBitFields::None);

      // GL hands the rows back bottom up, a PNG wants the top row first.
      const size_t rowBytes = size_t(width);
      for (int y = 0; y < height / 2; y++)
      {
        ubyte* top    = pixels.data() + size_t(y) * rowBytes;
        ubyte* bottom = pixels.data() + size_t(height - 1 - y) * rowBytes;
        for (size_t i = 0; i < rowBytes; i++)
        {
          std::swap(top[i], bottom[i]);
        }
      }

      const String file = ConcatPaths({ResourcePath(), "..", name});
      if (WritePNG(file, width, height, 1, pixels.data(), 0) == 0)
      {
        TK_ERR("SSAO dump: could not write %s", file.c_str());
        return;
      }

      TK_LOG("SSAO dump: wrote %s (%ix%i)", file.c_str(), width, height);
    }

    /**
     * Reads one render target back as RGBA and writes it as a 4 channel PNG. For inputs that pack
     * several signals per pixel - the ambient occlusion pass reads normal.xy and linear depth out of
     * one RGBA16F target - so each channel can be looked at on its own.
     */
    inline void DumpTextureRgba(Renderer* renderer, const TexturePtr& target, const String& name)
    {
      if (renderer == nullptr || target == nullptr || target->m_width <= 0 || target->m_height <= 0)
      {
        return;
      }

      const int width  = target->m_width;
      const int height = target->m_height;

      FramebufferSettings settings;
      settings.width           = width;
      settings.height          = height;
      settings.useDefaultDepth = false;

      RenderTargetPtr renderTarget = Cast<RenderTarget>(target);
      if (renderTarget == nullptr)
      {
        return;
      }

      FramebufferPtr readBuffer = MakeNewPtr<Framebuffer>(settings, "DumpReadFB");
      readBuffer->Init();
      readBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, renderTarget);

      FramebufferPtr previous = renderer->GetFrameBuffer();
      renderer->SetFramebuffer(readBuffer, GraphicBitFields::None);

      const int channels        = 4;
      std::vector<float> values(size_t(width) * size_t(height) * channels, 0.0f);
      renderer->GetBackend()->ReadPixels(0,
                                         0,
                                         width,
                                         height,
                                         GraphicTypes::FormatRGBA,
                                         GraphicTypes::TypeFloat,
                                         values.data());

      renderer->SetFramebuffer(previous, GraphicBitFields::None);

      // The target is float, so the channels are reported as numbers first: a normal that packs to
      // 0..1 and a depth in the scene's own units do not look the same, and the ranges say at once
      // which one came back empty.
      float minimum[channels] = {1e30f, 1e30f, 1e30f, 1e30f};
      float maximum[channels] = {-1e30f, -1e30f, -1e30f, -1e30f};
      for (size_t i = 0; i < values.size(); i += channels)
      {
        for (int c = 0; c < channels; c++)
        {
          minimum[c] = std::min(minimum[c], values[i + c]);
          maximum[c] = std::max(maximum[c], values[i + c]);
        }
      }

      TK_LOG("SSAO dump: %s ranges r[%.3f %.3f] g[%.3f %.3f] b[%.3f %.3f] a[%.3f %.3f]",
             name.c_str(),
             minimum[0],
             maximum[0],
             minimum[1],
             maximum[1],
             minimum[2],
             maximum[2],
             minimum[3],
             maximum[3]);

      // R and G are the packed normal, B a linear depth in the scene's own units: the depth is
      // scaled to a hundred units so the gradient is visible instead of saturated.
      std::vector<ubyte> pixels(size_t(width) * size_t(height) * channels, 0);
      for (size_t i = 0, p = 0; i < values.size(); i += channels, p += channels)
      {
        pixels[p + 0] = (ubyte) (std::min(std::max(values[i + 0], 0.0f), 1.0f) * 255.0f);
        pixels[p + 1] = (ubyte) (std::min(std::max(values[i + 1], 0.0f), 1.0f) * 255.0f);
        pixels[p + 2] = (ubyte) (std::min(std::max(values[i + 2] / 100.0f, 0.0f), 1.0f) * 255.0f);
        pixels[p + 3] = 255;
      }

      const size_t rowBytes = size_t(width) * channels;
      for (int y = 0; y < height / 2; y++)
      {
        ubyte* top    = pixels.data() + size_t(y) * rowBytes;
        ubyte* bottom = pixels.data() + size_t(height - 1 - y) * rowBytes;
        for (size_t i = 0; i < rowBytes; i++)
        {
          std::swap(top[i], bottom[i]);
        }
      }

      const String file = ConcatPaths({ResourcePath(), "..", name});
      if (WritePNG(file, width, height, channels, pixels.data(), 0) == 0)
      {
        TK_ERR("SSAO dump: could not write %s", file.c_str());
        return;
      }

      TK_LOG("SSAO dump: wrote %s (%ix%i)", file.c_str(), width, height);
    }

    /**
     * Writes the ambient occlusion maps the pass produced.
     *
     * The occlusion is multiplied into the lighting before anything reaches the screen, so a wrong
     * map only shows up as "the scene got darker". This pulls both targets out - the raw pass output
     * and the blurred one the forward pass samples - so the map itself can be looked at.
     */
    inline void DumpSsaoMaps(Renderer* renderer, GameRenderer* gameRenderer)
    {
      if (renderer == nullptr || gameRenderer == nullptr)
      {
        return;
      }

      auto path = std::static_pointer_cast<ForwardSceneRenderPath>(gameRenderer->GetSceneRenderPath());
      if (path == nullptr || path->m_ssaoPass == nullptr)
      {
        TK_LOG("SSAO dump: the scene path has no ssao pass.");
        return;
      }

      DumpRenderTarget(renderer, path->m_ssaoPass->GetRawSsaoTarget(), "ssao_raw.png");
      DumpRenderTarget(renderer, path->m_ssaoPass->m_ssaoTexture, "ssao_blur.png");

      // The pass's input, so a map that shows nothing can be told apart from an input that carries
      // nothing: R and G are the packed normal, B the linear depth.
      TexturePtr normalDepth = path->m_ssaoPass->m_params.GNormalDepthBuffer;
      if (normalDepth != nullptr && normalDepth->IsMultiSampled())
      {
        normalDepth = normalDepth->GetResolvedTexture();
      }

      if (normalDepth != nullptr)
      {
        DumpTextureRgba(renderer, normalDepth, "ssao_normaldepth.png");
      }
    }

    /** Accumulates every scope of one frame's profiler tree into name keyed
     * entries. */
    inline void FlattenProfilerNode(ProfilerNode* node, std::unordered_map<String, ProfilerEntry>& out, uint depth)
    {
      if (node == nullptr)
      {
        return;
      }

      ProfilerEntry& entry  = out[node->name];
      entry.m_inclusiveMs  += (double) node->accumulatedIncl;
      entry.m_exclusiveMs  += (double) node->accumulatedExcl;
      if (depth > entry.m_depth)
      {
        entry.m_depth = depth;
      }

      for (ProfilerNode* child : node->children)
      {
        FlattenProfilerNode(child, out, depth + 1);
      }
    }

    /**
     * Moves the viewport camera to one of the fixed poses, relative to where the
     * scene put it.
     *
     * A swipe driven pan is not reproducible: the same drag lands the camera
     * somewhere else on the next run, and what is inside the frustum decides most
     * of the frame. A pose is a fixed rotation of the authored camera, so two runs
     * - and two builds - frame the same geometry. Pose 0, the default, leaves the
     * scene's own camera untouched.
     *
     * @param pose is 0 for the authored camera, 1..5 for the fixed rotations below.
     * @param viewport is the game viewport whose camera is moved.
     * @return The pose that was actually applied.
     */
    inline int ApplyPose(int pose, Viewport* viewport)
    {
      if (pose <= 0 || viewport == nullptr)
      {
        return 0;
      }

      CameraPtr camera = viewport->GetCamera();
      if (camera == nullptr)
      {
        return 0;
      }

      DirectionComponentPtr direction = camera->GetComponent<DirectionComponent>();
      if (direction == nullptr)
      {
        return 0;
      }

      switch (pose)
      {
        case 1: // Turn toward the building on the left.
          direction->RotateOnUpVector(glm::radians(40.0f));
          break;
        case 2: // Turn toward the corner on the right.
          direction->RotateOnUpVector(glm::radians(-40.0f));
          break;
        case 3: // Down the street, the longest view of the block.
          direction->RotateOnUpVector(glm::radians(-85.0f));
          break;
        case 4: // Tilt down so the ground plane and the shadow it catches fill the
                // frame.
          direction->Pitch(glm::radians(-18.0f));
          break;
        case 5: // Tilt up at the facade, which leaves mostly sky to shade.
          direction->Pitch(glm::radians(22.0f));
          break;
        default:
          return 0;
      }

      return pose;
    }

    /**
     * Amplitude, in world units, of the per frame camera oscillation that
     * Game::Frame applies along the camera's own X axis. Zero means the camera
     * stays where the pose put it.
     *
     * The scene is otherwise static, so with this on every change on screen is a
     * change the render path made: a frame that is presented late, or twice, shows
     * up as the camera standing still for a frame instead of alternating.
     */
    inline int OscillationAmplitude() { return State().m_oscillation; }

    /**
     * When set, Game::Frame aims the camera at pose A on one frame and pose B on
     * the next, so the frame content alternates between two views that are easy to
     * tell apart. A presentation path that shows the previous frame next to the
     * current one has nowhere to hide in that pattern.
     */
    inline bool AlternatePoses()
    {
      // Every knob uses -1 for "not set", so this has to test for enabled and not
      // for non zero:
      // `-1 != 0` is true and turned the alternation on whenever the knobs were
      // reset.
      return State().m_alternatePoses > 0;
    }

    /**
     * When set, the first frame the game renders draws through the scene's camera
     * and every frame after it through a second camera standing two units along the
     * first one's local X axis.
     *
     * This is not a camera motion test, it is a buffer test. The scene and both
     * cameras are still, so after the first frame the picture has to repeat
     * exactly: one view of a motionless scene. A present path that alternates
     * between two buffers - one holding the frame it just drew, the other holding
     * an older one - turns that into two views flipping back and forth, and the
     * flip is the only thing that moves on screen.
     *
     * The pattern is measured off the display, not from inside the app: the frames
     * are recorded with `adb shell screenrecord` and every recorded frame is
     * matched against a reference still of view A and of view B. Reading the window
     * back in the app is not an option on this device - a `glReadPixels` between
     * the frame and `SDL_GL_SwapWindow` left the window black for the whole run, on
     * the SM-G950F's Mali driver, with no GL error reported, whether it read the
     * default framebuffer or the view's own render target. Flushing or finishing
     * before the swap breaks it the same way, which says the driver hands the frame
     * over asynchronously and app side sync is not the answer.
     *
     * Mode 3 is the control: camera B is set up but the viewport keeps drawing
     * through A, so a difference between 1 and 3 is the switch and not the second
     * camera's presence.
     */
    inline bool TwoCameras() { return State().m_twoCameras > 0; }

    /** The mode value itself, so a log line can say what the test is set to. */
    inline int TwoCameraMode() { return State().m_twoCameras; }

    /** True when the test sets camera B up but keeps drawing through A, the control
     * case. */
    inline bool TwoCameraKeepsA() { return State().m_twoCameras == 3; }

    /**
     * The frame the two camera test switches from A to B on.
     *
     * A switch on the very first frame measures the app's startup instead: the
     * window stays black until the first frame is complete, and a first frame that
     * spends two seconds compiling shaders is two seconds of black whatever the
     * present path does. Switching later puts both views on a display that is
     * already showing the scene, so what gets recorded is the present path and
     * nothing else.
     */
    inline uint TwoCameraSwitchFrame() { return State().m_twoCamerasAt > 0 ? (uint) State().m_twoCamerasAt : 1u; }

    /**
     * When set, Game::Frame logs the camera every frame, so a view that changes
     * between frames can be attributed to the frame it changed on.
     */
    inline bool FrameTrace() { return State().m_trace > 0; }

    /** False when the navigator is frozen, see the nav knob. */
    inline bool NavigationEnabled() { return State().m_navigation != 0; }

    /**
     * Applies the knobs and describes what it applied.
     * @param scene is the live scene, which owns the post process settings the
     * knobs edit.
     * @param viewport is the game viewport, rebuilt when a knob changes the render
     * target format.
     */
    inline void Configure(Scene* scene, Viewport* viewport)
    {
      ProbeState& s = State();

#if defined(TK_ANDROID)
      constexpr int defaultEnabled = 1;
#else
      constexpr int defaultEnabled = 0;
#endif

      s.m_enabled               = ReadIntProperty("debug.tk.perf", defaultEnabled) != 0;
      s.m_profiler              = ReadIntProperty("debug.tk.prof", defaultEnabled) != 0;

      GraphicSettings* graphics = GetEngineSettings().m_graphics.get();

      // The knob has to be able to say "100" as well: the shipped settings render at 65 % now, so a
      // value of 100 is a real override and not "leave the project default alone". Not set reads as
      // -1, like every other knob.
      s.m_resPercent = ReadIntProperty("debug.tk.res", -1);
      if (viewport != nullptr && s.m_resPercent > 0)
      {
        graphics->SetRenderResolutionScaleVal(s.m_resPercent / 100.0f);
        viewport->ReInitViewport();
      }

      s.m_msaa = ReadIntProperty("debug.tk.msaa", -1);
      if (viewport != nullptr && s.m_msaa >= 0)
      {
        MultiChoiceVariant msaa = graphics->GetMSAAVal();
        msaa.SetEnum((MsaaSampleCount) s.m_msaa);
        graphics->SetMSAAVal(msaa);
        viewport->ReInitViewport();
      }

      s.m_hdr = ReadIntProperty("debug.tk.hdr", -1);
      if (viewport != nullptr && s.m_hdr >= 0)
      {
        graphics->SetHDRPipelineVal(s.m_hdr != 0);
        viewport->ReInitViewport();
      }

      s.m_cull = ReadIntProperty("debug.tk.cull", -1);
      if (s.m_cull >= 0)
      {
        graphics->SetOcclusionCullingVal(s.m_cull != 0);
      }

      s.m_batch = ReadIntProperty("debug.tk.batch", -1);
      if (s.m_batch >= 0)
      {
        graphics->SetInstancedBatchingVal(s.m_batch != 0);
      }

      // Two knobs that take work away instead of tuning it, to price the two
      // biggest suspects. Shadows: a light that does not cast is dropped from the
      // shadow pass entirely, so the difference against a normal run is what the
      // shadow maps cost. Meshes: hides every mesh entity, which leaves the sky and
      // the post chain; the difference is what drawing the scene costs.
      s.m_shadow = ReadIntProperty("debug.tk.shadow", -1);
      s.m_mesh   = ReadIntProperty("debug.tk.mesh", -1);

      // The shadow master switch. Unlike debug.tk.shadow, which clears the per light flags, this is
      // the engine setting: the pass renders nothing and the shader is rebuilt without the sampling.
      const int castShadow = ReadIntProperty("debug.tk.castshadow", -1);
      if (castShadow >= 0)
      {
        GetEngineSettings().m_graphics->m_shadows->SetCastShadowVal(castShadow != 0);
      }

      if (scene != nullptr && (s.m_shadow == 0 || s.m_mesh == 0))
      {
        for (const EntityPtr& entity : scene->GetEntities())
        {
          if (entity == nullptr)
          {
            continue;
          }

          if (s.m_shadow == 0 && entity->IsA<Light>())
          {
            std::static_pointer_cast<Light>(entity)->SetCastShadowVal(false);
          }

          if (s.m_mesh == 0 && entity->GetComponentFast<MeshComponent>() != nullptr)
          {
            entity->SetVisibility(false, true);
          }
        }
      }

      if (PostProcessingSettingsPtr pps = scene ? scene->m_postProcessSettings : nullptr)
      {
        s.m_ssao = ReadIntProperty("debug.tk.ssao", -1);
        if (s.m_ssao >= 0)
        {
          pps->SetSSAOEnabledVal(s.m_ssao != 0);
        }

        // SSAO scale knobs, for the case where the map comes out as a flat veil instead of corner
        // darkening. Radius is in the scene's own units; bias is given in thousandths so it stays an
        // integer property.
        const int ssaoRadius = ReadIntProperty("debug.tk.ssaoradius", -1);
        if (ssaoRadius >= 0)
        {
          pps->SetSSAORadiusVal((float) ssaoRadius);
        }

        const int ssaoBias = ReadIntProperty("debug.tk.ssaobias", -1);
        if (ssaoBias >= 0)
        {
          pps->SetSSAOBiasVal((float) ssaoBias / 1000.0f);
        }

        // Anti aliasing is a quality against frame time trade, so both halves of it are knobs: the
        // sample count on the render target (msaa) and the post pass that costs a full screen shader
        // instead (fxaa). Which one a project wants is a look decision that needs the numbers.
        s.m_fxaa = ReadIntProperty("debug.tk.fxaa", -1);
        if (s.m_fxaa >= 0)
        {
          pps->SetFXAAEnabledVal(s.m_fxaa != 0);
        }

        // The stable shadow fit: a sphere around the view frustum, snapped to shadow texels, instead of a
        // tight box that follows the camera. It is what decides whether a moving camera leaves the cascade
        // matrices alone, so it is what decides whether the shadow atlas can be kept between frames.
        s.m_stableShadows = ReadIntProperty("debug.tk.stableshadow", -1);
        if (s.m_stableShadows >= 0)
        {
          GetEngineSettings().m_graphics->m_shadows->SetStableShadowMapVal(s.m_stableShadows != 0);
        }

        s.m_bloom = ReadIntProperty("debug.tk.bloom", -1);
        if (s.m_bloom >= 0)
        {
          pps->SetBloomEnabledVal(s.m_bloom > 0);
          if (s.m_bloom > 0)
          {
            pps->SetBloomIterationCountVal(s.m_bloom);
          }
        }

        s.m_tonemap = ReadIntProperty("debug.tk.tm", -1);
        if (s.m_tonemap >= 0)
        {
          pps->SetTonemappingEnabledVal(s.m_tonemap != 0);
        }
      }

      if (viewport != nullptr && viewport->m_renderTarget != nullptr)
      {
        s.m_renderWidth  = (uint) viewport->m_renderTarget->m_width;
        s.m_renderHeight = (uint) viewport->m_renderTarget->m_height;
      }

      // Fixed camera poses, so a visual or timing comparison runs against the same
      // view. Read once with the other knobs; restart the app to change it.
      s.m_camera         = viewport != nullptr ? viewport->GetCamera() : nullptr;
      s.m_pose           = ApplyPose(ReadIntProperty("debug.tk.pose", 0), viewport);
      s.m_oscillation    = ReadIntProperty("debug.tk.osc", 0);
      s.m_alternatePoses = ReadIntProperty("debug.tk.ab", 0);
      s.m_trace          = ReadIntProperty("debug.tk.trace", 0);
      // 0 freezes the navigator, so a measurement cannot be moved by a finger on
      // the screen.
      s.m_navigation     = ReadIntProperty("debug.tk.nav", 1);
      // The property service is the only way to reach the game on a device; a
      // desktop run sets the same switch in the environment, because
      // ReadIntProperty answers with its fallback there.
      s.m_twoCameras     = ReadIntProperty("debug.tk.twocam", ReadEnvInt("TK_TWOCAM"));
      s.m_twoCamerasAt   = ReadIntProperty("debug.tk.twocamframe", 1);

      // Frames of ambient occlusion map to write out, see DumpSsaoMaps. Two frames so the map has a
      // full frame of history behind it; 0 does nothing.
      s.m_ssaoDump       = ReadIntProperty("debug.tk.ssaodump", 0);

      // What the app ends up rendering with, not what a knob asked for: the project's settings file and
      // the scene both decide, and a run that does not say which msaa or fxaa it actually used is
      // impossible to compare against another.
      int msaaUsed   = (int) GetEngineSettings().m_graphics->GetMSAAVal().GetValue<int>();
      auto scenePps  = scene ? scene->m_postProcessSettings : nullptr;
      int fxaaUsed   = scenePps != nullptr && scenePps->GetFXAAEnabledVal() ? 1 : 0;

      // The scene's own post process block, as opposed to engine defaults: a run whose settings silently
      // came from the defaults is measuring a different scene than the one it names.
      char ppsUsed[96] = "pps=none";
      if (scenePps != nullptr)
      {
        snprintf(ppsUsed,
                 sizeof(ppsUsed),
                 "pps=radius%.2f bloom%i tm%i ssao%i",
                 (double) scenePps->GetSSAORadiusVal(),
                 (int) scenePps->GetBloomIterationCountVal(),
                 scenePps->GetTonemappingEnabledVal() ? 1 : 0,
                 scenePps->GetSSAOEnabledVal() ? 1 : 0);
      }

      const UVec2 window = GetRenderSystem()->GetAppWindowSize();
      TK_LOG("PERF config window=%ux%u render=%ux%u res=%i%% msaa=%i hdr=%i "
             "ssao=%i bloom=%i tm=%i cull=%i batch=%i "
             "shadow=%i mesh=%i pose=%i osc=%i ab=%i fxaa=%i stable=%i msaaUsed=%i fxaaUsed=%i twocam=%i at=%i %s",
             window.x,
             window.y,
             s.m_renderWidth,
             s.m_renderHeight,
             s.m_resPercent,
             s.m_msaa,
             s.m_hdr,
             s.m_ssao,
             s.m_bloom,
             s.m_tonemap,
             s.m_cull,
             s.m_batch,
             s.m_shadow,
             s.m_mesh,
             s.m_pose,
             s.m_oscillation,
             s.m_alternatePoses,
             s.m_fxaa,
             s.m_stableShadows,
             msaaUsed,
             fxaaUsed,
             s.m_twoCameras,
             s.m_twoCamerasAt,
             ppsUsed);

      s.m_windowStart = NowMs();
    }

    /** cpu time the game renderer needs to build and submit one frame. */
    inline void SetRenderCpuMs(float ms) { State().m_renderCpuMs = ms; }

    /** wall clock spent blocked in present. */
    inline void SetSwapWaitMs(float ms) { State().m_swapMs = ms; }

    /** Wall clock the present call blocked, for the caller that measures around it.
     */
    inline float GetSwapWaitMs() { return State().m_swapMs; }

    /**
     * Ends one frame and logs a window's worth of them.
     * @param periodMs is the wall clock between this frame body and the previous
     * one.
     * @param workMs is the wall clock this frame body took, present included.
     */
    inline void Tick(float periodMs, float workMs)
    {
      ProbeState& s = State();
      if (!s.m_enabled)
      {
        return;
      }

      if (s.m_warmupFrames > 0)
      {
        s.m_warmupFrames--;
        s.m_windowStart = NowMs();
        return;
      }

      s.m_frameCount++;
      s.m_periodSum += (double) periodMs;
      s.m_workSum   += (double) workMs;
      s.m_swapSum   += (double) s.m_swapMs;
      s.m_renderSum += (double) s.m_renderCpuMs;
      if ((double) periodMs > s.m_periodMax)
      {
        s.m_periodMax = (double) periodMs;
      }

      const double now     = NowMs();
      const double elapsed = now - s.m_windowStart;
      if (elapsed < 1000.0 || s.m_frameCount == 0)
      {
        return;
      }

      const double frames    = (double) s.m_frameCount;
      const double periodAvg = s.m_periodSum / frames;
      const double workAvg   = s.m_workSum / frames;
      const double swapAvg   = s.m_swapSum / frames;
      const double renderAvg = s.m_renderSum / frames;

      float engineCpu        = 0.0f;
      float engineGpu        = 0.0f;
      Stats::GetRenderTime(engineCpu, engineGpu);

      char camInfo[96] = "cam=none";
      if (s.m_camera != nullptr && s.m_camera->m_node != nullptr)
      {
        const Vec3 p = s.m_camera->m_node->GetTranslation(TransformationSpace::TS_WORLD);
        Vec3 d(0.0f, 0.0f, -1.0f);
        if (DirectionComponentPtr direction = s.m_camera->GetComponent<DirectionComponent>())
        {
          d = direction->GetDirection();
        }
        snprintf(camInfo, sizeof(camInfo), "cam=%.2f,%.2f,%.2f dir=%.3f,%.3f,%.3f", p.x, p.y, p.z, d.x, d.y, d.z);
      }

      const SelfMemory memInfo = ReadSelfMemory();
      TK_LOG("PERF fps=%.2f period=%.2f work=%.2f swap=%.2f render=%.2f "
             "engcpu=%.2f gpu=%.2f max=%.2f "
             "draws=%llu passes=%llu inst=%llu/%llu culled=%llu vram=%lluMB "
             "rss=%lluMB faults=%llu/%llu "
             "res=%ux%u %s",
             1000.0 / periodAvg,
             periodAvg,
             workAvg,
             swapAvg,
             renderAvg,
             (double) engineCpu,
             (double) engineGpu,
             s.m_periodMax,
             (unsigned long long) Stats::GetStatPrev(FrameStatType::DrawCall),
             (unsigned long long) Stats::GetStatPrev(FrameStatType::RenderPass),
             (unsigned long long) Stats::GetStatPrev(FrameStatType::InstancedBatch),
             (unsigned long long) Stats::GetStatPrev(FrameStatType::InstancedObject),
             (unsigned long long) Stats::GetStatPrev(FrameStatType::DepthGridCulledEntity),
             (unsigned long long) Stats::GetVRAMUsage(MemoryUnit::MB),
             (unsigned long long) (memInfo.m_rssBytes / (1024ull * 1024ull)),
             (unsigned long long) memInfo.m_minorFaults,
             (unsigned long long) memInfo.m_majorFaults,
             s.m_renderWidth,
             s.m_renderHeight,
             camInfo);

      // Per pass breakdown: the profiler accumulates since the engine started, so a
      // window's cost is the difference between two snapshots.
      if (s.m_profiler)
      {
        if (TKProfiler* profiler = Profiler::GetProfiler())
        {
          std::unordered_map<String, ProfilerEntry> current;
          for (ProfilerNode* root : profiler->GetRootNodes())
          {
            FlattenProfilerNode(root, current, 0);
          }

          // Sorted by exclusive cost: the wall clock a scope owns once its children
          // are subtracted is what a stall inside the driver actually shows up as,
          // and inclusive time is dominated by whichever scope wraps the frame.
          std::vector<std::pair<String, ProfilerEntry>> sorted(current.begin(), current.end());
          std::sort(sorted.begin(),
                    sorted.end(),
                    [](const std::pair<String, ProfilerEntry>& a, const std::pair<String, ProfilerEntry>& b)
                    { return a.second.m_exclusiveMs > b.second.m_exclusiveMs; });

          for (const auto& [name, entry] : sorted)
          {
            auto prev = s.m_profilerPrev.find(name);
            if (prev == s.m_profilerPrev.end())
            {
              continue;
            }

            const double inclPerFrame = (entry.m_inclusiveMs - prev->second.m_inclusiveMs) / frames;
            const double exclPerFrame = (entry.m_exclusiveMs - prev->second.m_exclusiveMs) / frames;
            if (exclPerFrame < 0.25 && inclPerFrame < 0.25)
            {
              continue;
            }

            TK_LOG("PROF d%u %-34s inc %7.2f exc %7.2f ms/frame",
                   entry.m_depth,
                   name.c_str(),
                   inclPerFrame,
                   exclPerFrame);
          }

          s.m_profilerPrev = std::move(current);
        }
      }

      s.m_frameCount  = 0;
      s.m_periodSum   = 0.0;
      s.m_workSum     = 0.0;
      s.m_swapSum     = 0.0;
      s.m_renderSum   = 0.0;
      s.m_periodMax   = 0.0;
      s.m_windowStart = now;
    }

    /** Drops the probe's state. Called from the game's Destroy, while the engine is
     * still alive. */
    inline void Shutdown()
    {
      ProbeState& s = State();
      s.m_enabled   = false;
      s.m_profiler  = false;
      s.m_profilerPrev.clear();
    }

  } // namespace Perf
} // namespace ToolKit
