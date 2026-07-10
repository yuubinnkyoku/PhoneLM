package com.yuubinnkyoku.phonelm

import androidx.test.ext.junit.runners.AndroidJUnit4
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NativeCpuTrainingTest {
    private val noProgress = ProgressCallback { }

    @Test
    fun cpuTrainingLowersLossAndUpdatesWeights() {
        val report = NativeBridge.nativeRunBenchmark(
            backend = Backend.CPU.nativeCode,
            batchSize = 8,
            dimension = 128,
            steps = 100,
            warmupSteps = 0,
            learningRate = 0.1f,
            seed = 20_260_710L,
            progressCallback = noProgress,
        )
        val result = BenchmarkResult.parse(report)
        assertEquals("SUCCESS", result.status)
        assertTrue(result.lossDecreased)
        assertTrue(result.weightsChanged)
        assertFalse(result.nanDetected)
        assertFalse(result.fallbackDetected)
        assertNotNull(result.initialLoss)
        assertTrue(result.finalLoss!! < result.initialLoss!!)
    }

    @Test
    fun sameSeedProducesSameInitialLoss() {
        fun oneStep(): BenchmarkResult = BenchmarkResult.parse(
            NativeBridge.nativeRunBenchmark(
                backend = Backend.CPU.nativeCode,
                batchSize = 2,
                dimension = 4,
                steps = 1,
                warmupSteps = 0,
                learningRate = 0.1f,
                seed = 42L,
                progressCallback = noProgress,
            ),
        )

        val first = oneStep()
        val second = oneStep()
        assertEquals(first.initialLoss!!, second.initialLoss!!, 0.0)
    }

    @Test
    fun nativeValidationRejectsBadBatchSize() {
        val result = BenchmarkResult.parse(
            NativeBridge.nativeRunBenchmark(
                backend = Backend.CPU.nativeCode,
                batchSize = 0,
                dimension = 4,
                steps = 1,
                warmupSteps = 0,
                learningRate = 0.1f,
                seed = 1L,
                progressCallback = noProgress,
            ),
        )
        assertEquals("FAILED", result.status)
        assertTrue(result.error.contains("batchSize"))
    }

    @Test
    fun stopRequestReturnsCancelledAtStepBoundary() {
        val executor = Executors.newSingleThreadExecutor()
        val future = executor.submit<String> {
            NativeBridge.nativeRunBenchmark(
                backend = Backend.CPU.nativeCode,
                batchSize = 32,
                dimension = 512,
                steps = 10_000,
                warmupSteps = 0,
                learningRate = 0.1f,
                seed = 7L,
                progressCallback = noProgress,
            )
        }
        Thread.sleep(100)
        NativeBridge.nativeRequestStop()
        val result = BenchmarkResult.parse(future.get(30, TimeUnit.SECONDS))
        executor.shutdownNow()
        assertEquals("CANCELLED", result.status)
    }
}
