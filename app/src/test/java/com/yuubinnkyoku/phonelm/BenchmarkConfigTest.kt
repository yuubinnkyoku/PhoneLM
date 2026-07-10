package com.yuubinnkyoku.phonelm

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class BenchmarkConfigTest {
    @Test
    fun presetsAreValid() {
        assertNull(BenchmarkConfig.small().validationError())
        assertNull(BenchmarkConfig.benchmark().validationError())
    }

    @Test
    fun invalidDimensionsAreRejected() {
        assertNotNull(BenchmarkConfig.small().copy(batchSize = 0).validationError())
        assertNotNull(BenchmarkConfig.small().copy(dimension = -1).validationError())
        assertNotNull(BenchmarkConfig.small().copy(steps = 0).validationError())
        assertNotNull(BenchmarkConfig.small().copy(warmupSteps = -1).validationError())
        assertNotNull(BenchmarkConfig.small().copy(learningRate = Float.NaN).validationError())
    }

    @Test
    fun benchmarkPresetMatchesProtocol() {
        val config = BenchmarkConfig.benchmark(Backend.VULKAN)
        assertEquals(Backend.VULKAN, config.backend)
        assertEquals(32, config.batchSize)
        assertEquals(512, config.dimension)
        assertEquals(200, config.steps)
        assertEquals(20, config.warmupSteps)
    }
}

