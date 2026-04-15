/**
 * @file cuems_errors.h
 * @brief Minimal error code shim for gradient-motion-engine.
 *
 * Provides only the error code referenced by the mtcreceiver submodule
 * when compiled with HAVE_CUEMS_LOGGER. The value matches the stub
 * definition in mtcreceiver.h (else branch).
 *
 * This is NOT a copy of VideoComposer's cuems_errors.h — it is a
 * self-contained shim that covers only what this project requires.
 */

#pragma once

/// Exit code returned when no MIDI ports are found on the system.
#define CUEMS_EXIT_NO_MIDI_PORTS_FOUND 1
