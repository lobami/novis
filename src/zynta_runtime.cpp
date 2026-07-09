// =============================================================================
// zynta runtime — non-inline shims
// =============================================================================
// The evaluator in novis calls these as plain C-linkage functions. They
// live here (not in zynta_runtime.h) because inline + extern "C" doesn't
// emit a strong external symbol on macOS/clang, and we want a real
// linkable function the novis binary can find.

#include "zynta_runtime.h"

extern "C" {
int zynta_app_new_impl() {
    return zynta_runtime::zynta_app_new();
}
int zynta_route_impl(int app, const std::string& method,
                     const std::string& path, const std::string& fn) {
    return zynta_runtime::zynta_route(app, method, path, fn);
}
int zynta_run_impl(int app, const std::string& host, int port) {
    return zynta_runtime::zynta_run(app, host, port);
}
void* zynta_json_parse_impl(const std::string& s) {
    return new ::Value(zynta_runtime::zynta_json_parse(s));
}
std::string zynta_json_stringify_impl(const void* v) {
    const ::Value* vp = static_cast<const ::Value*>(v);
    return zynta_runtime::zynta_json_stringify(*vp);
}
}
