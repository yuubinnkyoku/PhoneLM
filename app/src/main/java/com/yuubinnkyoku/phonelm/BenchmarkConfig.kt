package com.yuubinnkyoku.phonelm

enum class Backend(val nativeCode: Int) {
    CPU(0),
    OPENCL(1),
    VULKAN(2),
}

enum class ExecutionMode(val nativeCode: Int) {
    CPU_REFERENCE(0),
    MNN_CPU(1),
    MNN_OPENCL(2),
    MNN_VULKAN(3),
    QNN_CPU_FORWARD(4),
    QNN_HTP_FORWARD(5),
    QNN_HTP_FORWARD_CPU_BACKWARD(6),
    QNN_HTP_FORWARD_DW(7),
    QNN_HTP_FORWARD_DW_DX(8),
    QNN_HTP_FULL_STEP(9),
    QNN_HTP_DEVICE_PROBE(10),
    QNN_CPU_LINEAR_TRAINING(11),
    QNN_HTP_LINEAR_TRAINING(12),
    QNN_LINEAR_GRADIENT_CHECK(13),
    QNN_CPU_MULTIBATCH_TRAINING(14),
    QNN_HTP_MULTIBATCH_TRAINING(15),
    QNN_CPU_TRAINING_BENCHMARK(16),
    QNN_HTP_TRAINING_BENCHMARK(17),
    ;

    companion object {
        fun fromBackend(backend: Backend) = when (backend) {
            Backend.CPU -> MNN_CPU
            Backend.OPENCL -> MNN_OPENCL
            Backend.VULKAN -> MNN_VULKAN
        }
    }
}

data class BenchmarkConfig(
    val backend: Backend,
    val batchSize: Int,
    val dimension: Int,
    val steps: Int,
    val warmupSteps: Int,
    val learningRate: Float = 0.1f,
    val seed: Long = 20_260_710L,
    val sampleCount: Int = 512,
    val epochs: Int = 0,
    val measuredSteps: Int = 0,
    val correctnessInterval: Int = 0,
    val benchmarkMode: Boolean = false,
) {
    fun validationError(): String? {
        if (batchSize !in 1..4096) return "batchSize must be in 1..4096"
        if (dimension !in 1..4096) return "dimension must be in 1..4096"
        if (steps !in 1..100_000) return "steps must be in 1..100000"
        if (warmupSteps !in 0..10_000) return "warmupSteps must be in 0..10000"
        if (!learningRate.isFinite() || learningRate <= 0f || learningRate > 10f) {
            return "learningRate must be finite and in (0, 10]"
        }
        if (sampleCount !in 1..1_000_000) return "sampleCount must be in 1..1000000"
        if (epochs !in 0..100_000) return "epochs must be in 0..100000"
        if (measuredSteps !in 0..100_000) return "measuredSteps must be in 0..100000"
        if (correctnessInterval !in 0..100_000) return "correctnessInterval must be in 0..100000"

        val matrixElements = dimension.toLong() * dimension.toLong()
        val batchElements = batchSize.toLong() * dimension.toLong()
        val estimatedBytes = (matrixElements * 3L + batchElements * 6L) * 12L * Float.SIZE_BYTES
        if (estimatedBytes > MAX_ESTIMATED_BYTES) {
            return "estimated working set exceeds 1536 MiB safety limit"
        }
        return null
    }

    companion object {
        private const val MAX_ESTIMATED_BYTES = 1536L * 1024L * 1024L

        fun small(backend: Backend = Backend.CPU) = BenchmarkConfig(
            backend = backend,
            batchSize = 8,
            dimension = 128,
            steps = 100,
            warmupSteps = 0,
        )

        fun benchmark(backend: Backend = Backend.CPU) = BenchmarkConfig(
            backend = backend,
            batchSize = 32,
            dimension = 512,
            steps = 200,
            warmupSteps = 20,
        )
    }
}
