plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "io.nekohasekai.sagernet.plugin.naive"

    signingConfigs {
        create("release") {
            storeFile = rootProject.file("release.keystore")
            storePassword = System.getenv("KEYSTORE_PASS")
            keyAlias = "release"
            keyPassword = System.getenv("KEYSTORE_PASS")
        }
    }

    buildTypes {
        getByName("release") {
            isMinifyEnabled = true
            signingConfig = signingConfigs.getByName("release")
        }
    }

    buildToolsVersion = "35.0.0"

    compileSdk = 35

    defaultConfig {
        minSdk = 24
        targetSdk = 35

        applicationId = "io.nekohasekai.sagernet.plugin.naive"
        versionCode = System.getenv("APK_VERSION_NAME").removePrefix("v").split(".")[0].toInt() * 10 + System.getenv("APK_VERSION_NAME").removePrefix("v").split("-")[1].toInt()
        versionName = System.getenv("APK_VERSION_NAME").removePrefix("v")
        splits.abi {
            isEnable = true
            isUniversalApk = false
            reset()
            include(System.getenv("APK_ABI"))
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    lint {
        showAll = true
        checkAllWarnings = true
        checkReleaseBuilds = false
        warningsAsErrors = true
    }

    packaging {
        jniLibs.useLegacyPackaging = true
    }

    applicationVariants.all {
        outputs.all {
            this as com.android.build.gradle.internal.api.BaseVariantOutputImpl
            outputFileName =
                outputFileName.replace(project.name, "naiveproxy-plugin-v$versionName")
                    .replace("-release", "")
                    .replace("-oss", "")
        }
    }

    sourceSets.getByName("main") {
        jniLibs.srcDir("libs")
    }
}
