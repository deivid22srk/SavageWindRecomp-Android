// Android bridge inside libmain.so:
//   - JNI lifecycle (the Java side: MainActivity + Swgen, with Chaquopy running tools/gen.py)
//   - the folder / file pickers the start screen asks for (SAF, resolved to a real path)
// The compiler itself is a real clang shipped in the APK (libclang.so in nativeLibraryDir) and
// spawned as an ordinary child process by the original runtime/gamebuild.c flow.
#include <jni.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>  // SDL_DialogFileCallback

#include "../runtime/platform.h"  // PlatDialogCallback

// ------------------------------------------------------------------ state
static JavaVM *g_vm;
static jobject g_activity;      // global ref (MainActivity)
static jclass g_clsMain;        // MainActivity
static jclass g_clsSwgen;       // Swgen
static jmethodID g_mPickFolder, g_mPickFile;
static jmethodID g_mSwgenRun;
static jmethodID g_mUnpackDriverZip;  // (String zip, String dest) -> String error-or-null

// Pending dialog callbacks from runtime/ui/launcher.mm (SDL's dialog API is async).
static SDL_DialogFileCallback g_pending[2];  // 0 = folder (REQ 41), 1 = file (REQ 42)
static void *g_pending_userdata[2];

// ------------------------------------------------------------------ helpers
static JNIEnv *thread_env(void) {
    if (!g_vm) return NULL;
    JNIEnv *env = NULL;
    int rc = (*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6);
    if (rc == JNI_EDETACHED && (*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return NULL;
    return rc == JNI_OK || rc == JNI_EDETACHED ? env : NULL;
}

// ------------------------------------------------------------------ JNI entry points (Java -> C)
static void jni_set_context(JNIEnv *env, jclass cls, jobject context) {
    (void)cls;
    if (g_activity) (*env)->DeleteGlobalRef(env, g_activity);
    g_activity = (*env)->NewGlobalRef(env, context);
}

static void jni_on_picked(JNIEnv *env, jclass cls, jint code, jstring path) {
    (void)cls;
    int which = code == 41 ? 0 : code == 42 ? 1 : -1;
    if (which < 0) return;
    SDL_DialogFileCallback cb = g_pending[which];
    g_pending[which] = NULL;
    if (!cb) return;
    const char *cpath = path ? (*env)->GetStringUTFChars(env, path, NULL) : NULL;
    if (cpath) {
        const char *list[2] = {cpath, NULL};
        cb(g_pending_userdata[which], list, 0);
        (*env)->ReleaseStringUTFChars(env, path, cpath);
    } else {
        cb(g_pending_userdata[which], NULL, 0);  // cancelled
    }
}

static void jni_swgen_done(JNIEnv *env, jclass cls, jint slot, jint exit_code) {
    (void)env, (void)cls;
    extern void android_job_mark_done(int slot, int exit_code);  // runtime/platform/android.c
    android_job_mark_done(slot, exit_code);
}

static const JNINativeMethod kMainMethods[] = {
    {"nativeSetContext", "(Landroid/content/Context;)V", (void *)jni_set_context},
    {"nativeOnPicked", "(ILjava/lang/String;)V", (void *)jni_on_picked},
    {"nativeSwgenDone", "(II)V", (void *)jni_swgen_done},
};

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    g_vm = vm;
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;

    jclass main = (*env)->FindClass(env, "com/deivid22srk/savagewind/MainActivity");
    if (!main) return JNI_ERR;
    if ((*env)->RegisterNatives(env, main, kMainMethods, sizeof kMainMethods / sizeof *kMainMethods) != JNI_OK)
        return JNI_ERR;
    g_clsMain = (jclass)(*env)->NewGlobalRef(env, main);
    g_mPickFolder = (*env)->GetStaticMethodID(env, main, "pickFolder", "()V");
    g_mPickFile = (*env)->GetStaticMethodID(env, main, "pickFile", "()V");
    g_mUnpackDriverZip = (*env)->GetStaticMethodID(env, main, "unpackDriverZip",
                                                   "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");

    jclass swgen = (*env)->FindClass(env, "com/deivid22srk/savagewind/Swgen");
    if (swgen) {
        g_clsSwgen = (jclass)(*env)->NewGlobalRef(env, swgen);
        g_mSwgenRun = (*env)->GetStaticMethodID(env, swgen, "runAsync",
                                                "(I[Ljava/lang/String;Ljava/lang/String;)V");
    }
    return JNI_VERSION_1_6;
}

// ------------------------------------------------------------------ pickers (C -> Java)
void plat_pick_folder(void) {
    JNIEnv *env = thread_env();
    if (!env || !g_mPickFolder) return;
    (*env)->CallStaticVoidMethod(env, g_clsMain, g_mPickFolder);
}

void plat_pick_file(void) {
    JNIEnv *env = thread_env();
    if (!env || !g_mPickFile) return;
    (*env)->CallStaticVoidMethod(env, g_clsMain, g_mPickFile);
}

// ------------------------------------------------------------------ graphics drivers (C -> Java)
// Validate and unpack a Turnip driver .zip into dest (Java does the zip handling, the JSON
// validation and the zip-slip checks; see MainActivity.unpackDriverZip). On success dest holds
// meta.txt + the driver library and err is untouched; else err says why. All or nothing.
bool android_driver_import(const char *zip, const char *dest, char *err, size_t errn) {
    JNIEnv *env = thread_env();
    if (!env || !g_mUnpackDriverZip) {
        snprintf(err, errn, "the Java driver importer is unavailable");
        return false;
    }
    jstring jzip = (*env)->NewStringUTF(env, zip);
    jstring jdest = (*env)->NewStringUTF(env, dest);
    jstring jerr = (jstring)(*env)->CallStaticObjectMethod(env, g_clsMain, g_mUnpackDriverZip, jzip, jdest);
    (*env)->DeleteLocalRef(env, jzip);
    (*env)->DeleteLocalRef(env, jdest);
    bool ok = jerr == NULL;
    if (!ok) {
        const char *c = (*env)->GetStringUTFChars(env, jerr, NULL);
        snprintf(err, errn, "%s", c ? c : "import failed");
        if (c) (*env)->ReleaseStringUTFChars(env, jerr, c);
    }
    if (jerr) (*env)->DeleteLocalRef(env, jerr);
    return ok;
}

void plat_register_dialog_callback(int which, PlatDialogCallback cb, void *userdata) {
    if (which < 0 || which > 1) return;
    g_pending[which] = (SDL_DialogFileCallback)cb;
    g_pending_userdata[which] = userdata;
}

// ------------------------------------------------------------------ generator (C -> Chaquopy)
bool android_swgen_dispatch(int slot, const char *const *argv, const char *log) {
    JNIEnv *env = thread_env();
    if (!env || !g_mSwgenRun || !g_clsSwgen) return false;

    int n = 0;
    while (argv[n]) n++;
    jclass str = (*env)->FindClass(env, "java/lang/String");
    if (!str) return false;
    jobjectArray args = (*env)->NewObjectArray(env, n, str, NULL);
    for (int i = 0; i < n; i++) {
        jstring s = (*env)->NewStringUTF(env, argv[i]);
        (*env)->SetObjectArrayElement(env, args, i, s);
        (*env)->DeleteLocalRef(env, s);
    }
    jstring logj = (*env)->NewStringUTF(env, log ? log : "/dev/null");
    (*env)->CallStaticVoidMethod(env, g_clsSwgen, g_mSwgenRun, (jint)slot, args, logj);
    (*env)->DeleteLocalRef(env, args);
    (*env)->DeleteLocalRef(env, logj);
    return true;
}
