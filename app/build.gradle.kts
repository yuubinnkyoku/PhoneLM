plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val phoneLmEnableQnn = providers.gradleProperty("phonelm.enableQnn").orElse("false")
val qairtSdkRoot = providers.gradleProperty("qairt.sdkRoot").orElse("")
fun qairtBuildId(): String {
    val root = file(qairtSdkRoot.get())
    val yaml = root.resolve("sdk.yaml").readText()
    val header = root.resolve("include/QNN/QnnSdkBuildId.h").readText()
    val version = Regex("(?m)^version:\\s*(\\S+)").find(yaml)!!.groupValues[1]
    val build = Regex("(?m)^build_id:\\s*(\\S+)").find(yaml)!!.groupValues[1]
    val headerId = Regex("QNN_SDK_BUILD_ID\\s+\"v([^\"]+)\"").find(header)!!.groupValues[1]
    val expected = "$version.$build"
    require(headerId == expected) { "Mixed QAIRT distribution: sdk.yaml=$expected, header=$headerId" }
    listOf("libQnnSystem.so", "libQnnCpu.so", "libQnnHtp.so", "libQnnHtpPrepare.so",
        "libQnnHtpV81Stub.so").forEach {
        require(root.resolve("lib/aarch64-android/$it").isFile) { "Incomplete QAIRT distribution: $it" }
    }
    require(root.resolve("lib/hexagon-v81/unsigned/libQnnHtpV81Skel.so").isFile) {
        "Incomplete QAIRT distribution: libQnnHtpV81Skel.so"
    }
    logger.lifecycle("PhoneLM QAIRT SDK: ${root.absolutePath} ($expected)")
    return expected
}
val selectedQairtBuildId = if (phoneLmEnableQnn.get().toBoolean()) qairtBuildId() else "DISABLED"
val qnnJniDir = layout.buildDirectory.dir("generated/qnnJni/arm64-v8a")
val qnnDspAssetDir = layout.buildDirectory.dir("generated/qnnDspAssets/qnn")
val stageQnnDspAsset by tasks.registering(Sync::class) {
    onlyIf { phoneLmEnableQnn.get().toBoolean() }
    from(provider { file("${qairtSdkRoot.get()}/lib/hexagon-v81/unsigned") }) {
        include("libQnnHtpV81Skel.so")
    }
    into(qnnDspAssetDir)
}
val stageQnnJni by tasks.registering(Sync::class) {
    onlyIf { phoneLmEnableQnn.get().toBoolean() }
    from(provider { file("${qairtSdkRoot.get()}/lib/aarch64-android") }) {
        include("libQnnSystem.so", "libQnnCpu.so", "libQnnHtp.so", "libQnnHtpPrepare.so", "libQnnHtpV81Stub.so")
    }
    into(qnnJniDir)
}

android {
    namespace = "com.yuubinnkyoku.phonelm"
    compileSdk = 35
    // Match the Android NDK declared by the selected QAIRT distribution.
    ndkVersion = "26.2.11394342"

    defaultConfig {
        applicationId = "com.yuubinnkyoku.phonelm"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        ndk {
            abiFilters += "arm64-v8a"
            debugSymbolLevel = "FULL"
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DPHONELM_ENABLE_QNN=${phoneLmEnableQnn.get()}",
                    "-DQAIRT_SDK_ROOT=${qairtSdkRoot.get()}",
                    "-DPHONELM_EXPECTED_QAIRT_BUILD_ID=$selectedQairtBuildId",
                )
                targets += listOf("phonelm_native")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        debug {
            isJniDebuggable = true
        }
        release {
            isMinifyEnabled = false
            isShrinkResources = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
            keepDebugSymbols += setOf("**/libMNN.so", "**/libphonelm_native.so")
        }
    }

    testOptions {
        unitTests.isReturnDefaultValues = true
    }

    // SDK binaries remain outside Git. A QNN-enabled local build packages the
    // installed SDK's Android libraries so dlopen can resolve the selected
    // backend and the device-specific HTP transport at runtime.
    if (phoneLmEnableQnn.get().toBoolean()) {
        require(qairtSdkRoot.get().isNotBlank()) { "qairt.sdkRoot is required when QNN is enabled" }
        sourceSets.getByName("main").jniLibs.srcDir(layout.buildDirectory.dir("generated/qnnJni"))
        sourceSets.getByName("main").assets.srcDir(layout.buildDirectory.dir("generated/qnnDspAssets"))
    }
}

tasks.matching { it.name.startsWith("merge") && it.name.endsWith("JniLibFolders") }
    .configureEach { dependsOn(stageQnnJni) }
tasks.matching { it.name.startsWith("merge") && it.name.endsWith("Assets") }
    .configureEach { dependsOn(stageQnnDspAsset) }

dependencies {
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}
