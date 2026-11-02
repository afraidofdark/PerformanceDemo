<?xml version="1.0" encoding="UTF-8"?>
<template version="2" expand_clusters="true">
    <chart title="Instruction" series_composition="stacked" rendering_type="filled">
        <series color="0x2f3181" name="CPI" description="Clocks Per Instruction" expression="$CyclesCPUCycles / $InstructionsExecutedAll" display="accumulate"/>
    </chart>
    <chart title="Branch" series_composition="stacked" rendering_type="filled" percentage="yes">
        <series color="0xe0221b" name="Mispredicted Ratio" description="Branch mispredicted or not predicted" expression="$BranchPredictorMispredictions / $BranchPredictorPossiblePredictions" display="accumulate"/>
    </chart>
</template>