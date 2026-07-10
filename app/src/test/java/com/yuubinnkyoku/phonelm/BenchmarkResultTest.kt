package com.yuubinnkyoku.phonelm

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BenchmarkResultTest {
    @Test
    fun parsesRequiredResultFields() {
        val result = BenchmarkResult.parse(
            """
            RESULT
            backend_requested=OPENCL
            backend_actual=OPENCL
            initial_loss=1.25
            final_loss=0.25
            average_step_time_ms=4.5
            median_step_time_ms=4.0
            p95_step_time_ms=6.0
            total_time_ms=500.0
            loss_decreased=true
            weights_changed=true
            nan_detected=false
            fallback_detected=false
            status=SUCCESS
            error=none
            """.trimIndent(),
        )

        assertEquals("OPENCL", result.backendRequested)
        assertEquals(1.25, result.initialLoss!!, 0.0)
        assertEquals(0.25, result.finalLoss!!, 0.0)
        assertTrue(result.lossDecreased)
        assertTrue(result.weightsChanged)
        assertFalse(result.nanDetected)
        assertEquals("SUCCESS", result.status)
    }
}

