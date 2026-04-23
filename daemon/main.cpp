/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 *
 * Copyright (C) 2026 Stagelab Coop SCCL
 * Authors:
 *   Adrià Masip <adria@stagelab.coop>
 *
 * This file is part of gradient-motion-engine.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file main.cpp
 * @brief Entry point for the gradient-motiond daemon.
 *
 * Follows the VideoComposer lifecycle pattern: construct the application
 * on the stack, call initialize() with CLI arguments, and if successful
 * call run() which blocks until a termination signal is received.
 */

#include "GradientEngineApplication.h"

int main(int argc, char** argv) {
    GradientEngineApplication app;

    int init = app.initialize(argc, argv);
    if (init != 0) {
        // Negative = help/version (clean exit), positive = error
        return (init < 0) ? 0 : init;
    }

    return app.run();
}
