#ifndef CORM_CONFIG_H
#define CORM_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int max_open_conns;        /* 0 = unlimited */
    int max_idle_conns;        /* default: 2 */
    int conn_max_lifetime_ms;  /* 0 = unlimited */
    int timeout_ms;            /* query timeout */
    bool verbose_logging;
} corm_config_t;

#define CORM_DEFAULT_CONFIG { \
    .max_open_conns = 0,      \
    .max_idle_conns = 2,      \
    .conn_max_lifetime_ms = 0,\
    .timeout_ms = 30000,      \
    .verbose_logging = false, \
}

#ifdef __cplusplus
}
#endif

#endif /* CORM_CONFIG_H */
