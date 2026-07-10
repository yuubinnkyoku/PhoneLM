plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val phoneLmEnableQnn = providers.gradleProperty("phonelm.enableQnn").orElse("false")
val qairtSdkRoot = providers.gradleProperty("qairt.sdkRoot").orElse("")

android {
    namespace = "com.yuubinnkyoku.phonelm"
    compileSdk = 35
    // The local r26d package is incomplete (its build/cmake toolchain is
    // missing). r27 is installed in full and is pinned for reproducibility.
    ndkVersion = "27.0.12077973"

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
}

dependencies {
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}
