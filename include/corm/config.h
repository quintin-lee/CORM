#ifndef CORM_CONFIG_H
#define CORM_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/** Log severity levels for corm_set_logger(). */
typedef enum {
  CORM_LOG_DEBUG, /**< Detailed debug information */
  CORM_LOG_INFO,  /**< General informational messages */
  CORM_LOG_WARN,  /**< Warning conditions */
  CORM_LOG_ERROR  /**< Error conditions */
} corm_log_level_t;

/** Callback for logging SQL queries.
 *
 * @param level     Severity of the log message
 * @param sql       The SQL query string being executed
 * @param elapsed_us  Execution time in microseconds
 * @param user_data  Opaque pointer passed via corm_set_logger()
 */
typedef void (*corm_logger_fn)(corm_log_level_t level, const char *sql,
                               uint64_t elapsed_us, void *user_data);

/** Connection pool configuration for corm_open_with_config(). */
typedef struct {
  int max_open_conns; /**< Maximum simultaneous connections (0 = unlimited) */
  int max_idle_conns; /**< Maximum idle connections in pool (default: 2) */
  int conn_max_lifetime_ms; /**< Max lifetime of a connection in ms (0 =
                               forever) */
  int timeout_ms;           /**< Pool acquire timeout in ms (default: 30000) */
  bool verbose_logging;     /**< Log all SQL queries when true */
  int stmt_cache_size; /**< Prepared statement cache size (0 = default 64) */
} corm_config_t;

/** Default config: max_open=0, max_idle=2, lifetime=0, timeout=30000ms,
 * verbose=false */
#define CORM_DEFAULT_CONFIG {0, 2, 0, 30000, false, 0}

#endif /* CORM_CONFIG_H */
