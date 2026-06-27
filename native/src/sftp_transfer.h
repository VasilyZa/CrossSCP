#ifndef SCP_SFTP_TRANSFER_H_
#define SCP_SFTP_TRANSFER_H_

#include "crossscp/scp_api.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

typedef struct _LIBSSH2_SFTP LIBSSH2_SFTP;

namespace scp {

class SshConnection;

// Transfer state machine
enum class TransferState {
  kIdle,
  kRunning,
  kPaused,
  kCancelled,
  kCompleted,
  kError,
};

// High-performance SFTP transfer engine.
// Implements async concurrent SFTP requests, configurable block size,
// progress throttling, and pause/resume/cancel.
//
// Design notes:
// - Uses direct file I/O on the native side; Dart only passes paths.
// - Up to `max_concurrent` (default 32) outstanding SFTP read/write requests
//   to saturate network bandwidth.
// - Default block size 256KB balances throughput vs memory.
// - Progress callback throttled to ~100ms to avoid UI thread flooding.
class SftpTransfer {
 public:
  SftpTransfer(SshConnection* conn, const scp_transfer_config_t* config);
  ~SftpTransfer();

  SftpTransfer(const SftpTransfer&) = delete;
  SftpTransfer& operator=(const SftpTransfer&) = delete;

  // Start the transfer in a background thread. Returns immediately.
  scp_error_t Start();

  // Pause the transfer. Partial data is preserved for resume.
  scp_error_t Pause();

  // Resume a paused transfer.
  scp_error_t Resume();

  // Cancel the transfer. Partial data may be cleaned up.
  scp_error_t Cancel();

  // Block until the transfer completes or fails.
  scp_error_t Wait();

  // Poll progress.
  scp_error_t GetProgress(uint64_t* transferred, uint64_t* total);

  // Query current state.
  TransferState GetState() const {
    return state_.load(std::memory_order_acquire);
  }

  // Get transfer context (direction, paths).
  const scp_transfer_config_t& GetConfig() const { return config_; }
  const std::string& GetLocalPath() const { return local_path_; }
  const std::string& GetRemotePath() const { return remote_path_; }

  // Get last error.
  scp_error_t GetError() const {
    return last_error_.load(std::memory_order_acquire);
  }

 private:
  void TransferThread();
  scp_error_t DoUpload();
  scp_error_t DoDownload();
  void ReportProgress(uint64_t transferred, uint64_t total);

  SshConnection* conn_;
  scp_transfer_config_t config_;
  std::string local_path_;
  std::string remote_path_;

  std::atomic<TransferState> state_{TransferState::kIdle};
  std::atomic<scp_error_t> last_error_{SCP_OK};
  std::atomic<uint64_t> bytes_transferred_{0};
  std::atomic<uint64_t> bytes_total_{0};

  // Pause / resume coordination
  std::mutex pause_mutex_;
  std::condition_variable pause_cv_;
  bool paused_flag_{false};

  std::unique_ptr<std::thread> transfer_thread_;

  // Progress throttling
  std::chrono::steady_clock::time_point last_progress_time_;
  std::mutex progress_mutex_;

  // Cleanup flag
  std::atomic<bool> cleanup_on_cancel_{true};
};

}  // namespace scp

#endif  // SCP_SFTP_TRANSFER_H_
