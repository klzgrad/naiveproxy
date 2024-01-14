plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "moe.matsuri.exe.naive"

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
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                file("proguard-rules.pro")
            )
            isMinifyEnabled = true
            signingConfig = signingConfigs.getByName("release")
        }
    }

    compileSdk = 33

    defaultConfig {
        minSdk = 21
        targetSdk = 33

        applicationId = "moe.matsuri.exe.naive"
        versionCode = System.getenv("APK_VERSION_NAME").removePrefix("v").split(".")[0].toInt()
        versionName = System.getenv("APK_VERSION_NAME").removePrefix("v")
        splits.abi {
            isEnable = true
            isUniversalApk = false
            reset()
            include(System.getenv("APK_ABI"))
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    kotlinOptions {
        jvmTarget = "1.8"
    }

    lint {
        showAll = true
        checkAllWarnings = true
        checkReleaseBuilds = false
        warningsAsErrors = true
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
