/**
 * @file test_curves.cpp
 * @brief Unit tests for the gme::gradient curve module.
 *
 * Tests are organized by user story. Each test function returns true on
 * success and false (printing a message to stderr) on failure.
 * main() calls all test functions and returns 0 if all pass, 1 otherwise.
 */

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

#include "gradient/Curve.h"
#include "gradient/LinearCurve.h"
#include "gradient/EaseInCurve.h"
#include "gradient/EaseOutCurve.h"
#include "gradient/SCurve.h"
#include "gradient/SigmoidCurve.h"
#include "gradient/BezierCurve.h"
#include "gradient/ResampledCurve.h"
#include "gradient/CurveFactory.h"
#include "gradient/ScaledCurve.h"
#include "gradient/CrossfadePair.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool nearly_equal(double a, double b, double tol = 1e-9) {
    return std::abs(a - b) <= tol;
}

#define ASSERT_EQ(expr, expected, msg)                                  \
    do {                                                                 \
        double _val = (expr);                                           \
        double _exp = (expected);                                       \
        if (!nearly_equal(_val, _exp)) {                               \
            std::fprintf(stderr, "FAIL [%s]: %s == %.9f, expected %.9f\n", \
                         msg, #expr, _val, _exp);                      \
            return false;                                               \
        }                                                               \
    } while (0)

#define ASSERT_TRUE(cond, msg)                                          \
    do {                                                                 \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "FAIL [%s]: %s\n", msg, #cond);      \
            return false;                                               \
        }                                                               \
    } while (0)

// ---------------------------------------------------------------------------
// US1: Evaluate a fade curve at any point in time
// ---------------------------------------------------------------------------

static bool test_boundary_values() {
    using namespace gme::gradient;
    LinearCurve   lc;
    SigmoidCurve  sc;
    BezierCurve   bc;
    EaseInCurve   ei;
    EaseOutCurve  eo;
    SCurve        ss;

    const char* names[] = { "Linear", "Sigmoid", "Bezier",
                             "EaseIn", "EaseOut", "SCurve" };
    Curve* curves[] = { &lc, &sc, &bc, &ei, &eo, &ss };

    for (int i = 0; i < 6; ++i) {
        if (!nearly_equal(curves[i]->evaluate(0.0), 0.0)) {
            std::fprintf(stderr, "FAIL [test_boundary_values]: %s evaluate(0.0) != 0.0\n", names[i]);
            return false;
        }
        if (!nearly_equal(curves[i]->evaluate(1.0), 1.0)) {
            std::fprintf(stderr, "FAIL [test_boundary_values]: %s evaluate(1.0) != 1.0\n", names[i]);
            return false;
        }
    }
    return true;
}

static bool test_monotonicity() {
    using namespace gme::gradient;
    LinearCurve  lc;
    SigmoidCurve sc;
    EaseInCurve  ei;
    EaseOutCurve eo;
    SCurve       ss;

    const char* names[] = { "Linear", "Sigmoid", "EaseIn", "EaseOut", "SCurve" };
    Curve* curves[] = { &lc, &sc, &ei, &eo, &ss };

    const int N = 100;
    for (int c = 0; c < 5; ++c) {
        double prev = curves[c]->evaluate(0.0);
        for (int i = 1; i <= N; ++i) {
            double t = static_cast<double>(i) / N;
            double v = curves[c]->evaluate(t);
            if (v < prev - 1e-9) {
                std::fprintf(stderr,
                    "FAIL [test_monotonicity]: %s not monotone at t=%.3f (%.9f < %.9f)\n",
                    names[c], t, v, prev);
                return false;
            }
            prev = v;
        }
    }
    return true;
}

static bool test_input_clamping() {
    using namespace gme::gradient;
    LinearCurve  lc;
    SigmoidCurve sc;
    BezierCurve  bc;

    const char* names[] = { "Linear", "Sigmoid", "Bezier" };
    Curve* curves[] = { &lc, &sc, &bc };

    for (int i = 0; i < 3; ++i) {
        double lo = curves[i]->evaluate(-0.5);
        double hi = curves[i]->evaluate(1.5);
        if (!nearly_equal(lo, 0.0)) {
            std::fprintf(stderr,
                "FAIL [test_input_clamping]: %s evaluate(-0.5) = %.9f, expected 0.0\n",
                names[i], lo);
            return false;
        }
        if (!nearly_equal(hi, 1.0)) {
            std::fprintf(stderr,
                "FAIL [test_input_clamping]: %s evaluate(1.5) = %.9f, expected 1.0\n",
                names[i], hi);
            return false;
        }
    }
    return true;
}

static bool test_sigmoid_extreme_steepness() {
    using namespace gme::gradient;
    // Extreme steepness stresses the normalization arithmetic.
    // The curve must still deliver exact boundary values.
    SigmoidCurve extreme(50.0, 0.5);
    ASSERT_EQ(extreme.evaluate(0.0), 0.0, "sigmoid_extreme_steepness");
    ASSERT_EQ(extreme.evaluate(1.0), 1.0, "sigmoid_extreme_steepness");
    return true;
}

// ---------------------------------------------------------------------------
// US3: ResampledCurve — performant LUT-backed evaluation
// ---------------------------------------------------------------------------

static bool test_resampled_accuracy() {
    using namespace gme::gradient;
    SigmoidCurve   direct(10.0, 0.5);
    ResampledCurve resampled(direct, 256);

    const int N = 1000;
    for (int i = 0; i <= N; ++i) {
        double t  = static_cast<double>(i) / N;
        double d  = direct.evaluate(t);
        double r  = resampled.evaluate(t);
        double err = std::abs(r - d);
        if (err > 0.005) {
            std::fprintf(stderr,
                "FAIL [test_resampled_accuracy]: t=%.4f direct=%.6f resampled=%.6f err=%.6f > 0.005\n",
                t, d, r, err);
            return false;
        }
    }
    return true;
}

static bool test_resampled_boundary() {
    using namespace gme::gradient;
    SigmoidCurve   inner;
    ResampledCurve r(inner, 256);
    ASSERT_EQ(r.evaluate(0.0), 0.0, "resampled_boundary");
    ASSERT_EQ(r.evaluate(1.0), 1.0, "resampled_boundary");
    return true;
}

static bool test_resampled_n2() {
    // Degenerate 2-sample edge case: must not crash and must satisfy basics.
    using namespace gme::gradient;
    LinearCurve    lc;
    ResampledCurve r(lc, 2);
    ASSERT_EQ(r.evaluate(0.0), 0.0, "resampled_n2");
    ASSERT_EQ(r.evaluate(1.0), 1.0, "resampled_n2");
    double mid = r.evaluate(0.5);
    ASSERT_TRUE(mid >= 0.0 && mid <= 1.0, "resampled_n2_mid_in_range");
    return true;
}

// ---------------------------------------------------------------------------
// US2: CurveFactory — select a curve shape by name
// ---------------------------------------------------------------------------

static bool test_factory_all_types() {
    using namespace gme::gradient;
    const char* types[] = { "linear", "sigmoid", "bezier",
                             "ease_in", "ease_out", "scurve" };
    for (const char* type : types) {
        auto result = CurveFactory::createCurve(type);
        if (!result.has_value()) {
            std::fprintf(stderr,
                "FAIL [test_factory_all_types]: \"%s\" returned nullopt\n", type);
            return false;
        }
        auto& curve = *result;
        if (!nearly_equal(curve->evaluate(0.0), 0.0)) {
            std::fprintf(stderr,
                "FAIL [test_factory_all_types]: \"%s\" evaluate(0.0) != 0.0\n", type);
            return false;
        }
        if (!nearly_equal(curve->evaluate(1.0), 1.0)) {
            std::fprintf(stderr,
                "FAIL [test_factory_all_types]: \"%s\" evaluate(1.0) != 1.0\n", type);
            return false;
        }
    }
    return true;
}

static bool test_factory_unknown_returns_nullopt() {
    using namespace gme::gradient;
    auto result = CurveFactory::createCurve("nonexistent_curve");
    ASSERT_TRUE(!result.has_value(), "factory_unknown_returns_nullopt");
    return true;
}

static bool test_factory_default_params() {
    using namespace gme::gradient;
    auto result = CurveFactory::createCurve("sigmoid", nlohmann::json::object());
    ASSERT_TRUE(result.has_value(), "factory_default_params");
    ASSERT_EQ((*result)->evaluate(0.0), 0.0, "factory_default_params");
    ASSERT_EQ((*result)->evaluate(1.0), 1.0, "factory_default_params");
    return true;
}

static bool test_factory_caller_fallback_pattern() {
    using namespace gme::gradient;
    // Demonstrate and validate the recommended caller fallback pattern.
    auto result = CurveFactory::createCurve("nonexistent_curve");
    auto curve  = result
        ? std::move(*result)
        : std::make_unique<ResampledCurve>(LinearCurve{});
    ASSERT_TRUE(curve != nullptr, "factory_caller_fallback_nonnull");
    ASSERT_EQ(curve->evaluate(0.5), 0.5, "factory_caller_fallback_linear");
    return true;
}

// ---------------------------------------------------------------------------
// US4: ScaledCurve — remap output to arbitrary value ranges
// ---------------------------------------------------------------------------

static bool test_scaled_output_range() {
    using namespace gme::gradient;
    ScaledCurve sc(std::make_unique<LinearCurve>(),
                   0.0, 1.0,   // input range (identity)
                   0.0, 0.5);  // output range remapped to [0, 0.5]
    ASSERT_EQ(sc.evaluate(0.0), 0.0, "scaled_output_range");
    ASSERT_EQ(sc.evaluate(1.0), 0.5, "scaled_output_range");
    if (std::abs(sc.evaluate(0.5) - 0.25) > 1e-9) {
        std::fprintf(stderr, "FAIL [scaled_output_range]: evaluate(0.5) = %.9f, expected 0.25\n",
                     sc.evaluate(0.5));
        return false;
    }
    return true;
}

static bool test_scaled_input_range() {
    using namespace gme::gradient;
    // Input range [0, 2]: input 1.0 maps to normalized 0.5
    ScaledCurve sc(std::make_unique<LinearCurve>(),
                   0.0, 2.0,   // input range
                   0.0, 1.0);  // output range (identity)
    double v = sc.evaluate(1.0);
    if (std::abs(v - 0.5) > 1e-9) {
        std::fprintf(stderr, "FAIL [scaled_input_range]: evaluate(1.0) = %.9f, expected 0.5\n", v);
        return false;
    }
    return true;
}

static bool test_scaled_boundaries() {
    using namespace gme::gradient;
    ScaledCurve sc(std::make_unique<LinearCurve>(),
                   0.25, 0.75,  // input range
                   0.1,  0.9);  // output range
    ASSERT_EQ(sc.evaluate(0.25), 0.1, "scaled_boundaries_lo");
    ASSERT_EQ(sc.evaluate(0.75), 0.9, "scaled_boundaries_hi");
    return true;
}

// ---------------------------------------------------------------------------
// US5: CrossfadePair — complementary pair summing to 1.0
// ---------------------------------------------------------------------------

static bool test_crossfade_sum_invariant() {
    using namespace gme::gradient;
    CrossfadePair pair(std::make_unique<SigmoidCurve>());
    const int N = 100;
    for (int i = 0; i <= N; ++i) {
        double t = static_cast<double>(i) / N;
        auto [primary, complement] = pair.evaluate(t);
        double sum = primary + complement;
        if (!nearly_equal(sum, 1.0)) {
            std::fprintf(stderr,
                "FAIL [test_crossfade_sum_invariant]: t=%.3f primary+complement=%.15f != 1.0\n",
                t, sum);
            return false;
        }
    }
    return true;
}

static bool test_crossfade_boundaries() {
    using namespace gme::gradient;
    CrossfadePair pair(std::make_unique<SigmoidCurve>());

    auto [p0, c0] = pair.evaluate(0.0);
    ASSERT_EQ(p0, 0.0, "crossfade_boundary_p0");
    ASSERT_EQ(c0, 1.0, "crossfade_boundary_c0");

    auto [p1, c1] = pair.evaluate(1.0);
    ASSERT_EQ(p1, 1.0, "crossfade_boundary_p1");
    ASSERT_EQ(c1, 0.0, "crossfade_boundary_c1");

    return true;
}

// ---------------------------------------------------------------------------
// T027: Micro-benchmark — ResampledCurve::evaluate() latency
// ---------------------------------------------------------------------------

static bool bench_resampled_latency() {
    using namespace gme::gradient;
    SigmoidCurve   inner;
    ResampledCurve r(inner, 256);

    const long ITERS = 1'000'000;
    volatile double sink = 0.0; // prevent optimisation of the loop

    auto t0 = std::chrono::high_resolution_clock::now();
    for (long i = 0; i < ITERS; ++i) {
        double t = static_cast<double>(i % 1001) / 1000.0;
        sink = r.evaluate(t);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    (void)sink;

    double ns_per_call =
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count())
        / ITERS;

    std::fprintf(stdout, "bench_resampled_latency: %.2f ns/call\n", ns_per_call);

    if (ns_per_call >= 1000.0) {
        std::fprintf(stderr,
            "FAIL [bench_resampled_latency]: %.2f ns/call >= 1000 ns budget\n",
            ns_per_call);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    struct Test { const char* name; bool(*fn)(); };
    Test tests[] = {
        // US1
        { "boundary_values",          test_boundary_values },
        { "monotonicity",             test_monotonicity },
        { "input_clamping",           test_input_clamping },
        { "sigmoid_extreme_steepness",test_sigmoid_extreme_steepness },
        // US3
        { "resampled_accuracy",       test_resampled_accuracy },
        { "resampled_boundary",       test_resampled_boundary },
        { "resampled_n2",             test_resampled_n2 },
        // US2
        { "factory_all_types",        test_factory_all_types },
        { "factory_unknown_nullopt",  test_factory_unknown_returns_nullopt },
        { "factory_default_params",   test_factory_default_params },
        { "factory_caller_fallback",  test_factory_caller_fallback_pattern },
        // US4
        { "scaled_output_range",      test_scaled_output_range },
        { "scaled_input_range",       test_scaled_input_range },
        { "scaled_boundaries",        test_scaled_boundaries },
        // US5
        { "crossfade_sum_invariant",  test_crossfade_sum_invariant },
        { "crossfade_boundaries",     test_crossfade_boundaries },
        // T027 benchmark
        { "bench_resampled_latency",  bench_resampled_latency },
    };

    int failed = 0;
    for (auto& t : tests) {
        bool ok = t.fn();
        std::fprintf(stdout, "%s %s\n", ok ? "PASS" : "FAIL", t.name);
        if (!ok) ++failed;
    }

    std::fprintf(stdout, "\n%d/%zu tests passed\n",
                 (int)(sizeof(tests)/sizeof(tests[0])) - failed,
                 sizeof(tests)/sizeof(tests[0]));
    return failed > 0 ? 1 : 0;
}
