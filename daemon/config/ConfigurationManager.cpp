/**
 * @file ConfigurationManager.cpp
 * @brief Implementation of CLI argument parsing and validation.
 */

#include "ConfigurationManager.h"

#include <getopt.h>
#include <iostream>
#include <algorithm>
#include <vector>

// Version string — update per release
static const char* VERSION_STRING = "gradient-motiond 0.1.0 (Phase 0)";

static const std::vector<std::string> VALID_LOG_LEVELS = {
    "emergency", "alert", "critical", "error",
    "warning", "notice", "info", "debug"
};

ConfigurationManager::ConfigurationManager()
    : midiPort_("Midi Through Port-0")
    , logLevel_("info")
    , confPath_("/etc/cuems")
{
}

int ConfigurationManager::parseArgs(int argc, char** argv) {
    static struct option longOptions[] = {
        {"midi-port", required_argument, nullptr, 'm'},
        {"log-level", required_argument, nullptr, 'l'},
        {"conf-path", required_argument, nullptr, 'c'},
        {"help",      no_argument,       nullptr, 'h'},
        {"version",   no_argument,       nullptr, 'V'},
        {nullptr,     0,                 nullptr,  0 }
    };

    // Reset getopt state for repeated calls (e.g. testing)
    optind = 1;

    int opt;
    while ((opt = getopt_long(argc, argv, "m:l:c:hV", longOptions, nullptr)) != -1) {
        switch (opt) {
            case 'm':
                midiPort_ = optarg;
                break;
            case 'l':
                logLevel_ = optarg;
                break;
            case 'c':
                confPath_ = optarg;
                break;
            case 'h':
                printUsage();
                return -1;
            case 'V':
                printVersion();
                return -1;
            default:
                printUsage();
                return 1;
        }
    }

    if (!validateLogLevel()) {
        std::cerr << "Error: invalid log level '" << logLevel_ << "'" << std::endl;
        std::cerr << "Valid levels: emergency, alert, critical, error, "
                  << "warning, notice, info, debug" << std::endl;
        return 1;
    }

    return 0;
}

void ConfigurationManager::printUsage() const {
    std::cout
        << "Usage: gradient-motiond [OPTIONS]\n"
        << "\n"
        << "Options:\n"
        << "  -m, --midi-port <name>   MIDI port name for MTC reception\n"
        << "                           (default: \"Midi Through Port-0\")\n"
        << "  -l, --log-level <level>  Log verbosity: emergency, alert, critical,\n"
        << "                           error, warning, notice, info, debug\n"
        << "                           (default: info)\n"
        << "  -c, --conf-path <path>   Path to CUEMS configuration directory\n"
        << "                           (default: /etc/cuems)\n"
        << "  -h, --help               Print this help message and exit\n"
        << "  -V, --version            Print version information and exit\n";
}

void ConfigurationManager::printVersion() const {
    std::cout << VERSION_STRING << std::endl;
}

bool ConfigurationManager::validateLogLevel() const {
    return std::find(VALID_LOG_LEVELS.begin(), VALID_LOG_LEVELS.end(),
                     logLevel_) != VALID_LOG_LEVELS.end();
}
