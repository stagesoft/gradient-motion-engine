/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file OscSender.cpp
 * @brief Implementation of gme::osc free functions.
 */

#include "osc/OscSender.h"

#include <lo/lo.h>

namespace gme {
namespace osc {

int sendFloat(lo_address target, const char* path, float value) noexcept {
    // lo_send formats the message into a stack buffer then calls sendmsg(2).
    // Passing value as double matches lo_send's vararg "f" type expectation.
    return lo_send(target, path, "f", static_cast<double>(value));
}

lo_address makeAddress(const std::string& host, int port) noexcept {
    return lo_address_new(host.c_str(), std::to_string(port).c_str());
}

} // namespace osc
} // namespace gme
