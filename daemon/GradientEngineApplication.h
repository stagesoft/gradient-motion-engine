/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file GradientEngineApplication.h
 * @brief Top-level daemon orchestrator for gradient-motiond.
 *
 * Manages the full application lifecycle: construction → initialization
 * → running → shutdown → destruction. Follows the VideoComposer
 * pattern where main.cpp creates the application on the stack, calls
 * initialize(), then run(), and returns the exit code.
 *
 * Named GradientEngine* (not Fade*) because this class orchestrates
 * the general-purpose signal evaluation engine, not just the fade
 * sub-component.
 *
 * @par Lifecycle states:
 * - **Constructed**: Object created, no resources acquired.
 * - **Initialized**: CLI parsed, logger set up, config stored.
 * - **Running**: Main loop active, blocking on pause().
 * - **Shutting Down**: Signal received, cleaning up.
 * - **Destroyed**: Destructor complete.
 *
 * @par State transitions:
 * - Constructed → Initialized: initialize() succeeds.
 * - Constructed → Destroyed: initialize() fails (help/error).
 * - Initialized → Running: run() enters main loop.
 * - Running → Shutting Down: SIGTERM/SIGINT received.
 * - Shutting Down → Destroyed: shutdown() completes.
 *
 * @par Example usage:
 * @code
 * int main(int argc, char** argv) {
 *     GradientEngineApplication app;
 *     if (!app.initialize(argc, argv))
 *         return 0;
 *     return app.run();
 * }
 * @endcode
 */

#ifndef GRADIENT_ENGINE_APPLICATION_H
#define GRADIENT_ENGINE_APPLICATION_H

#include <memory>

#ifdef HAVE_CUEMS_LOGGER
class CuemsLogger;
#endif

class ConfigurationManager;

// Forward-declare to avoid pulling GradientEngine.h (and its transitive
// daemon headers) into every translation unit that includes this header.
namespace gme { namespace engine { class GradientEngine; } }

/**
 * @brief Top-level daemon application managing lifecycle and subsystems.
 *
 * Owns the ConfigurationManager and optional CuemsLogger instance.
 * Registers POSIX signal handlers for clean shutdown.
 */
class GradientEngineApplication {
public:
    GradientEngineApplication();
    ~GradientEngineApplication();

    // Non-copyable, non-movable
    GradientEngineApplication(const GradientEngineApplication&) = delete;
    GradientEngineApplication& operator=(const GradientEngineApplication&) = delete;

    /**
     * @brief Parse CLI arguments, set up logging, prepare subsystems.
     *
     * @param argc Argument count from main().
     * @param argv Argument vector from main().
     * @return 0 if initialization succeeded and run() should be called.
     *         Positive value on error (caller should exit with that code).
     *         Negative value if --help or --version was requested (exit 0).
     *
     * @throws None. Errors are logged and indicated via return value.
     *
     * @par Example:
     * @code
     * GradientEngineApplication app;
     * int init = app.initialize(argc, argv);
     * if (init != 0) return (init < 0) ? 0 : init;
     * return app.run();
     * @endcode
     */
    int initialize(int argc, char** argv);

    /**
     * @brief Enter the main event loop, blocking until a signal is received.
     *
     * @return Exit code (0 on clean shutdown).
     *
     * @throws None.
     *
     * @par Example:
     * @code
     * return app.run();
     * @endcode
     */
    int run();

    /**
     * @brief Release resources and log shutdown event.
     *
     * Called automatically by run() after the main loop exits, and also
     * from the destructor as a safety net.
     */
    void shutdown();

private:
    bool running_;
    bool initialized_;
    bool shutdownComplete_;
    std::unique_ptr<ConfigurationManager>     config_;
    std::unique_ptr<gme::engine::GradientEngine> engine_;

#ifdef HAVE_CUEMS_LOGGER
    CuemsLogger* logger_;
#endif

    /**
     * @brief Install signal handlers for SIGTERM and SIGINT.
     */
    void installSignalHandlers();
};

#endif // GRADIENT_ENGINE_APPLICATION_H
