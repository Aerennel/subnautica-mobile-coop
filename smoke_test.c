// Smoke test: Dobby inline-hook on a plain ARM64 function (with adrp/bl in prologue).
// Запускается под qemu-aarch64 на хосте, без Android/Unity.
#include <stdio.h>
#include <stdint.h>
#include "dobby.h"

__attribute__((noinline)) int target_func(int a, int b) {
    // намеренно нетривиальное тело, чтобы были PC-relative инструкции
    volatile int x = a * 7 + b;
    return x + 42;
}

static int (*orig_target)(int, int) = NULL;
static int hook_calls = 0;

int fake_target(int a, int b) {
    hook_calls++;
    int r = orig_target(a, b);
    return r + 1000; // маркер, что hook отработал
}

int main() {
    int before = target_func(3, 4);
    printf("before: %d (expect %d)\n", before, 3*7+4+42);

    void *orig = NULL;
    int rc = DobbyHook((void *)target_func, (void *)fake_target, &orig);
    if (rc != 0) {
        printf("DobbyHook FAILED rc=%d\n", rc);
        return 2;
    }
    orig_target = (int (*)(int, int))orig;
    printf("hook installed, orig=%p\n", orig);

    int after = target_func(3, 4);
    printf("after: %d (expect %d = orig+1000), hook_calls=%d\n", after, before+1000, hook_calls);

    if (after == before + 1000 && hook_calls == 1) {
        printf("SMOKE TEST PASS\n");
        return 0;
    }
    printf("SMOKE TEST FAIL\n");
    return 1;
}
