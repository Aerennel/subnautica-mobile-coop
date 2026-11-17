// snmp.c — Subnautica Mobile hello-world mod (ЭТАП 2, A5) v2
// Inline-hook il2cpp_init (Unity main thread) + inline-hook Player.Update. No pthread.
#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

#define LOG_TAG "SNMP"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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

static fn_domain_get p_domain_get;
static fn_domain_get_assemblies p_domain_get_assemblies;
static fn_assembly_get_image p_assembly_get_image;
static fn_class_from_name p_class_from_name;
static fn_class_get_method_from_name p_class_get_method_from_name;
static fn_class_get_field_from_name p_class_get_field_from_name;
static fn_field_static_get_value p_field_static_get_value;
static fn_runtime_invoke p_runtime_invoke;
static fn_string_new p_string_new;

#define PAGE_SIZE 4096
static void* g_il2cpp_base;

static void* find_base(const char* soname) {
    FILE* f=fopen("/proc/self/maps","r"); if(!f) return NULL;
    char line[512]; void* base=NULL;
    while(fgets(line,sizeof line,f)){
        if(strstr(line,soname)&&strstr(line,"r-xp")){
            unsigned long s=0,o=0; sscanf(line,"%lx-%*lx %*4s %lx",&s,&o);
            base=(void*)(s-o); break;
        }
    }
    fclose(f); return base;
}


// ---- ARM64 relocating trampoline ----
static int is_adrp(uint32_t i){ return (i & 0x9f000000u)==0x90000000u; }
static int is_adr (uint32_t i){ return (i & 0x9f000000u)==0x10000000u; }
static int is_bl  (uint32_t i){ return (i & 0xfc000000u)==0x94000000u; }
static int is_b   (uint32_t i){ return (i & 0xfc000000u)==0x14000000u; }
static int is_cb  (uint32_t i){ return (i & 0x7e000000u)==0x34000000u; }
static int is_tb  (uint32_t i){ return (i & 0x7e000000u)==0x36000000u; }
static int is_ldrlit(uint32_t i){ return (i & 0x3b000000u)==0x18000000u; }

static int emit_abs(uint32_t* out, int rd, uint64_t addr){
    out[0]=0xd2800000u | ((addr & 0xffffu)<<5) | rd;
    out[1]=0xf2800000u | (((addr>>16)&0xffffu)<<5) | (1u<<21) | rd;
    out[2]=0xf2800000u | (((addr>>32)&0xffffu)<<5) | (2u<<21) | rd;
    out[3]=0xf2800000u | (((addr>>48)&0xffffu)<<5) | (3u<<21) | rd;
    return 4;
}

static int reloc1(uint32_t insn, uint64_t src, uint32_t* out){
    if(is_adrp(insn) || is_adr(insn)){
        int rd=insn&31;
        uint64_t immlo=(insn>>29)&3, immhi=(insn>>5)&0x7ffff;
        int64_t imm=(int64_t)((immhi<<2)|immlo);
        if(imm & (1LL<<20)) imm -= (1LL<<21);
        uint64_t target;
        if(is_adrp(insn)){ imm <<= 12; target=(src & ~0xfffULL)+imm; }
        else target=src+imm;
        return emit_abs(out, rd, target);
    }
    if(is_bl(insn) || is_b(insn)){
        int64_t imm=(int64_t)(insn & 0x3ffffffu);
        if(imm & (1LL<<25)) imm -= (1LL<<26);
        uint64_t target=src+(imm<<2);
        int n=emit_abs(out,16,target);
        out[n++]= is_bl(insn)?0xd63f0200u:0xd61f0200u;
        return n;
    }
    if(is_cb(insn)){
        int64_t imm=(int64_t)((insn>>5)&0x7ffff);
        if(imm & (1LL<<18)) imm -= (1LL<<19);
        uint64_t target=src+(imm<<2);
        out[0]=(insn & 0xff00001fu) | (5u<<5);
        int n=1+emit_abs(out+1,16,target);
        out[n++]=0xd61f0200u;
        return n;
    }
    if(is_tb(insn)){
        int64_t imm=(int64_t)((insn>>5)&0x3fff);
        if(imm & (1LL<<13)) imm -= (1LL<<14);
        uint64_t target=src+(imm<<2);
        out[0]=(insn & 0xfff8001fu) | (5u<<5);
        int n=1+emit_abs(out+1,16,target);
        out[n++]=0xd61f0200u;
        return n;
    }
    if(is_ldrlit(insn) && (insn & 0x04000000u)==0){
        int64_t imm=(int64_t)((insn>>5)&0x7ffff);
        if(imm & (1LL<<18)) imm -= (1LL<<19);
        uint64_t target=src+(imm<<2);
        int rt=insn&31;
        int n=emit_abs(out,16,target);
        out[n++]=(insn & 0x40000000u)?(0xf9400000u|(16<<5)|rt):(0xb9400000u|(16<<5)|rt);
        return n;
    }
    out[0]=insn;
    return 1;
}

#define STOLEN 16
static void* hook(void* target, void* replacement) {
    void* tramp = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE|PROT_EXEC,
                       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if(tramp==MAP_FAILED){ LOGE("tramp mmap"); return NULL; }
    uint32_t* out=(uint32_t*)tramp;
    int n=0;
    for(int off=0; off<STOLEN; off+=4){
        uint32_t insn=*(uint32_t*)((char*)target+off);
        uint64_t src=(uint64_t)target+off;
        n+=reloc1(insn, src, out+n);
    }
    n+=emit_abs(out+n,16,(uint64_t)target+STOLEN);
    out[n++]=0xd61f0200u;
    __builtin___clear_cache((char*)tramp,(char*)tramp+n*4);
    uintptr_t tp=(uintptr_t)target & ~(PAGE_SIZE-1);
    mprotect((void*)tp,PAGE_SIZE*2,PROT_READ|PROT_WRITE|PROT_EXEC);
    uint32_t* p=(uint32_t*)target;
    p[0]=0x58000050u; p[1]=0xd61f0200u;
    *(void**)((char*)target+8)=replacement;
    __builtin___clear_cache((char*)target,(char*)target+16);
    mprotect((void*)tp,PAGE_SIZE*2,PROT_READ|PROT_EXEC);
    return tramp;
}

static Il2CppDomain* g_dom;
static Il2CppClass* find_class(const char* ns, const char* name) {
    size_t n = 0;
    Il2CppAssembly** asmbs = p_domain_get_assemblies(g_dom, &n);
    for (size_t i = 0; i < n; i++) {
        Il2CppImage* img = p_assembly_get_image(asmbs[i]);
        if (!img) continue;
        Il2CppClass* c = p_class_from_name(img, ns, name);
        if (c) return c;
    }
    return NULL;
}

static void show_message(const char* text) {
    Il2CppClass* em = find_class("", "ErrorMessage");
    if (!em) return;
    Il2CppMethod* add = p_class_get_method_from_name(em, "AddMessage", 1);
    if (!add) return;
    void* args[1] = { p_string_new(text) };
    p_runtime_invoke(add, NULL, args, NULL);
}

static int get_player_pos(Vector3* out) {
    Il2CppClass* player = find_class("", "Player");
    if (!player) return 0;
    Il2CppField* mainf = p_class_get_field_from_name(player, "main");
    if (!mainf) return 0;
    void* playerObj = NULL;
    p_field_static_get_value(mainf, &playerObj);
    if (!playerObj) return 0;
    Il2CppClass* component = find_class("UnityEngine", "Component");
    Il2CppMethod* get_transform = p_class_get_method_from_name(component, "get_transform", 0);
    void* transform = p_runtime_invoke(get_transform, playerObj, NULL, NULL);
    if (!transform) return 0;
    Il2CppClass* transformC = find_class("UnityEngine", "Transform");
    Il2CppMethod* get_position = p_class_get_method_from_name(transformC, "get_position", 0);
    void* boxed = p_runtime_invoke(get_position, transform, NULL, NULL);
    if (!boxed) return 0;
    memcpy(out, (char*)boxed + 16, sizeof(Vector3));
    return 1;
}

typedef void (*PlayerUpdate_t)(void* self, void* method);
static PlayerUpdate_t orig_PlayerUpdate;
static time_t g_lastLog = 0;

static void PlayerUpdate_hook(void* self, void* method) {
    orig_PlayerUpdate(self, method);
    time_t now = time(NULL);
    if (now - g_lastLog >= 5) {
        g_lastLog = now;
        Vector3 p;
        if (get_player_pos(&p)) {
            char buf[160];
            snprintf(buf, sizeof buf, "[SNMP] pos %.1f %.1f %.1f", p.x, p.y, p.z);
            LOGI("%s", buf);
            show_message(buf);
        }
    }
}

static void hook_player_update(void) {
    Il2CppClass* player = find_class("", "Player");
    if (!player) { LOGE("Player class not found"); return; }
    Il2CppMethod* m = p_class_get_method_from_name(player, "Update", 0);
    if (!m) { LOGE("Player.Update not found"); return; }
    void* code = *(void**)m;
    if (!code) { LOGE("Player.Update no code ptr"); return; }
    orig_PlayerUpdate = (PlayerUpdate_t)hook(code, (void*)PlayerUpdate_hook);
    if (orig_PlayerUpdate) LOGI("Player.Update hooked @%p", code);
}

typedef void (*il2cpp_init_t)(const char*);
static il2cpp_init_t orig_init;

static void il2cpp_init_hook(const char* domain_name) {
    orig_init(domain_name);
    LOGI("il2cpp_init done; resolving classes");
    g_dom = p_domain_get();
    if (!g_dom) { LOGE("domain still null"); return; }
    LOGI("SNMP loader alive (domain=%p)", g_dom);
    show_message("[SNMP] loader alive");
    hook_player_update();
}

static int resolve_api(void) {
    void* h = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h) h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { LOGE("dlopen il2cpp failed: %s", dlerror()); return 0; }
#define R(name) p_##name=(fn_##name)dlsym(h,"il2cpp_"#name); if(!p_##name){LOGE("no sym %s",#name);return 0;}
    R(domain_get) R(domain_get_assemblies) R(assembly_get_image)
    R(class_from_name) R(class_get_method_from_name) R(class_get_field_from_name)
    R(field_static_get_value) R(runtime_invoke) R(string_new)
#undef R
    return 1;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("=== SNMP mod v2 loaded (hello-world, versionCode 83811) ===");
    if (!resolve_api()) return JNI_VERSION_1_6;
    g_il2cpp_base = find_base("libil2cpp.so");
    if (!g_il2cpp_base) { LOGE("no il2cpp base"); return JNI_VERSION_1_6; }
    void* init_addr = (char*)g_il2cpp_base + 0x25b3bec;
    orig_init = (il2cpp_init_t)hook(init_addr, (void*)il2cpp_init_hook);
    if (orig_init) LOGI("il2cpp_init hooked @%p", init_addr);
    else LOGE("failed to hook il2cpp_init");
    return JNI_VERSION_1_6;
}
