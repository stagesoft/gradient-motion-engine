/**
 * @file ConfigurationManager.h
 * @brief CLI argument storage for the gradient-motiond daemon.
 *
 * Parses and stores command-line arguments using getopt_long. XML
 * configuration file parsing is deferred to a future phase — Phase 0
 * uses CLI arguments and built-in defaults only.
 *
 * @par Supported arguments:
 * | Long flag      | Short | Type   | Default              |
 * |----------------|-------|--------|----------------------|
 * | --midi-port    | -m    | string | "Midi Through Port-0"|
 * | --log-level    | -l    | string | "info"               |
 * | --conf-path    | -c    | string | "/etc/cuems"         |
 * | --help         | -h    | flag   | —                    |
 * | --version      | -V    | flag   | —                    |
 *
 * @par Log level validation:
 * The --log-level value MUST be one of: emergency, alert, critical,
 * error, warning, notice, info, debug. Invalid values cause parseArgs()
 * to return failure.
 *
 * @par Example usage:
 * @code
 * ConfigurationManager config;
 * int result = config.parseArgs(argc, argv);
 * if (result != 0) {
 *     // -1 = help/version requested, >0 = error
 *     return (result < 0) ? 0 : 1;
 * }
 * std::string port = config.getMidiPort();
 * @endcode
 */

#ifndef GRADIENT_ENGINE_CONFIGURATION_MANAGER_H
#define GRADIENT_ENGINE_CONFIGURATION_MANAGER_H

#include <string>

/**
 * @brief Stores and validates daemon configuration from CLI arguments.
 *
 * All configuration values have sensible defaults matching the CUEMS
 * ecosystem conventions. Future phases will add XML file parsing via
 * the confPath_ directory.
 */
class ConfigurationManager {
public:
    /**
     * @brief Construct with default configuration values.
     *
     * Defaults: midiPort_ = "Midi Through Port-0", logLevel_ = "info",
     * confPath_ = "/etc/cuems".
     */
    ConfigurationManager();

    ~ConfigurationManager() = default;

    /**
     * @brief Parse command-line arguments into configuration fields.
     *
     * @param argc Argument count from main().
     * @param argv Argument vector from main().
     * @return 0 on success, -1 if --help or --version was requested
     *         (caller should exit cleanly), >0 on error.
     *
     * @throws None. Errors are reported to stderr and indicated via
     *         return value.
     *
     * @par Example:
     * @code
     * ConfigurationManager cfg;
     * int r = cfg.parseArgs(argc, argv);
     * if (r != 0) return (r < 0) ? 0 : 1;
     * @endcode
     */
    int parseArgs(int argc, char** argv);

    /** @brief Get the configured MIDI port name. */
    const std::string& getMidiPort() const { return midiPort_; }

    /** @brief Get the configured log level name. */
    const std::string& getLogLevel() const { return logLevel_; }

    /** @brief Get the configured CUEMS configuration directory path. */
    const std::string& getConfPath() const { return confPath_; }

private:
    std::string midiPort_;
    std::string logLevel_;
    std::string confPath_;

    /**
     * @brief Print usage information to stdout.
     */
    void printUsage() const;

    /**
     * @brief Print version information to stdout.
     */
    void printVersion() const;

    /**
     * @brief Validate that logLevel_ is a recognised syslog level name.
     *
     * @return true if valid, false otherwise.
     */
    bool validateLogLevel() const;
};

#endif // GRADIENT_ENGINE_CONFIGURATION_MANAGER_H
