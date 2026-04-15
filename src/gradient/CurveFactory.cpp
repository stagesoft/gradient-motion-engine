#include "CurveFactory.h"

#include <iostream>
#include <memory>

#include "LinearCurve.h"
#include "SigmoidCurve.h"
#include "BezierCurve.h"
#include "EaseInCurve.h"
#include "EaseOutCurve.h"
#include "SCurve.h"
#include "ResampledCurve.h"

namespace gme {
namespace gradient {

std::optional<std::unique_ptr<Curve>>
CurveFactory::createCurve(const std::string& type,
                           const nlohmann::json& params)
{
    std::unique_ptr<Curve> inner;

    if (type == "linear") {
        inner = std::make_unique<LinearCurve>();

    } else if (type == "sigmoid") {
        double steepness = params.value("steepness", 8.0);
        double midpoint  = params.value("midpoint",  0.5);
        inner = std::make_unique<SigmoidCurve>(steepness, midpoint);

    } else if (type == "bezier") {
        double cx1 = params.value("cx1", 0.25);
        double cy1 = params.value("cy1", 0.1);
        double cx2 = params.value("cx2", 0.75);
        double cy2 = params.value("cy2", 0.9);
        inner = std::make_unique<BezierCurve>(cx1, cy1, cx2, cy2);

    } else if (type == "ease_in") {
        double exp = params.value("exponent", 2.0);
        inner = std::make_unique<EaseInCurve>(exp);

    } else if (type == "ease_out") {
        double exp = params.value("exponent", 2.0);
        inner = std::make_unique<EaseOutCurve>(exp);

    } else if (type == "scurve") {
        inner = std::make_unique<SCurve>();

    } else {
        std::cerr << "[CurveFactory] Unknown curve type: '"
                  << type << "' — returning nullopt\n";
        return std::nullopt;
    }

    auto wrapped = std::make_unique<ResampledCurve>(*inner);
    return std::make_optional(std::move(wrapped));
}

} // namespace gradient
} // namespace gme
