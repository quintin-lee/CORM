#ifndef CORM_CONFIG_H
#define CORM_CONFIG_H

#include <stdbool.h>

#include <stdint.h>

typedef enum {
    CORM_LOG_DEBUG,
    CORM_LOG_INFO,
    CORM_LOG_WARN,
    CORM_LOG_ERROR
} corm_log_level_t;

typedef void (*corm_logger_fn)(corm_log_level_t level, const char *sql, uint64_t elapsed_us, void *user_data);

typedef struct {
  int max_open_conns;
  int max_idle_conns;
  int conn_max_lifetime_ms;
  int timeout_ms;
  bool verbose_logging;
} corm_config_t;

#define CORM_DEFAULT_CONFIG {0, 2, 0, 30000, false}

#endif /* CORM_CONFIG_H */
