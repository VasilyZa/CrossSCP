#ifndef SCP_LOGGER_H_
#define SCP_LOGGER_H_

#include "scp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Log levels matching the callback `level` parameter.
typedef enum {
  SCP_LOG_DEBUG = 0,
  SCP_LOG_INFO  = 1,
  SCP_LOG_WARN  = 2,
  SCP_LOG_ERROR = 3,
  SCP_LOG_NONE  = 4,
} scp_log_level_t;

// Set global minimum log level. Messages below this level are dropped.
void scp_set_log_level(scp_log_level_t level);

// Set global log callback. NULL disables logging.
// The callback may be called from any thread; implementations must be thread-safe.
void scp_set_log_callback(scp_log_cb callback, void* userdata);

#ifdef __cplusplus
}

// C++ convenience macros for internal use
#include <cstdio>
#include <cstdarg>

namespace scp {
namespace internal {

void LogInternal(int level, const char* file, int line,
                 const char* fmt, ...);

}  // namespace internal
}  // namespace scp

#define SCP_LOG_DEBUG(fmt, ...) \
  scp::internal::LogInternal(SCP_LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define SCP_LOG_INFO(fmt, ...) \
  scp::internal::LogInternal(SCP_LOG_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define SCP_LOG_WARN(fmt, ...) \
  scp::internal::LogInternal(SCP_LOG_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define SCP_LOG_ERROR(fmt, ...) \
  scp::internal::LogInternal(SCP_LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif  // __cplusplus

#endif  // SCP_LOGGER_H_
