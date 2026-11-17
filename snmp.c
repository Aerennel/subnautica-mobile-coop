// SNMP — Subnautica Mobile Native Plugin (hello-world probe) v3 — ShadowHook
#include <android/log.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "dobby.h"

#define TAG "SNMP"
#define RVA_RUNTIME_INVOKE_BODY   0x260c8a4   // real body of il2cpp_runtime_invoke (thunk target)
#define RVA_PLAYER_UPDATE         0x2ab8aa0

static void *g_il2cpp_base = NULL;
static void *(*p_domain_get)(void) = NULL;
static void *(*p_class_from_name)(const void *, const char *, const char *) = NULL;
static void *(*p_class_get_field_from_name)(const void *, const char *) = NULL;
static void (*p_field_static_get_value)(const void *, void *) = NULL;
static void *(*p_class_get_method_from_name)(const void *, const char *, int) = NULL;
static void *(*p_runtime_invoke)(const void *, void *, void **, void **) = NULL;
static void *(*p_string_new)(const char *) = NULL;
// new: image enumeration (class_from_name(NULL,...) crashes this IL2CPP build)
static void *(*p_domain_get_assemblies)(void *, void *) = NULL;
static void *(*p_assembly_get_image)(void *) = NULL;

// find_class: iterate all assembly images, class_from_name on each.
// il2cpp_class_from_name(image, ns, name) — image MUST be valid (NULL segfaults here).
static void *find_class(const char *ns, const char *name) {
    if (!p_domain_get || !p_domain_get_assemblies || !p_assembly_get_image) return NULL;
    void *domain = p_domain_get();
    if (!domain) return NULL;
    size_t count = 0;
    void **assemblies = (void **)p_domain_get_assemblies(domain, &count);
    if (!assemblies) return NULL;
    for (size_t i = 0; i < count; i++) {
        void *image = p_assembly_get_image(assemblies[i]);
        if (!image) continue;
        void *k = p_class_from_name(image, ns, name);
        if (k) return k;
    }
    return NULL;
}

static int g_player_hooked = 0;
static void (*Player_Update_orig)(void *self) = NULL;
static double g_last = 0;
static int g_msg_ok = 0;

static void Player_Update_hook(void *self) {
    Player_Update_orig(self);
    if (!self) return;
    // cache classes once (find_class walks images — don't do it every frame)
    static void *kPlayer = NULL, *kTransform = NULL, *kErrorMsg = NULL;
    static int resolved = 0;
    if (!resolved) {
        kPlayer   = find_class("", "Player");
        kTransform= find_class("UnityEngine", "Transform");
        kErrorMsg = find_class("", "ErrorMessage");
        resolved = 1;
        __android_log_print(ANDROID_LOG_INFO, TAG, "resolve: Player=%p Transform=%p ErrorMessage=%p", kPlayer, kTransform, kErrorMsg);
    }
    void *player = NULL;
    void *klass = kPlayer;
    if (!klass) { __android_log_print(ANDROID_LOG_INFO, TAG, "no Player class"); return; }
    // Player.main is a static FIELD (dump.cs: public static Player main; // 0x8)
    // correct API: class_get_field_from_name -> field_static_get_value (class_get_static_field doesn't exist here)
    void *fld_main = p_class_get_field_from_name(klass, "main");
    if (fld_main && p_field_static_get_value) p_field_static_get_value(fld_main, &player);
    if (!player) return;
    const void *get_tr = p_class_get_method_from_name(klass, "get_transform", 0);
    void *tr = get_tr ? p_runtime_invoke(get_tr, player, NULL, NULL) : NULL;
    if (!tr) return;
    void *trk = kTransform;
    const void *get_pos = trk ? p_class_get_method_from_name(trk, "get_position", 0) : NULL;
    void *vbox = get_pos ? p_runtime_invoke(get_pos, tr, NULL, NULL) : NULL;

    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = ts.tv_sec + ts.tv_nsec / 1e9;
    if (now - g_last < 5.0) return;
    g_last = now;

    void *emk = kErrorMsg;
    const void *addm = emk ? p_class_get_method_from_name(emk, "AddMessage", 1) : NULL;
    if (!g_msg_ok) {
        if (addm) {
            void *exc = NULL;
            p_runtime_invoke(addm, NULL, (void *[]){p_string_new("[SNMP] loader alive (hello-world OK)")}, &exc);
            g_msg_ok = 1;
            __android_log_print(ANDROID_LOG_INFO, TAG, "SNMP loader alive (domain=%p)", p_domain_get ? p_domain_get() : NULL);
        }
    }
    if (vbox) {
        float *v = (float *)((char *)vbox + 16);
        __android_log_print(ANDROID_LOG_INFO, TAG, "[SNMP] pos %.2f %.2f %.2f", v[0], v[1], v[2]);
        if (addm) {
            char buf[96];
            snprintf(buf, sizeof(buf), "[SNMP] pos %.1f %.1f %.1f", v[0], v[1], v[2]);
            void *exc = NULL;
            p_runtime_invoke(addm, NULL, (void *[]){p_string_new(buf)}, &exc);
        }
    }
}

// il2cpp_runtime_invoke(const MethodInfo* method, void* obj, void** params, void** exc)
static void *(*RI_orig)(const void *, void *, void **, void **) = NULL;
static int g_ready = 0;

static void *RI_hook(const void *method, void *obj, void **params, void **exc) {
    if (!g_ready) {
        g_ready = 1;
        void *domain = p_domain_get ? p_domain_get() : NULL;
        __android_log_print(ANDROID_LOG_INFO, TAG, "SNMP loader alive (domain=%p)", domain);
        void *target = (char *)g_il2cpp_base + RVA_PLAYER_UPDATE;
        void *orig = NULL;
        if (DobbyHook(target, (void *)Player_Update_hook, &orig) != 0) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "Player.Update DobbyHook FAILED");
        } else {
            Player_Update_orig = (void (*)(void *))orig;
            g_player_hooked = 1;
            __android_log_print(ANDROID_LOG_INFO, TAG, "Player.Update hooked @%p orig=%p", target, orig);
        }
    }
    return RI_orig(method, obj, params, exc);
}

__attribute__((constructor)) static void snmp_init(void) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "=== SNMP mod v7 (Dobby, field-static fix) loaded ===");
    void *h = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h) h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { __android_log_print(ANDROID_LOG_ERROR, TAG, "no libil2cpp: %s", dlerror()); return; }
    void *sym = dlsym(h, "il2cpp_domain_get_assemblies");
    Dl_info di;
    if (sym && dladdr(sym, &di)) g_il2cpp_base = di.dli_fbase;
    if (!g_il2cpp_base) { __android_log_print(ANDROID_LOG_ERROR, TAG, "no il2cpp base"); return; }
#define SYM(n) p_##n = dlsym(h, "il2cpp_" #n)
    SYM(domain_get); SYM(class_from_name); SYM(class_get_field_from_name);
    SYM(field_static_get_value);
    SYM(class_get_method_from_name); SYM(runtime_invoke); SYM(string_new);
    SYM(domain_get_assemblies); SYM(assembly_get_image);
#undef SYM
    __android_log_print(ANDROID_LOG_INFO, TAG, "il2cpp base=%p", g_il2cpp_base);
    void *target = (char *)g_il2cpp_base + RVA_RUNTIME_INVOKE_BODY;
    void *orig = NULL;
    if (DobbyHook(target, (void *)RI_hook, &orig) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "runtime_invoke DobbyHook FAILED");
    } else {
        RI_orig = (void *(*)(const void *, void *, void **, void **))orig;
        __android_log_print(ANDROID_LOG_INFO, TAG, "runtime_invoke hooked @%p orig=%p", target, orig);
    }
}
