package com.yuubinnkyoku.phonelm

import android.os.Handler
import android.os.Looper
import java.io.Closeable
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

data class BenchmarkUiState(
    val running: Boolean = false,
    val output: String = "",
    val lastResult: BenchmarkResult? = null,
    val qnnStatus: String = "QNN status loading...",
)

fun interface UiDispatcher {
    fun dispatch(block: () -> Unit)
}

interface BenchmarkEngine {
    fun environmentReport(): String
    fun run(config: BenchmarkConfig, progress: (String) -> Unit): String
    fun requestStop()

    fun qnnStatus(): String = "qnn_status=QNN_SDK_NOT_FOUND"

    fun runMode(
        mode: ExecutionMode,
        config: BenchmarkConfig,
        progress: (String) -> Unit,
    ): String = run(config, progress)
}

object NativeBenchmarkEngine : BenchmarkEngine {
    override fun environmentReport(): String = NativeBridge.nativeGetEnvironmentInfo()

    override fun run(config: BenchmarkConfig, progress: (String) -> Unit): String =
        NativeBridge.nativeRunBenchmark(
            backend = config.backend.nativeCode,
            batchSize = config.batchSize,
            dimension = config.dimension,
            steps = config.steps,
            warmupSteps = config.warmupSteps,
            learningRate = config.learningRate,
            seed = config.seed,
            progressCallback = ProgressCallback(progress),
        )

    override fun requestStop() = NativeBridge.nativeRequestStop()

    override fun qnnStatus(): String = NativeBridge.nativeGetQnnStatus()

    override fun runMode(
        mode: ExecutionMode,
        config: BenchmarkConfig,
        progress: (String) -> Unit,
    ): String = NativeBridge.nativeRunExecutionMode(
        executionMode = mode.nativeCode,
        batchSize = config.batchSize,
        dimension = config.dimension,
        steps = config.steps,
        warmupSteps = config.warmupSteps,
        learningRate = config.learningRate,
        seed = config.seed,
        progressCallback = ProgressCallback(progress),
    )
}

class BenchmarkViewModel(
    private val engine: BenchmarkEngine = NativeBenchmarkEngine,
    private val worker: ExecutorService = Executors.newSingleThreadExecutor { runnable ->
        Thread(runnable, "PhoneLM-benchmark").apply { isDaemon = true }
    },
    private val uiDispatcher: UiDispatcher = AndroidUiDispatcher(),
) : Closeable {
    private val lock = Any()
    private val output = StringBuilder()
    private var running = false
    private var closed = false
    private var lastResult: BenchmarkResult? = null
    private var qnnStatus = "QNN status loading..."

    @Volatile
    private var listener: ((BenchmarkUiState) -> Unit)? = null

    fun setListener(listener: ((BenchmarkUiState) -> Unit)?) {
        this.listener = listener
        publish(snapshot())
    }

    fun loadEnvironment() {
        synchronized(lock) {
            if (closed) return
        }
        worker.execute {
            try {
                append(engine.environmentReport())
                val status = engine.qnnStatus()
                synchronized(lock) { qnnStatus = status }
                append(status)
            } catch (error: Throwable) {
                append("environment_status=FAILED\nerror=${error.message ?: error.javaClass.simpleName}")
            }
        }
    }

    fun start(config: BenchmarkConfig): Boolean {
        return startMode(ExecutionMode.fromBackend(config.backend), config)
    }

    fun startMode(mode: ExecutionMode, config: BenchmarkConfig): Boolean {
        config.validationError()?.let { error ->
            append("status=REJECTED\nerror=$error")
            return false
        }

        val state = synchronized(lock) {
            if (closed || running) return false
            running = true
            lastResult = null
            output.appendLine()
            output.appendLine("RUN")
            output.appendLine("execution_mode=${mode.name}")
            output.appendLine("backend_requested=${config.backend.name}")
            currentStateLocked()
        }
        publish(state)

        worker.execute {
            var lastProgress = ""
            try {
                val report = engine.runMode(mode, config) { message ->
                    lastProgress = message
                    append(message)
                }
                if (report != lastProgress) {
                    append(report)
                }
                synchronized(lock) {
                    lastResult = BenchmarkResult.parse(report)
                }
            } catch (error: Throwable) {
                append(
                    "RESULT\nbackend_requested=${config.backend.name}\n" +
                        "backend_actual=UNINITIALIZED\nstatus=FAILED\n" +
                        "execution_mode=${mode.name}\n" +
                        "error=${error.message ?: error.javaClass.simpleName}",
                )
            } finally {
                val finalState = synchronized(lock) {
                    running = false
                    currentStateLocked()
                }
                publish(finalState)
            }
        }
        return true
    }

    fun requestStop(): Boolean {
        val shouldStop = synchronized(lock) { running && !closed }
        if (!shouldStop) return false
        engine.requestStop()
        append("stop_requested=true\nstop_semantics=after_current_step")
        return true
    }

    fun snapshot(): BenchmarkUiState = synchronized(lock) { currentStateLocked() }

    private fun append(message: String) {
        val state = synchronized(lock) {
            if (output.isNotEmpty() && output.last() != '\n') output.appendLine()
            output.appendLine(message)
            currentStateLocked()
        }
        publish(state)
    }

    private fun currentStateLocked() = BenchmarkUiState(
        running = running,
        output = output.toString(),
        lastResult = lastResult,
        qnnStatus = qnnStatus,
    )

    private fun publish(state: BenchmarkUiState) {
        uiDispatcher.dispatch {
            listener?.invoke(state)
        }
    }

    override fun close() {
        val stop = synchronized(lock) {
            if (closed) return
            closed = true
            running
        }
        if (stop) engine.requestStop()
        worker.shutdown()
    }

    private class AndroidUiDispatcher : UiDispatcher {
        private val handler = Handler(Looper.getMainLooper())
        override fun dispatch(block: () -> Unit) {
            handler.post(block)
        }
    }
}
