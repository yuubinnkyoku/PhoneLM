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
import java.io.FileOutputStream
import java.security.MessageDigest
import java.nio.file.Files
import java.nio.file.StandardCopyOption

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
        if (!BuildConfig.PHONELM_QNN_ENABLED) return
        val assetName = "qnn/libQnnHtpV81Skel.so"
        val metadataName = "qnn/qairt.properties"
        check(assets.list("qnn")?.contains("libQnnHtpV81Skel.so") == true) {
            "QNN HTP initialization blocked: V81 Skel asset is missing"
        }
        val metadata = assets.open(metadataName).bufferedReader().useLines { lines ->
            lines.mapNotNull { line ->
                val separator = line.indexOf('=')
                if (separator <= 0) null else line.substring(0, separator) to line.substring(separator + 1)
            }.toMap()
        }
        val buildId = checkNotNull(metadata["buildId"]) { "QNN asset metadata has no build ID" }
        val expectedHash = checkNotNull(metadata["skelSha256"]) { "QNN asset metadata has no Skel hash" }
        check(buildId == BuildConfig.QAIRT_BUILD_ID) {
            "QNN HTP initialization blocked: asset build ID $buildId != app ${BuildConfig.QAIRT_BUILD_ID}"
        }
        val dspRoot = File(filesDir, "qnn-dsp")
        val dspDir = File(dspRoot, buildId)
        check(dspDir.canonicalPath.startsWith(dspRoot.canonicalPath + File.separator)) {
            "QNN DSP directory escaped the app-private root"
        }
        check(dspDir.mkdirs() || dspDir.isDirectory) { "Cannot create QNN DSP directory" }
        val target = File(dspDir, "libQnnHtpV81Skel.so")
        var actualHash = if (target.isFile) sha256(target) else "MISSING"
        var action = "reused"
        if (!actualHash.equals(expectedHash, ignoreCase = true)) {
            val temporary = File.createTempFile("libQnnHtpV81Skel.so.", ".part", dspDir)
            try {
                assets.open(assetName).use { input ->
                    FileOutputStream(temporary).use { output ->
                        input.copyTo(output)
                        output.fd.sync()
                    }
                }
                val temporaryHash = sha256(temporary)
                check(temporaryHash.equals(expectedHash, ignoreCase = true)) {
                    "QNN HTP initialization blocked: Skel asset SHA-256 mismatch"
                }
                Files.move(
                    temporary.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING,
                )
                action = "replaced"
            } finally {
                if (temporary.exists()) temporary.delete()
            }
            actualHash = sha256(target)
        }
        check(actualHash.equals(expectedHash, ignoreCase = true)) {
            "QNN HTP initialization blocked: deployed Skel SHA-256 mismatch"
        }

        val entries = mutableListOf(dspDir.absolutePath)
        Os.getenv("ADSP_LIBRARY_PATH")?.split(';')?.filterTo(entries) { it.isNotBlank() }
        entries += listOf(
            "/vendor/lib/rfsa/adsp",
            "/vendor/dsp/cdsp",
            "/system/lib/rfsa/adsp",
        )
        val path = entries.map(String::trim).filter(String::isNotBlank).distinct().joinToString(";")
        Os.setenv("ADSP_LIBRARY_PATH", path, true)
        Os.setenv("PHONELM_QNN_SKEL_DIR", dspDir.absolutePath, true)
        Os.setenv("PHONELM_QNN_SKEL_EXPECTED_SHA256", expectedHash, true)
        Os.setenv("PHONELM_QNN_SKEL_ACTUAL_SHA256", actualHash, true)
        Os.setenv("PHONELM_QNN_SKEL_ACTION", action, true)
        check(Os.getenv("ADSP_LIBRARY_PATH") == path) {
            "QNN HTP initialization blocked: failed to set process-local ADSP_LIBRARY_PATH"
        }
        Log.i(
            "PhoneLMQnn",
            "qairt_build_id=$buildId htp_architecture=${BuildConfig.HTP_ARCHITECTURE} " +
                "qnn_skel_dir=${dspDir.absolutePath} qnn_skel_expected_sha256=$expectedHash " +
                "qnn_skel_actual_sha256=$actualHash qnn_skel_action=$action",
        )
    }

    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                digest.update(buffer, 0, count)
            }
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    private fun runDebugIntentIfRequested() {
        if (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE == 0) return
        val requested = intent.getStringExtra("phonelm.mode") ?: return
        val mode = runCatching { ExecutionMode.valueOf(requested) }.getOrNull() ?: return
        val batchSize = intent.getIntExtra("phonelm.batch_size", 2)
        val dimension = intent.getIntExtra("phonelm.dimension", 4)
        val steps = intent.getIntExtra("phonelm.steps", 20)
        val warmupSteps = intent.getIntExtra("phonelm.warmup_steps", 0)
        val learningRate = intent.getStringExtra("phonelm.learning_rate")?.toFloatOrNull() ?: 0.1f
        val seed = intent.getStringExtra("phonelm.seed")?.toLongOrNull() ?: 20_260_710L
        applyPreset(BenchmarkConfig(Backend.CPU, batchSize, dimension, steps, warmupSteps, learningRate, seed))
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
