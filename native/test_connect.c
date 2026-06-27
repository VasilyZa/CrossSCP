#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crossscp/scp_api.h"

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <host> <port> <username> [password]\n", argv[0]);
        fprintf(stderr, "  e.g. %s nodeup1.yunmc.vip 60002 myuser\n", argv[0]);
        return 1;
    }

    scp_init();

    scp_connect_config_t cfg = {0};
    cfg.host              = argv[1];
    cfg.port              = (uint16_t)atoi(argv[2]);
    cfg.username          = argv[3];
    cfg.tcp_nodelay       = true;
    cfg.connect_timeout_s = 15;

    if (argc >= 5 && strlen(argv[4]) > 0) {
        cfg.auth_type = SCP_AUTH_PASSWORD;
        cfg.password = argv[4];
    } else {
        cfg.auth_type = SCP_AUTH_NONE;
    }

    scp_session_t session = NULL;
    scp_error_t err = scp_connect(&cfg, &session);
    if (err != SCP_OK) {
        fprintf(stderr, "Connect failed: %s (code %d)\n", scp_error_string(err), err);
        scp_cleanup();
        return 1;
    }

    printf("Connected to %s:%s as %s\n", argv[1], argv[2], argv[3]);

    // List root directory
    scp_file_info_t* files = NULL;
    uint32_t count = 0;
    err = scp_list_dir(session, "/", &files, &count);
    if (err == SCP_OK) {
        printf("Root directory (%u entries):\n", count);
        for (uint32_t i = 0; i < count; i++) {
            printf("  %c %10llu  %s\n",
                   files[i].is_dir ? 'd' : '-',
                   (unsigned long long)files[i].filesize,
                   files[i].filename);
        }
        scp_free_file_list(files, count);
    } else {
        printf("List dir failed: %s (code %d)\n", scp_error_string(err), err);
    }

    scp_disconnect(session);
    scp_cleanup();
    return 0;
}
