/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file ConfigurationManager.cpp
 * @brief Implementation of CLI argument parsing and validation.
 */

#include "ConfigurationManager.h"
#include "version.h"

#include <getopt.h>
#include <iostream>
#include <algorithm>
#include <vector>
#include <climits>
#include <cstdlib>
#include <unistd.h>

static const std::vector<std::string> VALID_LOG_LEVELS = {
    "emergency", "alert", "critical", "error",
    "warning", "notice", "info", "debug"
};

static constexpr int DEFAULT_OSC_PORT = 7100;

ConfigurationManager::ConfigurationManager()
    : midiPort_("Midi Through Port-0")
    , logLevel_("info")
    , confPath_("/etc/cuems")
    , oscPort_(DEFAULT_OSC_PORT)
    , nodeName_("")
{
    // Resolution order for oscPort_: CLI flag > env var > default 7100.
    // CLI flag is applied in parseArgs(); env var checked here at construction.
    const char* envPort = std::getenv("CUEMS_GRADIENT_OSC_PORT");
    if (envPort) {
        int p = std::atoi(envPort);
        if (p > 0 && p <= 65535)
            oscPort_ = p;
    }
}

int ConfigurationManager::parseArgs(int argc, char** argv) {
    static struct option longOptions[] = {
        {"midi-port",  required_argument, nullptr, 'm'},
        {"log-level",  required_argument, nullptr, 'l'},
        {"conf-path",  required_argument, nullptr, 'c'},
        {"osc-port",   required_argument, nullptr, 'p'},
        {"node-name",  required_argument, nullptr, 'n'},
        {"help",       no_argument,       nullptr, 'h'},
        {"version",    no_argument,       nullptr, 'V'},
        {nullptr,      0,                 nullptr,  0 }
    };

    // Reset getopt state for repeated calls (e.g. testing)
    optind = 1;

    int opt;
    while ((opt = getopt_long(argc, argv, "m:l:c:p:n:hV", longOptions, nullptr)) != -1) {
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
            case 'p': {
                int p = std::atoi(optarg);
                if (p <= 0 || p > 65535) {
                    std::cerr << "Error: --osc-port must be 1–65535, got '" << optarg << "'" << std::endl;
                    return 1;
                }
                oscPort_ = p;
                break;
            }
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
        << "  -p, --osc-port <port>    UDP port for OSC command input (1-65535)\n"
        << "                           Env override: CUEMS_GRADIENT_OSC_PORT\n"
        << "                           (default: 7100)\n"
        << "  -n, --node-name <name>   Node name for OSC message filtering\n"
        << "                           (default: system hostname)\n"
        << "  -h, --help               Print this help message and exit\n"
        << "  -V, --version            Print version information and exit\n";
}

void ConfigurationManager::printVersion() const {
    std::cout << GME_VERSION_STRING << std::endl;
}

bool ConfigurationManager::validateLogLevel() const {
    return std::find(VALID_LOG_LEVELS.begin(), VALID_LOG_LEVELS.end(),
                     logLevel_) != VALID_LOG_LEVELS.end();
}
