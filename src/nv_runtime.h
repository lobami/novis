#pragma once

// =============================================================================
// Novis native runtime (header-only, no Python, no external deps).
// =============================================================================
// This is the runtime embedded into every native binary emitted by the
// Novis C++ backend (`src/native.h`). It is intentionally kept inside
// `evaluator.h` so the interpreter and the native codegen share semantics.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "evaluator.h"

namespace nvrt {

// --- helpers ---------------------------------------------------------------
inline double nv_to_double(const Value& v) {
    if (std::holds_alternative<int64_t>(v))  return static_cast<double>(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))   return std::get<double>(v);
    if (std::holds_alternative<bool>(v))     return std::get<bool>(v) ? 1.0 : 0.0;
    if (std::holds_alternative<Decimal>(v))  return static_cast<double>(std::get<Decimal>(v).units) / Decimal::SCALE_FACTOR;
    if (std::holds_alternative<Money>(v))    return static_cast<double>(std::get<Money>(v).amount.units) / Decimal::SCALE_FACTOR;
    return 0.0;
}

inline bool nv_truthy(const Value& v) {
    if (std::holds_alternative<bool>(v))        return std::get<bool>(v);
    if (std::holds_alternative<int64_t>(v))     return std::get<int64_t>(v) != 0;
    if (std::holds_alternative<double>(v))      return std::get<double>(v) != 0.0;
    if (std::holds_alternative<std::string>(v)) return !std::get<std::string>(v).empty();
    if (std::holds_alternative<Decimal>(v))     return std::get<Decimal>(v).units != 0;
    if (std::holds_alternative<Money>(v))       return std::get<Money>(v).amount.units != 0;
    if (std::holds_alternative<Tensor>(v))      return !std::get<Tensor>(v).data.empty();
    return false;
}

inline Value nv_neg(const Value& a) {
    if (std::holds_alternative<int64_t>(a))    return Value{static_cast<int64_t>(-std::get<int64_t>(a))};
    if (std::holds_alternative<double>(a))         return Value{-std::get<double>(a)};
    if (std::holds_alternative<Decimal>(a))        return Decimal{-std::get<Decimal>(a).units};
    if (std::holds_alternative<Money>(a)) {
        Money m = std::get<Money>(a);
        m.amount.units = -m.amount.units;
        return Value{m};
    }
    if (std::holds_alternative<Tensor>(a)) {
        Tensor t = std::get<Tensor>(a);
        for (double& x : t.data) x = -x;
        return Value{t};
    }
    throw std::runtime_error("nv_neg: unsupported type");
}

inline Value nv_not(const Value& a) {
    return Value{!nv_truthy(a)};
}

inline Value nv_and(const Value& a, const Value& b) {
    return Value{!nv_truthy(a) ? false : nv_truthy(b)};
}

inline Value nv_or(const Value& a, const Value& b) {
    return Value{nv_truthy(a) ? true : nv_truthy(b)};
}

inline Value nv_add(const Value& a, const Value& b) {
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
        return Value{std::get<std::string>(a) + std::get<std::string>(b)};
    }
    if (std::holds_alternative<Money>(a) && std::holds_alternative<Money>(b)) {
        Money x = std::get<Money>(a); Money y = std::get<Money>(b);
        if (x.currency != y.currency) throw std::runtime_error("nv_add: currency mismatch");
        return Money{Decimal{x.amount.units + y.amount.units}, x.currency};
    }
    if (std::holds_alternative<Tensor>(a) || std::holds_alternative<Tensor>(b)) {
        Tensor x = std::holds_alternative<Tensor>(a) ? std::get<Tensor>(a) : Tensor{{nv_to_double(a)}, {1}};
        const Tensor& y = std::holds_alternative<Tensor>(b) ? std::get<Tensor>(b) : Tensor{{nv_to_double(b)}, {1}};
        for (std::size_t i = 0; i < x.data.size(); ++i) x.data[i] += y.data[i % y.data.size()];
        return Value{x};
    }
    if (std::holds_alternative<Decimal>(a) || std::holds_alternative<Decimal>(b)) {
        Decimal da = std::holds_alternative<Decimal>(a) ? std::get<Decimal>(a) : Decimal{static_cast<int64_t>(nv_to_double(a) * Decimal::SCALE_FACTOR)};
        Decimal db = std::holds_alternative<Decimal>(b) ? std::get<Decimal>(b) : Decimal{static_cast<int64_t>(nv_to_double(b) * Decimal::SCALE_FACTOR)};
        return Decimal{da.units + db.units};
    }
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b)) {
        return Value{nv_to_double(a) + nv_to_double(b)};
    }
    return Value{std::get<int64_t>(a) + std::get<int64_t>(b)};
}

inline Value nv_sub(const Value& a, const Value& b) {
    if (std::holds_alternative<Money>(a) && std::holds_alternative<Money>(b)) {
        Money x = std::get<Money>(a); Money y = std::get<Money>(b);
        if (x.currency != y.currency) throw std::runtime_error("nv_sub: currency mismatch");
        return Money{Decimal{x.amount.units - y.amount.units}, x.currency};
    }
    if (std::holds_alternative<Tensor>(a) || std::holds_alternative<Tensor>(b)) {
        Tensor x = std::holds_alternative<Tensor>(a) ? std::get<Tensor>(a) : Tensor{{nv_to_double(a)}, {1}};
        const Tensor& y = std::holds_alternative<Tensor>(b) ? std::get<Tensor>(b) : Tensor{{nv_to_double(b)}, {1}};
        for (std::size_t i = 0; i < x.data.size(); ++i) x.data[i] -= y.data[i % y.data.size()];
        return Value{x};
    }
    if (std::holds_alternative<Decimal>(a) || std::holds_alternative<Decimal>(b)) {
        Decimal da = std::holds_alternative<Decimal>(a) ? std::get<Decimal>(a) : Decimal{static_cast<int64_t>(nv_to_double(a) * Decimal::SCALE_FACTOR)};
        Decimal db = std::holds_alternative<Decimal>(b) ? std::get<Decimal>(b) : Decimal{static_cast<int64_t>(nv_to_double(b) * Decimal::SCALE_FACTOR)};
        return Decimal{da.units - db.units};
    }
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b)) {
        return Value{nv_to_double(a) - nv_to_double(b)};
    }
    return Value{std::get<int64_t>(a) - std::get<int64_t>(b)};
}

inline Value nv_mul(const Value& a, const Value& b) {
    if (std::holds_alternative<Money>(a) || std::holds_alternative<Money>(b)) {
        if (std::holds_alternative<Money>(a) && std::holds_alternative<Money>(b)) {
            throw std::runtime_error("nv_mul: money * money is not allowed");
        }
        Money m = std::holds_alternative<Money>(a) ? std::get<Money>(a) : std::get<Money>(b);
        Value factor = std::holds_alternative<Money>(a) ? b : a;
        double d = nv_to_double(factor);
        return Money{Decimal{static_cast<int64_t>(m.amount.units * d)}, m.currency};
    }
    if (std::holds_alternative<Tensor>(a) || std::holds_alternative<Tensor>(b)) {
        Tensor x = std::holds_alternative<Tensor>(a) ? std::get<Tensor>(a) : Tensor{{nv_to_double(a)}, {1}};
        const Tensor& y = std::holds_alternative<Tensor>(b) ? std::get<Tensor>(b) : Tensor{{nv_to_double(b)}, {1}};
        for (std::size_t i = 0; i < x.data.size(); ++i) x.data[i] *= y.data[i % y.data.size()];
        return Value{x};
    }
    if (std::holds_alternative<Decimal>(a) || std::holds_alternative<Decimal>(b)) {
        double da = nv_to_double(a), db = nv_to_double(b);
        return Decimal{static_cast<int64_t>(da * db * Decimal::SCALE_FACTOR)};
    }
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b)) {
        return Value{nv_to_double(a) * nv_to_double(b)};
    }
    return Value{std::get<int64_t>(a) * std::get<int64_t>(b)};
}

inline Value nv_div(const Value& a, const Value& b) {
    if (std::holds_alternative<Money>(a)) {
        Money m = std::get<Money>(a);
        double d = nv_to_double(b);
        if (d == 0.0) throw std::runtime_error("nv_div: division by zero");
        return Money{Decimal{static_cast<int64_t>(m.amount.units / d)}, m.currency};
    }
    if (std::holds_alternative<Tensor>(a) || std::holds_alternative<Tensor>(b)) {
        Tensor x = std::holds_alternative<Tensor>(a) ? std::get<Tensor>(a) : Tensor{{nv_to_double(a)}, {1}};
        const Tensor& y = std::holds_alternative<Tensor>(b) ? std::get<Tensor>(b) : Tensor{{nv_to_double(b)}, {1}};
        for (std::size_t i = 0; i < x.data.size(); ++i) {
            if (y.data[i % y.data.size()] == 0.0) throw std::runtime_error("nv_div: division by zero");
            x.data[i] /= y.data[i % y.data.size()];
        }
        return Value{x};
    }
    if (std::holds_alternative<Decimal>(a) || std::holds_alternative<Decimal>(b)) {
        double da = nv_to_double(a), db = nv_to_double(b);
        if (db == 0.0) throw std::runtime_error("nv_div: division by zero");
        return Decimal{static_cast<int64_t>(da / db * Decimal::SCALE_FACTOR)};
    }
    if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b)) {
        double y = nv_to_double(b);
        if (y == 0.0) throw std::runtime_error("nv_div: division by zero");
        return Value{nv_to_double(a) / y};
    }
    int64_t y = std::get<int64_t>(b);
    if (y == 0) throw std::runtime_error("nv_div: division by zero");
    return Value{std::get<int64_t>(a) / y};
}

inline Value nv_mod(const Value& a, const Value& b) {
    int64_t y = std::holds_alternative<int64_t>(b) ? std::get<int64_t>(b) : static_cast<int64_t>(nv_to_double(b));
    if (y == 0) throw std::runtime_error("nv_mod: modulo by zero");
    int64_t x = std::holds_alternative<int64_t>(a) ? std::get<int64_t>(a) : static_cast<int64_t>(nv_to_double(a));
    return Value{x % y};
}

inline bool nv_eq(const Value& a, const Value& b) {
    if (a.index() == b.index()) return a == b;
    if ((std::holds_alternative<int64_t>(a) || std::holds_alternative<double>(a) || std::holds_alternative<Decimal>(a)) &&
        (std::holds_alternative<int64_t>(b) || std::holds_alternative<double>(b) || std::holds_alternative<Decimal>(b))) {
        return nv_to_double(a) == nv_to_double(b);
    }
    return false;
}

inline bool nv_neq(const Value& a, const Value& b) { return !nv_eq(a, b); }
inline bool nv_lt(const Value& a, const Value& b) { return nv_to_double(a) <  nv_to_double(b); }
inline bool nv_gt(const Value& a, const Value& b) { return nv_to_double(a) >  nv_to_double(b); }
inline bool nv_lte(const Value& a, const Value& b) { return nv_to_double(a) <= nv_to_double(b); }
inline bool nv_gte(const Value& a, const Value& b) { return nv_to_double(a) >= nv_to_double(b); }

// --- constructors / extractors -------------------------------------------
inline Value nv_money(const Value& a, const Value& currency) {
    Money m;
    double d = nv_to_double(a);
    m.amount = Decimal{static_cast<int64_t>(d * Decimal::SCALE_FACTOR)};
    if (std::holds_alternative<std::string>(currency)) m.currency = std::get<std::string>(currency);
    return Value{m};
}

inline Value nv_decimal(const Value& a) {
    return Value{Decimal{static_cast<int64_t>(nv_to_double(a) * Decimal::SCALE_FACTOR)}};
}

inline Value nv_currency(const Value& a) {
    if (!std::holds_alternative<Money>(a)) throw std::runtime_error("nv_currency: not money");
    return Value{std::get<Money>(a).currency};
}

inline Value nv_round_bankers(const Value& a, int places) {
    if (places < 0 || places > 6) throw std::runtime_error("nv_round_bankers: places must be 0..6");
    int64_t divisor = 1;
    for (int i = 0; i < 6 - places; ++i) divisor *= 10;
    int64_t sign = 1;
    int64_t units;
    if (std::holds_alternative<Money>(a)) {
        units = std::get<Money>(a).amount.units;
    } else if (std::holds_alternative<Decimal>(a)) {
        units = std::get<Decimal>(a).units;
    } else {
        units = static_cast<int64_t>(nv_to_double(a) * Decimal::SCALE_FACTOR);
    }
    if (units < 0) { sign = -1; units = -units; }
    int64_t q = units / divisor;
    int64_t r = units % divisor;
    int64_t half = divisor / 2;
    if (r > half || (r == half && (q % 2 != 0))) ++q;
    if (std::holds_alternative<Money>(a)) {
        Money m = std::get<Money>(a);
        m.amount.units = sign * q * divisor;
        return Value{m};
    }
    return Value{Decimal{sign * q * divisor}};
}

inline bool nv_is_balanced(const std::vector<Value>& args) {
    if (args.empty()) throw std::runtime_error("nv_is_balanced: needs at least one money");
    std::string currency;
    int64_t total = 0;
    for (const auto& a : args) {
        if (!std::holds_alternative<Money>(a)) throw std::runtime_error("nv_is_balanced: not money");
        const Money& m = std::get<Money>(a);
        if (currency.empty()) currency = m.currency;
        if (m.currency != currency) throw std::runtime_error("nv_is_balanced: mixed currencies");
        total += m.amount.units;
    }
    return total == 0;
}

// --- tensor / stats -------------------------------------------------------
inline Tensor nv_make_tensor(const std::vector<Value>& args) {
    if (args.size() == 1 && std::holds_alternative<Tensor>(args[0])) return std::get<Tensor>(args[0]);
    Tensor t;
    t.shape = {args.size()};
    for (const auto& a : args) t.data.push_back(nv_to_double(a));
    return t;
}

inline Value nv_tensor(const std::vector<Value>& args) {
    return Value{nv_make_tensor(args)};
}

inline Value nv_shape(const Value& a) {
    if (!std::holds_alternative<Tensor>(a)) throw std::runtime_error("nv_shape: not tensor");
    Tensor t = std::get<Tensor>(a);
    Tensor out;
    out.shape = {t.shape.size()};
    for (auto d : t.shape) out.data.push_back(static_cast<double>(d));
    return Value{out};
}

inline Value nv_len(const Value& a) {
    if (std::holds_alternative<std::string>(a)) return Value{static_cast<int64_t>(std::get<std::string>(a).size())};
    if (std::holds_alternative<Tensor>(a))      return Value{static_cast<int64_t>(std::get<Tensor>(a).data.size())};
    throw std::runtime_error("nv_len: unsupported type");
}

inline double nv_sum_t(const Tensor& t) {
    double s = 0.0; for (double x : t.data) s += x; return s;
}

inline Value nv_sum(const Value& a) {
    if (std::holds_alternative<Tensor>(a)) return Value{nv_sum_t(std::get<Tensor>(a))};
    return Value{nv_to_double(a)};
}

inline Value nv_mean(const Value& a) {
    if (std::holds_alternative<Tensor>(a)) {
        const Tensor& t = std::get<Tensor>(a);
        if (t.data.empty()) return Value{0.0};
        return Value{nv_sum_t(t) / static_cast<double>(t.data.size())};
    }
    return Value{nv_to_double(a)};
}

inline Value nv_variance(const Value& a) {
    if (!std::holds_alternative<Tensor>(a)) throw std::runtime_error("nv_variance: not tensor");
    const Tensor& t = std::get<Tensor>(a);
    if (t.data.empty()) return Value{0.0};
    double m = nv_mean(a).index() == 0 ? 0.0 : 0.0;
    m = std::get<double>(nv_mean(a));
    double acc = 0.0;
    for (double x : t.data) acc += (x - m) * (x - m);
    return Value{acc / static_cast<double>(t.data.size())};
}

inline Value nv_stddev(const Value& a) {
    Value v = nv_variance(a);
    return Value{std::sqrt(std::get<double>(v))};
}

inline Value nv_min(const Value& a) {
    if (!std::holds_alternative<Tensor>(a)) return a;
    const Tensor& t = std::get<Tensor>(a);
    if (t.data.empty()) return Value{0.0};
    return Value{*std::min_element(t.data.begin(), t.data.end())};
}

inline Value nv_max(const Value& a) {
    if (!std::holds_alternative<Tensor>(a)) return a;
    const Tensor& t = std::get<Tensor>(a);
    if (t.data.empty()) return Value{0.0};
    return Value{*std::max_element(t.data.begin(), t.data.end())};
}

inline Value nv_dot(const Value& a, const Value& b) {
    if (!std::holds_alternative<Tensor>(a) || !std::holds_alternative<Tensor>(b))
        throw std::runtime_error("nv_dot: both args must be tensors");
    const Tensor& x = std::get<Tensor>(a);
    const Tensor& y = std::get<Tensor>(b);
    if (x.data.size() != y.data.size()) throw std::runtime_error("nv_dot: tensor sizes differ");
    double acc = 0.0;
    for (std::size_t i = 0; i < x.data.size(); ++i) acc += x.data[i] * y.data[i];
    return Value{acc};
}

inline Value nv_relu(const Value& a) {
    if (!std::holds_alternative<Tensor>(a)) return a;
    Tensor t = std::get<Tensor>(a);
    for (double& x : t.data) x = std::max(0.0, x);
    return Value{t};
}

inline Value nv_sigmoid(const Value& a) {
    if (!std::holds_alternative<Tensor>(a)) {
        return Value{1.0 / (1.0 + std::exp(-nv_to_double(a)))};
    }
    Tensor t = std::get<Tensor>(a);
    for (double& x : t.data) x = 1.0 / (1.0 + std::exp(-x));
    return Value{t};
}

inline Value nv_softmax(const Value& a) {
    if (!std::holds_alternative<Tensor>(a)) throw std::runtime_error("nv_softmax: not tensor");
    Tensor t = std::get<Tensor>(a);
    if (t.data.empty()) return Value{t};
    double m = *std::max_element(t.data.begin(), t.data.end());
    double s = 0.0;
    for (double& x : t.data) { x = std::exp(x - m); s += x; }
    for (double& x : t.data) x /= s;
    return Value{t};
}

inline Value nv_argmax(const Value& a) {
    if (!std::holds_alternative<Tensor>(a)) throw std::runtime_error("nv_argmax: not tensor");
    const Tensor& t = std::get<Tensor>(a);
    if (t.data.empty()) throw std::runtime_error("nv_argmax: empty");
    auto it = std::max_element(t.data.begin(), t.data.end());
    return Value{static_cast<int64_t>(std::distance(t.data.begin(), it))};
}

inline Value nv_risk_score(const Value& a) {
    double income = 0, debt = 0, late = 0;
    if (std::holds_alternative<Tensor>(a)) {
        const Tensor& t = std::get<Tensor>(a);
        if (t.data.size() > 0) income = t.data[0];
        if (t.data.size() > 1) debt = t.data[1];
        if (t.data.size() > 2) late = t.data[2];
    }
    double ratio = income <= 0.0 ? 1.0 : debt / income;
    return Value{1.0 / (1.0 + std::exp(-(-2.0 + 3.0 * ratio + 0.8 * late)))};
}

inline Value nv_sqrt(const Value& a)   { return Value{std::sqrt(nv_to_double(a))}; }
inline Value nv_pow(const Value& a, const Value& b) { return Value{std::pow(nv_to_double(a), nv_to_double(b))}; }

inline Value nv_read_text(const Value& path) {
    if (!std::holds_alternative<std::string>(path)) throw std::runtime_error("nv_read_text: path not str");
    std::ifstream in(std::get<std::string>(path));
    if (!in) throw std::runtime_error("nv_read_text: cannot open");
    std::ostringstream ss; ss << in.rdbuf();
    return Value{ss.str()};
}

inline Value nv_write_text(const Value& path, const Value& content) {
    if (!std::holds_alternative<std::string>(path)) throw std::runtime_error("nv_write_text: path not str");
    if (!std::holds_alternative<std::string>(content)) throw std::runtime_error("nv_write_text: content not str");
    std::ofstream out(std::get<std::string>(path));
    if (!out) throw std::runtime_error("nv_write_text: cannot write");
    out << std::get<std::string>(content);
    return Value{static_cast<int64_t>(0)};
}

inline std::string nv_to_string(const Value& v) {
    if (std::holds_alternative<int64_t>(v))     return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))      { std::ostringstream ss; ss << std::setprecision(12) << std::get<double>(v); return ss.str(); }
    if (std::holds_alternative<bool>(v))        return std::get<bool>(v) ? "true" : "false";
    if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
    if (std::holds_alternative<Decimal>(v))     { std::ostringstream ss; ss << (std::get<Decimal>(v).units / double(Decimal::SCALE_FACTOR)); return ss.str(); }
    if (std::holds_alternative<Money>(v))       { std::ostringstream ss; ss << std::get<Money>(v).currency << " " << (std::get<Money>(v).amount.units / double(Decimal::SCALE_FACTOR)); return ss.str(); }
    if (std::holds_alternative<Tensor>(v))      { std::ostringstream ss; ss << "["; for (std::size_t i = 0; i < std::get<Tensor>(v).data.size(); ++i) { if (i) ss << ", "; ss << std::get<Tensor>(v).data[i]; } ss << "]"; return ss.str(); }
    return "<value>";
}

inline Value nv_print(const Value& a) {
    std::printf("%s\n", nv_to_string(a).c_str());
    std::fflush(stdout);
    return Value{static_cast<int64_t>(0)};
}

} // namespace nvrt
