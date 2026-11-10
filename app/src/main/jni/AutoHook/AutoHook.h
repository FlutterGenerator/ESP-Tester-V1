#pragma once

#include "Struct.h"
#include <dlfcn.h>
#include <unistd.h>

typedef Il2CppClass *(*class_from_name_t)(const Il2CppImage *assembly, const char *name_space,
                                          const char *name);

typedef MethodInfo *(*class_get_method_from_name_t)(Il2CppClass *klass, const char *name,
                                                    int paramcount);

typedef Il2CppDomain *(*domain_get_t)();

typedef const Il2CppAssembly **(*domain_get_assemblies_t)(const Il2CppDomain *domain, size_t *size);

typedef const Il2CppImage *(*assembly_get_image_t)(const Il2CppAssembly *assembly);

typedef const Il2CppAssembly *(*domain_assembly_open_t)(Il2CppDomain *domain, const char *name);

typedef FieldInfo *(*class_get_field_from_name_t)(Il2CppClass *klass, const char *name);


template<typename T>
class Field : FieldInfo {
    bool statik;

    bool CheckStatic() {
        if ((type->attrs & 0x10) == 0)
            return false;

        if ((type->attrs & 0x40) != 0)
            return false;

        if (thread_static = offset == -1)
            LOGI(OBFUSCATE("thread static fields is not supported!"));

        return true;
    }

    bool CheckStaticFieldInfo(FieldInfo *fieldInfo) {
        if ((fieldInfo->type->attrs & 0x10) == 0)
            return false;

        if ((fieldInfo->type->attrs & 0x40) != 0)
            return false;

        if (fieldInfo->offset == -1)
            LOGI(OBFUSCATE("thread static fields is not supported!"));

        return true;
    }

public:
    bool init;
    bool thread_static;
    void *clazz;

    Field(FieldInfo *thiz, void *clas = NULL) {
        if (!CheckStaticFieldInfo(thiz))
            clazz = clas;
        init = (thiz != NULL);
        if (init) {
            parent = thiz->parent;
            offset = thiz->offset;
            name = thiz->name;
            token = thiz->token;
            type = thiz->type;
            statik = CheckStatic();
        }
    }

    DWORD get_offset() {
        return offset;
    }

    T get() {
        if (!init || thread_static) return T();
        if (statik) {
            return *(T *) ((uint64_t) parent->static_fields + offset);
        }
        return *(T *) ((uint64_t) clazz + offset);
    }

    void set(T val) {
        if (!init || thread_static) return;
        if (statik) {
            *(T *) ((uint64_t) parent->static_fields + offset) = val;
            return;
        }
        *(T *) ((uint64_t) clazz + offset) = val;
    }

    operator T() {
        return get();
    }

    void operator=(T val) {
        set(val);
    }

    bool operator==(Field other) {
        if (!init) return false;
        return (type == other.type && parent == other.parent &&
                offset == other.offset && name == other.name && token == other.token);
    }

    T operator()() {
        return get();
    }
};

typedef void *(*thread_attach_t)(Il2CppDomain *domain);

// Handle of the already loaded libil2cpp.so (never loads it by itself).
// Returns NULL while the game has not loaded il2cpp yet - callers must not spin on it.
static void *Il2cppHandle() {
    static void *h = nullptr;
    if (!h) h = dlopen(OBFUSCATE("libil2cpp.so"), RTLD_NOLOAD | RTLD_LAZY);
    return h;
}

// true when libil2cpp.so is loaded AND the il2cpp runtime (domain + assemblies) is initialised
static bool Il2cppReady() {
    void *h = Il2cppHandle();
    if (!h) return false;
    auto domain_get = (domain_get_t) dlsym(h, OBFUSCATE("il2cpp_domain_get"));
    auto domain_get_assemblies = (domain_get_assemblies_t) dlsym(h, OBFUSCATE(
            "il2cpp_domain_get_assemblies"));
    if (!domain_get || !domain_get_assemblies) return false;
    Il2CppDomain *dom = domain_get();
    if (!dom) return false;
    size_t n = 0;
    const Il2CppAssembly **all = domain_get_assemblies(dom, &n);
    return all && n > 0;
}

class LoadClass {
    // Blocks (sleeping, not burning CPU) until libil2cpp.so is loaded
    void *get_il2cpp() {
        void *mod = Il2cppHandle();
        while (!mod) {
            usleep(200 * 1000);
            mod = Il2cppHandle();
        }
        return mod;
    }

    Il2CppClass *GetClassFromName(const char *name_space, const char *type_name) {
        void *h = Il2cppHandle();
        if (!h) return nullptr;
        auto domain_get = (domain_get_t) dlsym(h, OBFUSCATE("il2cpp_domain_get"));
        auto domain_get_assemblies = (domain_get_assemblies_t) dlsym(h, OBFUSCATE(
                "il2cpp_domain_get_assemblies"));
        auto assembly_get_image = (assembly_get_image_t) dlsym(h, OBFUSCATE(
                "il2cpp_assembly_get_image"));
        auto class_from_name = (class_from_name_t) dlsym(h, OBFUSCATE("il2cpp_class_from_name"));
        auto thread_attach = (thread_attach_t) dlsym(h, OBFUSCATE("il2cpp_thread_attach"));
        if (!domain_get || !domain_get_assemblies || !assembly_get_image || !class_from_name)
            return nullptr;
        Il2CppDomain *dom = domain_get();
        if (!dom) {
            return nullptr;
        }
        // Our own thread must be known to the il2cpp runtime (attach once per thread)
        static thread_local bool attached = false;
        if (thread_attach && !attached) {
            LOGI(OBFUSCATE("AutoHook: il2cpp_thread_attach"));
            thread_attach(dom);
            attached = true;
        }
        size_t assemb_count = 0;
        const Il2CppAssembly **allAssemb = domain_get_assemblies(dom, &assemb_count);
        if (!allAssemb) return nullptr;
        LOGI(OBFUSCATE("AutoHook: looking for %s.%s in %d assemblies"), name_space, type_name,
             (int) assemb_count);
        for (size_t i = 0; i < assemb_count; i++) {
            auto assemb = allAssemb[i];
            if (!assemb) continue;
            auto img = assembly_get_image(assemb);
            if (!img) {
                continue;
            }
            auto klass = class_from_name(img, name_space, type_name);
            if (klass) {
                namespaze_txt = name_space;
                clazz_txt = type_name;
                dllname_txt = img->name;
                dll = img;
                return klass;
            }
        }
        return nullptr;
    }

public:
    const Il2CppImage *dll;
    Il2CppClass *thisclass;
    const char *namespaze_txt;
    const char *clazz_txt;
    const char *dllname_txt;

    // Waits (forever) until the class exists
    LoadClass(const char *namespaze, const char *clazz) {
        dll = nullptr;
        namespaze_txt = clazz_txt = dllname_txt = nullptr;
        thisclass = nullptr;
        get_il2cpp();
        do {
            thisclass = GetClassFromName(namespaze, clazz);
            if (!thisclass) usleep(500 * 1000);
        } while (!thisclass);
    }

    // Tries `tries` times (0.5 s apart). Check IsValid() afterwards - never blocks forever.
    LoadClass(const char *namespaze, const char *clazz, int tries) {
        dll = nullptr;
        namespaze_txt = clazz_txt = dllname_txt = nullptr;
        thisclass = nullptr;
        for (int i = 0; i < tries && !thisclass; i++) {
            thisclass = GetClassFromName(namespaze, clazz);
            if (!thisclass && i + 1 < tries) usleep(500 * 1000);
        }
    }

    LoadClass(const char *namespaze, const char *clazz, const char *dllname) {
        dll = nullptr;
        namespaze_txt = namespaze;
        clazz_txt = clazz;
        dllname_txt = dllname;
        thisclass = nullptr;
        void *h = get_il2cpp();
        auto domain_assembly_open = (domain_assembly_open_t) dlsym(h, OBFUSCATE(
                "il2cpp_domain_assembly_open"));
        auto assembly_get_image = (assembly_get_image_t) dlsym(h, OBFUSCATE(
                "il2cpp_assembly_get_image"));
        auto domain_get = (domain_get_t) dlsym(h, OBFUSCATE("il2cpp_domain_get"));
        auto class_from_name = (class_from_name_t) dlsym(h, OBFUSCATE("il2cpp_class_from_name"));
        if (!domain_assembly_open || !assembly_get_image || !domain_get || !class_from_name) return;
        do {
            Il2CppDomain *dom = domain_get();
            const Il2CppAssembly *as = dom ? domain_assembly_open(dom, dllname) : nullptr;
            dll = as ? assembly_get_image(as) : nullptr;
            if (dll) thisclass = class_from_name(dll, namespaze, clazz);
            if (!thisclass) usleep(500 * 1000);
        } while (!thisclass);
    }

    bool IsValid() const { return thisclass != nullptr; }

    FieldInfo *GetFieldInfoByName(const char *name) {
        if (!thisclass) return nullptr;
        auto class_get_field_from_name = (class_get_field_from_name_t) dlsym(get_il2cpp(),
                                                                             OBFUSCATE(
                                                                                     "il2cpp_class_get_field_from_name"));
        if (!class_get_field_from_name) return nullptr;
        return class_get_field_from_name(thisclass, name);
    }

    template<typename T>
    Field<T> GetFieldByName(const char *name) {
        return Field<T>(GetFieldInfoByName(name), (void *) thisclass);
    }

    DWORD GetFieldOffset(const char *name) {
        FieldInfo *f = GetFieldInfoByName(name);
        return f ? f->offset : 0;
    }

    DWORD GetFieldOffset(FieldInfo *filed) {
        return filed ? filed->offset : 0;
    }

    // Searches the class and its parents (e.g. "Update" that lives in a base class)
    MethodInfo *GetMethodInfoByName(const char *name, int paramcount) {
        if (!thisclass) return nullptr;
        auto class_get_method_from_name = (class_get_method_from_name_t) dlsym(get_il2cpp(),
                                                                               OBFUSCATE(
                                                                                       "il2cpp_class_get_method_from_name"));
        if (!class_get_method_from_name) return nullptr;
        return class_get_method_from_name(thisclass, name, paramcount);
    }

    // Absolute address of the native code of the method (NULL if not found).
    void *GetMethodPointerByName(const char *name, int paramcount) {
        MethodInfo *mi = GetMethodInfoByName(name, paramcount);
        return mi ? (void *) mi->methodPointer : nullptr;
    }

    // Kept for old code. NOTE: this is the absolute address, not an RVA
    DWORD GetMethodOffsetByName(const char *name, int paramcount) {
        return (DWORD) GetMethodPointerByName(name, paramcount);
    }
};

// One-shot helper: absolute address of ns.cls.method(argc) or NULL. Does not block forever.
static void *FindMethodPtr(const char *ns, const char *cls, const char *method, int argc,
                           int tries = 1) {
    LoadClass k(ns, cls, tries);
    return k.IsValid() ? k.GetMethodPointerByName(method, argc) : nullptr;
}

// Resolves an internal call by its il2cpp name, e.g. "UnityEngine.Camera::get_main()".
// Returns NULL when il2cpp is not loaded yet or the icall does not exist.
void *get_Method(const char *str) {
    void *h = Il2cppHandle();
    if (!h) return nullptr;
    auto il2cpp_resolve_icall_0 = (void *(*)(const char *)) dlsym(h, OBFUSCATE(
            "il2cpp_resolve_icall"));
    if (!il2cpp_resolve_icall_0) return nullptr;
    return il2cpp_resolve_icall_0(str);
}

#define InitResolveFunc(x, y) *reinterpret_cast<void **>(&x) = get_Method(y)
#define InitFunc(x, y) if (y != 0) *reinterpret_cast<void **>(&x) = (void *)(y)
#define FieldBN(myfield, type, inst, nameSpacec, clazzz, fieldName, key) Field<type> myfield = (new LoadClass(OBFUSCATE_KEY(nameSpacec, key), OBFUSCATE_KEY(clazzz, key)))->GetFieldByName<type>(OBFUSCATE_KEY(fieldName, key)); myfield.clazz = inst
