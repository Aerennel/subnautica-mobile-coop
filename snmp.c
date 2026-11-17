// snmp.c — Subnautica Mobile hello-world mod (ЭТАП 2, A5)
// Только диагностика: лог + координаты игрока раз в ~5с. Никакого мультиплеера.
// Подход: il2cpp API по именам (A4), RVA — только fallback с проверкой versionCode.
// Без inline-хуков: фоновый поток опрашивает Player.main -> transform -> position.
#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define LOG_TAG "SNMP"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ---- il2cpp API types ----
typedef struct { float x, y, z; } Vector3;
typedef void Il2CppDomain, Il2CppAssembly, Il2CppImage, Il2CppClass, Il2CppMethod, Il2CppObject, Il2CppField;

typedef Il2CppDomain* (*fn_domain_get)(void);
typedef Il2CppAssembly** (*fn_domain_get_assemblies)(Il2CppDomain*, size_t*);
typedef Il2CppImage* (*fn_assembly_get_image)(Il2CppAssembly*);
typedef Il2CppClass* (*fn_class_from_name)(Il2CppImage*, const char*, const char*);
typedef Il2CppMethod* (*fn_class_get_method_from_name)(Il2CppClass*, const char*, int);
typedef Il2CppField* (*fn_class_get_field_from_name)(Il2CppClass*, const char*);
typedef void (*fn_field_static_get_value)(Il2CppField*, void*);
typedef void* (*fn_runtime_invoke)(Il2CppMethod*, void*, void**, void*);
typedef Il2CppObject* (*fn_string_new)(const char*);
typedef void* (*fn_thread_attach)(Il2CppDomain*);

static fn_domain_get p_domain_get;
static fn_domain_get_assemblies p_domain_get_assemblies;
static fn_assembly_get_image p_assembly_get_image;
static fn_class_from_name p_class_from_name;
static fn_class_get_method_from_name p_class_get_method_from_name;
static fn_class_get_field_from_name p_class_get_field_from_name;
static fn_field_static_get_value p_field_static_get_value;
static fn_runtime_invoke p_runtime_invoke;
static fn_string_new p_string_new;
static fn_thread_attach p_thread_attach;

static int g_api_ready = 0;

// Resolve il2cpp exports from the already-loaded libil2cpp.so (no hardcoded RVA).
static int resolve_api(void) {
    void* h = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h) h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { LOGE("dlopen libil2cpp.so failed: %s", dlerror()); return 0; }
#define R(name) p_##name = (fn_##name)dlsym(h, "il2cpp_" #name); if(!p_##name){LOGE("no sym il2cpp_%s",#name);return 0;}
    R(domain_get) R(domain_get_assemblies) R(assembly_get_image)
    R(class_from_name) R(class_get_method_from_name) R(class_get_field_from_name)
    R(field_static_get_value) R(runtime_invoke) R(string_new) R(thread_attach)
#undef R
    g_api_ready = 1;
    LOGI("il2cpp API resolved");
    return 1;
}

// Find a class across all loaded assemblies by namespace+name.
static Il2CppClass* find_class(Il2CppDomain* dom, const char* ns, const char* name) {
    size_t n = 0;
    Il2CppAssembly** asmbs = p_domain_get_assemblies(dom, &n);
    for (size_t i = 0; i < n; i++) {
        Il2CppImage* img = p_assembly_get_image(asmbs[i]);
        if (!img) continue;
        Il2CppClass* c = p_class_from_name(img, ns, name);
        if (c) return c;
    }
    return NULL;
}

static Il2CppDomain* g_dom;

// Show a message in-game via ErrorMessage.AddMessage(string)
static void show_message(const char* text) {
    Il2CppClass* em = find_class(g_dom, "", "ErrorMessage");
    if (!em) { LOGE("ErrorMessage class not found"); return; }
    Il2CppMethod* add = p_class_get_method_from_name(em, "AddMessage", 1);
    if (!add) { LOGE("AddMessage not found"); return; }
    Il2CppObject* str = p_string_new(text);
    void* args[1] = { str };
    p_runtime_invoke(add, NULL, args, NULL);
}

// Read player position; return 1 on success.
static int get_player_pos(Vector3* out) {
    Il2CppClass* player = find_class(g_dom, "", "Player");
    if (!player) return 0;
    Il2CppField* mainf = p_class_get_field_from_name(player, "main");
    if (!mainf) return 0;
    void* playerObj = NULL;
    p_field_static_get_value(mainf, &playerObj);
    if (!playerObj) return 0; // not in game yet

    // Player : Living : ... : MonoBehaviour -> Component.get_transform()
    Il2CppClass* component = find_class(g_dom, "UnityEngine", "Component");
    if (!component) return 0;
    Il2CppMethod* get_transform = p_class_get_method_from_name(component, "get_transform", 0);
    if (!get_transform) return 0;
    void* transform = p_runtime_invoke(get_transform, playerObj, NULL, NULL);
    if (!transform) return 0;

    Il2CppClass* transformC = find_class(g_dom, "UnityEngine", "Transform");
    if (!transformC) return 0;
    Il2CppMethod* get_position = p_class_get_method_from_name(transformC, "get_position", 0);
    if (!get_position) return 0;
    void* boxed = p_runtime_invoke(get_position, transform, NULL, NULL);
    if (!boxed) return 0;
    // boxed Vector3: data starts after object header (2 pointers = 16 bytes on arm64)
    memcpy(out, (char*)boxed + 16, sizeof(Vector3));
    return 1;
}

static void* worker(void* _) {
    LOGI("worker started, waiting for il2cpp init");
    // wait until il2cpp domain exists (game initialized)
    for (int i = 0; i < 120; i++) { // up to ~60s
        if (!g_api_ready) { if (!resolve_api()) { usleep(500000); continue; } }
        g_dom = p_domain_get();
        if (g_dom) break;
        usleep(500000);
    }
    if (!g_dom) { LOGE("il2cpp domain never appeared"); return NULL; }
    p_thread_attach(g_dom);
    LOGI("SNMP loader alive, domain=%p", g_dom);
    show_message("[SNMP] loader alive");

    int tick = 0;
    while (1) {
        Vector3 p;
        if (get_player_pos(&p)) {
            char buf[160];
            snprintf(buf, sizeof buf, "[SNMP] pos %.1f %.1f %.1f", p.x, p.y, p.z);
            LOGI("%s", buf);
            if (tick % 1 == 0) show_message(buf); // every loop (~5s)
        } else {
            if (tick % 6 == 0) LOGI("player not ready (menu?)");
        }
        tick++;
        usleep(5000000); // 5s
    }
    return NULL;
}

// Entry: called from smali via System.loadLibrary("snmp") -> JNI_OnLoad
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("=== SNMP mod loaded (hello-world, versionCode 83811) ===");
    pthread_t t;
    pthread_create(&t, NULL, worker, NULL);
    pthread_detach(t);
    return JNI_VERSION_1_6;
}
