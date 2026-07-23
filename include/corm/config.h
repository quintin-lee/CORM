#ifndef CORM_CONFIG_H
#define CORM_CONFIG_H

#include <stdbool.h>

typedef struct {
    int max_open_conns;
    int max_idle_conns;
    int conn_max_lifetime_ms;
    int timeout_ms;
    bool verbose_logging;
} corm_config_t;

#define CORM_DEFAULT_CONFIG {0, 2, 0, 30000, false}

#endif /* CORM_CONFIG_H */
