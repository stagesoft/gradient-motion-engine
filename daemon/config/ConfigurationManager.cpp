/**
 * @file ConfigurationManager.cpp
 * @brief Implementation of CLI argument parsing and validation.
 */

#include "ConfigurationManager.h"

#include <getopt.h>
#include <iostream>
#include <algorithm>
#include <vector>
#include <climits>
#include <unistd.h>

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
    , nngUrl_("tcp://127.0.0.1:9093")
    , nodeName_("")
{
}

int ConfigurationManager::parseArgs(int argc, char** argv) {
    static struct option longOptions[] = {
        {"midi-port",  required_argument, nullptr, 'm'},
        {"log-level",  required_argument, nullptr, 'l'},
        {"conf-path",  required_argument, nullptr, 'c'},
        {"nng-url",    required_argument, nullptr, 'u'},
        {"node-name",  required_argument, nullptr, 'n'},
        {"help",       no_argument,       nullptr, 'h'},
        {"version",    no_argument,       nullptr, 'V'},
        {nullptr,      0,                 nullptr,  0 }
    };

    // Reset getopt state for repeated calls (e.g. testing)
    optind = 1;

    int opt;
    while ((opt = getopt_long(argc, argv, "m:l:c:u:n:hV", longOptions, nullptr)) != -1) {
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
            case 'u':
                nngUrl_ = optarg;
                break;
            case 'n':
                nodeName_ = optarg;
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

    if (nodeName_.empty()) {
        char buf[HOST_NAME_MAX + 1];
        if (gethostname(buf, sizeof(buf)) != 0) {
            std::cerr << "Error: could not determine hostname for --node-name default" << std::endl;
            return 1;
        }
        buf[HOST_NAME_MAX] = '\0';
        nodeName_ = buf;
        std::cerr << "node_name defaulted to hostname: " << nodeName_ << std::endl;
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
        << "  -u, --nng-url <url>      NNG bus dial URL\n"
        << "                           (default: tcp://127.0.0.1:9093)\n"
        << "  -n, --node-name <name>   Node name for NNG bus filtering\n"
        << "                           (default: system hostname)\n"
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
