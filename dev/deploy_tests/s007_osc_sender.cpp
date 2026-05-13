/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file osc_sender.cpp
 * @brief Minimal liblo CLI tool for deploy-test scripts.
 *
 * Usage:
 *   osc_sender <port> start_fade <motion_id> <node> <cb_host> <cb_port> <cb_path> \
 *              <start_val> <end_val> <start_mtc_ms> <dur_ms> <curve_type> <curve_params_json>
 *   osc_sender <port> cancel_motion <motion_id> <node>
 *   osc_sender <port> cancel_all   <node>
 *
 * Wire contract (parseFadeOscCommand.cpp kExpectedTypes "sssisffhiss", 11 args):
 *   argv[0]  s  motion_id
 *   argv[1]  s  node_name
 *   argv[2]  s  osc_host  (callback host, e.g. "127.0.0.1")
 *   argv[3]  i  osc_port  (callback port, e.g. 0 for none)
 *   argv[4]  s  osc_path  (callback OSC path, e.g. "/status")
 *   argv[5]  f  start_value
 *   argv[6]  f  end_value
 *   argv[7]  h  start_mtc_ms  (int64)
 *   argv[8]  i  duration_ms   (must be > 0)
 *   argv[9]  s  curve_type    (e.g. "linear")
 *   argv[10] s  curve_params_json (JSON object string, e.g. "{}")
 *
 * All messages sent to 127.0.0.1:<port>.
 * Exit 0 on success, 1 on error.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <lo/lo.h>

static void usage() {
    std::fprintf(stderr,
        "Usage:\n"
        "  osc_sender <port> start_fade <motion_id> <node> <cb_host> <cb_port> <cb_path>"
        " <start_val> <end_val> <start_mtc_ms> <dur_ms> <curve_type> <curve_params_json>\n"
        "  osc_sender <port> cancel_motion <motion_id> <node>\n"
        "  osc_sender <port> cancel_all <node>\n");
}

int main(int argc, char* argv[]) {
    if (argc < 4) { usage(); return 1; }

    const char* port = argv[1];
    const char* cmd  = argv[2];

    lo_address dest = lo_address_new("127.0.0.1", port);
    if (!dest) {
        std::fprintf(stderr, "osc_sender: lo_address_new failed\n");
        return 1;
    }

    int ret = 0;

    if (std::strcmp(cmd, "cancel_all") == 0) {
        if (argc < 4) { usage(); lo_address_free(dest); return 1; }
        const char* node = argv[3];
        ret = lo_send(dest, "/gradient/cancel_all", "s", node);

    } else if (std::strcmp(cmd, "cancel_motion") == 0) {
        if (argc < 5) { usage(); lo_address_free(dest); return 1; }
        const char* motion_id = argv[3];
        const char* node      = argv[4];
        ret = lo_send(dest, "/gradient/cancel_motion", "ss", motion_id, node);

    } else if (std::strcmp(cmd, "start_fade") == 0) {
        // expects 11 positional args after "start_fade":
        //   motion_id node cb_host cb_port cb_path start_val end_val start_mtc_ms dur_ms curve_type curve_params_json
        // argv[0]=prog argv[1]=port argv[2]=start_fade argv[3..13]=args  → argc=14
        if (argc < 14) { usage(); lo_address_free(dest); return 1; }

        const char* motion_id       = argv[3];
        const char* node            = argv[4];
        const char* cb_host         = argv[5];
        int32_t     cb_port         = (int32_t)std::atoi(argv[6]);
        const char* cb_path         = argv[7];
        float       start_val       = (float)std::atof(argv[8]);
        float       end_val         = (float)std::atof(argv[9]);
        int64_t     start_mtc_ms    = (int64_t)std::atoll(argv[10]);
        int32_t     dur_ms          = (int32_t)std::atoi(argv[11]);
        const char* curve_type      = argv[12];
        const char* curve_params    = argv[13];

        // Wire tag: sssisffhiss  (11 args)
        ret = lo_send(dest, "/gradient/start_fade", "sssisffhiss",
                      motion_id,    // s [0]
                      node,         // s [1]
                      cb_host,      // s [2]
                      cb_port,      // i [3]
                      cb_path,      // s [4]
                      start_val,    // f [5]
                      end_val,      // f [6]
                      start_mtc_ms, // h [7]
                      dur_ms,       // i [8]
                      curve_type,   // s [9]
                      curve_params  // s [10]
        );

    } else {
        std::fprintf(stderr, "osc_sender: unknown command '%s'\n", cmd);
        usage();
        lo_address_free(dest);
        return 1;
    }

    lo_address_free(dest);

    if (ret < 0) {
        std::fprintf(stderr, "osc_sender: lo_send failed (%d)\n", ret);
        return 1;
    }

    const char* addr =
        (std::strcmp(cmd, "start_fade") == 0)    ? "/gradient/start_fade"   :
        (std::strcmp(cmd, "cancel_motion") == 0) ? "/gradient/cancel_motion" :
                                                   "/gradient/cancel_all";
    std::printf("osc_sender: sent %s to 127.0.0.1:%s (%d bytes)\n", addr, port, ret);
    return 0;
}
