<?xml version="1.0" encoding="UTF-8"?>
<template version="2" expand_clusters="true">
    <chart title="Instruction" series_composition="stacked" rendering_type="filled" percentage="yes">
        <series name="Load" expression="$InstructionsSpeculatedLoad / $InstructionsExecutedAll" description="Load instruction speculatively executed" display="accumulate" color="81,119,156"/>
        <series name="Store" expression="$InstructionsSpeculatedStore / $InstructionsExecutedAll" description="Store instruction speculatively executed" display="accumulate" color="176,64,61"/>
    </chart>
    <chart title="Cache" series_composition="stacked" rendering_type="filled" percentage="yes">
        <series name="L1 Refill" expression="$L1DataCacheRefill / $L1DataCacheAccess" description="Memory Read or Write operation that causes a refill of at least the level of data or unified cache closest to the processor" display="accumulate" color="224,34,27"/>
    </chart>
</template>
