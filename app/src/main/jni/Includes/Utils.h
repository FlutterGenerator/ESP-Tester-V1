#ifndef UTILS
#define UTILS

#include <jni.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>
#include "Logger.h"

typedef unsigned long DWORD;
static uintptr_t libBase;

bool isGameLibLoaded = false;

// Base address of a loaded library: the mapping whose file path ends with "/<library>" and whose
// file offset is 0 (the ELF header). Matching any line that merely contains the name picked
// unrelated mappings (e.g. anonymous regions named after the library) and returned a wrong base.
// Exact load base of a library: ask the dynamic linker where one of its exported functions lives.
// (/proc/self/maps can contain several mappings of the same file, which gave a wrong base.)
static DWORD baseFromDladdr(const char *library) {
    void *h = dlopen(library, RTLD_NOLOAD | RTLD_LAZY);
    if (!h) return 0;
    static const char *syms[] = {"il2cpp_init", "il2cpp_domain_get", "il2cpp_class_from_name",
                                 "il2cpp_string_new", "il2cpp_object_new"};
    for (size_t i = 0; i < sizeof(syms) / sizeof(syms[0]); i++) {
        void *p = dlsym(h, syms[i]);
        Dl_info info;
        if (p && dladdr(p, &info) && info.dli_fbase) return (DWORD) info.dli_fbase;
    }
    return 0;
}

// RVA of any address inside a loaded library (exact, via the dynamic linker). 0 if unknown.
static unsigned long rvaOf(void *addr) {
    Dl_info info;
    if (addr && dladdr(addr, &info) && info.dli_fbase)
        return (unsigned long) addr - (unsigned long) info.dli_fbase;
    return 0;
}

DWORD findLibrary(const char *library) {
    static DWORD cached = 0;
    static std::string cachedName;
    if (cached && cachedName == library) return cached;
    DWORD exact = baseFromDladdr(library);
    if (exact) {
        cachedName = library;
        return cached = exact;
    }

    char buffer[1024] = {0};
    FILE *fp = fopen(OBFUSCATE("/proc/self/maps"), OBFUSCATE("rt"));
    if (fp == NULL) {
        perror(OBFUSCATE("fopen"));
        return 0;
    }
    size_t libLen = strlen(library);
    DWORD firstMatch = 0, result = 0;

    while (fgets(buffer, sizeof(buffer), fp)) {
        unsigned long start = 0, end = 0, offset = 0;
        int pathPos = 0;
        char perms[8] = {0};
        if (sscanf(buffer, "%lx-%lx %7s %lx %*s %*s %n", &start, &end, perms, &offset, &pathPos) < 4)
            continue;
        if (pathPos <= 0) continue;
        std::string path(buffer + pathPos);
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r' || path.back() == ' '))
            path.pop_back();
        if (path.size() < libLen || path.compare(path.size() - libLen, libLen, library) != 0)
            continue;
        if (path.size() > libLen && path[path.size() - libLen - 1] != '/')
            continue; // e.g. "xlibil2cpp.so"
        if (!firstMatch) firstMatch = (DWORD) start;
        if (offset == 0) {
            result = (DWORD) start;
            break;
        }
    }
    fclose(fp);
    return result ? result : firstMatch;
}

DWORD getAbsoluteAddress(const char *libraryName, DWORD relativeAddr) {
    libBase = findLibrary(libraryName);
    if (libBase == 0)
        return 0;
    return (reinterpret_cast<DWORD>(libBase + relativeAddr));
}

extern "C" {
JNIEXPORT jboolean JNICALL
Java_esp_killshot_launcher_FloatingModMenuService_isGameLibLoaded(JNIEnv *env, jobject thiz) {
    return isGameLibLoaded;
}
}

bool isLibraryLoaded(const char *libraryName) {
    //isGameLibLoaded = true;
    char line[512] = {0};
    FILE *fp = fopen(OBFUSCATE("/proc/self/maps"), OBFUSCATE("rt"));
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            std::string a = line;
            if (strstr(line, libraryName)) {
                isGameLibLoaded = true;
                fclose(fp);
                return true;
            }
        }
        fclose(fp);
    }
    return false;
}

//Credit: Octowolve
void MakeToast(JNIEnv *env, jobject thiz, const char *text, int length) {
    //Add our toast in here so it wont be easy to change by simply editing the smali and cant
    //be cut out because this method is needed to start the hack (Octowolve is smart)
    jstring jstr = env->NewStringUTF(text); //Edit this text to your desired toast message!
    jclass toast = env->FindClass(OBFUSCATE("android/widget/Toast"));
    jmethodID methodMakeText =
            env->GetStaticMethodID(
                    toast,
                    OBFUSCATE("makeText"),
                    OBFUSCATE(
                            "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;"));
    if (methodMakeText == NULL) {
        LOGE(OBFUSCATE("toast.makeText not Found"));
        return;
    }
    //The last int is the length on how long the toast should be displayed
    //0 = Short, 1 = Long
    jobject toastobj = env->CallStaticObjectMethod(toast, methodMakeText,
                                                   thiz, jstr, length);

    jmethodID methodShow = env->GetMethodID(toast, OBFUSCATE("show"), OBFUSCATE("()V"));
    if (methodShow == NULL) {
        LOGE(OBFUSCATE("toast.show not Found"));
        return;
    }
    env->CallVoidMethod(toastobj, methodShow);
}

uintptr_t string2Offset(const char *c) {
    int base = 16;
    // See if this function catches all possibilities.
    // If it doesn't, the function would have to be amended
    // whenever you add a combination of architecture and
    // compiler that is not yet addressed.
    static_assert(sizeof(uintptr_t) == sizeof(unsigned long)
                  || sizeof(uintptr_t) == sizeof(unsigned long long),
                  "Please add string to handle conversion for this architecture.");

    // Now choose the correct function ...
    if (sizeof(uintptr_t) == sizeof(unsigned long)) {
        return strtoul(c, nullptr, base);
    }

    // All other options exhausted, sizeof(uintptr_t) == sizeof(unsigned long long))
    return strtoull(c, nullptr, base);
}

namespace Toast {
    inline const int LENGTH_LONG = 1;
    inline const int LENGTH_SHORT = 0;
}

#endif