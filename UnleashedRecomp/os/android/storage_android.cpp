#include "storage_android.h"

#include <user/paths.h>

#include <SDL.h>
#include <SDL_system.h>
#include <jni.h>

#include <cstdio>
#include <cstdlib>

namespace os::android
{
    std::string LocaliseMessage(const char *resourceName, const char *fallback, const std::string &detail)
    {
        JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        std::string result;
        if (env && activity)
        {
            jclass activityClass = env->GetObjectClass(activity);
            jmethodID method = env->GetMethodID(activityClass, "getLocalizedMessage",
                "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
            jstring key = env->NewStringUTF(resourceName);
            jstring argument = env->NewStringUTF(detail.c_str());
            jstring text = method ? static_cast<jstring>(env->CallObjectMethod(activity, method, key, argument)) : nullptr;
            if (env->ExceptionCheck()) env->ExceptionClear();
            else if (text)
            {
                const char *chars = env->GetStringUTFChars(text, nullptr);
                if (chars) result = chars;
                if (chars) env->ReleaseStringUTFChars(text, chars);
            }
            if (text) env->DeleteLocalRef(text);
            env->DeleteLocalRef(argument);
            env->DeleteLocalRef(key);
            env->DeleteLocalRef(activityClass);
        }
        if (activity && env) env->DeleteLocalRef(activity);
        return result.empty() ? std::string(fallback) : result;
    }

    // A directory can exist but be unusable: e.g. created via `adb shell mkdir` it's owned
    // by the shell uid, and the app gets EACCES through FUSE. std::filesystem calls on such
    // paths throw all over the codebase (Config::Load etc.), so catch this case up front.
    static bool ProbeDirWritable(const std::filesystem::path &dir)
    {
        std::filesystem::path probePath = dir / ".write_probe";
        FILE *file = fopen(probePath.c_str(), "wb");
        if (file == nullptr)
            return false;

        fclose(file);
        remove(probePath.c_str());
        return true;
    }

    const std::filesystem::path & GetInternalFilesDir()
    {
        static std::filesystem::path path = []() -> std::filesystem::path
        {
            const char *storagePath = SDL_AndroidGetInternalStoragePath();
            return (storagePath != nullptr) ? std::filesystem::path(storagePath) : std::filesystem::path();
        }();

        return path;
    }

    const std::filesystem::path & GetExternalFilesDir()
    {
        static std::filesystem::path path = []() -> std::filesystem::path
        {
            const char *storagePath = SDL_AndroidGetExternalStoragePath();
            return (storagePath != nullptr) ? std::filesystem::path(storagePath) : std::filesystem::path();
        }();

        return path;
    }

    const std::filesystem::path & GetExternalMediaDir()
    {
        static std::filesystem::path path = []() -> std::filesystem::path
        {
            // SDL has no accessor for Android/media, so derive it from the external
            // files path: .../Android/data/<pkg>/files -> .../Android/media/<pkg>.
            const std::filesystem::path &external = GetExternalFilesDir();
            if (external.empty())
                return {};

            std::string str = external.string();
            const std::string marker = "/Android/data/";
            size_t markerPos = str.find(marker);
            if (markerPos == std::string::npos)
                return {};

            size_t pkgBegin = markerPos + marker.size();
            size_t pkgEnd = str.find('/', pkgBegin);
            std::string pkg = (pkgEnd == std::string::npos)
                ? str.substr(pkgBegin)
                : str.substr(pkgBegin, pkgEnd - pkgBegin);
            if (pkg.empty())
                return {};

            return std::filesystem::path(str.substr(0, markerPos)) / "Android" / "media" / pkg;
        }();

        return path;
    }

    const std::filesystem::path & GetDataRoot()
    {
        static std::filesystem::path root = []() -> std::filesystem::path
        {
            JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
            jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
            std::string selected;
            if (env && activity)
            {
                jclass activityClass = env->GetObjectClass(activity);
                jmethodID method = env->GetMethodID(activityClass, "getGameStoragePath", "()Ljava/lang/String;");
                jstring path = method ? static_cast<jstring>(env->CallObjectMethod(activity, method)) : nullptr;
                if (env->ExceptionCheck())
                {
                    env->ExceptionClear();
                }
                else if (path)
                {
                    const char *chars = env->GetStringUTFChars(path, nullptr);
                    if (chars) selected = chars;
                    if (chars) env->ReleaseStringUTFChars(path, chars);
                }
                if (path) env->DeleteLocalRef(path);
                env->DeleteLocalRef(activityClass);
                env->DeleteLocalRef(activity);
            }
            if (selected.empty() || !ProbeDirWritable(selected))
            {
                const auto message = LocaliseMessage("storage_native_unavailable",
                    "The selected game storage is unavailable or not writable. Reconnect the SD card or choose another storage location in the launcher.");
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Unleashed Recomp",
                    message.c_str(), nullptr);
                std::_Exit(1);
            }
            return std::filesystem::path(selected);
        }();

        return root;
    }
}
