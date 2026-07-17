package com.yuubinnkyoku.phonelm

import android.app.Activity
import android.os.Bundle
import android.content.pm.ApplicationInfo
import android.util.Log
import android.system.Os
import android.widget.Button
import android.widget.EditText
import android.widget.ArrayAdapter
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import java.util.Locale
import java.io.File

class MainActivity : Activity() {
    private lateinit var viewModel: BenchmarkViewModel
    private lateinit var batchSizeInput: EditText
    private lateinit var dimensionInput: EditText
    private lateinit var stepsInput: EditText
    private lateinit var warmupStepsInput: EditText
    private lateinit var learningRateInput: EditText
    private lateinit var cpuButton: Button
    private lateinit var openClButton: Button
    private lateinit var vulkanButton: Button
    private lateinit var stopButton: Button
    private lateinit var resultText: TextView
    private lateinit var resultScrollView: ScrollView
    private lateinit var qnnStatusText: TextView
    private lateinit var executionModeSpinner: Spinner
    private lateinit var runSelectedModeButton: Button
    private lateinit var qnnForwardButton: Button
    private lateinit var qnnForwardDwButton: Button

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        prepareQnnDspLibrary()

        batchSizeInput = findViewById(R.id.batchSizeInput)
        dimensionInput = findViewById(R.id.dimensionInput)
        stepsInput = findViewById(R.id.stepsInput)
        warmupStepsInput = findViewById(R.id.warmupStepsInput)
        learningRateInput = findViewById(R.id.learningRateInput)
        cpuButton = findViewById(R.id.cpuButton)
        openClButton = findViewById(R.id.openClButton)
        vulkanButton = findViewById(R.id.vulkanButton)
        stopButton = findViewById(R.id.stopButton)
        resultText = findViewById(R.id.resultText)
        resultScrollView = findViewById(R.id.resultScrollView)
        qnnStatusText = findViewById(R.id.qnnStatusText)
        executionModeSpinner = findViewById(R.id.executionModeSpinner)
        runSelectedModeButton = findViewById(R.id.runSelectedModeButton)
        qnnForwardButton = findViewById(R.id.qnnForwardButton)
        qnnForwardDwButton = findViewById(R.id.qnnForwardDwButton)

        executionModeSpinner.adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_dropdown_item,
            ExecutionMode.values().map(ExecutionMode::name),
        )

        viewModel = BenchmarkViewModel()
        viewModel.setListener(::render)

        findViewById<Button>(R.id.smallPresetButton).setOnClickListener {
            applyPreset(BenchmarkConfig.small())
        }
        findViewById<Button>(R.id.benchmarkPresetButton).setOnClickListener {
            applyPreset(BenchmarkConfig.benchmark())
        }
        findViewById<Button>(R.id.qnnPresetButton).setOnClickListener {
            applyPreset(
                BenchmarkConfig(
                    backend = Backend.CPU,
                    batchSize = 2,
                    dimension = 4,
                    steps = 20,
                    warmupSteps = 0,
                ),
            )
        }
        cpuButton.setOnClickListener { start(Backend.CPU) }
        openClButton.setOnClickListener { start(Backend.OPENCL) }
        vulkanButton.setOnClickListener { start(Backend.VULKAN) }
        stopButton.setOnClickListener {
            if (!viewModel.requestStop()) toast("No benchmark is running")
        }
        runSelectedModeButton.setOnClickListener {
            startMode(ExecutionMode.values()[executionModeSpinner.selectedItemPosition])
        }
        qnnForwardButton.setOnClickListener { startMode(ExecutionMode.QNN_HTP_FORWARD) }
        qnnForwardDwButton.setOnClickListener { startMode(ExecutionMode.QNN_HTP_FORWARD_DW) }

        applyPreset(BenchmarkConfig.small())
        viewModel.loadEnvironment()
        runDebugIntentIfRequested()
    }

    private fun prepareQnnDspLibrary() {
        val assetName = "qnn/libQnnHtpV81Skel.so"
        if (assets.list("qnn")?.contains("libQnnHtpV81Skel.so") != true) return
        val dspDir = File(filesDir, "qnn-dsp").apply { mkdirs() }
        val target = File(dspDir, "libQnnHtpV81Skel.so")
        assets.open(assetName).use { input ->
            target.outputStream().use { output -> input.copyTo(output) }
        }
        Os.setenv(
            "ADSP_LIBRARY_PATH",
            "${dspDir.absolutePath};/vendor/lib/rfsa/adsp;/vendor/dsp/cdsp;/system/lib/rfsa/adsp",
            true,
        )
        Log.i("PhoneLMQnn", "Configured process-local DSP library path")
    }

    private fun runDebugIntentIfRequested() {
        if (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE == 0) return
        val requested = intent.getStringExtra("phonelm.mode") ?: return
        val mode = runCatching { ExecutionMode.valueOf(requested) }.getOrNull() ?: return
        applyPreset(BenchmarkConfig(Backend.CPU, 2, 4, 20, 0, 0.1f, 20_260_710L))
        Log.i("PhoneLMDeviceTest", "DEVICE_TEST_START mode=$requested")
        startMode(mode)
    }

    private fun applyPreset(config: BenchmarkConfig) {
        batchSizeInput.setText(String.format(Locale.ROOT, "%d", config.batchSize))
        dimensionInput.setText(String.format(Locale.ROOT, "%d", config.dimension))
        stepsInput.setText(String.format(Locale.ROOT, "%d", config.steps))
        warmupStepsInput.setText(String.format(Locale.ROOT, "%d", config.warmupSteps))
        learningRateInput.setText(String.format(Locale.ROOT, "%.3g", config.learningRate))
    }

    private fun start(backend: Backend) {
        val config = readConfig(backend) ?: return
        val error = config.validationError()
        if (error != null) {
            toast(error)
            return
        }
        if (!viewModel.start(config)) {
            toast("A benchmark is already running")
        }
    }

    private fun startMode(mode: ExecutionMode) {
        val backend = when (mode) {
            ExecutionMode.MNN_OPENCL -> Backend.OPENCL
            ExecutionMode.MNN_VULKAN -> Backend.VULKAN
            else -> Backend.CPU
        }
        val config = readConfig(backend) ?: return
        val error = config.validationError()
        if (error != null) {
            toast(error)
            return
        }
        if (!viewModel.startMode(mode, config)) {
            toast("A benchmark is already running")
        }
    }

    private fun readConfig(backend: Backend): BenchmarkConfig? {
        val batchSize = batchSizeInput.text.toString().toIntOrNull()
        val dimension = dimensionInput.text.toString().toIntOrNull()
        val steps = stepsInput.text.toString().toIntOrNull()
        val warmup = warmupStepsInput.text.toString().toIntOrNull()
        val learningRate = learningRateInput.text.toString().toFloatOrNull()
        if (batchSize == null || dimension == null || steps == null || warmup == null ||
            learningRate == null
        ) {
            toast("All configuration fields must contain valid numbers")
            return null
        }
        return BenchmarkConfig(
            backend = backend,
            batchSize = batchSize,
            dimension = dimension,
            steps = steps,
            warmupSteps = warmup,
            learningRate = learningRate,
        )
    }

    private fun render(state: BenchmarkUiState) {
        cpuButton.isEnabled = !state.running
        openClButton.isEnabled = !state.running
        vulkanButton.isEnabled = !state.running
        stopButton.isEnabled = state.running
        executionModeSpinner.isEnabled = !state.running
        runSelectedModeButton.isEnabled = !state.running
        qnnForwardButton.isEnabled = !state.running
        qnnForwardDwButton.isEnabled = !state.running
        qnnStatusText.text = state.qnnStatus
        resultText.text = state.output
        resultScrollView.post { resultScrollView.fullScroll(ScrollView.FOCUS_DOWN) }
        if (!state.running && state.lastResult != null &&
            applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
        ) {
            openFileOutput("device-test-result.txt", MODE_PRIVATE).bufferedWriter().use {
                it.write(state.output)
            }
            Log.i("PhoneLMDeviceTest", "DEVICE_TEST_DONE\n${state.output}")
        }
    }

    private fun toast(message: String) {
        Toast.makeText(this, message, Toast.LENGTH_SHORT).show()
    }

    override fun onDestroy() {
        viewModel.setListener(null)
        if (isFinishing) viewModel.close()
        super.onDestroy()
    }
}
