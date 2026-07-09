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
}  // extern "C"

// Helper used by Evaluator::call_zynta_db_query (in src/evaluator.h).
// The C-side shim zynta_db_query_impl heap-allocates a
// zynta::ValuePtr (a std::shared_ptr<zynta::Value>) and returns it as
// a void*. We downcast, convert to a novis::Value via from_zynta, and
// free the heap pointer.
//
// We put this here (not in evaluator.h) because the body needs to
// see both ::Value and zynta::ValuePtr at a point where the include
// order makes one of them invisible. Defining it in a .cpp file
// after both headers are processed sidesteps the include-order
// gymnastics.
Value zynta_value_from_packed(void* packed) {
    auto* sp = static_cast<std::shared_ptr<zynta::Value>*>(packed);
    zynta::ValuePtr zv = *sp;
    delete sp;
    return zynta_runtime::from_zynta(zv);
}
