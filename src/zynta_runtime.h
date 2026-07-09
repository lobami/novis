#pragma once

// =============================================================================
// zynta runtime for novis
// =============================================================================
// This header is the bridge between novis's tree-walking evaluator and the
// zynta C++ HTTP server. The five zynta builtins are registered in the
// evaluator's global environment and dispatch to handlers in this file:
//
//   zynta_app_new()       -> int (handle)
//   zynta_route(handle, method, path, fn_name)
//   zynta_run(handle, host, port)
//   zynta_json_parse(s)   -> dict
//   zynta_json_stringify(d) -> str
//
// The fn_name in zynta_route must be the name of a novis `fn` defined at
// the top level. We store the (fn_name, method, path) triple in a
// thread-local zynta::Server instance, then on each request we look up
// the named function via Evaluator::call_named_function and serialize the
// returned dict as JSON.
//
// We deliberately keep this file header-only because the rest of novis is
// header-only and adding a .cpp would require touching the Makefile more
// than needed. The zynta HTTP code (zynta_http.h) is also header-only.

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "evaluator.h"

// Pull in zynta from the sibling project. The Makefile is responsible
// for making ../zynta/include available; if zynta isn't present this
// header will fail to compile, which is the intended behavior — the
// novis binary only knows about zynta when zynta is a sibling repo.
#include "zynta_http.h"
#include "zynta_json.h"
#include "zynta_value.h"

namespace zynta_runtime {

// The zynta::ValuePtr/Value type from zynta's headers is *not* the same
// as the novis `Value` (std::variant). We bridge by writing tiny
// to_zynta_value / from_novis_value helpers below. The two type systems
// are intentionally similar (Dict, Array, String, Int, Double, Bool) so
// the conversion is straightforward.

inline zynta::ValuePtr to_zynta(const ::Value& v);
inline ::Value from_zynta(const zynta::ValuePtr& v);

// The route table: one row per registered (app_handle, method, path, fn).
// fn_name is the novis function name to call.
struct NovisRoute {
    std::string method;
    std::string path;
    std::string fn_name;
};

struct AppState {
    int handle = -1;
    std::vector<NovisRoute> routes;
};

// We keep the table as a function-local static so the builtin
// implementations can reach it without a global mutable singleton. The
// std::mutex guards concurrent route registration; the zynta::Server
// already has its own worker thread pool and serializes access to
// handlers internally.
inline std::mutex& app_mutex() {
    static std::mutex m;
    return m;
}
inline std::map<int, std::shared_ptr<AppState>>& app_table() {
    static std::map<int, std::shared_ptr<AppState>> t;
    return t;
}
inline int& next_app_handle() {
    static int h = 1;
    return h;
}
inline std::shared_ptr<::Evaluator>& active_evaluator() {
    static std::shared_ptr<::Evaluator> e;
    return e;
}

// ----- zynta Value <-> novis Value bridges ---------------------------------

inline zynta::ValuePtr to_zynta(const ::Value& v) {
    // The novis Value variant is documented in src/evaluator.h. The order
    // of alternatives matters because we use std::visit below.
    struct Visitor {
        zynta::ValuePtr operator()(int64_t i) const      { return zynta::Value::make_int(i); }
        zynta::ValuePtr operator()(double d) const       { return zynta::Value::make_double(d); }
        zynta::ValuePtr operator()(const std::string& s) const {
            return zynta::Value::make_string(s);
        }
        zynta::ValuePtr operator()(bool b) const         { return zynta::Value::make_bool(b); }
        zynta::ValuePtr operator()(const ::Decimal& d) const {
            // Decimal -> double (lossy but simple; zynta never sees Decimals
            // because the novis-side handlers operate on dict literals).
            return zynta::Value::make_double(d.units / double(::Decimal::SCALE_FACTOR));
        }
        zynta::ValuePtr operator()(const ::Money& m) const {
            zynta::ValuePtr obj = zynta::Value::make_dict({
                {"currency", zynta::Value::make_string(m.currency)},
                {"amount",   zynta::Value::make_double(
                                  m.amount.units / double(::Decimal::SCALE_FACTOR))},
            });
            return obj;
        }
        zynta::ValuePtr operator()(const ::Tensor& t) const {
            zynta::ValuePtr arr = zynta::Value::make_array({});
            for (double d : t.data) arr->data = std::vector<zynta::ValuePtr>{};
            // Re-fill (we can't push_back to the variant directly).
            std::vector<zynta::ValuePtr> items;
            items.reserve(t.data.size());
            for (double d : t.data) items.push_back(zynta::Value::make_double(d));
            return zynta::Value::make_array(std::move(items));
        }
        zynta::ValuePtr operator()(const ::DictPtr& dp) const {
            if (!dp) return zynta::Value::make_dict({});
            zynta::Dict out;
            for (const auto& [k, vptr] : dp->entries) {
                auto vp = std::static_pointer_cast<::Value>(vptr);
                out.emplace(k, to_zynta(*vp));
            }
            return zynta::Value::make_dict(std::move(out));
        }
        zynta::ValuePtr operator()(const std::shared_ptr<::UserFunction>&) const {
            return zynta::Value::make_string("<function>");
        }
        zynta::ValuePtr operator()(const std::shared_ptr<::InterpreterTask>&) const {
            return zynta::Value::make_string("<task>");
        }
    };
    return std::visit(Visitor{}, v);
}

inline ::Value from_zynta(const zynta::ValuePtr& v) {
    if (!v) return ::Value{static_cast<int64_t>(0)};
    switch (v->kind) {
        case zynta::Value::Kind::Null:   return ::Value{static_cast<int64_t>(0)};
        case zynta::Value::Kind::Bool:   return ::Value{std::get<bool>(v->data)};
        case zynta::Value::Kind::Int:    return ::Value{std::get<int64_t>(v->data)};
        case zynta::Value::Kind::Double: return ::Value{std::get<double>(v->data)};
        case zynta::Value::Kind::String: {
            auto s = std::get<std::string>(v->data);
            return ::Value{std::move(s)};
        }
        case zynta::Value::Kind::Array: {
            // Recursive: from_zynta is recursive on each element.
            // But the novis Value variant doesn't carry std::vector<Value> —
            // we represent arrays of unknown shape as a Tensor with 1-D
            // shape. The user can iterate it with Tensor primitives.
            const auto& a = std::get<std::vector<zynta::ValuePtr>>(v->data);
            ::Tensor t;
            t.data.reserve(a.size());
            for (const auto& item : a) {
                if (item && item->kind == zynta::Value::Kind::Double) {
                    t.data.push_back(std::get<double>(item->data));
                } else if (item && item->kind == zynta::Value::Kind::Int) {
                    t.data.push_back((double)std::get<int64_t>(item->data));
                } else {
                    t.data.push_back(0.0);
                }
            }
            t.shape = {t.data.size()};
            return ::Value{std::move(t)};
        }
        case zynta::Value::Kind::Dict: {
            // Build a ::Dict with type-erased shared_ptr<void> values.
            const auto& d = std::get<zynta::Dict>(v->data);
            ::DictPtr out = std::make_shared<::Dict>();
            for (const auto& [k, val] : d) {
                auto vp = std::make_shared<::Value>(from_zynta(val));
                out->entries.emplace(k, std::static_pointer_cast<void>(vp));
            }
            return ::Value{out};
        }
    }
    return ::Value{static_cast<int64_t>(0)};
}

// ----- Builtin implementations --------------------------------------------

// zynta_app_new() -> int
inline int zynta_app_new() {
    std::lock_guard<std::mutex> lk(app_mutex());
    int h = next_app_handle()++;
    app_table()[h] = std::make_shared<AppState>();
    app_table()[h]->handle = h;
    return h;
}

// zynta_route(app, method, path, fn_name)
inline int zynta_route(int app, const std::string& method,
                       const std::string& path, const std::string& fn_name) {
    std::lock_guard<std::mutex> lk(app_mutex());
    auto it = app_table().find(app);
    if (it == app_table().end()) {
        throw std::runtime_error("zynta_route: invalid app handle " + std::to_string(app));
    }
    it->second->routes.push_back({method, path, fn_name});
    return 0;
}

// Convert a novis Dict back into a JSON object string we can ship to the
// zynta HTTP server's req.json_body. We do this by serialising the novis
// Dict to a zynta::ValuePtr and then calling zynta::json_stringify on it.
inline std::string novis_dict_to_json(const ::Value& v) {
    if (auto* dp = std::get_if<::DictPtr>(&v)) {
        if (!*dp) return "{}";
        zynta::ValuePtr zv = to_zynta(v);
        return zynta::json_stringify(zv, zynta::JsonStyle::Compact);
    }
    return "{}";
}

// zynta_run(app, host, port) -> blocks until the server stops
inline int zynta_run(int app, const std::string& host, int port) {
    std::shared_ptr<AppState> state;
    {
        std::lock_guard<std::mutex> lk(app_mutex());
        auto it = app_table().find(app);
        if (it == app_table().end()) {
            throw std::runtime_error("zynta_run: invalid app handle");
        }
        state = it->second;
    }
    auto server = std::make_shared<zynta::Server>();
    auto routes = state->routes;  // copy: workers run without holding the mutex
    for (const auto& r : routes) {
        std::string method = r.method;
        std::string path = r.path;
        std::string fn_name = r.fn_name;
        server->router().add(method, path, [fn_name](const zynta::Request& req) {
            // Build the novis-side request value: a Dict with method, path,
            // body, and parsed JSON (as a Dict).
            ::DictPtr d = std::make_shared<::Dict>();
            d->entries["method"] = std::static_pointer_cast<void>(
                std::make_shared<::Value>(::Value{std::string(req.method)}));
            d->entries["path"] = std::static_pointer_cast<void>(
                std::make_shared<::Value>(::Value{std::string(req.path)}));
            d->entries["query"] = std::static_pointer_cast<void>(
                std::make_shared<::Value>(::Value{std::string(req.query)}));
            d->entries["body"] = std::static_pointer_cast<void>(
                std::make_shared<::Value>(::Value{std::string(req.body)}));
            if (req.json_body) {
                // Convert the zynta JSON value back to a novis Dict.
                ::Value body_novis = from_zynta(req.json_body);
                d->entries["json"] = std::static_pointer_cast<void>(
                    std::make_shared<::Value>(std::move(body_novis)));
            }
            ::Value req_val{d};
            auto ev = active_evaluator();
            if (!ev) {
                return zynta::Response::json(500, "Internal Server Error",
                    zynta::Value::make_string("no active novis evaluator"));
            }
            try {
                ::Value ret = ev->call_named_function(fn_name, {req_val});
                // The returned value is expected to be a Dict. Serialise as JSON.
                zynta::ValuePtr zv = to_zynta(ret);
                int status = 200;
                if (zv->kind == zynta::Value::Kind::Dict) {
                    auto& dd = std::get<zynta::Dict>(zv->data);
                    auto it2 = dd.find("status");
                    if (it2 != dd.end() && it2->second &&
                        it2->second->kind == zynta::Value::Kind::Int) {
                        status = (int)std::get<int64_t>(it2->second->data);
                        dd.erase(it2);
                    }
                }
                return zynta::Response::json(status, status == 200 ? "OK" : "Created", zv);
            } catch (const std::exception& e) {
                zynta::ValuePtr err = zynta::Value::make_dict({
                    {"error", zynta::Value::make_string(e.what())},
                });
                return zynta::Response::json(500, "Internal Server Error", err);
            }
        });
    }
    server->run(host, port);
    return 0;
}

// zynta_json_parse(s) -> novis Dict
inline ::Value zynta_json_parse(const std::string& s) {
    auto zv = zynta::json_parse(s);
    return from_zynta(zv);
}

// zynta_json_stringify(d) -> str (renders a novis Dict as JSON)
inline std::string zynta_json_stringify(const ::Value& v) {
    return novis_dict_to_json(v);
}

// Plain C-linkage shims the evaluator calls into (see the call_zynta_*
// methods in evaluator.h). The actual definitions live in
// src/zynta_runtime.cpp because we need real external symbols the
// linker can find — `inline` + `extern "C"` doesn't reliably emit
// strong symbols on macOS/clang.
extern "C" {
int zynta_app_new_impl();
int zynta_route_impl(int app, const std::string& method,
                     const std::string& path, const std::string& fn);
int zynta_run_impl(int app, const std::string& host, int port);
void* zynta_json_parse_impl(const std::string& s);
std::string zynta_json_stringify_impl(const void* v);
}

// Register all zynta builtins into the novis global environment. Called
// once at startup from the `novis zynta-serve` driver path.
inline void register_zynta_builtins(::Evaluator& eval) {
    active_evaluator() = std::shared_ptr<::Evaluator>(&eval, [](::Evaluator*){});
    // The novis evaluator uses sentinel string keys to dispatch builtins;
    // matching happens in visitCallExpr on the string "__builtin___<name>__".
    // For zynta we hook into the same mechanism: each registered name has
    // a sentinel value that the visitCallExpr branch routes to a handler
    // we add in a follow-up step.
    auto* g = eval.current_scope().get();
    g->define("zynta_app_new",   ::Value{std::string("__builtin___zynta_app_new__")});
    g->define("zynta_route",     ::Value{std::string("__builtin___zynta_route__")});
    g->define("zynta_run",       ::Value{std::string("__builtin___zynta_run__")});
    g->define("zynta_json_parse",::Value{std::string("__builtin___zynta_json_parse__")});
    g->define("zynta_json_stringify",
              ::Value{std::string("__builtin___zynta_json_stringify__")});
}

} // namespace zynta_runtime
