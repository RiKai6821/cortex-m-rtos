#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H
#include <stdio.h>

static int g_checks = 0, g_fails = 0;

#define GROUP(name) printf("\n[ %s ]\n", (name))

#define CHECK(cond) do {                                            \
    g_checks++;                                                     \
    if (!(cond)) { g_fails++;                                       \
        printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); }  \
    else printf("  ok   %s\n", #cond);                             \
} while (0)

#define TEST_SUMMARY() ( printf("\n%s: %d checks, %d failed\n",      \
        g_fails ? "FAILED" : "PASSED", g_checks, g_fails),         \
        g_fails ? 1 : 0 )

#endif
