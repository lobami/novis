// Novis Wasm runtime stub (C-only, no libcxx headers required on wasm32).
// Forwards every print to a host-imported `host_print` that lives in the JS
// shim. We avoid <cstdarg>/<cstdio> because wasm32's clang ships without
// libcxx headers.

extern "C" void host_print(const char* data, int len);

extern "C" int putchar(int c) {
    char ch = (char)c;
    host_print(&ch, 1);
    return c;
}

extern "C" int puts(const char* s) {
    int len = 0;
    while (s[len]) ++len;
    host_print(s, len);
    host_print("\n", 1);
    return len;
}

extern "C" int printf(const char* fmt, ...) {
    int len = 0;
    while (fmt[len]) ++len;
    host_print(fmt, len);
    return len;
}

extern "C" void* memcpy(void* dst, const void* src, int n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (int i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

extern "C" void* memset(void* dst, int c, int n) {
    char* d = (char*)dst;
    for (int i = 0; i < n; ++i) d[i] = (char)c;
    return dst;
}

extern "C" int strlen(const char* s) {
    int n = 0;
    while (s[n]) ++n;
    return n;
}
