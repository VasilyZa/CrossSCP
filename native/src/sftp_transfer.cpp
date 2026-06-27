#include "sftp_transfer.h"
#include "ssh_connection.h"
#include "memory_guard.h"
#include "crossscp/scp_logger.h"

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <algorithm>

#ifdef _WIN32
#include <io.h>
#define fseeko _fseeki64
#define ftello _ftelli64
#else
#include <unistd.h>
#endif

namespace scp {

// Defaults
static constexpr uint32_t kDefaultMaxConcurrent = 32;
static constexpr uint32_t kDefaultBlockSize      = 256 * 1024;  // 256KB
static constexpr uint32_t kDefaultProgressThrottleMs = 100;
static constexpr uint32_t kMinBlockSize          = 4 * 1024;    // 4KB min
static constexpr uint32_t kMaxBlockSize          = 8 * 1024 * 1024;  // 8MB max

static uint32_t ClampUint32(uint32_t val, uint32_t min_val, uint32_t max_val) {
  if (val == 0) return min_val;
  if (val < min_val) return min_val;
  if (val > max_val) return max_val;
  return val;
}

SftpTransfer::SftpTransfer(SshConnection* conn,
                           const scp_transfer_config_t* config)
    : conn_(conn) {
  memcpy(&config_, config, sizeof(scp_transfer_config_t));

  // Copy path strings so caller can free their copies immediately
  if (config_.local_path) {
    local_path_ = config_.local_path;
    config_.local_path = local_path_.c_str();
  }
  if (config_.remote_path) {
    remote_path_ = config_.remote_path;
    config_.remote_path = remote_path_.c_str();
  }

  // Clamp to reasonable values
  config_.max_concurrent = ClampUint32(config->max_concurrent, 1,
                                        kDefaultMaxConcurrent * 4);
  config_.block_size = ClampUint32(config->block_size, kMinBlockSize,
                                    kMaxBlockSize);
  config_.progress_throttle_ms = ClampUint32(config->progress_throttle_ms,
                                              10, 5000);

  if (config_.max_concurrent == 0) config_.max_concurrent = kDefaultMaxConcurrent;
  if (config_.block_size == 0) config_.block_size = kDefaultBlockSize;
  if (config_.progress_throttle_ms == 0)
    config_.progress_throttle_ms = kDefaultProgressThrottleMs;
}

SftpTransfer::~SftpTransfer() {
  Cancel();
  if (transfer_thread_ && transfer_thread_->joinable()) {
    transfer_thread_->join();
  }
}

scp_error_t SftpTransfer::Start() {
  TransferState expected = TransferState::kIdle;
  if (!state_.compare_exchange_strong(expected, TransferState::kRunning)) {
    SCP_LOG_WARN("Transfer already started (state=%d)", static_cast<int>(expected));
    return SCP_ERROR_STATE_MACHINE;
  }

  transfer_thread_ = std::make_unique<std::thread>(&SftpTransfer::TransferThread,
                                                    this);
  return SCP_OK;
}

scp_error_t SftpTransfer::Pause() {
  TransferState current = state_.load(std::memory_order_acquire);
  if (current != TransferState::kRunning) {
    return SCP_ERROR_STATE_MACHINE;
  }
  {
    std::lock_guard<std::mutex> lock(pause_mutex_);
    paused_flag_ = true;
  }
  state_.store(TransferState::kPaused, std::memory_order_release);
  return SCP_OK;
}

scp_error_t SftpTransfer::Resume() {
  TransferState expected = TransferState::kPaused;
  if (!state_.compare_exchange_strong(expected, TransferState::kRunning)) {
    return SCP_ERROR_STATE_MACHINE;
  }
  {
    std::lock_guard<std::mutex> lock(pause_mutex_);
    paused_flag_ = false;
  }
  pause_cv_.notify_all();
  return SCP_OK;
}

scp_error_t SftpTransfer::Cancel() {
  TransferState current = state_.load(std::memory_order_acquire);
  if (current == TransferState::kCancelled ||
      current == TransferState::kCompleted ||
      current == TransferState::kError) {
    return SCP_OK;  // Already in terminal state
  }
  state_.store(TransferState::kCancelled, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(pause_mutex_);
    paused_flag_ = false;
  }
  pause_cv_.notify_all();
  return SCP_OK;
}

scp_error_t SftpTransfer::Wait() {
  if (transfer_thread_ && transfer_thread_->joinable()) {
    transfer_thread_->join();
  }
  return GetError();
}

scp_error_t SftpTransfer::GetProgress(uint64_t* transferred, uint64_t* total) {
  if (transferred) *transferred = bytes_transferred_.load(std::memory_order_acquire);
  if (total) *total = bytes_total_.load(std::memory_order_acquire);
  return SCP_OK;
}

void SftpTransfer::TransferThread() {
  scp_error_t err;
  if (config_.direction == SCP_UPLOAD) {
    err = DoUpload();
  } else {
    err = DoDownload();
  }

  last_error_.store(err, std::memory_order_release);
  if (err == SCP_OK) {
    state_.store(TransferState::kCompleted, std::memory_order_release);
  } else if (err == SCP_ERROR_CANCELLED) {
    state_.store(TransferState::kCancelled, std::memory_order_release);
  } else if (err == SCP_ERROR_PAUSED) {
    state_.store(TransferState::kPaused, std::memory_order_release);
  } else {
    state_.store(TransferState::kError, std::memory_order_release);
  }
}

// Check if we should pause or cancel
static bool ShouldStop(const SftpTransfer* self, TransferState* out_current) {
  TransferState s = self->GetState();
  if (out_current) *out_current = s;
  return s == TransferState::kPaused ||
         s == TransferState::kCancelled;
}

// Wait while paused
static void WaitWhilePaused(std::mutex& mtx,
                             std::condition_variable& cv,
                             bool& flag,
                             const SftpTransfer* self) {
  std::unique_lock<std::mutex> lock(mtx);
  while (flag && self->GetState() != TransferState::kCancelled) {
    cv.wait(lock);
  }
}

scp_error_t SftpTransfer::DoUpload() {
  LIBSSH2_SFTP* sftp = conn_->GetSftpSession();
  if (!sftp) return SCP_ERROR_SFTP;

  const char* local_path  = config_.local_path;
  const char* remote_path = config_.remote_path;

  // Open local file for reading
  FILE* local_file = fopen(local_path, "rb");
  if (!local_file) {
    SCP_LOG_ERROR("Failed to open local file for upload: %s", local_path);
    return SCP_ERROR_FILE_OPEN;
  }

  // Get local file size
  fseeko(local_file, 0, SEEK_END);
  uint64_t total_size = ftello(local_file);
  fseeko(local_file, 0, SEEK_SET);
  bytes_total_.store(total_size, std::memory_order_release);

  // Handle resume: if resume is enabled, check remote file size
  uint64_t resume_offset = 0;
  int open_flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC;
  if (config_.resume) {
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int ret = libssh2_sftp_stat(sftp, remote_path, &attrs);
    if (ret == 0 && attrs.filesize < total_size) {
      resume_offset = attrs.filesize;
      // Open in append mode (no truncation)
      open_flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT;
      fseeko(local_file, resume_offset, SEEK_SET);
      SCP_LOG_INFO("Resuming upload from offset %llu/%llu",
                   (unsigned long long)resume_offset,
                   (unsigned long long)total_size);
    }
  }

  // Open remote file
  LIBSSH2_SFTP_HANDLE* remote_handle = libssh2_sftp_open(
      sftp, remote_path, open_flags,
      LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
      LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
  if (!remote_handle) {
    fclose(local_file);
    SCP_LOG_ERROR("Failed to open remote file for upload: %s", remote_path);
    return SCP_ERROR_FILE_OPEN;
  }

  // Allocate block buffer
  ScopedBuffer<char> block(config_.block_size);
  if (!block) {
    libssh2_sftp_close(remote_handle);
    fclose(local_file);
    return SCP_ERROR_MEMORY;
  }

  uint64_t bytes_sent = resume_offset;

  // Simple sequential upload with keepalive-style progress.
  // For true async concurrent requests, we would need to use
  // libssh2_session_block_directions() with a select loop.
  // For simplicity and reliability, we use a streaming sequential
  // approach with large buffers, which is sufficient for most use cases.
  // The WinSCP-level concurrency comes from multiple parallel transfers.

  while (bytes_sent < total_size) {
    // Check pause / cancel
    {
      TransferState st;
      if (ShouldStop(this, &st)) {
        libssh2_sftp_close(remote_handle);
        fclose(local_file);
        if (st == TransferState::kCancelled) {
          // Optionally clean up partial remote file
          if (cleanup_on_cancel_.load(std::memory_order_acquire)) {
            libssh2_sftp_unlink(sftp, remote_path);
          }
          return SCP_ERROR_CANCELLED;
        }
        // Paused: preserve state so caller can resume
        // TODO: store resume_offset for external resume
        return SCP_ERROR_PAUSED;
      }
      WaitWhilePaused(pause_mutex_, pause_cv_, paused_flag_, this);
    }

    size_t to_read = static_cast<size_t>(
        std::min<uint64_t>(config_.block_size, total_size - bytes_sent));
    size_t bytes_read = fread(block.get(), 1, to_read, local_file);
    if (bytes_read == 0) {
      if (ferror(local_file)) {
        SCP_LOG_ERROR("Error reading local file at offset %llu",
                      (unsigned long long)bytes_sent);
        libssh2_sftp_close(remote_handle);
        fclose(local_file);
        return SCP_ERROR_FILE_READ;
      }
      break;  // EOF
    }

    // Write to remote
    ssize_t rc = libssh2_sftp_write(remote_handle, block.get(), bytes_read);
    if (rc < 0) {
      SCP_LOG_ERROR("SFTP write failed at offset %llu", (unsigned long long)bytes_sent);
      libssh2_sftp_close(remote_handle);
      fclose(local_file);
      return SCP_ERROR_REMOTE_IO;
    }

    bytes_sent += rc;
    bytes_transferred_.store(bytes_sent, std::memory_order_release);
    ReportProgress(bytes_sent, total_size);
  }

  // Close handles
  libssh2_sftp_close(remote_handle);
  fclose(local_file);

  // Optional checksum verification
  if (config_.verify_checksum) {
    SCP_LOG_INFO("Upload complete (%llu bytes), skipping checksum (not yet implemented)",
                 (unsigned long long)bytes_sent);
  }

  bytes_transferred_.store(bytes_sent, std::memory_order_release);
  SCP_LOG_INFO("Upload complete: %s -> %s (%llu bytes)",
               local_path, remote_path, (unsigned long long)bytes_sent);
  return SCP_OK;
}

scp_error_t SftpTransfer::DoDownload() {
  LIBSSH2_SFTP* sftp = conn_->GetSftpSession();
  if (!sftp) return SCP_ERROR_SFTP;

  const char* remote_path = config_.remote_path;
  const char* local_path  = config_.local_path;

  // Stat remote file to get total size
  LIBSSH2_SFTP_ATTRIBUTES attrs;
  int ret = libssh2_sftp_stat(sftp, remote_path, &attrs);
  if (ret != 0) {
    SCP_LOG_ERROR("Failed to stat remote file: %s", remote_path);
    return SCP_ERROR_NOT_FOUND;
  }
  uint64_t total_size = attrs.filesize;
  bytes_total_.store(total_size, std::memory_order_release);

  // Handle resume
  uint64_t resume_offset = 0;
  const char* open_mode = "wb";
  if (config_.resume) {
    // Check local file size
    FILE* f = fopen(local_path, "rb");
    if (f) {
      fseeko(f, 0, SEEK_END);
      uint64_t local_size = ftello(f);
      fclose(f);
      if (local_size < total_size) {
        resume_offset = local_size;
        open_mode = "ab";
        SCP_LOG_INFO("Resuming download from offset %llu/%llu",
                     (unsigned long long)resume_offset,
                     (unsigned long long)total_size);
      }
    }
  }

  // Open local file
  FILE* local_file = fopen(local_path, open_mode);
  if (!local_file) {
    SCP_LOG_ERROR("Failed to open local file for download: %s", local_path);
    return SCP_ERROR_FILE_OPEN;
  }

  // Open remote file
  LIBSSH2_SFTP_HANDLE* remote_handle = libssh2_sftp_open(
      sftp, remote_path, LIBSSH2_FXF_READ, 0);
  if (!remote_handle) {
    fclose(local_file);
    SCP_LOG_ERROR("Failed to open remote file for download: %s", remote_path);
    return SCP_ERROR_FILE_OPEN;
  }

  // Seek to resume offset if needed
  if (resume_offset > 0) {
    libssh2_sftp_seek64(remote_handle, resume_offset);
  }

  // Allocate block buffer
  ScopedBuffer<char> block(config_.block_size);
  if (!block) {
    libssh2_sftp_close(remote_handle);
    fclose(local_file);
    return SCP_ERROR_MEMORY;
  }

  uint64_t bytes_received = resume_offset;

  while (bytes_received < total_size) {
    // Check pause / cancel
    {
      TransferState st;
      if (ShouldStop(this, &st)) {
        libssh2_sftp_close(remote_handle);
        fclose(local_file);
        if (st == TransferState::kCancelled) {
          if (cleanup_on_cancel_.load(std::memory_order_acquire)) {
            // Don't delete on download cancel - user may want partial file
          }
          return SCP_ERROR_CANCELLED;
        }
        return SCP_ERROR_PAUSED;
      }
      WaitWhilePaused(pause_mutex_, pause_cv_, paused_flag_, this);
    }

    size_t to_read = static_cast<size_t>(
        std::min<uint64_t>(config_.block_size, total_size - bytes_received));
    ssize_t rc = libssh2_sftp_read(remote_handle, block.get(), to_read);
    if (rc < 0) {
      SCP_LOG_ERROR("SFTP read failed at offset %llu",
                    (unsigned long long)bytes_received);
      libssh2_sftp_close(remote_handle);
      fclose(local_file);
      return SCP_ERROR_REMOTE_IO;
    }
    if (rc == 0) break;  // EOF

    size_t written = fwrite(block.get(), 1, rc, local_file);
    if (written != static_cast<size_t>(rc)) {
      SCP_LOG_ERROR("Failed to write local file: %s (disk full?)", local_path);
      libssh2_sftp_close(remote_handle);
      fclose(local_file);
      return (errno == ENOSPC) ? SCP_ERROR_DISK_FULL : SCP_ERROR_FILE_WRITE;
    }

    bytes_received += rc;
    bytes_transferred_.store(bytes_received, std::memory_order_release);
    ReportProgress(bytes_received, total_size);
  }

  libssh2_sftp_close(remote_handle);
  fclose(local_file);

  if (config_.verify_checksum) {
    SCP_LOG_INFO("Download complete (%llu bytes), skipping checksum (not yet implemented)",
                 (unsigned long long)bytes_received);
  }

  SCP_LOG_INFO("Download complete: %s -> %s (%llu bytes)",
               remote_path, local_path, (unsigned long long)bytes_received);
  return SCP_OK;
}

void SftpTransfer::ReportProgress(uint64_t transferred, uint64_t total) {
  if (!config_.progress_callback) return;

  // Throttle: only call callback at configured interval
  auto now = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_progress_time_).count();
    if (elapsed < static_cast<long long>(config_.progress_throttle_ms)) {
      return;  // Skip this callback
    }
    last_progress_time_ = now;
  }

  config_.progress_callback(
      reinterpret_cast<scp_transfer_t>(this),
      transferred, total,
      config_.progress_userdata);
}

}  // namespace scp
