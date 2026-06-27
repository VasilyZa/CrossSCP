#include "crossscp/scp_logger.h"

#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>

namespace {

std::atomic<scp_log_level_t> g_min_level{SCP_LOG_INFO};
std::mutex g_log_mutex;

struct LogCfg {
  scp_log_cb callback = nullptr;
  void* userdata = nullptr;
};

LogCfg g_log_cfg;

const char* LevelPrefix(int level) {
  switch (level) {
    case SCP_LOG_DEBUG: return "[D]";
    case SCP_LOG_INFO:  return "[I]";
    case SCP_LOG_WARN:  return "[W]";
    case SCP_LOG_ERROR: return "[E]";
    default:            return "[?]";
  }
}

}  // anonymous namespace

void scp_set_log_level(scp_log_level_t level) {
  g_min_level.store(level, std::memory_order_relaxed);
}

void scp_set_log_callback(scp_log_cb callback, void* userdata) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_log_cfg.callback = callback;
  g_log_cfg.userdata = userdata;
}

namespace scp {
namespace internal {

void LogInternal(int level, const char* file, int line,
                 const char* fmt, ...) {
  if (level < g_min_level.load(std::memory_order_relaxed)) return;

  // Format message
  char buf[4096];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (n < 0) return;
  if (static_cast<size_t>(n) >= sizeof(buf)) n = sizeof(buf) - 1;

  // Build final log line
  // Extract filename from path
  const char* fname = strrchr(file, '/');
  if (!fname) fname = strrchr(file, '\\');
  if (!fname) fname = file;
  else fname++;

  char linebuf[4600];
  snprintf(linebuf, sizeof(linebuf), "%s [%s:%d] %s\n",
           LevelPrefix(level), fname, line, buf);

  // Route to callback (thread-safe snapshot)
  scp_log_cb cb = nullptr;
  void* ud = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    cb = g_log_cfg.callback;
    ud = g_log_cfg.userdata;
  }

  if (cb) {
    cb(level, linebuf, ud);
  } else {
    // Default: write to stderr
    fputs(linebuf, stderr);
    fflush(stderr);
  }
}

}  // namespace internal
}  // namespace scp
