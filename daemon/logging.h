/**
 * @file logging.h
 * @brief Compile-time logging abstraction for the gradient-motiond daemon.
 *
 * Selects between CuemsLogger (syslog-based, journal-integrated) and a
 * standard library fallback (<iostream> to stderr) based on the
 * HAVE_CUEMS_LOGGER preprocessor define, which is controlled by the
 * CMake option ENABLE_CUEMS_LOGGER.
 *
 * All daemon code uses these macros exclusively — never call CuemsLogger
 * or std::cerr directly. This ensures a single build flag switches the
 * entire logging backend.
 *
 * Log level filtering is handled by gme_log_set_level() / gme_log_level_
 * so that messages below the configured verbosity are suppressed without
 * modifying the shared CuemsLogger submodule.
 *
 * All macros use the GME_LOG_ prefix to avoid collision with POSIX
 * syslog.h constants (LOG_WARNING, LOG_NOTICE, LOG_INFO, etc.).
 *
 * @par Syslog level mapping (highest → lowest severity):
 * | Macro              | Syslog level | Numeric |
 * |--------------------|-------------|---------|
 * | GME_LOG_EMERGENCY  | LOG_EMERG   | 0       |
 * | GME_LOG_ALERT      | LOG_ALERT   | 1       |
 * | GME_LOG_CRITICAL   | LOG_CRIT    | 2       |
 * | GME_LOG_ERROR      | LOG_ERR     | 3       |
 * | GME_LOG_WARNING    | LOG_WARNING | 4       |
 * | GME_LOG_NOTICE     | LOG_NOTICE  | 5       |
 * | GME_LOG_INFO       | LOG_INFO    | 6       |
 * | GME_LOG_DEBUG      | LOG_DEBUG   | 7       |
 *
 * @par Example usage:
 * @code
 * #include "daemon/logging.h"
 *
 * gme_log_set_level(GME_LEVEL_DEBUG);
 * GME_LOG_INFO("Daemon started on port " + portName);
 * GME_LOG_DEBUG("Tick elapsed: " + std::to_string(ms) + " ms");
 * @endcode
 */

#ifndef GRADIENT_ENGINE_LOGGING_H
#define GRADIENT_ENGINE_LOGGING_H

#include <string>

/**
 * @brief Numeric log level constants, mirroring syslog severity.
 *
 * Lower values are more severe. A message is emitted only when its
 * level is <= the configured threshold (gme_log_level_).
 */
enum GmeLogLevel {
    GME_LEVEL_EMERGENCY = 0,
    GME_LEVEL_ALERT     = 1,
    GME_LEVEL_CRITICAL  = 2,
    GME_LEVEL_ERROR     = 3,
    GME_LEVEL_WARNING   = 4,
    GME_LEVEL_NOTICE    = 5,
    GME_LEVEL_INFO      = 6,
    GME_LEVEL_DEBUG     = 7
};

/// Current log level threshold. Messages with severity > this value are suppressed.
inline int gme_log_level_ = GME_LEVEL_INFO;

/**
 * @brief Set the runtime log level threshold.
 *
 * @param level One of the GmeLogLevel constants. Messages with numeric
 *              severity greater than @p level are suppressed.
 *
 * @par Example:
 * @code
 * gme_log_set_level(GME_LEVEL_DEBUG); // show everything
 * gme_log_set_level(GME_LEVEL_ERROR); // only errors and above
 * @endcode
 */
inline void gme_log_set_level(int level) { gme_log_level_ = level; }

/**
 * @brief Convert a log-level name string to its numeric constant.
 *
 * @param name Case-sensitive level name (e.g. "debug", "info", "error").
 * @return Corresponding GmeLogLevel value, or GME_LEVEL_INFO if unrecognised.
 *
 * @par Example:
 * @code
 * int lvl = gme_log_level_from_string("debug"); // returns GME_LEVEL_DEBUG
 * @endcode
 */
inline int gme_log_level_from_string(const std::string& name) {
    if (name == "emergency") return GME_LEVEL_EMERGENCY;
    if (name == "alert")     return GME_LEVEL_ALERT;
    if (name == "critical")  return GME_LEVEL_CRITICAL;
    if (name == "error")     return GME_LEVEL_ERROR;
    if (name == "warning")   return GME_LEVEL_WARNING;
    if (name == "notice")    return GME_LEVEL_NOTICE;
    if (name == "info")      return GME_LEVEL_INFO;
    if (name == "debug")     return GME_LEVEL_DEBUG;
    return GME_LEVEL_INFO;
}

// ===========================================================
// Backend selection
// ===========================================================

#ifdef HAVE_CUEMS_LOGGER

#include "cuemslogger/cuemslogger.h"

#define GME_LOG_EMERGENCY(msg) \
    do { if (GME_LEVEL_EMERGENCY <= gme_log_level_) CuemsLogger::getLogger()->logEmergency(msg); } while(0)
#define GME_LOG_ALERT(msg) \
    do { if (GME_LEVEL_ALERT <= gme_log_level_) CuemsLogger::getLogger()->logAlert(msg); } while(0)
#define GME_LOG_CRITICAL(msg) \
    do { if (GME_LEVEL_CRITICAL <= gme_log_level_) CuemsLogger::getLogger()->logCritical(msg); } while(0)
#define GME_LOG_ERROR(msg) \
    do { if (GME_LEVEL_ERROR <= gme_log_level_) CuemsLogger::getLogger()->logError(msg); } while(0)
#define GME_LOG_WARNING(msg) \
    do { if (GME_LEVEL_WARNING <= gme_log_level_) CuemsLogger::getLogger()->logWarning(msg); } while(0)
#define GME_LOG_NOTICE(msg) \
    do { if (GME_LEVEL_NOTICE <= gme_log_level_) CuemsLogger::getLogger()->logNotice(msg); } while(0)
#define GME_LOG_INFO(msg) \
    do { if (GME_LEVEL_INFO <= gme_log_level_) CuemsLogger::getLogger()->logInfo(msg); } while(0)
#define GME_LOG_DEBUG(msg) \
    do { if (GME_LEVEL_DEBUG <= gme_log_level_) CuemsLogger::getLogger()->logDebug(msg); } while(0)

#else // stdlib fallback

#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace gme_log_detail {

/**
 * @brief Emit a timestamped log line to stderr.
 *
 * @param level   Human-readable level tag (e.g. "INFO", "ERROR").
 * @param message The log message body.
 */
inline void emit(const char* level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    struct tm buf;
    localtime_r(&tt, &buf);
    std::cerr << std::put_time(&buf, "%Y-%m-%d %H:%M:%S")
              << " [" << level << "] " << message << std::endl;
}

} // namespace gme_log_detail

#define GME_LOG_EMERGENCY(msg) \
    do { if (GME_LEVEL_EMERGENCY <= gme_log_level_) gme_log_detail::emit("EMERGENCY", msg); } while(0)
#define GME_LOG_ALERT(msg) \
    do { if (GME_LEVEL_ALERT <= gme_log_level_) gme_log_detail::emit("ALERT", msg); } while(0)
#define GME_LOG_CRITICAL(msg) \
    do { if (GME_LEVEL_CRITICAL <= gme_log_level_) gme_log_detail::emit("CRITICAL", msg); } while(0)
#define GME_LOG_ERROR(msg) \
    do { if (GME_LEVEL_ERROR <= gme_log_level_) gme_log_detail::emit("ERROR", msg); } while(0)
#define GME_LOG_WARNING(msg) \
    do { if (GME_LEVEL_WARNING <= gme_log_level_) gme_log_detail::emit("WARNING", msg); } while(0)
#define GME_LOG_NOTICE(msg) \
    do { if (GME_LEVEL_NOTICE <= gme_log_level_) gme_log_detail::emit("NOTICE", msg); } while(0)
#define GME_LOG_INFO(msg) \
    do { if (GME_LEVEL_INFO <= gme_log_level_) gme_log_detail::emit("INFO", msg); } while(0)
#define GME_LOG_DEBUG(msg) \
    do { if (GME_LEVEL_DEBUG <= gme_log_level_) gme_log_detail::emit("DEBUG", msg); } while(0)

#endif // HAVE_CUEMS_LOGGER

#endif // GRADIENT_ENGINE_LOGGING_H
