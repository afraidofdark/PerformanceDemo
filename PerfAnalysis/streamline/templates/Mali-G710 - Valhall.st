<?xml version="1.0" ?>
<template expand_clusters="true" version="2">
	<chart average_selection="yes" rendering_type="line" series_composition="overlay" title="Frame Rate">
		<series color="19,148,180" description="The context frame rate" display="AVERAGE" match_with="$FrameRateEGLContext*" name="Frame rate GLES ${*}" units="FPS"/>
		<series color="19,148,180" description="The device frame rate" display="AVERAGE" match_with="$FrameRateVkDevice*" name="Frame rate Vk ${*}" units="FPS"/>
	</chart>
	<chart average_selection="yes" rendering_type="bar" series_composition="stacked" title="Device Thermal State">
		<series color="41,189,111" description="Device is not being throttled" display="AVERAGE" expression="$AndroidThermalThrottlingNone * 100" name="No throttle" units="%"/>
		<series color="230,230,100" description="Device is being lightly throttled" display="AVERAGE" expression="$AndroidThermalThrottlingLight * 100" name="Light throttle" units="%"/>
		<series color="246,138,51" description="Device is being moderately throttled" display="AVERAGE" expression="$AndroidThermalThrottlingModerate * 100" name="Moderate throttle" units="%"/>
		<series color="227,80,45" description="Device is being severely throttled" display="AVERAGE" expression="$AndroidThermalThrottlingSevere * 100" name="Severe throttle" units="%"/>
		<series color="177,35,65" description="Device is in a critical, or more severe, thermal state" display="AVERAGE" expression="($AndroidThermalThrottlingCritical + $AndroidThermalThrottlingEmergency + $AndroidThermalThrottlingShutdown) * 100" name="Critical+ throttle" units="%"/>
	</chart>
	<chart average_cores="yes" average_selection="yes" height="61" percentage="yes" rendering_type="bar" series_composition="stacked" title="CPU Activity">
		<series color="58,223,67" description="User activity" display="AVERAGE" expression="$CPUActivityUser" name="User activity"/>
		<series color="223,71,66" description="System activity" display="AVERAGE" expression="$CPUActivitySystem" name="System activity"/>
	</chart>
	<chart average_cores="yes" average_selection="yes" height="61" rendering_type="line" series_composition="overlay" title="CPU Cycles">
		<series color="66,188,72" description="The number of core clock cycles" display="ACCUMULATE" expression="$CyclesCPUCycles" name="Cycles" units="cycles"/>
	</chart>
	<chart average_selection="yes" rendering_type="line" series_composition="overlay" title="Workloads / Frame">
		<series colorList="(19,148,180),(119,218,241),(11,90,109)" description="The number of context render passes per frame" display="AVERAGE" match_with="$RenderPassesFrameEGLContext*" name="Render passes GLES ${*}" units="passes"/>
		<series colorList="(119,218,241),(11,90,109),(19,148,180)" description="The number of device render passes per frame" display="AVERAGE" match_with="$RenderPassesFrameVkDevice*" name="Render passes Vk ${*}" units="passes"/>
		<series colorList="(41,203,118),(167,237,200),(24,116,68)" description="The number of context compute dispatches per frame" display="AVERAGE" match_with="$ComputeDispatchesFrameEGLContext*" name="Compute dispatches GLES ${*}" units="dispatches"/>
		<series colorList="(167,237,200),(24,116,68),(41,203,118)" description="The number of device compute dispatches per frame" display="AVERAGE" match_with="$ComputeDispatchesFrameVkDevice*" name="Compute dispatches Vk ${*}" units="dispatches"/>
		<series colorList="(246,138,51),(251,195,149),(182,87,8)" description="The number of device trace rays calls per frame" display="AVERAGE" match_with="$TraceRaysFrameVkDevice*" name="Trace rays Vk ${*}" units="calls"/>
	</chart>
	<chart average_selection="yes" rendering_type="line" series_composition="overlay" title="Draw Calls / Frame">
		<series color="164,71,164" description="The number of context draw calls per frame" display="AVERAGE" match_with="$DrawCallsFrameEGLContext*" name="Draw calls GLES ${*}" units="draws"/>
		<series color="164,71,164" description="The number of device draw calls per frame" display="AVERAGE" match_with="$DrawCallsFrameVkDevice*" name="Draw calls Vk ${*}" units="draws"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali GPU Usage">
		<series color="164,71,164" description="The number of cycles when the GPU has a workload of any type queued for processing" expression="$MaliGPUCyclesGPUActive" name="GPU active" units="cycles"/>
		<series color="41,203,118" description="The number of cycles that the compute queue is processing work" expression="$MaliGPUQueuedCyclesComputeQueued - $MaliGPUWaitCyclesComputeQueueEndpointStall" name="Compute queue active" units="cycles"/>
		<series color="246,138,51" description="The number of cycles that the vertex queue is processing work" expression="$MaliGPUQueuedCyclesVertexQueued - $MaliGPUWaitCyclesVertexQueueEndpointStall" name="Vertex queue active" units="cycles"/>
		<series color="19,148,180" description="The number of cycles that the fragment queue is processing work" expression="$MaliGPUQueuedCyclesFragmentQueued - $MaliGPUWaitCyclesFragmentQueueEndpointStall" name="Fragment queue active" units="cycles"/>
		<series color="217,21,74" description="The number of cycles when the GPU has a pending interrupt" expression="$MaliGPUCyclesGPUInterruptActive" name="GPU interrupt active" units="cycles"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali GPU Utilization">
		<series color="41,203,118" description="The compute queue utilization compared against the GPU active cycles" expression="max(min((($MaliGPUQueuedCyclesComputeQueued - $MaliGPUWaitCyclesComputeQueueEndpointStall) / $MaliGPUCyclesGPUActive) * 100, 100), 0)" name="Compute queue utilization" units="%"/>
		<series color="246,138,51" description="The vertex queue utilization compared against the GPU active cycles" expression="max(min((($MaliGPUQueuedCyclesVertexQueued - $MaliGPUWaitCyclesVertexQueueEndpointStall) / $MaliGPUCyclesGPUActive) * 100, 100), 0)" name="Vertex queue utilization" units="%"/>
		<series color="19,148,180" description="The fragment queue utilization compared against the GPU active cycles" expression="max(min((($MaliGPUQueuedCyclesFragmentQueued - $MaliGPUWaitCyclesFragmentQueueEndpointStall) / $MaliGPUCyclesGPUActive) * 100, 100), 0)" name="Fragment queue utilization" units="%"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Memory Bandwidth Usage">
		<series color="41,203,118" description="The total output read bandwidth for the GPU" expression="$MaliExternalBusBeatsReadBeats * ($MaliConstantsBusWidthBits / 8)" name="Read bytes" units="bytes"/>
		<series color="217,21,74" description="The total output write bandwidth for the GPU" expression="$MaliExternalBusBeatsWriteBeats * ($MaliConstantsBusWidthBits / 8)" name="Write bytes" units="bytes"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Memory Stall Rate">
		<series color="41,203,118" description="The percentage of cycles with an external read transaction stalled" expression="max(min(($MaliExternalBusStallCyclesReadStall / $MaliConstantsL2SliceCount / $MaliGPUCyclesGPUActive) * 100, 100), 0)" name="Read stall rate" units="%"/>
		<series color="217,21,74" description="The percentage of cycles with an external write transaction stalled" expression="max(min(($MaliExternalBusStallCyclesWriteStall / $MaliConstantsL2SliceCount / $MaliGPUCyclesGPUActive) * 100, 100), 0)" name="Write stall rate" units="%"/>
	</chart>
	<chart rendering_type="filled" series_composition="stacked" title="Mali Memory Read Latency">
		<series color="92,205,145" description="The number of data beats returned 0-127 cycles after the read request" expression="$MaliExternalBusReadLatency0127Cycles" name="0-127 cycles" units="beats"/>
		<series color="41,189,111" description="The number of data beats returned 128-191 cycles after the read request" expression="$MaliExternalBusReadLatency128191Cycles" name="128-191 cycles" units="beats"/>
		<series color="230,230,100" description="The number of data beats returned 192-255 cycles after the read request" expression="$MaliExternalBusReadLatency192255Cycles" name="192-255 cycles" units="beats"/>
		<series color="246,138,51" description="The number of data beats returned 256-319 cycles after the read request" expression="$MaliExternalBusReadLatency256319Cycles" name="256-319 cycles" units="beats"/>
		<series color="227,80,45" description="The number of data beats returned 320-383 cycles after the read request" expression="$MaliExternalBusReadLatency320383Cycles" name="320-383 cycles" units="beats"/>
		<series color="177,35,65" description="The number of read beats that are returned at least 384 cycles after the transaction started" expression="$MaliExternalBusBeatsReadBeats - $MaliExternalBusReadLatency0127Cycles - $MaliExternalBusReadLatency128191Cycles - $MaliExternalBusReadLatency192255Cycles - $MaliExternalBusReadLatency256319Cycles - $MaliExternalBusReadLatency320383Cycles" name="384+ cycles" units="beats"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Geometry Usage">
		<series color="41,189,111" description="The number of primitives that are visible after culling" expression="$MaliPrimitiveCullingVisiblePrimitives" name="Visible primitives" units="primitives"/>
		<series color="227,80,45" description="The number of primitives that were culled during the rendering process" expression="$MaliPrimitiveCullingFacingOrXYPlaneTestCulledPrimitives + $MaliPrimitiveCullingZPlaneTestCulledPrimitives + $MaliPrimitiveCullingSampleTestCulledPrimitives" name="Culled primitives" units="primitives"/>
		<series color="246,138,51" description="The total number of input primitives to the rendering process" expression="$MaliPrimitiveCullingFacingOrXYPlaneTestCulledPrimitives + $MaliPrimitiveCullingZPlaneTestCulledPrimitives + $MaliPrimitiveCullingSampleTestCulledPrimitives + $MaliPrimitiveCullingVisiblePrimitives" name="Input primitives" units="primitives"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Geometry Culling Rate">
		<series color="41,189,111" description="The percentage of primitives that are visible after culling" expression="max(min(($MaliPrimitiveCullingVisiblePrimitives / ($MaliPrimitiveCullingFacingOrXYPlaneTestCulledPrimitives + $MaliPrimitiveCullingZPlaneTestCulledPrimitives + $MaliPrimitiveCullingSampleTestCulledPrimitives + $MaliPrimitiveCullingVisiblePrimitives)) * 100, 100), 0)" name="Visible primitive rate" units="%"/>
		<series color="246,138,51" description="The percentage of primitives culled by the facing or frustum XY plane tests" expression="max(min(($MaliPrimitiveCullingFacingOrXYPlaneTestCulledPrimitives / ($MaliPrimitiveCullingFacingOrXYPlaneTestCulledPrimitives + $MaliPrimitiveCullingZPlaneTestCulledPrimitives + $MaliPrimitiveCullingSampleTestCulledPrimitives + $MaliPrimitiveCullingVisiblePrimitives)) * 100, 100), 0)" name="Facing or XY plane culled primitive rate" units="%"/>
		<series color="227,80,45" description="The percentage of primitives culled by the frustum Z plane test" expression="max(min(($MaliPrimitiveCullingZPlaneTestCulledPrimitives / (($MaliPrimitiveCullingFacingOrXYPlaneTestCulledPrimitives + $MaliPrimitiveCullingZPlaneTestCulledPrimitives + $MaliPrimitiveCullingSampleTestCulledPrimitives + $MaliPrimitiveCullingVisiblePrimitives) - $MaliPrimitiveCullingFacingOrXYPlaneTestCulledPrimitives)) * 100, 100), 0)" name="Z plane culled primitive rate" units="%"/>
		<series color="177,35,65" description="The percentage of primitives culled by the sample coverage test" expression="max(min(($MaliPrimitiveCullingSampleTestCulledPrimitives / (($MaliPrimitiveCullingFacingOrXYPlaneTestCulledPrimitives + $MaliPrimitiveCullingZPlaneTestCulledPrimitives + $MaliPrimitiveCullingSampleTestCulledPrimitives + $MaliPrimitiveCullingVisiblePrimitives) - $MaliPrimitiveCullingFacingOrXYPlaneTestCulledPrimitives - $MaliPrimitiveCullingZPlaneTestCulledPrimitives)) * 100, 100), 0)" name="Sample culled primitive rate" units="%"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Geometry Threads">
		<series color="246,138,51" description="The number of position shader thread invocations" expression="$MaliTilerShadingRequestsPositionShadingRequests * 4" name="Position shading threads" units="threads"/>
		<series color="251,195,149" description="The number of varying shader thread invocations" expression="$MaliTilerShadingRequestsVaryingShadingRequests * 4" name="Varying shading threads" units="threads"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Geometry Efficiency">
		<series color="246,138,51" description="The number of position shader invocations per input primitive" expression="($MaliTilerShadingRequestsPositionShadingRequests * 4) / ($MaliPrimitiveCullingFacingOrXYPlaneTestCulledPrimitives + $MaliPrimitiveCullingZPlaneTestCulledPrimitives + $MaliPrimitiveCullingSampleTestCulledPrimitives + $MaliPrimitiveCullingVisiblePrimitives)" name="Position threads/input primitive" units="threads"/>
		<series color="251,195,149" description="The number of varying shader invocations per visible primitive" expression="($MaliTilerShadingRequestsVaryingShadingRequests * 4) / $MaliPrimitiveCullingVisiblePrimitives" name="Varying threads/visible primitive" units="threads"/>
	</chart>
	<chart rendering_type="line" series_composition="stacked" title="Mali Pixels">
		<series color="19,148,180" description="The total number of pixels that are shaded" expression="$MaliGPUTasksFragmentTasks * 1024" name="Pixels" units="pixels"/>
	</chart>
	<chart rendering_type="line" series_composition="stacked" title="Mali Overdraw">
		<series color="19,148,180" description="The number of fragments shaded per output pixel" expression="($MaliShaderWarpsFragmentWarps * 16) / ($MaliGPUTasksFragmentTasks * 1024)" name="Fragments/pixel" units="threads"/>
	</chart>
	<chart rendering_type="line" series_composition="stacked" title="Mali Pixel Throughput">
		<series color="19,148,180" description="The average number of GPU cycles spent per pixel rendered" expression="$MaliGPUCyclesGPUActive / ($MaliGPUTasksFragmentTasks * 1024)" name="Average GPU cycles/pixel" units="cycles"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Early ZS Rate">
		<series color="41,189,111" description="The percentage of rasterized quads that were subjected to early depth and stencil testing" expression="max(min(($MaliFragmentZSQuadsEarlyZSTestedQuads / $MaliFragmentQuadsRasterizedFineQuads) * 100, 100), 0)" name="Early ZS test rate" units="%"/>
		<series color="230,230,100" description="The percentage of rasterized quads that update the framebuffer during early depth and stencil testing" expression="max(min(($MaliFragmentZSQuadsEarlyZSUpdatedQuads / $MaliFragmentQuadsRasterizedFineQuads) * 100, 100), 0)" name="Early ZS update rate" units="%"/>
		<series color="246,138,51" description="The percentage of rasterized quads that are killed by early depth and stencil testing" expression="max(min(($MaliFragmentZSQuadsEarlyZSKilledQuads / $MaliFragmentQuadsRasterizedFineQuads) * 100, 100), 0)" name="Early ZS kill rate" units="%"/>
		<series color="227,80,45" description="The percentage of rasterized quads that are killed by hidden surface removal" expression="max(min((($MaliFragmentQuadsRasterizedFineQuads - $MaliFragmentZSQuadsEarlyZSKilledQuads - (($MaliShaderWarpsFragmentWarps * 16) / 4)) / $MaliFragmentQuadsRasterizedFineQuads) * 100, 100), 0)" name="FPK kill rate" units="%"/>
		<series color="177,35,65" description="The percentage of quads that are valid occluders for hidden surface removal" expression="max(min(($MaliFragmentFPKQuadsOccludingQuads / ($MaliFragmentQuadsRasterizedFineQuads - $MaliFragmentZSQuadsEarlyZSKilledQuads)) * 100, 100), 0)" name="Occluding quad rate" units="%"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Late ZS Rate">
		<series color="177,35,65" description="The percentage of rasterized quads that are tested by late depth and stencil testing" expression="max(min(($MaliFragmentZSQuadsLateZSTestedQuads / $MaliFragmentQuadsRasterizedFineQuads) * 100, 100), 0)" name="Late ZS test rate" units="%"/>
		<series color="117,43,84" description="The percentage of rasterized quads that are killed by late depth and stencil testing" expression="max(min(($MaliFragmentZSQuadsLateZSKilledQuads / $MaliFragmentQuadsRasterizedFineQuads) * 100, 100), 0)" name="Late ZS kill rate" units="%"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Core Warps">
		<series color="246,138,51" description="The number of non-fragment warps created" expression="$MaliShaderWarpsNonFragmentWarps" name="Non-fragment warps" units="warps"/>
		<series color="19,148,180" description="The number of fragment warps created" expression="$MaliShaderWarpsFragmentWarps" name="Fragment warps" units="warps"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Core Throughput">
		<series color="246,138,51" description="The average number of shader core cycles per non-fragment thread" expression="$MaliShaderCoreCyclesNonFragmentActive / ($MaliShaderWarpsNonFragmentWarps * 16)" name="Cycles/non-fragment thread" units="cycles"/>
		<series color="19,148,180" description="The average number of shader core cycles per fragment thread" expression="$MaliShaderCoreCyclesFragmentActive / ($MaliShaderWarpsFragmentWarps * 16)" name="Cycles/fragment thread" units="cycles"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Core Utilization">
		<series color="164,71,164" description="The usage of the shader core relative to the GPU top-level clock" expression="max(min(($MaliShaderCoreCyclesAnyWorkloadActive / $MaliConstantsShaderCoreCount / $MaliGPUCyclesGPUActive) * 100, 100), 0)" name="Shader core usage" units="%"/>
		<series color="246,138,51" description="The utilization of the shader core non-fragment path" expression="max(min(($MaliShaderCoreCyclesNonFragmentActive / $MaliShaderCoreCyclesAnyWorkloadActive) * 100, 100), 0)" name="Non-fragment utilization" units="%"/>
		<series color="19,148,180" description="The utilization of the shader core fragment path" expression="max(min(($MaliShaderCoreCyclesFragmentActive / $MaliShaderCoreCyclesAnyWorkloadActive) * 100, 100), 0)" name="Fragment utilization" units="%"/>
		<series color="119,218,241" description="The percentage of cycles where at least one quad is buffered for fragment shading" expression="max(min(($MaliShaderCoreCyclesFragmentFPKBufferActive / $MaliShaderCoreCyclesFragmentActive) * 100, 100), 0)" name="Fragment FPK buffer utilization" units="%"/>
		<series color="217,21,74" description="The utilization of the programmable shader core" expression="max(min(($MaliShaderCoreCyclesExecutionCoreActive / $MaliShaderCoreCyclesAnyWorkloadActive) * 100, 100), 0)" name="Execution core utilization" units="%"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Core Unit Utilization">
		<series color="164,71,164" description="The percentage utilization of the arithmetic unit" expression="max(min((max($MaliALUInstructionsFMAPipeInstructions + $MaliALUInstructionsCVTPipeInstructions + $MaliALUInstructionsSFUPipeInstructions, $MaliALUInstructionsSFUPipeInstructions * 4) / $MaliShaderCoreCyclesExecutionCoreActive) * 100, 100), 0)" name="Arithmetic unit utilization" units="%"/>
		<series color="41,203,118" description="The percentage utilization of the varying unit" expression="max(min(((($MaliVaryingUnitIssues32BitInterpolationIssues / 2) + ($MaliVaryingUnitIssues16BitInterpolationIssues / 2)) / $MaliShaderCoreCyclesExecutionCoreActive) * 100, 100), 0)" name="Varying unit utilization" units="%"/>
		<series color="19,148,180" description="The percentage utilization of the texturing unit" expression="max(min(($MaliTextureUnitCyclesTextureFilteringActive / $MaliShaderCoreCyclesExecutionCoreActive) * 100, 100), 0)" name="Texture unit utilization" units="%"/>
		<series color="246,138,51" description="The percentage utilization of the load/store unit" expression="max(min((($MaliLoadStoreUnitCyclesFullRead + $MaliLoadStoreUnitCyclesPartialRead + $MaliLoadStoreUnitCyclesFullWrite + $MaliLoadStoreUnitCyclesPartialWrite + $MaliLoadStoreUnitCyclesAtomicAccess) / $MaliShaderCoreCyclesExecutionCoreActive) * 100, 100), 0)" name="Load/store unit utilization" units="%"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Core Program Property Rate">
		<series color="164,71,164" description="The percentage of instructions that have control flow divergence across the warp" expression="max(min(($MaliALUInstructionsDivergedInstructions / ($MaliALUInstructionsFMAPipeInstructions + $MaliALUInstructionsCVTPipeInstructions + $MaliALUInstructionsSFUPipeInstructions)) * 100, 100), 0)" name="Warp divergence rate" units="%"/>
		<series color="19,148,180" description="The percentage of warps that require more than 32 registers" expression="max(min(($MaliShaderWarpsAllRegisterWarps / ($MaliShaderWarpsNonFragmentWarps + $MaliShaderWarpsFragmentWarps)) * 100, 100), 0)" name="All registers warp rate" units="%"/>
		<series color="246,138,51" description="The percentage of fragments that use shader-based blending" expression="max(min((($MaliALUInstructionsBlendShaderCalls * 4) / $MaliShaderWarpsFragmentWarps) * 100, 100), 0)" name="Shader blend rate" units="%"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Core Workload Property Rate">
		<series color="41,203,118" description="The percentage of rasterized fine quads that contain samples with no coverage" expression="max(min(($MaliFragmentQuadsPartialRasterizedFineQuads / $MaliFragmentQuadsRasterizedFineQuads) * 100, 100), 0)" name="Partial coverage rate" units="%"/>
		<series color="246,138,51" description="The percentage of warps that have a full thread slot allocation" expression="max(min(($MaliShaderWarpsFullWarps / ($MaliShaderWarpsNonFragmentWarps + $MaliShaderWarpsFragmentWarps)) * 100, 100), 0)" name="Full warp rate" units="%"/>
		<series color="164,71,164" description="The percentage of tiles that are killed by transaction elimination" expression="max(min(($MaliShaderCoreTilesKilledUnchangedTiles / (4 * $MaliShaderCoreTilesTiles)) * 100, 100), 0)" name="Unchanged tile kill rate" units="%"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Varying Usage">
		<series color="41,203,118" description="The total number of varying unit issue cycles" expression="($MaliVaryingUnitIssues32BitInterpolationIssues / 2) + ($MaliVaryingUnitIssues16BitInterpolationIssues / 2)" name="Varying unit issue" units="cycles"/>
		<series color="19,148,180" description="The number of 16-bit interpolation cycles used by the varying unit" expression="$MaliVaryingUnitIssues16BitInterpolationIssues / 2" name="16-bit interpolation active" units="cycles"/>
		<series color="246,138,51" description="The number of 32-bit interpolation cycles used by the varying unit" expression="$MaliVaryingUnitIssues32BitInterpolationIssues / 2" name="32-bit interpolation active" units="cycles"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Texture Usage">
		<series color="19,148,180" description="The number of texture filtering issue cycles" expression="$MaliTextureUnitCyclesTextureFilteringActive" name="Texture filtering active" units="cycles"/>
		<series color="41,203,118" description="The number of cycles when the filtering unit is filled with bilinear filtering" expression="$MaliTextureUnitCyclesFullBilinearFilterActive" name="Full bilinear filter active" units="cycles"/>
		<series color="246,138,51" description="The number of cycles when the filtering unit is filled with trilinear filtering" expression="$MaliTextureUnitCyclesFullTrilinearFilterActive" name="Full trilinear filter active" units="cycles"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Texture CPI">
		<series color="19,148,180" description="The average number of texture filtering cycles per instruction" expression="$MaliTextureUnitCyclesTextureFilteringActive / ($MaliTextureUnitQuadsTextureMessages * 8)" name="Texture CPI" units="cycles"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Texture Workload Property Rate">
		<series color="41,189,111" description="The percentage of texture filtering cycles using the full width of the texture filtering data path" expression="max(min((($MaliTextureUnitCyclesFullBilinearFilterActive + $MaliTextureUnitCyclesFullTrilinearFilterActive) / $MaliTextureUnitCyclesTextureFilteringActive) * 100, 100), 0)" name="Full speed filter rate" units="%"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Texture Bus Rate">
		<series color="41,203,118" description="The percentage load on the texture message input bus" expression="max(min(($MaliTextureUnitBusInputBeats / $MaliShaderCoreCyclesExecutionCoreActive) * 100, 100), 0)" name="Input bus utilization" units="%"/>
		<series color="217,21,74" description="The percentage load on the texture message output bus" expression="max(min(($MaliTextureUnitBusOutputBeats / $MaliShaderCoreCyclesExecutionCoreActive) * 100, 100), 0)" name="Output bus utilization" units="%"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Texture Memory Usage">
		<series color="41,203,118" description="The average number of bytes read from the L2 memory system by the texture unit per filtering cycle" expression="($MaliShaderCoreL2ReadsTextureL2ReadBeats * 16) / $MaliTextureUnitCyclesTextureFilteringActive" name="L2 read bytes/cy" units="bytes"/>
		<series color="24,116,68" description="The average number of bytes read from the external memory system by the texture unit per filtering cycle" expression="($MaliShaderCoreExternalReadsTextureExternalReadBeats * 16) / $MaliTextureUnitCyclesTextureFilteringActive" name="External read bytes/cy" units="bytes"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Load/Store Usage">
		<series color="164,71,164" description="The total number of load/store issue cycles" expression="$MaliLoadStoreUnitCyclesFullRead + $MaliLoadStoreUnitCyclesPartialRead + $MaliLoadStoreUnitCyclesFullWrite + $MaliLoadStoreUnitCyclesPartialWrite + $MaliLoadStoreUnitCyclesAtomicAccess" name="Load/store unit issue" units="cycles"/>
		<series color="41,203,118" description="The number of full-width load/store cache reads" expression="$MaliLoadStoreUnitCyclesFullRead" name="Full read" units="cycles"/>
		<series color="24,116,68" description="The number of partial-width load/store cache reads" expression="$MaliLoadStoreUnitCyclesPartialRead" name="Partial read" units="cycles"/>
		<series color="217,21,74" description="The number of full-width load/store cache writes" expression="$MaliLoadStoreUnitCyclesFullWrite" name="Full write" units="cycles"/>
		<series color="136,14,46" description="The number of partial-width load/store cache writes" expression="$MaliLoadStoreUnitCyclesPartialWrite" name="Partial write" units="cycles"/>
		<series color="246,138,51" description="The number of load/store atomic accesses" expression="$MaliLoadStoreUnitCyclesAtomicAccess" name="Atomic access" units="cycles"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Load/Store Memory Usage">
		<series color="41,203,118" description="The average number of bytes read from the L2 memory system by the load/store unit per read cycle" expression="($MaliShaderCoreL2ReadsLoadStoreL2ReadBeats * 16) / ($MaliLoadStoreUnitCyclesFullRead + $MaliLoadStoreUnitCyclesPartialRead)" name="L2 read bytes/cy" units="bytes"/>
		<series color="24,116,68" description="The average number of bytes read from the external memory system by the load/store unit per read cycle" expression="($MaliShaderCoreExternalReadsLoadStoreExternalReadBeats * 16) / ($MaliLoadStoreUnitCyclesFullRead + $MaliLoadStoreUnitCyclesPartialRead)" name="External read bytes/cy" units="bytes"/>
		<series color="217,21,74" description="The average number of bytes written to the L2 memory system by the load/store unit per write cycle" expression="(($MaliShaderCoreWritesLoadStoreWriteBackWriteBeats + $MaliShaderCoreWritesLoadStoreOtherWriteBeats) * 16) / ($MaliLoadStoreUnitCyclesFullWrite + $MaliLoadStoreUnitCyclesPartialWrite)" name="L2 write bytes/cy" units="bytes"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Core L2 Memory Reads">
		<series color="164,71,164" description="The total number of bytes read from the L2 memory system by the shader core fragment front-end unit" expression="$MaliShaderCoreL2ReadsFragmentL2ReadBeats * 16" name="Front-end unit bytes" units="bytes"/>
		<series color="246,138,51" description="The total number of bytes read from the L2 memory system by the load/store unit" expression="$MaliShaderCoreL2ReadsLoadStoreL2ReadBeats * 16" name="Load/store unit bytes" units="bytes"/>
		<series color="19,148,180" description="The total number of bytes read from the L2 memory system by the texture unit" expression="$MaliShaderCoreL2ReadsTextureL2ReadBeats * 16" name="Texture unit bytes" units="bytes"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Core External Memory Reads">
		<series color="164,71,164" description="The total number of bytes read from the external memory system by the shader core fragment front-end unit" expression="$MaliShaderCoreExternalReadsFragmentExternalReadBeats * 16" name="Front-end unit bytes" units="bytes"/>
		<series color="246,138,51" description="The total number of bytes read from the external memory system by the load/store unit" expression="$MaliShaderCoreExternalReadsLoadStoreExternalReadBeats * 16" name="Load/store unit bytes" units="bytes"/>
		<series color="19,148,180" description="The total number of bytes read from the external memory system by the texture unit" expression="$MaliShaderCoreExternalReadsTextureExternalReadBeats * 16" name="Texture unit bytes" units="bytes"/>
	</chart>
	<chart rendering_type="line" series_composition="overlay" title="Mali Core Memory Writes">
		<series color="246,138,51" description="The total number of bytes written to the L2 memory system by the load/store unit" expression="($MaliShaderCoreWritesLoadStoreWriteBackWriteBeats + $MaliShaderCoreWritesLoadStoreOtherWriteBeats) * 16" name="Load/store unit bytes" units="bytes"/>
		<series color="217,21,74" description="The total number of bytes written to the L2 memory system by the tile write-back unit" expression="$MaliShaderCoreWritesTileUnitWriteBeats * 16" name="Tile unit bytes" units="bytes"/>
	</chart>
</template>
