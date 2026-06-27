// =============================================================================
// scp_core.cpp  -  Main C API implementation (wrapper around C++ internals)
// =============================================================================
// This file provides the `extern "C"` functions declared in scp_api.h.
// It delegates to the C++ classes in the `scp` namespace.
// =============================================================================

#include "crossscp/scp_api.h"
#include "crossscp/scp_logger.h"

#include "ssh_connection.h"
#include "sftp_session.h"
#include "sftp_transfer.h"
#include "connection_pool.h"

#include <libssh2.h>
#include <libssh2_sftp.h>
#include <cstring>
#include <cstdlib>
#include <atomic>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <signal.h>
#endif

namespace {

std::atomic<bool> g_initialized{false};

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Library lifecycle
// ---------------------------------------------------------------------------

scp_error_t scp_init(void) {
  if (g_initialized.load(std::memory_order_acquire)) return SCP_OK;

#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    return SCP_ERROR_GENERIC;
  }
#else
  // Ignore SIGPIPE - libssh2 handles broken pipes itself
  signal(SIGPIPE, SIG_IGN);
#endif

  // Initialize libssh2 globally
  int ret = libssh2_init(0);
  if (ret != 0) {
    SCP_LOG_ERROR("libssh2_init failed: %d", ret);
    return SCP_ERROR_GENERIC;
  }

  // Set default log callback to null (uses stderr)
  scp_set_log_level(SCP_LOG_INFO);

  g_initialized.store(true, std::memory_order_release);
  SCP_LOG_INFO("crossscp native library initialized (version %s)", SCP_VERSION);
  return SCP_OK;
}

void scp_cleanup(void) {
  if (!g_initialized.load(std::memory_order_acquire)) return;

  scp::SessionPool::Instance().Clear();
  scp::TransferRegistry::Instance().Clear();
  libssh2_exit();

#ifdef _WIN32
  WSACleanup();
#endif

  g_initialized.store(false, std::memory_order_release);
  SCP_LOG_INFO("crossscp native library cleaned up");
}

const char* scp_get_version(void) {
  return SCP_VERSION;
}

// ---------------------------------------------------------------------------
// Error strings
// ---------------------------------------------------------------------------

const char* scp_error_string(scp_error_t error) {
  switch (error) {
    case SCP_OK:                   return "Success";
    case SCP_ERROR_GENERIC:        return "Generic error";
    case SCP_ERROR_NOT_INITIALIZED:return "Library not initialized";
    case SCP_ERROR_INVALID_ARG:    return "Invalid argument";
    case SCP_ERROR_CONNECT:        return "Failed to connect to host";
    case SCP_ERROR_HOST_NOT_FOUND: return "Host not found (DNS resolution failed)";
    case SCP_ERROR_TIMEOUT:        return "Connection timed out";
    case SCP_ERROR_NETWORK:        return "Network I/O error";
    case SCP_ERROR_DISCONNECTED:   return "Session disconnected";
    case SCP_ERROR_AUTH:           return "Authentication failed";
    case SCP_ERROR_AUTH_METHOD:    return "Unsupported authentication method";
    case SCP_ERROR_AUTH_DENIED:    return "Access denied - invalid credentials";
    case SCP_ERROR_AUTH_KEY:       return "Invalid or unreadable key file";
    case SCP_ERROR_AUTH_PASSPHRASE:return "Wrong key passphrase";
    case SCP_ERROR_AUTH_PARTIAL:   return "Partial authentication success";
    case SCP_ERROR_AUTH_BANNER:    return "Server sent authentication banner";
    case SCP_ERROR_SFTP:           return "SFTP protocol error";
    case SCP_ERROR_SFTP_INIT:      return "Failed to initialize SFTP subsystem";
    case SCP_ERROR_SFTP_CHANNEL:   return "Failed to open SFTP channel";
    case SCP_ERROR_FILE:           return "File operation error";
    case SCP_ERROR_NOT_FOUND:      return "File or directory not found";
    case SCP_ERROR_PERMISSION:     return "Permission denied";
    case SCP_ERROR_ALREADY_EXISTS: return "File already exists";
    case SCP_ERROR_NOT_A_DIR:      return "Not a directory";
    case SCP_ERROR_IS_A_DIR:       return "Is a directory";
    case SCP_ERROR_FILE_OPEN:      return "Failed to open file";
    case SCP_ERROR_FILE_WRITE:     return "Failed to write file";
    case SCP_ERROR_FILE_READ:      return "Failed to read file";
    case SCP_ERROR_DISK_FULL:      return "Insufficient disk space";
    case SCP_ERROR_QUOTA_EXCEEDED: return "Quota exceeded";
    case SCP_ERROR_TRANSFER:       return "Transfer error";
    case SCP_ERROR_CANCELLED:      return "Transfer cancelled by user";
    case SCP_ERROR_PAUSED:         return "Transfer is paused";
    case SCP_ERROR_CHECKSUM:       return "Integrity check failed";
    case SCP_ERROR_REMOTE_IO:      return "Remote I/O error during transfer";
    case SCP_ERROR_MEMORY:         return "Memory allocation failed";
    case SCP_ERROR_BUFFER_TOO_SMALL:return "Buffer too small";
    case SCP_ERROR_INVALID_HANDLE: return "Invalid handle";
    case SCP_ERROR_POOL_EXHAUSTED: return "Connection pool exhausted";
    case SCP_ERROR_STATE_MACHINE:  return "Invalid state transition";
    default:                       return "Unknown error";
  }
}

// ---------------------------------------------------------------------------
// Cipher configuration
// ---------------------------------------------------------------------------

scp_error_t scp_set_cipher_enabled(scp_cipher_t cipher, bool enabled) {
  // Cipher preferences are applied per-connection in SshConnection::Connect()
  // This is a placeholder for future global cipher management.
  (void)cipher;
  (void)enabled;
  return SCP_OK;
}

// ---------------------------------------------------------------------------
// Logging helper - delegates to logger.cpp's scp_set_log_callback
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------

scp_error_t scp_connect(const scp_connect_config_t* config,
                        scp_session_t* out_session) {
  if (!g_initialized.load(std::memory_order_acquire)) {
    return SCP_ERROR_NOT_INITIALIZED;
  }
  if (!config || !out_session) return SCP_ERROR_INVALID_ARG;

  // Check for existing connection (connection reuse)
  uint16_t port = config->port > 0 ? config->port : 22;
  scp_session_t existing = scp::SessionPool::Instance().FindExisting(
      config->host, port, config->username);
  if (existing) {
    SCP_LOG_INFO("Reusing existing connection to %s@%s:%u",
                 config->username, config->host, port);
    *out_session = existing;
    return SCP_OK;
  }

  auto conn = std::make_unique<scp::SshConnection>();
  scp_error_t err = conn->Connect(config);
  if (err != SCP_OK) {
    return err;
  }

  *out_session = scp::SessionPool::Instance().Register(
      std::move(conn), config->host, config->username, port);
  return SCP_OK;
}

scp_error_t scp_connect2(
    const char* host, uint16_t port, const char* username,
    const char* password,
    const char* key_path, const char* passphrase,
    uint32_t timeout_s,
    scp_session_t* out_session) {
  scp_connect_config_t config;
  memset(&config, 0, sizeof(config));
  config.host = host;
  config.port = port;
  config.username = username;
  config.tcp_nodelay = true;
  config.connect_timeout_s = timeout_s > 0 ? timeout_s : 30;

  if (password && password[0]) {
    config.auth_type = SCP_AUTH_PASSWORD;
    config.password = password;
  } else if (key_path && key_path[0]) {
    config.auth_type = SCP_AUTH_PUBLICKEY;
    config.key_path = key_path;
    config.passphrase = passphrase;
  } else {
    config.auth_type = SCP_AUTH_NONE;
  }

  return scp_connect(&config, out_session);
}

scp_error_t scp_disconnect(scp_session_t session) {
  return scp::SessionPool::Instance().Remove(session);
}

bool scp_is_connected(scp_session_t session) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  return conn && conn->IsConnected();
}

const char* scp_session_get_host(scp_session_t session) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  if (!conn) return nullptr;
  return conn->GetHost().c_str();
}

scp_error_t scp_keepalive(scp_session_t session) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  if (!conn) return SCP_ERROR_INVALID_HANDLE;
  return conn->Keepalive();
}

const char* scp_session_get_last_error(scp_session_t session) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  if (!conn) return "Invalid session handle";
  return conn->GetLastError().c_str();
}

// ---------------------------------------------------------------------------
// File / directory operations
// ---------------------------------------------------------------------------

scp_error_t scp_list_dir(scp_session_t session,
                         const char* path,
                         scp_file_info_t** out_files,
                         uint32_t* out_count) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  if (!conn) return SCP_ERROR_INVALID_HANDLE;

  scp::SftpSession sftp(conn);
  return sftp.ListDir(path, out_files, out_count);
}

scp_error_t scp_stat(scp_session_t session,
                     const char* path,
                     scp_file_info_t* out_info) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  if (!conn) return SCP_ERROR_INVALID_HANDLE;

  scp::SftpSession sftp(conn);
  return sftp.Stat(path, out_info);
}

scp_error_t scp_mkdir(scp_session_t session,
                      const char* path,
                      uint32_t permissions) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  if (!conn) return SCP_ERROR_INVALID_HANDLE;

  scp::SftpSession sftp(conn);
  return sftp.Mkdir(path, permissions);
}

scp_error_t scp_rmdir(scp_session_t session, const char* path) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  if (!conn) return SCP_ERROR_INVALID_HANDLE;

  scp::SftpSession sftp(conn);
  return sftp.Rmdir(path);
}

scp_error_t scp_unlink(scp_session_t session, const char* path) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  if (!conn) return SCP_ERROR_INVALID_HANDLE;

  scp::SftpSession sftp(conn);
  return sftp.Unlink(path);
}

scp_error_t scp_rename(scp_session_t session,
                       const char* old_path,
                       const char* new_path) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  if (!conn) return SCP_ERROR_INVALID_HANDLE;

  scp::SftpSession sftp(conn);
  return sftp.Rename(old_path, new_path);
}

scp_error_t scp_chmod(scp_session_t session,
                      const char* path,
                      uint32_t permissions) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  if (!conn) return SCP_ERROR_INVALID_HANDLE;

  scp::SftpSession sftp(conn);
  return sftp.Chmod(path, permissions);
}

scp_error_t scp_df(scp_session_t session,
                   const char* path,
                   uint64_t* out_total,
                   uint64_t* out_free) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  if (!conn) return SCP_ERROR_INVALID_HANDLE;

  scp::SftpSession sftp(conn);
  return sftp.Df(path, out_total, out_free);
}

scp_error_t scp_touch(scp_session_t session, const char* path) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  if (!conn) return SCP_ERROR_INVALID_HANDLE;
  if (!path || !path[0]) return SCP_ERROR_INVALID_ARG;

  LIBSSH2_SFTP* sftp = conn->GetSftpSession();
  if (!sftp) return SCP_ERROR_SFTP;

  LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_open(
      sftp, path,
      LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT,
      LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
      LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
  if (!handle) return SCP_ERROR_FILE_OPEN;
  libssh2_sftp_close(handle);
  return SCP_OK;
}

void scp_free_file_list(scp_file_info_t* files, uint32_t count) {
  scp::SftpSession::FreeFileList(files, count);
}

// ---------------------------------------------------------------------------
// Transfer operations
// ---------------------------------------------------------------------------

scp_error_t scp_transfer_start(scp_session_t session,
                               const scp_transfer_config_t* config,
                               scp_transfer_t* out_transfer) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  if (!conn) return SCP_ERROR_INVALID_HANDLE;
  if (!config || !out_transfer) return SCP_ERROR_INVALID_ARG;

  auto transfer = std::make_unique<scp::SftpTransfer>(conn, config);
  scp_error_t err = transfer->Start();
  if (err != SCP_OK) return err;

  // Transfer is self-managing; we only need to register its handle.
  // The TransferRegistry holds the raw pointer; the unique_ptr releases ownership.
  auto* raw = transfer.release();
  *out_transfer = scp::TransferRegistry::Instance().Register(raw);
  return SCP_OK;
}

scp_error_t scp_batch_transfer(scp_session_t session,
                               const scp_batch_config_t* config) {
  auto* conn = scp::SessionPool::Instance().Find(session);
  if (!conn) return SCP_ERROR_INVALID_HANDLE;
  if (!config || config->count == 0) return SCP_ERROR_INVALID_ARG;

  uint32_t max_concurrent = config->max_concurrent;
  if (max_concurrent == 0) max_concurrent = 4;  // default concurrent transfers

  // For simplicity, we launch all transfers and let the OS scheduler
  // handle concurrency. A production implementation would use a work queue
  // with `max_concurrent` slots.
  SCP_LOG_INFO("Starting batch transfer of %u files (max concurrent: %u)",
               config->count, max_concurrent);

  for (uint32_t i = 0; i < config->count; i++) {
    auto transfer = std::make_unique<scp::SftpTransfer>(
        conn, &config->transfers[i]);
    scp_error_t err = transfer->Start();
    if (err != SCP_OK) {
      SCP_LOG_ERROR("Failed to start transfer %u/%u: %s",
                    i + 1, config->count, scp_error_string(err));
      return err;
    }
    auto* raw = transfer.release();
    scp::TransferRegistry::Instance().Register(raw);
  }

  return SCP_OK;
}

scp_error_t scp_transfer_pause(scp_transfer_t transfer) {
  auto* t = scp::TransferRegistry::Instance().Find(transfer);
  if (!t) return SCP_ERROR_INVALID_HANDLE;
  return t->Pause();
}

scp_error_t scp_transfer_resume(scp_transfer_t transfer) {
  auto* t = scp::TransferRegistry::Instance().Find(transfer);
  if (!t) return SCP_ERROR_INVALID_HANDLE;
  return t->Resume();
}

scp_error_t scp_transfer_cancel(scp_transfer_t transfer) {
  auto* t = scp::TransferRegistry::Instance().Find(transfer);
  if (!t) return SCP_ERROR_INVALID_HANDLE;
  return t->Cancel();
}

scp_error_t scp_transfer_wait(scp_transfer_t transfer) {
  auto* t = scp::TransferRegistry::Instance().Find(transfer);
  if (!t) return SCP_ERROR_INVALID_HANDLE;
  return t->Wait();
}

scp_error_t scp_transfer_get_progress(scp_transfer_t transfer,
                                      uint64_t* transferred,
                                      uint64_t* total) {
  auto* t = scp::TransferRegistry::Instance().Find(transfer);
  if (!t) return SCP_ERROR_INVALID_HANDLE;
  return t->GetProgress(transferred, total);
}

void scp_transfer_free(scp_transfer_t transfer) {
  auto* t = scp::TransferRegistry::Instance().Find(transfer);
  if (t) {
    t->Cancel();
    t->Wait();
    delete t;
    scp::TransferRegistry::Instance().Remove(transfer);
  }
}
