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
