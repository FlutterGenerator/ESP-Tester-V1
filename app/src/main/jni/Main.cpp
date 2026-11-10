#include <list>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <string.h>
#include <pthread.h>
#include <cstring>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <dlfcn.h>
#include <map>
#include <mutex>
#include <chrono>
#include <sys/prctl.h>
#include <sys/stat.h>

#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.h"

#include "KittyMemory/MemoryPatch.h"
#include "And64InlineHook/And64InlineHook.hpp"
#include "Menu.h"

#include "AutoHook/AutoHook.h"

//Target lib here
#define targetLibName OBFUSCATE("libil2cpp.so")

#include "Includes/Macros.h"

bool espEnabled = false; // "Enable Esp" toggle (feature 1)
bool espLine = false;    // "ESP Line" toggle (feature 2)

struct Vec3 {
    float x, y, z;
};

// ---------------------------------------------------------------------------
// Unity API, found automatically BY NAME through AutoHook (il2cpp runtime API).
// No RVA / dump.cs needed for these and they keep working after game updates.
//   Camera.get_main()                    -> static Camera* get_main()
//   Camera.WorldToScreenPoint(Vector3)   -> Vector3 WorldToScreenPoint(Camera* this, Vector3 pos)
//   Component.get_transform()            -> Transform* get_transform(Component* this)
//   Transform.get_position()             -> Vector3 get_position(Transform* this)
//   Screen.get_width / get_height        -> int (used to scale to the overlay size)
// ---------------------------------------------------------------------------
static void *(*Camera_get_main)(void *mi) = NULL;
static Vec3 (*Camera_WorldToScreenPoint)(void *cam, Vec3 pos, void *mi) = NULL;
static void *(*Component_get_transform)(void *comp, void *mi) = NULL;
static Vec3 (*Transform_get_position)(void *tr, void *mi) = NULL;
static int (*Screen_get_width)(void *mi) = NULL;
static int (*Screen_get_height)(void *mi) = NULL;
static void *(*Camera_get_current)(void *mi) = NULL;
static void *(*Object_FindObjectsOfType)(void *type, bool inactive, void *mi) = NULL;
static void *(*fn_class_get_type)(void *klass) = NULL;
static void *(*fn_type_get_object)(void *type) = NULL;
static volatile bool espFuncsReady = false;

// Fallback RVAs from this game's dump.cs (used only for functions the name lookup did not find).
// They are valid for the build the dump was taken from.
#define RVA_CAMERA_GET_MAIN          0x25573B8
#define RVA_CAMERA_GET_CURRENT       0x25573E0
#define RVA_CAMERA_WORLD_TO_SCREEN   0x25571A4
#define RVA_COMPONENT_GET_TRANSFORM  0x257A7F0
#define RVA_TRANSFORM_GET_POSITION   0x2585A14
#define RVA_SCREEN_GET_WIDTH         0x255F3DC
#define RVA_SCREEN_GET_HEIGHT        0x255F404
#define RVA_OBJECT_FIND_OBJECTS      0x25811F4
#define RVA_TIME_GET_DELTATIME       0x2571368
// (Transform.get_position fallback RVA is RVA_TRANSFORM_GET_POSITION)

// The RVA_* constants above belong to THIS game only. In another game they point at random code,
// so the fallback is OFF by default (name lookup does not need them). Set to 1 only for this game.
#define USE_DUMP_RVA_FALLBACK 0
static void *rvaPtr(uint64_t rva) {
#if USE_DUMP_RVA_FALLBACK
    return rva ? (void *) getAbsoluteAddress(targetLibName, (DWORD) rva) : NULL;
#else
    return NULL;
#endif
}

// debug counters shown by the "Show Debug Info" toggle
static bool espDebug = false;
static volatile int dbgUpdateCalls = 0, dbgNoCam = 0;

// Called from hack_thread only. Returns true when every Unity function was found.
static bool resolveEspFuncs() {
    Camera_get_main = (void *(*)(void *)) FindMethodPtr(OBFUSCATE("UnityEngine"), OBFUSCATE("Camera"), OBFUSCATE("get_main"), 0);
    Camera_WorldToScreenPoint = (Vec3 (*)(void *, Vec3, void *)) FindMethodPtr(OBFUSCATE("UnityEngine"), OBFUSCATE("Camera"), OBFUSCATE("WorldToScreenPoint"), 1);
    Component_get_transform = (void *(*)(void *, void *)) FindMethodPtr(OBFUSCATE("UnityEngine"), OBFUSCATE("Component"), OBFUSCATE("get_transform"), 0);
    Transform_get_position = (Vec3 (*)(void *, void *)) FindMethodPtr(OBFUSCATE("UnityEngine"), OBFUSCATE("Transform"), OBFUSCATE("get_position"), 0);
    Screen_get_width = (int (*)(void *)) FindMethodPtr(OBFUSCATE("UnityEngine"), OBFUSCATE("Screen"), OBFUSCATE("get_width"), 0);
    Screen_get_height = (int (*)(void *)) FindMethodPtr(OBFUSCATE("UnityEngine"), OBFUSCATE("Screen"), OBFUSCATE("get_height"), 0);

    Camera_get_current = (void *(*)(void *)) FindMethodPtr(OBFUSCATE("UnityEngine"), OBFUSCATE("Camera"), OBFUSCATE("get_current"), 0);

    // fallback to the dump.cs RVAs for whatever was not found by name
    if (!Camera_get_main) Camera_get_main = (void *(*)(void *)) rvaPtr(RVA_CAMERA_GET_MAIN);
    if (!Camera_get_current) Camera_get_current = (void *(*)(void *)) rvaPtr(RVA_CAMERA_GET_CURRENT);
    if (!Camera_WorldToScreenPoint) Camera_WorldToScreenPoint = (Vec3 (*)(void *, Vec3, void *)) rvaPtr(RVA_CAMERA_WORLD_TO_SCREEN);
    if (!Component_get_transform) Component_get_transform = (void *(*)(void *, void *)) rvaPtr(RVA_COMPONENT_GET_TRANSFORM);
    if (!Transform_get_position) Transform_get_position = (Vec3 (*)(void *, void *)) rvaPtr(RVA_TRANSFORM_GET_POSITION);
    if (!Screen_get_width) Screen_get_width = (int (*)(void *)) rvaPtr(RVA_SCREEN_GET_WIDTH);
    if (!Screen_get_height) Screen_get_height = (int (*)(void *)) rvaPtr(RVA_SCREEN_GET_HEIGHT);

    // automatic object search (FindObjectsOfType(Type, bool))
    Object_FindObjectsOfType = (void *(*)(void *, bool, void *)) FindMethodPtr(OBFUSCATE("UnityEngine"), OBFUSCATE("Object"), OBFUSCATE("FindObjectsOfType"), 2);
    if (!Object_FindObjectsOfType) Object_FindObjectsOfType = (void *(*)(void *, bool, void *)) rvaPtr(RVA_OBJECT_FIND_OBJECTS);
    void *ih = Il2cppHandle();
    if (ih) {
        fn_class_get_type = (void *(*)(void *)) dlsym(ih, OBFUSCATE("il2cpp_class_get_type"));
        fn_type_get_object = (void *(*)(void *)) dlsym(ih, OBFUSCATE("il2cpp_type_get_object"));
    }
    LOGI(OBFUSCATE("auto search: find=%p get_type=%p type_obj=%p"), Object_FindObjectsOfType, fn_class_get_type, fn_type_get_object);

    bool ok = (Camera_get_main || Camera_get_current) && Camera_WorldToScreenPoint &&
              Component_get_transform && Transform_get_position;
    LOGI(OBFUSCATE("ESP funcs (final): main=%p cur=%p w2s=%p transform=%p position=%p"),
         Camera_get_main, Camera_get_current, Camera_WorldToScreenPoint, Component_get_transform,
         Transform_get_position);
    return ok;
}

static long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Screen position of every tracked object. It is computed inside the Update hook, i.e. on Unity's
// main thread (Unity APIs must not be called from the overlay thread).
struct TrackedObj {
    bool hasScreen;
    Vec3 screen; // x,y in Unity screen pixels (origin bottom-left), z = depth (<=0: behind camera)
    long long lastSeenMs;
    int slot; // which Update hook saw it (decides the colour)
};

static std::mutex trackMtx;
static std::map<void *, TrackedObj> trackedObjs;
static volatile int unityScreenW = 0, unityScreenH = 0;

// ---------------------------------------------------------------------------
// Hooks for YOUR game's class. Both are plain instance methods: void Method(void *this).
//   hkU<N>       -> remembers every `this` and its screen position
//   hkD<N>       -> forgets that `this`
// The offsets are installed in hack_thread (see "INSERT YOUR OFFSETS HERE").
// ---------------------------------------------------------------------------
// Up to 4 Update hooks and 4 Destroy hooks (e.g. bots + players). Every hook needs its own
// replacement function and its own "original" pointer, hence the template slots.
#define MAX_HOOKS 4
static void (*old_U[MAX_HOOKS])(void *, void *) = {NULL, NULL, NULL, NULL};
static void (*old_D[MAX_HOOKS])(void *, void *) = {NULL, NULL, NULL, NULL};
static volatile int dbgSlotCalls[MAX_HOOKS] = {0, 0, 0, 0};

// Runs on Unity's main thread inside Update, so the il2cpp runtime is guaranteed to be fully
// started. (Touching il2cpp from our own thread while the game starts up crashes the game.)
static void lazyResolveEspFuncs() {
    static long long lastTry = 0;
    static int attempts = 0;
    if (espFuncsReady || attempts >= 5) return;
    long long t = nowMs();
    if (t - lastTry < 2000) return;
    lastTry = t;
    attempts++;
    espFuncsReady = resolveEspFuncs();
    LOGI(OBFUSCATE("ESP funcs ready: %d (attempt %d)"), (int) espFuncsReady, attempts);
}

// Computes the screen position of one object and stores it (main thread only)
// UnityEngine.Object.m_CachedPtr (field at 2 pointers into the object) becomes 0 once Unity has
// destroyed the native side. Calling get_transform on such an object throws and kills the game.
static bool unityAlive(void *inst) {
    return inst && *(void **) ((char *) inst + 2 * sizeof(void *)) != NULL;
}

// dedupe=true: key the entry by the transform, so several components on one GameObject
// (e.g. PlayerControl + PlayerCombat) give one marker instead of many.
static void recordObj(int slot, void *inst, bool dedupe = false) {
    if (!unityAlive(inst)) return;
    TrackedObj t;
    t.hasScreen = false;
    t.screen = Vec3{0, 0, 0};
    t.lastSeenMs = nowMs();
    t.slot = slot;

    static long long lastScreenMs = 0;
    if (Screen_get_width && Screen_get_height && t.lastSeenMs - lastScreenMs > 500) {
        lastScreenMs = t.lastSeenMs;
        unityScreenW = Screen_get_width(NULL);
        unityScreenH = Screen_get_height(NULL);
    }

    void *cam = Camera_get_main ? Camera_get_main(NULL) : NULL;
    if (!cam && Camera_get_current) cam = Camera_get_current(NULL);
    if (!cam) dbgNoCam++;
    void *tr = Component_get_transform(inst, NULL);
    if (cam && tr) {
        Vec3 world = Transform_get_position(tr, NULL);
        t.screen = Camera_WorldToScreenPoint(cam, world, NULL);
        t.hasScreen = true;
    }
    if (!tr) return;
    std::lock_guard<std::mutex> g(trackMtx);
    void *key = dedupe ? tr : inst;
    std::map<void *, TrackedObj>::iterator it = trackedObjs.find(key);
    if (dedupe && it != trackedObjs.end() && it->second.lastSeenMs > t.lastSeenMs - 100 &&
        it->second.slot < t.slot)
        t.slot = it->second.slot; // keep the higher-priority class (lower slot) colour
    trackedObjs[key] = t;
}

static void trackUpdate(int slot, void *inst) {
    dbgUpdateCalls++;
    if (espEnabled && !espFuncsReady) lazyResolveEspFuncs();
    if (inst && espEnabled && espFuncsReady) recordObj(slot, inst);
}

static void eraseTracked(void *inst) {
    if (!inst) return;
    std::lock_guard<std::mutex> g(trackMtx);
    trackedObjs.erase(inst);
}

template<int N>
static void hkU(void *inst, void *mi) {
    dbgSlotCalls[N]++;
    trackUpdate(N, inst);
    if (old_U[N]) old_U[N](inst, mi);
}

template<int N>
static void hkD(void *inst, void *mi) {
    eraseTracked(inst);
    if (old_D[N]) old_D[N](inst, mi);
}

static void *const hkUs[MAX_HOOKS] = {(void *) hkU<0>, (void *) hkU<1>, (void *) hkU<2>, (void *) hkU<3>};
static void *const hkDs[MAX_HOOKS] = {(void *) hkD<0>, (void *) hkD<1>, (void *) hkD<2>, (void *) hkD<3>};

// ---------------------------------------------------------------------------
// AUTOMATIC MODE (no Update/Destroy offsets needed).
// Hooks Time.get_deltaTime (called every frame on Unity's main thread, found BY NAME), and from
// there finds every live object of the classes in the "Enter Class Names" field with
// Object.FindObjectsOfType. Default classes: NpcControl, PlayerControl.
// ---------------------------------------------------------------------------
// Empty = AUTO-DETECT (default): scan the game's own classes and pick the ones that look like
// characters. Otherwise a comma separated list of class names, e.g. "Enemy,Player".
static std::string autoClasses = "";
static std::mutex autoMtx;
#define MAX_AUTO 16
struct AutoClass {
    std::string name;
    void *klass;
    void *typeObj;
    bool rvaDone;
    unsigned long rvaUpdate, rvaDestroy, rvaDisable; // RVAs in the running game (0 = no such method)
    unsigned long rvaLate, rvaFixed, rvaAwake, rvaStart;
    int score;    // how much the name looks like a character (auto-detect)
    int maxFound; // largest number of live objects seen so far
};
static AutoClass autoCls[MAX_AUTO];
static int autoClsCount = 0;
static std::string autoClsSource = "\x01"; // != any real value, forces the first build
static bool autoDetectMode = true;
static volatile int dbgTicks = 0, dbgFound[MAX_AUTO] = {0};
static volatile int dbgAutoStage = 0; // 0 idle, 2 class not found, 3 no type api, 4 searching, 5 no candidate classes
static float (*old_deltaTime)(void *) = NULL;

static std::string trimStr(const std::string &in);

// ----- automatic detection of "character" classes ---------------------------------------------
static std::string lowerStr(std::string v) {
    for (size_t i = 0; i < v.size(); i++) v[i] = (char) tolower((unsigned char) v[i]);
    return v;
}

// 0 = not interesting. Higher = more likely a thing you want an ESP on.
static int classScore(const std::string &nameIn) {
    std::string n = lowerStr(nameIn);
    static const char *bad[] = {"manager", "ui", "menu", "spawn", "camera", "stat", "data", "info",
                                "setting", "config", "cue", "anim", "sound", "audio", "effect", "hud",
                                "canvas", "weapon", "gun", "bullet", "ball", "button", "panel", "score",
                                "list", "item", "slot", "icon", "text", "name", "ragdoll",
                                "particle", "trigger", "sync", "loader", "pool", "pocket", "event", "view",
                                "input", "joystick", "look", "mouse", "sprite", "image", "scene",
                                "network", "shop", "inventory", "skin", "cosmetic", "tutorial", "table"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        if (n.find(bad[i]) != std::string::npos) return 0;
    static const struct { const char *k; int w; } good[] = {
            {"enemy", 100}, {"enemies", 100}, {"zombie", 95}, {"monster", 95}, {"npc", 90},
            {"bot", 90}, {"opponent", 90}, {"hostile", 85}, {"mob", 80}, {"soldier", 75},
            {"creature", 75}, {"player", 70}, {"character", 60}, {"hero", 55}, {"pawn", 50},
            {"target", 45}, {"unit", 40}, {"human", 40}, {"survivor", 40}, {"animal", 35}};
    int best = 0;
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++)
        if (n.find(good[i].k) != std::string::npos && good[i].w > best) best = good[i].w;
    return best;
}

static void buildCandidates() {
    void *h = Il2cppHandle();
    autoClsCount = 0;
    if (!h) return;
    typedef void *(*dom_get_t)();
    typedef void **(*get_asm_t)(void *, size_t *);
    typedef void *(*asm_img_t)(void *);
    typedef const char *(*img_name_t)(void *);
    typedef size_t (*img_count_t)(void *);
    typedef void *(*img_class_t)(void *, size_t);
    typedef const char *(*cls_str_t)(void *);
    typedef void *(*cls_parent_t)(void *);
    dom_get_t domain_get = (dom_get_t) dlsym(h, OBFUSCATE("il2cpp_domain_get"));
    get_asm_t get_asms = (get_asm_t) dlsym(h, OBFUSCATE("il2cpp_domain_get_assemblies"));
    asm_img_t asm_image = (asm_img_t) dlsym(h, OBFUSCATE("il2cpp_assembly_get_image"));
    img_name_t image_name = (img_name_t) dlsym(h, OBFUSCATE("il2cpp_image_get_name"));
    img_count_t image_count = (img_count_t) dlsym(h, OBFUSCATE("il2cpp_image_get_class_count"));
    img_class_t image_class = (img_class_t) dlsym(h, OBFUSCATE("il2cpp_image_get_class"));
    cls_str_t class_name = (cls_str_t) dlsym(h, OBFUSCATE("il2cpp_class_get_name"));
    cls_str_t class_ns = (cls_str_t) dlsym(h, OBFUSCATE("il2cpp_class_get_namespace"));
    cls_parent_t class_parent = (cls_parent_t) dlsym(h, OBFUSCATE("il2cpp_class_get_parent"));
    if (!domain_get || !get_asms || !asm_image || !image_name || !image_count || !image_class ||
        !class_name || !class_ns || !class_parent) {
        LOGE(OBFUSCATE("auto-detect: il2cpp class enumeration API missing"));
        return;
    }
    void *dom = domain_get();
    size_t n = 0;
    void **asms = dom ? get_asms(dom, &n) : NULL;
    if (!asms) return;

    struct Cand { int score; std::string ns, name; void *klass; };
    std::vector<Cand> cands;
    for (size_t i = 0; i < n; i++) {
        void *img = asm_image(asms[i]);
        if (!img) continue;
        const char *iname = image_name(img);
        if (!iname || strncmp(iname, "Assembly-CSharp", 15) != 0) continue; // the game's own code
        size_t cnt = image_count(img);
        for (size_t c = 0; c < cnt; c++) {
            void *k = image_class(img, c);
            if (!k) continue;
            const char *nm = class_name(k);
            if (!nm || strchr(nm, '<') || strchr(nm, '`') || strchr(nm, '/')) continue;
            int sc = classScore(nm);
            if (!sc) continue;
            bool mono = false; // must derive from MonoBehaviour (so FindObjectsOfType can find it)
            void *p = k;
            for (int d = 0; p && d < 12; d++, p = class_parent(p)) {
                const char *pn = class_name(p);
                if (pn && strcmp(pn, "MonoBehaviour") == 0) { mono = true; break; }
            }
            if (!mono) continue;
            const char *ns = class_ns(k);
            cands.push_back(Cand{sc, ns ? ns : "", nm, k});
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand &x, const Cand &y) {
        return x.score != y.score ? x.score > y.score : x.name < y.name;
    });
    for (size_t i = 0; i < cands.size() && autoClsCount < MAX_AUTO; i++) {
        AutoClass &ac = autoCls[autoClsCount++];
        ac.name = cands[i].ns.empty() ? cands[i].name : cands[i].ns + "." + cands[i].name;
        ac.klass = cands[i].klass;
        ac.typeObj = NULL;
        ac.rvaDone = false;
        ac.rvaUpdate = ac.rvaDestroy = ac.rvaDisable = ac.rvaLate = ac.rvaFixed = ac.rvaAwake = ac.rvaStart = 0;
        ac.maxFound = 0;
        ac.score = cands[i].score;
        LOGI(OBFUSCATE("auto-detect candidate #%d: %s (score %d)"), autoClsCount - 1, ac.name.c_str(), cands[i].score);
    }
}

// RVA (offset inside libil2cpp.so) of a parameterless method of a class, 0 if it has none.
// Inherited methods are found too (the RVA is then the base class's method).
static unsigned long methodRva(void *klass, const char *method) {
    void *h = Il2cppHandle();
    if (!h || !klass) return 0;
    typedef MethodInfo *(*get_m_t)(void *, const char *, int);
    get_m_t get_m = (get_m_t) dlsym(h, OBFUSCATE("il2cpp_class_get_method_from_name"));
    if (!get_m) return 0;
    MethodInfo *mi = get_m(klass, method, 0);
    return mi && mi->methodPointer ? rvaOf((void *) mi->methodPointer) : 0;
}

static void autoTick() {
    static thread_local int isMain = -1;
    if (isMain < 0) {
        char nm[32] = {0};
        prctl(PR_GET_NAME, nm, 0, 0, 0);
        isMain = strcmp(nm, "UnityMain") == 0 ? 1 : 0;
    }
    if (!isMain || !espEnabled) return;
    static thread_local bool inTick = false;
    if (inTick) return; // our own Unity calls must not trigger another tick
    struct Guard { Guard() { inTick = true; } ~Guard() { inTick = false; } } guard;
    if (!espFuncsReady) { lazyResolveEspFuncs(); if (!espFuncsReady) return; }

    long long now = nowMs();
    static long long lastTick = 0, lastScan = 0;
    if (now - lastTick < 33) return; // ~30 updates per second is plenty
    lastTick = now;
    dbgTicks++;

    if (!Object_FindObjectsOfType || !fn_class_get_type || !fn_type_get_object) { dbgAutoStage = 3; return; }

    // (re)build the class list when the field changed
    std::string src;
    {
        std::lock_guard<std::mutex> g(autoMtx);
        src = trimStr(autoClasses);
    }
    static int detectTries = 0;
    if (src != autoClsSource) {
        autoClsSource = src;
        autoClsCount = 0;
        detectTries = 0;
        autoDetectMode = src.empty();
        for (int i = 0; i < MAX_AUTO; i++) dbgFound[i] = 0;
        if (!autoDetectMode) {
            size_t pos = 0;
            while (pos <= src.size() && autoClsCount < MAX_HOOKS) {
                size_t c = src.find(',', pos);
                if (c == std::string::npos) c = src.size();
                std::string tok = trimStr(src.substr(pos, c - pos));
                pos = c + 1;
                if (tok.empty()) continue;
                autoCls[autoClsCount].name = tok;
                autoCls[autoClsCount].klass = NULL;
                autoCls[autoClsCount].typeObj = NULL;
                autoCls[autoClsCount].rvaDone = false;
                autoCls[autoClsCount].rvaUpdate = autoCls[autoClsCount].rvaDestroy = autoCls[autoClsCount].rvaDisable = 0;
                autoCls[autoClsCount].rvaLate = autoCls[autoClsCount].rvaFixed = autoCls[autoClsCount].rvaAwake = autoCls[autoClsCount].rvaStart = 0;
                autoCls[autoClsCount].maxFound = 0;
                autoCls[autoClsCount].score = 0;
                autoClsCount++;
            }
        }
        lastScan = 0;
    }
    if (autoDetectMode && autoClsCount == 0 && detectTries < 10 && now - lastScan > 3000) {
        lastScan = now;
        detectTries++;
        buildCandidates();
        if (!autoClsCount) dbgAutoStage = 5;
        lastScan = 0;
    }

    static std::vector<void *> inst[MAX_AUTO];
    static int cursor = 0;
    if (now - lastScan > 500 && autoClsCount > 0) { // look for objects twice a second
        lastScan = now;
        // explicit list: all classes; auto-detect: 2 classes per scan (FindObjectsOfType is heavy)
        int budget = autoDetectMode ? 2 : autoClsCount;
        for (int step = 0; step < budget; step++) {
            int i = autoDetectMode ? (cursor++ % autoClsCount) : step;
            AutoClass &ac = autoCls[i];
            if (!ac.typeObj) {
                if (!ac.klass) {
                    std::string full = ac.name, ns;
                    size_t d = full.rfind('.');
                    if (d != std::string::npos) { ns = full.substr(0, d); full = full.substr(d + 1); }
                    LoadClass lc(ns.c_str(), full.c_str(), 1);
                    if (!lc.IsValid()) { dbgAutoStage = 2; inst[i].clear(); continue; }
                    ac.klass = (void *) lc.thisclass;
                }
                void *ty = fn_class_get_type(ac.klass);
                ac.typeObj = ty ? fn_type_get_object(ty) : NULL;
                if (!ac.typeObj) { dbgAutoStage = 3; continue; }
                LOGI(OBFUSCATE("auto class %s -> %p"), ac.name.c_str(), ac.klass);
            }
            dbgAutoStage = 4;
            if (!ac.rvaDone && ac.klass) {
                ac.rvaUpdate = methodRva(ac.klass, "Update");
                ac.rvaDestroy = methodRva(ac.klass, "OnDestroy");
                ac.rvaDisable = methodRva(ac.klass, "OnDisable");
                ac.rvaLate = methodRva(ac.klass, "LateUpdate");
                ac.rvaFixed = methodRva(ac.klass, "FixedUpdate");
                ac.rvaAwake = methodRva(ac.klass, "Awake");
                ac.rvaStart = methodRva(ac.klass, "Start");
                ac.rvaDone = true;
                LOGI(OBFUSCATE("OFFSET %s : Update 0x%lX | OnDestroy 0x%lX | OnDisable 0x%lX"),
                     ac.name.c_str(), ac.rvaUpdate, ac.rvaDestroy, ac.rvaDisable);
            }
            inst[i].clear();
            void *arr = Object_FindObjectsOfType(ac.typeObj, false, NULL);
            if (!arr) { dbgFound[i] = 0; continue; }
            // il2cpp array: object header (2 ptrs), bounds ptr, max_length, then the elements
            size_t len = *(size_t *) ((char *) arr + 3 * sizeof(void *));
            void **vec = (void **) ((char *) arr + 4 * sizeof(void *));
            if (len > 256) len = 256;
            for (size_t k = 0; k < len; k++) if (vec[k]) inst[i].push_back(vec[k]);
            if (dbgFound[i] != (int) inst[i].size())
                LOGI(OBFUSCATE("CLASS %s: %d objects"), ac.name.c_str(), (int) inst[i].size());
            dbgFound[i] = (int) inst[i].size();
            if (dbgFound[i] > ac.maxFound) ac.maxFound = dbgFound[i];
        }
    }
    for (int i = 0; i < autoClsCount; i++)
        for (size_t k = 0; k < inst[i].size(); k++) recordObj(i, inst[i][k], autoDetectMode);
}

// Second frame source: Transform.get_position is called many times per frame by the game itself
// (every bot), so the tick keeps running even when nothing calls Time.deltaTime.
static Vec3 (*old_getpos)(void *, void *) = NULL;
static volatile int dbgTickHooks = 0;
static Vec3 hk_getpos(void *tr, void *mi) {
    Vec3 r = old_getpos(tr, mi);
    autoTick();
    return r;
}

static float hk_deltaTime(void *mi) {
    float r = old_deltaTime ? old_deltaTime(mi) : 0.016f;
    autoTick();
    return r;
}

// ---------------------------------------------------------------------------
// "Enter Update Offset" / "Enter Destroy Offset" fields of the menu.
// Type an RVA from dump.cs (hex, with or without 0x). Each hook is installed once per launch
// (a hook can't be removed again). If the game is not ready yet, the offset is remembered
// and applied automatically as soon as it is.
// ---------------------------------------------------------------------------
static std::string updateOffset, destroyOffset;
static std::mutex hookMtx;
struct HookEntry {
    std::string text;
    void *addr;
    bool hooked;
    int tries;
};
static HookEntry updEntries[MAX_HOOKS], desEntries[MAX_HOOKS];
static int updCount = 0, desCount = 0;
static int hookedCount(HookEntry *e, int n) {
    int c = 0;
    for (int i = 0; i < n; i++) c += e[i].hooked ? 1 : 0;
    return c;
}

static std::string trimStr(const std::string &in) {
    size_t a = in.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = in.find_last_not_of(" \t\r\n");
    return in.substr(a, b - a + 1);
}

static bool isHexString(const std::string &t) {
    size_t i = 0;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) i = 2;
    if (i >= t.size()) return false;
    for (; i < t.size(); i++)
        if (!isxdigit((unsigned char) t[i])) return false;
    return true;
}

static long long libLoadedMs = 0; // when libil2cpp.so was first seen (set by hack_thread)

// il2cpp must not be touched while the game is still starting (that crashes it), so lookups
// by name are only allowed a good while after libil2cpp.so appeared.
static bool nameLookupAllowed() {
    return libLoadedMs && nowMs() - libLoadedMs > 40000;
}

// Input is either
//   - an RVA in hex:            1203FFC   or  0x1203FFC
//   - a name  Class.Method:     PlayerControl.Update      (finds it in the running game, so it
//                               also works when your dump.cs is from another game version)
//   - a name  Namespace.Class.Method
// Returns NULL when it can't be resolved (yet).
static void *resolveHookTarget(const std::string &input, bool *final) {
    *final = false;
    std::string t = trimStr(input);
    if (isHexString(t)) {
        uint64_t off = (uint64_t) strtoull(t.c_str(), NULL, 16);
        if (!off) { *final = true; return NULL; }
        return (void *) getAbsoluteAddress(targetLibName, (DWORD) off);
    }
    size_t dot = t.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= t.size()) {
        LOGE(OBFUSCATE("Offset must be hex or Class.Method"));
        *final = true;
        return NULL;
    }
    if (!nameLookupAllowed()) return NULL; // try again later
    std::string method = t.substr(dot + 1);
    std::string cls = t.substr(0, dot), ns;
    size_t d2 = cls.rfind('.');
    if (d2 != std::string::npos) {
        ns = cls.substr(0, d2);
        cls = cls.substr(d2 + 1);
    }
    void *p = FindMethodPtr(ns.c_str(), cls.c_str(), method.c_str(), 0, 1);
    LOGI(OBFUSCATE("Lookup %s -> %p"), t.c_str(), p);
    return p;
}

static bool hookAddr(void *addr, void *replacement, void **orig) {
    if (!addr) return false;
#if defined(__aarch64__)
    A64HookFunction(addr, replacement, orig);
#else
    MSHookFunction(addr, replacement, orig);
#endif
    return true;
}

// Real RVAs of the game methods in the RUNNING game (found by name), shown by Show Debug Info.
// Type these into the Update / Destroy fields if you want to hook by offset.
static unsigned long realRva[4] = {0, 0, 0, 0}; // Npc.Update, Npc.OnDestroy, Player.Update, Player.OnDisable
static void findRealRvas() {
    static const char *cls[4] = {"NpcControl", "NpcControl", "PlayerControl", "PlayerControl"};
    static const char *met[4] = {"Update", "OnDestroy", "Update", "OnDisable"};
    static int tries = 0;
    if (!nameLookupAllowed() || tries >= 30) return;
    bool all = true;
    for (int i = 0; i < 4; i++) {
        if (realRva[i]) continue;
        void *a = FindMethodPtr("", cls[i], met[i], 0, 1);
        if (a) {
            realRva[i] = rvaOf(a);
            LOGI(OBFUSCATE("REAL RVA %s.%s = 0x%lX"), cls[i], met[i], realRva[i]);
        } else all = false;
    }
    if (!all) tries++; else tries = 30;
}

// Splits "A.Update, B.Update" (comma separated) and adds new entries (max MAX_HOOKS).
static void syncEntries(const std::string &list, HookEntry *e, int &count) {
    size_t pos = 0;
    while (pos <= list.size()) {
        size_t c = list.find(',', pos);
        if (c == std::string::npos) c = list.size();
        std::string tok = trimStr(list.substr(pos, c - pos));
        pos = c + 1;
        if (tok.empty()) continue;
        bool known = false;
        for (int i = 0; i < count; i++) if (e[i].text == tok) known = true;
        if (!known && count < MAX_HOOKS) {
            e[count].text = tok;
            e[count].addr = NULL;
            e[count].hooked = false;
            e[count].tries = 0;
            count++;
        }
    }
}

static void applyList(HookEntry *e, int count, void *const *repl, void (**origs)(void *, void *), const char *what) {
    for (int i = 0; i < count; i++) {
        if (e[i].hooked || e[i].tries >= 30) continue;
        bool fin;
        void *a = resolveHookTarget(e[i].text, &fin);
        if (fin) e[i].tries = 30;
        else if (nameLookupAllowed() || isHexString(trimStr(e[i].text))) e[i].tries++;
        if (a) {
            e[i].addr = a;
            e[i].hooked = hookAddr(a, repl[i], (void **) &origs[i]);
            LOGI(OBFUSCATE("%s hook #%d (%s) at %p: %d"), what, i, e[i].text.c_str(), a, (int) e[i].hooked);
        }
    }
}

// Installs the hooks whose offsets/names are known. Called every second by hack_thread.
static void applyOffsetHooks() {
    if (!isLibraryLoaded(targetLibName)) return;
    std::lock_guard<std::mutex> g(hookMtx);
    syncEntries(updateOffset, updEntries, updCount);
    syncEntries(destroyOffset, desEntries, desCount);
    static bool tickHooked = false;
    static int tickTries = 0;
    if (!tickHooked && nameLookupAllowed() && tickTries < 60) {
        tickTries++;
        void *a = FindMethodPtr("UnityEngine", "Time", "get_deltaTime", 0, 1);
        if (!a && tickTries >= 5) a = rvaPtr(RVA_TIME_GET_DELTATIME); // dump.cs fallback
        if (a) {
            tickHooked = hookAddr(a, (void *) hk_deltaTime, (void **) &old_deltaTime);
            if (tickHooked) dbgTickHooks++;
            LOGI(OBFUSCATE("Frame hook (Time.get_deltaTime) at %p: %d"), a, (int) tickHooked);
        }
    }
    static bool posHooked = false;
    static int posTries = 0;
    if (!posHooked && nameLookupAllowed() && posTries < 60) {
        posTries++;
        void *a = FindMethodPtr("UnityEngine", "Transform", "get_position", 0, 1);
        if (!a && posTries >= 5) a = rvaPtr(RVA_TRANSFORM_GET_POSITION);
        if (a) {
            posHooked = hookAddr(a, (void *) hk_getpos, (void **) &old_getpos);
            if (posHooked) dbgTickHooks++;
            LOGI(OBFUSCATE("Frame hook (Transform.get_position) at %p: %d"), a, (int) posHooked);
        }
    }
    applyList(updEntries, updCount, hkUs, old_U, "Update");
    applyList(desEntries, desCount, hkDs, old_D, "Destroy");
}

// we will run our hacks in a new thread so our while loop doesn't block process main thread
void *hack_thread(void *) {
    LOGI(OBFUSCATE("pthread created"));

    // 1. wait for the game library
    do {
        sleep(1);
    } while (!isLibraryLoaded(targetLibName));
    LOGI(OBFUSCATE("%s has been loaded"), (const char *) targetLibName);

    libLoadedMs = nowMs();
    sleep(5); // let the library finish loading before patching it

    // NOTE: il2cpp is only touched much later (see nameLookupAllowed) and only from this thread.
    // Unity functions for the ESP are looked up on the main thread by hkU.

    // Hooks from the menu fields (hex RVA or Class.Method). Retried every second until applied.
    applyOffsetHooks();

    // ======================= INSERT YOUR OFFSETS HERE (optional, instead of the menu fields) =======================
    // HOOK(offset, replacement, original) works on arm64 and armv7.
    // Replace the example offsets with the RVAs of your game's Update / OnDestroy (dump.cs):
    //
    // HOOK("0x1234567", hkU<0>, old_U[0]);
    // HOOK("0x1234567", hkD<0>, old_D[0]);
    // ========================================================================

    while (true) {
        sleep(1);
        applyOffsetHooks();
    }
    return NULL;
}

//JNI calls
extern "C" {

static void EspLine(JNIEnv *env, jobject espView, jobject canvas, int a, int r, int g, int b,
                    float w, float x1, float y1, float x2, float y2) {
    static jmethodID mid = NULL;
    if (!mid) {
        jclass c = env->GetObjectClass(espView);
        mid = env->GetMethodID(c, "DrawLine", "(Landroid/graphics/Canvas;IIIIFFFFF)V");
        env->DeleteLocalRef(c);
    }
    if (mid) env->CallVoidMethod(espView, mid, canvas, a, r, g, b, w, x1, y1, x2, y2);
}

static void EspRect(JNIEnv *env, jobject espView, jobject canvas, int a, int r, int g, int b,
                    int stroke, float x, float y, float w, float h) {
    static jmethodID mid = NULL;
    if (!mid) {
        jclass c = env->GetObjectClass(espView);
        mid = env->GetMethodID(c, "DrawRect", "(Landroid/graphics/Canvas;IIIIIFFFF)V");
        env->DeleteLocalRef(c);
    }
    if (mid) env->CallVoidMethod(espView, mid, canvas, a, r, g, b, stroke, x, y, w, h);
}

static void EspCircle(JNIEnv *env, jobject espView, jobject canvas, int a, int r, int g, int b,
                      float stroke, float x, float y, float radius) {
    static jmethodID mid = NULL;
    if (!mid) {
        jclass c = env->GetObjectClass(espView);
        mid = env->GetMethodID(c, "DrawCircle", "(Landroid/graphics/Canvas;IIIIFFFF)V");
        env->DeleteLocalRef(c);
    }
    if (mid) env->CallVoidMethod(espView, mid, canvas, a, r, g, b, stroke, x, y, radius);
}

static void EspText(JNIEnv *env, jobject espView, jobject canvas, int a, int r, int g, int b,
                    const char *txt, float x, float y, float size) {
    static jmethodID mid = NULL;
    if (!mid) {
        jclass c = env->GetObjectClass(espView);
        mid = env->GetMethodID(c, "DrawText",
                               "(Landroid/graphics/Canvas;IIIILjava/lang/String;FFF)V");
        env->DeleteLocalRef(c);
    }
    if (!mid) return;
    jstring js = env->NewStringUTF(txt);
    env->CallVoidMethod(espView, mid, canvas, a, r, g, b, js, x, y, size);
    env->DeleteLocalRef(js);
}

// ---------------------------------------------------------------------------
// Full report (menu button "Save Report"): every class that was checked, how many live objects
// it has, and the RVAs of its lifecycle methods + the Unity functions the ESP uses.
// Saved to a text file; the path is shown on the screen and written to logcat (tag/lines "REPORT").
// ---------------------------------------------------------------------------
static std::string reportMsg;
static long long reportMsgUntil = 0;

static std::string packageName() {
    char buf[256] = {0};
    FILE *f = fopen("/proc/self/cmdline", "r");
    if (f) {
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = 0;
        fclose(f);
    }
    return buf;
}

static void writeReport() {
    std::string out;
    char line[400];
    std::string pkg = packageName();
    snprintf(line, sizeof(line), "ESP Tester report\nPackage: %s\nLibrary: %s  base 0x%lX\nMode: %s\n\n",
             pkg.c_str(), (const char *) targetLibName, (unsigned long) findLibrary(targetLibName),
             autoDetectMode ? "auto-detect" : "class names");
    out += line;
    snprintf(line, sizeof(line),
             "Unity functions (RVA):\n  Camera.get_main            0x%lX\n  Camera.get_current         0x%lX\n"
             "  Camera.WorldToScreenPoint  0x%lX\n  Component.get_transform    0x%lX\n"
             "  Transform.get_position     0x%lX\n  Object.FindObjectsOfType   0x%lX\n"
             "  Screen.get_width/height    0x%lX / 0x%lX\n\n",
             rvaOf((void *) Camera_get_main), rvaOf((void *) Camera_get_current),
             rvaOf((void *) Camera_WorldToScreenPoint), rvaOf((void *) Component_get_transform),
             rvaOf((void *) Transform_get_position), rvaOf((void *) Object_FindObjectsOfType),
             rvaOf((void *) Screen_get_width), rvaOf((void *) Screen_get_height));
    out += line;
    out += "CLASSES THAT HAVE OBJECTS (these work for ESP):\n";
    int working = 0;
    for (int i = 0; i < autoClsCount; i++) {
        if (autoCls[i].maxFound <= 0) continue;
        working++;
        const AutoClass &c = autoCls[i];
        snprintf(line, sizeof(line),
                 "  %s  now=%d max=%d\n    Update 0x%lX | LateUpdate 0x%lX | FixedUpdate 0x%lX\n"
                 "    Awake 0x%lX | Start 0x%lX | OnDestroy 0x%lX | OnDisable 0x%lX\n",
                 c.name.c_str(), (int) dbgFound[i], c.maxFound, c.rvaUpdate, c.rvaLate, c.rvaFixed,
                 c.rvaAwake, c.rvaStart, c.rvaDestroy, c.rvaDisable);
        out += line;
    }
    if (!working) out += "  (none yet - join a match / scene with objects and save again)\n";
    out += "\nOTHER CLASSES CHECKED (no objects seen):\n";
    for (int i = 0; i < autoClsCount; i++) {
        if (autoCls[i].maxFound > 0) continue;
        out += "  " + autoCls[i].name + (autoCls[i].score ? "  (name score " + std::to_string(autoCls[i].score) + ")" : "") + "\n";
    }
    out += "\n0x0 = the class has no such method. Use OnDisable when OnDestroy is 0x0.\n"
           "Offsets are valid only for this game build.\n";

    const char *dirs[] = {"/sdcard/Android/data/%s/files", "/storage/emulated/0/Android/data/%s/files",
                          "/data/data/%s/files", "/sdcard/Download"};
    std::string saved;
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        char dir[300], path[360];
        snprintf(dir, sizeof(dir), dirs[i], pkg.c_str());
        mkdir(dir, 0777);
        snprintf(path, sizeof(path), "%s/ESP_Tester_report.txt", dir);
        FILE *f = fopen(path, "w");
        if (!f) continue;
        fwrite(out.data(), 1, out.size(), f);
        fclose(f);
        saved = path;
        break;
    }
    // also to logcat, line by line
    size_t pos = 0;
    while (pos < out.size()) {
        size_t e = out.find('\n', pos);
        if (e == std::string::npos) e = out.size();
        LOGI(OBFUSCATE("REPORT %s"), out.substr(pos, e - pos).c_str());
        pos = e + 1;
    }
    reportMsg = saved.empty() ? "Report: could not write a file, see logcat (REPORT)" : "Report saved: " + saved;
    reportMsgUntil = nowMs() + 15000;
}

static void drawDebug(JNIEnv *env, jobject espView, jobject canvas, float cx, float h,
                      int total, int shown) {
    char b1[200];
    snprintf(b1, sizeof(b1), "Hooks U:%d/%d D:%d/%d | Update calls %d [%d,%d,%d,%d] | funcs %d | no cam %d",
             hookedCount(updEntries, updCount), updCount, hookedCount(desEntries, desCount), desCount,
             (int) dbgUpdateCalls, (int) dbgSlotCalls[0], (int) dbgSlotCalls[1], (int) dbgSlotCalls[2],
             (int) dbgSlotCalls[3], (int) espFuncsReady, (int) dbgNoCam);
    EspText(env, espView, canvas, 255, 255, 255, 0, b1, cx, h * 0.1f, 22);

    char b2[200];
    snprintf(b2, sizeof(b2), "Update RVA: 0x%lX 0x%lX | Destroy RVA: 0x%lX 0x%lX",
             rvaOf(updEntries[0].addr),
             rvaOf(updEntries[1].addr),
             rvaOf(desEntries[0].addr),
             rvaOf(desEntries[1].addr));
    EspText(env, espView, canvas, 255, 255, 255, 0, b2, cx, h * 0.14f, 22);

    int foundSum = 0;
    for (int i = 0; i < MAX_AUTO; i++) foundSum += dbgFound[i];
    char b3[160];
    snprintf(b3, sizeof(b3), "auto(%s): src %d ticks %d stage %d classes %d found %d | tracked %d | shown %d | %dx%d",
             autoDetectMode ? "detect" : "names", (int) dbgTickHooks, (int) dbgTicks, (int) dbgAutoStage,
             autoClsCount, foundSum, total, shown,
             (int) unityScreenW, (int) unityScreenH);
    EspText(env, espView, canvas, 255, 255, 255, 0, b3, cx, h * 0.18f, 22);

    // One line per class that has live objects right now:  Class=count  U:update D:destroy/disable
    // These are the classes that work AND the offsets to use in another mod menu.
    int lineNo = 0;
    for (int i = 0; i < autoClsCount && lineNo < 6; i++) {
        if (dbgFound[i] <= 0) continue;
        const AutoClass &ac = autoCls[i];
        char line[220];
        snprintf(line, sizeof(line), "%s=%d  Update 0x%lX  OnDestroy 0x%lX  OnDisable 0x%lX",
                 ac.name.c_str(), (int) dbgFound[i], ac.rvaUpdate, ac.rvaDestroy, ac.rvaDisable);
        EspText(env, espView, canvas, 255, 0, 255, 0, line, cx, h * (0.22f + 0.04f * lineNo), 22);
        lineNo++;
    }
}

JNIEXPORT void JNICALL
Java_esp_killshot_launcher_FloatingModMenuService_DrawOn(JNIEnv *env, jclass clazz,
                                                         jobject espView, jobject canvas) {
    if (espView == NULL || canvas == NULL || !espEnabled) return;

    static jmethodID midW = NULL, midH = NULL;
    if (!midW || !midH) {
        jclass cc = env->GetObjectClass(canvas);
        midW = env->GetMethodID(cc, "getWidth", "()I");
        midH = env->GetMethodID(cc, "getHeight", "()I");
        env->DeleteLocalRef(cc);
    }
    if (!midW || !midH) return;

    float w = (float) env->CallIntMethod(canvas, midW);
    float h = (float) env->CallIntMethod(canvas, midH);
    if (w <= 0 || h <= 0) return;

    float cx = w / 2;
    if (!espFuncsReady) {
        if (espDebug) drawDebug(env, espView, canvas, cx, h, 0, 0);
        return;
    }

    // Unity screen pixels -> overlay canvas pixels
    int uw = unityScreenW, uh = unityScreenH;
    float scaleX = uw > 0 ? w / (float) uw : 1.0f;
    float scaleY = uh > 0 ? h / (float) uh : 1.0f;

    long long now = nowMs();
    std::lock_guard<std::mutex> g(trackMtx);
    int total = 0, shown = 0;
    for (std::map<void *, TrackedObj>::iterator it = trackedObjs.begin(); it != trackedObjs.end();) {
        const TrackedObj &t = it->second;
        if (now - t.lastSeenMs > 2000) { // object stopped updating (and OnDestroy did not fire)
            it = trackedObjs.erase(it);
            continue;
        }
        ++it;
        if (now - t.lastSeenMs > 500) continue; // not updated recently: do not draw
        total++;
        if (!t.hasScreen || t.screen.z <= 0) continue; // behind the camera
        float sx = t.screen.x * scaleX;
        float sy = h - t.screen.y * scaleY; // Unity origin is bottom-left
        if (sx < -50 || sx > w + 50 || sy < -50 || sy > h + 50) continue;
        shown++;
        static const int col[MAX_HOOKS][3] = {{0, 255, 0}, {255, 60, 60}, {255, 255, 0}, {0, 200, 255}};
        const int *c = col[(t.slot >= 0 ? t.slot : 0) % MAX_HOOKS];
        if (espLine)
            EspLine(env, espView, canvas, 255, c[0], c[1], c[2], 3, cx, 0, sx, sy);
        EspCircle(env, espView, canvas, 255, c[0], c[1], c[2], 3, sx, sy, 18);
    }
    if (espDebug) drawDebug(env, espView, canvas, cx, h, total, shown);
    if (nowMs() < reportMsgUntil)
        EspText(env, espView, canvas, 255, 255, 255, 255, reportMsg.c_str(), cx, h * 0.92f, 22);
}

JNIEXPORT jobjectArray
JNICALL
Java_esp_killshot_launcher_FloatingModMenuService_getFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;

    MakeToast(env, context, OBFUSCATE("Modded by Gamer Killshot"), Toast::LENGTH_LONG);

    const char *features[] = {
            OBFUSCATE("Category_ESP Features"),
            OBFUSCATE("1_Toggle_Enable Esp"),
            OBFUSCATE("2_Toggle_ESP Line"),
            OBFUSCATE("Category_ESP Offset"),
            OBFUSCATE("3_InputText_Enter Update Offset"),
            OBFUSCATE("4_InputText_Enter Destroy Offset"),
            OBFUSCATE("5_Toggle_Show Debug Info"),
            OBFUSCATE("6_InputText_Class Names (empty = auto-detect)"),
            OBFUSCATE("7_Button_Save Report (classes + offsets)"),
    };

    int Total_Feature = (sizeof features / sizeof features[0]);
    ret = (jobjectArray)
            env->NewObjectArray(Total_Feature, env->FindClass(OBFUSCATE("java/lang/String")),
                                env->NewStringUTF(""));

    for (int i = 0; i < Total_Feature; i++)
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));

    pthread_t ptid;
    pthread_create(&ptid, NULL, antiLeech, NULL);

    return (ret);
}

JNIEXPORT void JNICALL
Java_esp_killshot_launcher_Preferences_Changes(JNIEnv *env, jclass clazz, jobject obj,
                                        jint featNum, jstring featName, jint value,
                                        jboolean boolean, jstring str) {

    {
        const char *fn = featName != NULL ? env->GetStringUTFChars(featName, 0) : "";
        LOGD(OBFUSCATE("Feature: %d - %s | Value: %d | Bool: %d"), featNum, fn, value, boolean);
        if (featName != NULL) env->ReleaseStringUTFChars(featName, fn);
    }

    switch (featNum) {
        case 1:
            espEnabled = boolean;
            break;
        case 2:
            espLine = boolean;
            break;
        case 5:
            espDebug = boolean;
            break;
        case 7:
            writeReport();
            break;
        case 6:
            if (str != NULL) {
                const char *txt = env->GetStringUTFChars(str, 0);
                {
                    std::lock_guard<std::mutex> g(autoMtx);
                    autoClasses = txt; // empty = auto-detect
                }
                env->ReleaseStringUTFChars(str, txt);
            }
            break;
        case 3:
        case 4:
            if (str != NULL) {
                const char *txt = env->GetStringUTFChars(str, 0);
                {
                    std::lock_guard<std::mutex> g(hookMtx);
                    (featNum == 3 ? updateOffset : destroyOffset) = txt;
                }
                env->ReleaseStringUTFChars(str, txt);
                // applied by hack_thread within a second
            }
            break;
    }
}
}

__attribute__((constructor))
void lib_main() {
    pthread_t ptid;
    pthread_create(&ptid, NULL, hack_thread, NULL);
}

