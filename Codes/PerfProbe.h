/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file PerfProbe.h Frame timing for a device run of the game build.
 *
 * Stats::GetRenderTime() reports one render span, which cannot answer the question a slow phone asks.
 * This probe splits a frame into the three spans that any platform can measure without a driver
 * feature:
 *
 *   work   - wall clock spent inside one frame body, i.e. the cpu time the frame needs to build and
 *            submit all of its gpu commands.
 *   swap   - wall clock the present call blocks, i.e. the cpu waiting for a buffer the gpu is still
 *            reading from (or for the display to take one).
 *   period - wall clock from one frame body to the next, i.e. what the frame rate actually is.
 *
 * work close to period means the cpu is the limit. work far below period, with swap taking up the
 * difference, means the gpu is. The knobs below then attribute a gpu bound frame: each one turns a
 * single feature off, and the per pass snapshot says where the cpu time (and the stalls) land.
 *
 * The same spans are also reported by the engine once GL_EXT_disjoint_timer_query is available, as
 * Stats::GetRenderTime(cpu, gpu). Both are logged here so a device that answers and a device that
 * does not can be read the same way; gpu reads 1.00 when the driver has no timer query.
 *
 * The knobs are system properties, so an experiment needs no rebuild. They are read once, when the
 * scene comes up; restart the app to apply a new set:
 *
 *   adb shell setprop debug.tk.res 50     render resolution percentage, 10 to 200
 *   adb shell setprop debug.tk.msaa 0    0 disables msaa, 2, 4 and 8 select a sample count
 *   adb shell setprop debug.tk.hdr 0     0 renders into rgba8 instead of rgba16f
 *   adb shell setprop debug.tk.ssao 0    0 disables screen space ambient occlusion
 *   adb shell setprop debug.tk.bloom 0   0 disables bloom, otherwise the iteration count
 *   adb shell setprop debug.tk.tm 0      tonemapping
 *   adb shell setprop debug.tk.cull 0    depth grid occlusion culling
 *   adb shell setprop debug.tk.batch 0   instanced batching
 *   adb shell setprop debug.tk.perf 0    silence the frame log
 *   adb shell setprop debug.tk.prof 0    silence the per pass breakdown
 *
 * To clear a knob again: adb shell setprop debug.tk.res 100 (or '' to unset it).
 */

#include <ToolKit.h>

#include "EngineSettings.h"
#include "Light.h"
#include "MeshComponent.h"
#include "RenderSystem.h"
#include "Scene.h"
#include "Stats.h"
#include "Viewport.h"

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

    /** Everything the probe holds. Plain values only: no engine object survives a frame in here. */
    struct ProbeState
    {
      bool m_enabled          = false;
      bool m_profiler         = false;

      uint m_warmupFrames     = 20;
      uint m_frameCount       = 0;
      double m_windowStart    = 0.0;
      double m_periodSum      = 0.0;
      double m_periodMax      = 0.0;
      double m_workSum        = 0.0;
      double m_swapSum        = 0.0;
      double m_renderSum      = 0.0;

      float m_renderCpuMs     = 0.0f;
      float m_swapMs          = 0.0f;

      // Applied configuration, so a log line says what produced the numbers.
      int m_resPercent        = 100;
      int m_msaa              = -1;
      int m_hdr               = -1;
      int m_ssao              = -1;
      int m_bloom             = -1;
      int m_tonemap           = -1;
      int m_cull              = -1;
      int m_batch             = -1;
      int m_shadow            = -1;
      int m_mesh              = -1;

      uint m_renderWidth      = 0;
      uint m_renderHeight     = 0;

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

    /** Accumulates every scope of one frame's profiler tree into name keyed entries. */
    inline void FlattenProfilerNode(ProfilerNode* node,
                                    std::unordered_map<String, ProfilerEntry>& out,
                                    uint depth)
    {
      if (node == nullptr)
      {
        return;
      }

      ProfilerEntry& entry = out[node->name];
      entry.m_inclusiveMs += (double) node->accumulatedIncl;
      entry.m_exclusiveMs += (double) node->accumulatedExcl;
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
     * Applies the knobs and describes what it applied.
     * @param scene is the live scene, which owns the post process settings the knobs edit.
     * @param viewport is the game viewport, rebuilt when a knob changes the render target format.
     */
    inline void Configure(Scene* scene, Viewport* viewport)
    {
      ProbeState& s = State();

#if defined(TK_ANDROID)
      constexpr int defaultEnabled = 1;
#else
      constexpr int defaultEnabled = 0;
#endif

      s.m_enabled  = ReadIntProperty("debug.tk.perf", defaultEnabled) != 0;
      s.m_profiler = ReadIntProperty("debug.tk.prof", defaultEnabled) != 0;

      GraphicSettings* graphics = GetEngineSettings().m_graphics.get();

      s.m_resPercent = ReadIntProperty("debug.tk.res", 100);
      if (viewport != nullptr && s.m_resPercent > 0 && s.m_resPercent != 100)
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

      // Two knobs that take work away instead of tuning it, to price the two biggest suspects.
      // Shadows: a light that does not cast is dropped from the shadow pass entirely, so the
      // difference against a normal run is what the shadow maps cost.
      // Meshes: hides every mesh entity, which leaves the sky and the post chain; the difference is
      // what drawing the scene costs.
      s.m_shadow = ReadIntProperty("debug.tk.shadow", -1);
      s.m_mesh   = ReadIntProperty("debug.tk.mesh", -1);
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

      const UVec2 window = GetRenderSystem()->GetAppWindowSize();
      TK_LOG("PERF config window=%ux%u render=%ux%u res=%i%% msaa=%i hdr=%i ssao=%i bloom=%i tm=%i cull=%i batch=%i "
             "shadow=%i mesh=%i",
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
             s.m_mesh);

      s.m_windowStart = NowMs();
    }

    /** cpu time the game renderer needs to build and submit one frame. */
    inline void SetRenderCpuMs(float ms) { State().m_renderCpuMs = ms; }

    /** wall clock spent blocked in present. */
    inline void SetSwapWaitMs(float ms) { State().m_swapMs = ms; }

    /** Wall clock the present call blocked, for the caller that measures around it. */
    inline float GetSwapWaitMs() { return State().m_swapMs; }

    /**
     * Ends one frame and logs a window's worth of them.
     * @param periodMs is the wall clock between this frame body and the previous one.
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

      const double now = NowMs();
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

      float engineCpu = 0.0f;
      float engineGpu = 0.0f;
      Stats::GetRenderTime(engineCpu, engineGpu);

      TK_LOG("PERF fps=%.2f period=%.2f work=%.2f swap=%.2f render=%.2f engcpu=%.2f gpu=%.2f max=%.2f "
             "draws=%llu passes=%llu inst=%llu/%llu culled=%llu vram=%lluMB res=%ux%u",
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
             s.m_renderWidth,
             s.m_renderHeight);

      // Per pass breakdown: the profiler accumulates since the engine started, so a window's cost is
      // the difference between two snapshots.
      if (s.m_profiler)
      {
        if (TKProfiler* profiler = Profiler::GetProfiler())
        {
          std::unordered_map<String, ProfilerEntry> current;
          for (ProfilerNode* root : profiler->GetRootNodes())
          {
            FlattenProfilerNode(root, current, 0);
          }

          // Sorted by exclusive cost: the wall clock a scope owns once its children are subtracted is
          // what a stall inside the driver actually shows up as, and inclusive time is dominated by
          // whichever scope wraps the frame.
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

            TK_LOG("PROF d%u %-34s inc %7.2f exc %7.2f ms/frame", entry.m_depth, name.c_str(), inclPerFrame, exclPerFrame);
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

    /** Drops the probe's state. Called from the game's Destroy, while the engine is still alive. */
    inline void Shutdown()
    {
      ProbeState& s = State();
      s.m_enabled   = false;
      s.m_profiler  = false;
      s.m_profilerPrev.clear();
    }

  } // namespace Perf
} // namespace ToolKit
