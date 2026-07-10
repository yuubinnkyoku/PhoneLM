package com.yuubinnkyoku.phonelm

data class BenchmarkResult(
    val backendRequested: String,
    val backendActual: String,
    val initialLoss: Double?,
    val finalLoss: Double?,
    val averageStepTimeMs: Double?,
    val medianStepTimeMs: Double?,
    val p95StepTimeMs: Double?,
    val totalTimeMs: Double?,
    val lossDecreased: Boolean,
    val weightsChanged: Boolean,
    val nanDetected: Boolean,
    val fallbackDetected: Boolean,
    val status: String,
    val error: String,
    val rawReport: String,
) {
    companion object {
        fun parse(report: String): BenchmarkResult {
            val resultBlock = report.substringAfterLast("RESULT\n", report)
            val values = resultBlock.lineSequence()
                .mapNotNull { line ->
                    val separator = line.indexOf('=')
                    if (separator <= 0) null
                    else line.substring(0, separator) to line.substring(separator + 1)
                }
                .toMap()

            return BenchmarkResult(
                backendRequested = values["backend_requested"] ?: "UNKNOWN",
                backendActual = values["backend_actual"] ?: "UNKNOWN",
                initialLoss = values["initial_loss"].finiteDoubleOrNull(),
                finalLoss = values["final_loss"].finiteDoubleOrNull(),
                averageStepTimeMs = values["average_step_time_ms"].finiteDoubleOrNull(),
                medianStepTimeMs = values["median_step_time_ms"].finiteDoubleOrNull(),
                p95StepTimeMs = values["p95_step_time_ms"].finiteDoubleOrNull(),
                totalTimeMs = values["total_time_ms"].finiteDoubleOrNull(),
                lossDecreased = values["loss_decreased"] == "true",
                weightsChanged = values["weights_changed"] == "true",
                nanDetected = values["nan_detected"] == "true",
                fallbackDetected = values["fallback_detected"] == "true",
                status = values["status"] ?: "FAILED",
                error = values["error"] ?: "missing error field",
                rawReport = report,
            )
        }

        private fun String?.finiteDoubleOrNull(): Double? {
            val value = this?.toDoubleOrNull() ?: return null
            return value.takeIf(Double::isFinite)
        }
    }
}

