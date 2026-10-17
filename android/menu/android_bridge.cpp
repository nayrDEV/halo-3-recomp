#include "android_bridge.h"
#include "iso.h"

#include <cstdio>
#include <thread>

#ifdef __ANDROID__
#include <jni.h>
#include <android/log.h>

namespace
{
    JavaVM* g_vm = nullptr;
    jobject g_activity = nullptr;
    std::filesystem::path g_root;

    JNIEnv* GetEnv(bool& attached)
    {
        attached = false;
        if (!g_vm || !g_activity)
            return nullptr;
        JNIEnv* env = nullptr;
        if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED)
        {
            if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK)
                return nullptr;
            attached = true;
        }
        return env;
    }

    void CallVoid(const char* name)
    {
        bool attached = false;
        JNIEnv* env = GetEnv(attached);
        if (!env)
            return;
        jclass cls = env->GetObjectClass(g_activity);
        jmethodID method = env->GetMethodID(cls, name, "()V");
        if (method)
            env->CallVoidMethod(g_activity, method);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        if (attached)
            g_vm->DetachCurrentThread();
    }

    void CallString(const char* name, const std::string& value)
    {
        bool attached = false;
        JNIEnv* env = GetEnv(attached);
        if (!env)
            return;
        jclass cls = env->GetObjectClass(g_activity);
        jmethodID method = env->GetMethodID(cls, name, "(Ljava/lang/String;)V");
        if (method)
        {
            jstring text = env->NewStringUTF(value.c_str());
            env->CallVoidMethod(g_activity, method, text);
            env->DeleteLocalRef(text);
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
        if (attached)
            g_vm->DetachCurrentThread();
    }
}

extern "C" JNIEXPORT void JNICALL Java_org_h3recomp_H3SlintActivity_nativeInit(JNIEnv* env, jobject thiz, jstring root)
{
    (void)env->GetJavaVM(&g_vm);
    if (g_activity)
        env->DeleteGlobalRef(g_activity);
    g_activity = env->NewGlobalRef(thiz);

    const char* text = env->GetStringUTFChars(root, nullptr);
    if (text)
    {
        g_root = text;
        env->ReleaseStringUTFChars(root, text);
    }
}

extern "C" JNIEXPORT void JNICALL Java_org_h3recomp_H3SlintActivity_nativeImportFinished(
    JNIEnv* env, jclass, jstring path, jstring error)
{
    const char* p = env->GetStringUTFChars(path, nullptr);
    const char* e = env->GetStringUTFChars(error, nullptr);
    std::string pathText = p ? p : "";
    std::string errorText = e ? e : "";
    if (p) env->ReleaseStringUTFChars(path, p);
    if (e) env->ReleaseStringUTFChars(error, e);
    h3::android::OnImportFinished(pathText, errorText);
}

extern "C" JNIEXPORT void JNICALL Java_org_h3recomp_H3SlintActivity_nativeExtractIso(
    JNIEnv* env, jclass, jstring isoPath)
{
    const char* p = env->GetStringUTFChars(isoPath, nullptr);
    std::string pathText = p ? p : "";
    if (p) env->ReleaseStringUTFChars(isoPath, p);

    std::filesystem::path root = g_root;
    std::thread([pathText, root] {
        std::error_code error;
        std::filesystem::path gameDir = root / "game";
        std::filesystem::create_directories(gameDir, error);
        std::string result = h3::android::ExtractIso(pathText, gameDir);
        std::filesystem::remove(pathText, error);
        h3::android::OnExtractFinished(root.string(), result);
    }).detach();
}

extern "C" JNIEXPORT void JNICALL Java_org_h3recomp_H3SlintActivity_nativeSavesExported(
    JNIEnv* env, jclass, jstring path, jstring error)
{
    const char* p = env->GetStringUTFChars(path, nullptr);
    const char* e = env->GetStringUTFChars(error, nullptr);
    std::string pathText = p ? p : "";
    std::string errorText = e ? e : "";
    if (p) env->ReleaseStringUTFChars(path, p);
    if (e) env->ReleaseStringUTFChars(error, e);
    h3::android::OnSavesExported(pathText, errorText);
}

extern "C" JNIEXPORT void JNICALL Java_org_h3recomp_H3SlintActivity_nativeSavesImported(
    JNIEnv* env, jclass, jstring path, jstring error)
{
    const char* p = env->GetStringUTFChars(path, nullptr);
    const char* e = env->GetStringUTFChars(error, nullptr);
    std::string pathText = p ? p : "";
    std::string errorText = e ? e : "";
    if (p) env->ReleaseStringUTFChars(path, p);
    if (e) env->ReleaseStringUTFChars(error, e);
    h3::android::OnSavesImported(pathText, errorText);
}

extern "C" JNIEXPORT void JNICALL Java_org_h3recomp_H3SlintActivity_nativeLogExported(
    JNIEnv* env, jclass, jstring path, jstring error)
{
    const char* p = env->GetStringUTFChars(path, nullptr);
    const char* e = env->GetStringUTFChars(error, nullptr);
    std::string pathText = p ? p : "";
    std::string errorText = e ? e : "";
    if (p) env->ReleaseStringUTFChars(path, p);
    if (e) env->ReleaseStringUTFChars(error, e);
    h3::android::OnLogExported(pathText, errorText);
}

namespace h3::android
{
    std::filesystem::path GameRoot()
    {
        if (!g_root.empty())
            return g_root;
        return std::filesystem::current_path() / "private";
    }

    void PickIso() { CallVoid("pickIso"); }
    void PickFolder() { CallVoid("pickFolder"); }
    void OpenFolder(const std::string& path) { CallString("openFolder", path); }
    void StartGame() { CallVoid("startGame"); }
    void ExportSaves() { CallVoid("exportSaves"); }
    void ImportSaves() { CallVoid("importSaves"); }
    void ExportLog() { CallVoid("exportLog"); }
}
#else

namespace h3::android
{
    std::filesystem::path GameRoot()
    {
        return std::filesystem::current_path() / "private";
    }

    void PickIso() { std::fprintf(stderr, "[menu] ISO import is Android-only\n"); }
    void PickFolder() { std::fprintf(stderr, "[menu] folder import is Android-only\n"); }
    void OpenFolder(const std::string& path) { std::fprintf(stderr, "[menu] save folder: %s\n", path.c_str()); }
    void StartGame() { std::fprintf(stderr, "[menu] start game: Android-only\n"); }
    void ExportSaves() { std::fprintf(stderr, "[menu] save export is Android-only\n"); }
    void ImportSaves() { std::fprintf(stderr, "[menu] save import is Android-only\n"); }
    void ExportLog() { std::fprintf(stderr, "[menu] log export is Android-only\n"); }
}
#endif
