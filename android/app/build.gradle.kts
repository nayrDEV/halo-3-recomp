plugins {
    id("com.android.application")
}

android {
    namespace = "org.h3recomp"
    compileSdk = 35
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "org.h3recomp"
        minSdk = 30
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_static",
                    "-DANDROID_PLATFORM=android-30",
                    "-DH3_ANDROID=ON"
                )
            }
        }
    }

    buildTypes {
        debug {
            // The x86_64 copy is for the Android emulator; release is arm64.
            ndk {
                abiFilters += "x86_64"
            }
        }
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            java.srcDirs(
                "src/main/java",
                "../../thirdparty/SDL3/android-project/app/src/main/java"
            )
            jniLibs.srcDirs("src/main/jniLibs")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }
}

dependencies {
    implementation("androidx.documentfile:documentfile:1.0.1")
}
