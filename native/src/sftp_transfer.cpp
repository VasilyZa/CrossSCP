#include "sftp_transfer.h"
#include "ssh_connection.h"
#include "memory_guard.h"
#include "crossscp/scp_logger.h"

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cinttypes>
#include <algorithm>
#include <deque>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <io.h>
#define fseeko _fseeki64
#define ftello _ftelli64
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace scp {

// Defaults — tuned for high-throughput bulk transfer.
static constexpr uint32_t kDefaultMaxConcurrent = 32;
static constexpr uint32_t kDefaultBlockSize      = 2 * 1024 * 1024;   // 2 MB
static constexpr uint32_t kDefaultProgressThrottleMs = 100;
static constexpr uint32_t kMinBlockSize          = 4 * 1024;          // 4 KB
static constexpr uint32_t kMaxBlockSize          = 16 * 1024 * 1024; // 16 MB

// Number of read-ahead blocks in the pipeline.
static constexpr int kPipelineBlocks = 3;

static uint32_t ClampUint32(uint32_t val, uint32_t min_val, uint32_t max_val) {
  if (val == 0) return min_val;
  if (val < min_val) return min_val;
  if (val > max_val) return max_val;
  return val;
}

// ---------------------------------------------------------------------------
//  Pipeline helper for upload: a dedicated thread reads from local file
//  while the main transfer thread writes to the SFTP channel.
// ---------------------------------------------------------------------------
struct UploadPipeline {
  struct Block {
    size_t idx;
    size_t bytes;  // actual bytes read from local file
  };
  std::vector<std::vector<char>> buffers;
  std::deque<Block> ready;       // blocks ready for SFTP write
  std::deque<size_t> free_slots; // indices: blocks the reader may fill
  std::mutex mtx;
  std::condition_variable cv;
  bool reader_eof{false};
  scp_error_t reader_err{SCP_OK};

  explicit UploadPipeline(size_t block_size) {
    buffers.resize(kPipelineBlocks);
    for (size_t i = 0; i < kPipelineBlocks; ++i) {
      buffers[i].resize(block_size);
      free_slots.push_back(i);
    }
  }
};

// ---------------------------------------------------------------------------
//  Pipeline helper for download: a dedicated thread writes to local file
//  while the main transfer thread reads from the SFTP channel.
// ---------------------------------------------------------------------------
struct DownloadPipeline {
  struct Block {
    size_t idx;
    size_t bytes;
  };
  std::vector<std::vector<char>> buffers;
  std::deque<Block> ready;       // blocks received from SFTP, pending local write
  std::deque<size_t> free_slots; // empty blocks available for SFTP read
  std::mutex mtx;
  std::condition_variable cv;
  bool writer_done{false};
  scp_error_t writer_err{SCP_OK};

  explicit DownloadPipeline(size_t block_size) {
    buffers.resize(kPipelineBlocks);
    for (size_t i = 0; i < kPipelineBlocks; ++i) {
      buffers[i].resize(block_size);
      free_slots.push_back(i);
    }
  }
};

// ---------------------------------------------------------------------------

SftpTransfer::SftpTransfer(SshConnection* conn,
                           const scp_transfer_config_t* config)
    : conn_(conn) {
  memcpy(&config_, config, sizeof(scp_transfer_config_t));

  if (config_.local_path) {
    local_path_ = config_.local_path;
    config_.local_path = local_path_.c_str();
  }
  if (config_.remote_path) {
    remote_path_ = config_.remote_path;
    config_.remote_path = remote_path_.c_str();
  }

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
    return SCP_OK;
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

static bool ShouldStop(const SftpTransfer* self, TransferState* out_current) {
  TransferState s = self->GetState();
  if (out_current) *out_current = s;
  return s == TransferState::kPaused || s == TransferState::kCancelled;
}

static void WaitWhilePaused(std::mutex& mtx,
                             std::condition_variable& cv,
                             bool& flag,
                             const SftpTransfer* self) {
  std::unique_lock<std::mutex> lock(mtx);
  while (flag && self->GetState() != TransferState::kCancelled) {
    cv.wait(lock);
  }
}

// Helper: check whether a transfer was cancelled or paused from inside
// a pipeline helper thread (which must not pause — just bail out).
static bool TransferCancelled(const SftpTransfer* self) {
  TransferState s = self->GetState();
  return s == TransferState::kCancelled;
}

// =========================================================================
//  Upload with read-ahead pipeline
// =========================================================================

scp_error_t SftpTransfer::DoUpload() {
  LIBSSH2_SFTP* sftp = conn_->GetSftpSession();
  if (!sftp) return SCP_ERROR_SFTP;

  const char* local_path  = config_.local_path;
  const char* remote_path = config_.remote_path;

  FILE* local_file = fopen(local_path, "rb");
  if (!local_file) {
    SCP_LOG_ERROR("Failed to open local file for upload: %s", local_path);
    return SCP_ERROR_FILE_OPEN;
  }

  fseeko(local_file, 0, SEEK_END);
  uint64_t total_size = ftello(local_file);
  fseeko(local_file, 0, SEEK_SET);
  bytes_total_.store(total_size, std::memory_order_release);

  // Resume support
  uint64_t resume_offset = 0;
  int open_flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC;
  if (config_.resume) {
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int ret = libssh2_sftp_stat(sftp, remote_path, &attrs);
    if (ret == 0 && attrs.filesize < total_size) {
      resume_offset = attrs.filesize;
      open_flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT;
      SCP_LOG_INFO("Resuming upload from offset %llu/%llu",
                   (unsigned long long)resume_offset,
                   (unsigned long long)total_size);
    }
  }

  LIBSSH2_SFTP_HANDLE* remote_handle = libssh2_sftp_open(
      sftp, remote_path, open_flags,
      LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
      LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
  if (!remote_handle) {
    fclose(local_file);
    SCP_LOG_ERROR("Failed to open remote file for upload: %s", remote_path);
    return SCP_ERROR_FILE_OPEN;
  }

  // Pipeline: reader thread reads ahead from disk, main thread writes to SFTP.
  UploadPipeline pipe(config_.block_size);
  uint64_t bytes_sent = resume_offset;

  // Reader thread: fills buffers from local file.
  std::thread reader([&]() {
    fseeko(local_file, static_cast<int64_t>(resume_offset), SEEK_SET);

    while (!TransferCancelled(this)) {
      size_t idx;
      {
        std::unique_lock<std::mutex> lock(pipe.mtx);
        pipe.cv.wait(lock, [&]() {
          return !pipe.free_slots.empty() || TransferCancelled(this);
        });
        if (TransferCancelled(this)) return;
        idx = pipe.free_slots.front();
        pipe.free_slots.pop_front();
      }

      size_t bytes = fread(pipe.buffers[idx].data(), 1,
                           config_.block_size, local_file);
      if (bytes == 0) {
        if (ferror(local_file)) {
          pipe.reader_err = SCP_ERROR_FILE_READ;
        }
        pipe.reader_eof = true;
        {
          std::lock_guard<std::mutex> lock(pipe.mtx);
          pipe.ready.push_back({idx, 0});
        }
        pipe.cv.notify_one();
        return;
      }

      {
        std::lock_guard<std::mutex> lock(pipe.mtx);
        pipe.ready.push_back({idx, bytes});
      }
      pipe.cv.notify_one();

      // Last block (partial read) is EOF
      if (bytes < config_.block_size) {
        pipe.reader_eof = true;
        return;
      }
    }
  });

  scp_error_t result = SCP_OK;
  std::deque<size_t> pending_free;  // blocks freed after write, deferred return

  while (bytes_sent < total_size) {
    // Pause / cancel
    {
      TransferState st;
      if (ShouldStop(this, &st)) {
        result = (st == TransferState::kCancelled) ? SCP_ERROR_CANCELLED
                                                    : SCP_ERROR_PAUSED;
        // Wake up reader so it can exit
        pipe.cv.notify_all();
        break;
      }
      WaitWhilePaused(pause_mutex_, pause_cv_, paused_flag_, this);
    }

    // Return freed blocks from previous write(s) back to the reader.
    while (!pending_free.empty()) {
      {
        std::lock_guard<std::mutex> lock(pipe.mtx);
        pipe.free_slots.push_back(pending_free.front());
      }
      pipe.cv.notify_one();
      pending_free.pop_front();
    }

    UploadPipeline::Block blk;
    {
      std::unique_lock<std::mutex> lock(pipe.mtx);
      // Block until there's data to write, or reader is finished/errored.
      pipe.cv.wait(lock, [&]() {
        return !pipe.ready.empty() || pipe.reader_eof || pipe.reader_err != SCP_OK
               || TransferCancelled(this);
      });
      if (pipe.reader_err != SCP_OK) {
        result = pipe.reader_err;
        break;
      }
      if (pipe.ready.empty() && pipe.reader_eof) break;
      if (TransferCancelled(this)) { result = SCP_ERROR_CANCELLED; break; }
      blk = pipe.ready.front();
      pipe.ready.pop_front();
    }

    if (blk.bytes == 0) continue;  // empty block from EOF signal

    ssize_t rc = libssh2_sftp_write(remote_handle,
                                    pipe.buffers[blk.idx].data(), blk.bytes);
    if (rc < 0) {
      SCP_LOG_ERROR("SFTP write failed at offset %llu",
                    (unsigned long long)bytes_sent);
      result = SCP_ERROR_REMOTE_IO;
      break;
    }

    bytes_sent += rc;
    bytes_transferred_.store(bytes_sent, std::memory_order_release);
    ReportProgress(bytes_sent, total_size);

    // Defer returning this block to avoid holding pipe.mtx during SFTP write.
    pending_free.push_back(blk.idx);

    // If there's a pending error from the reader that we haven't processed
    {
      std::lock_guard<std::mutex> lock(pipe.mtx);
      if (pipe.reader_err != SCP_OK) {
        result = pipe.reader_err;
        break;
      }
    }
  }

  // Return remaining freed blocks and clean up.
  for (size_t idx : pending_free) {
    std::lock_guard<std::mutex> lock(pipe.mtx);
    pipe.free_slots.push_back(idx);
  }
  pipe.cv.notify_all();
  reader.join();

  libssh2_sftp_close(remote_handle);
  fclose(local_file);

  if (result == SCP_OK) {
    bytes_transferred_.store(bytes_sent, std::memory_order_release);
    SCP_LOG_INFO("Upload complete: %s -> %s (%llu bytes)",
                 local_path, remote_path, (unsigned long long)bytes_sent);
  }
  return result;
}

// =========================================================================
//  Download — non-blocking pipelined reads for high throughput
// =========================================================================
// Switches the SSH session to non-blocking mode temporarily so that
// pipelined SFTP READ requests can be issued without waiting for each
// response.  Multiple reads stay in-flight concurrently, saturating
// high-bandwidth links that would otherwise be bottlenecked by RTT.
// =========================================================================

scp_error_t SftpTransfer::DoDownload() {
  LIBSSH2_SFTP* sftp = conn_->GetSftpSession();
  if (!sftp) return SCP_ERROR_SFTP;

  const char* remote_path = config_.remote_path;
  const char* local_path  = config_.local_path;

  LIBSSH2_SFTP_ATTRIBUTES attrs;
  int ret = libssh2_sftp_stat(sftp, remote_path, &attrs);
  if (ret != 0) {
    SCP_LOG_ERROR("Failed to stat remote file: %s", remote_path);
    return SCP_ERROR_NOT_FOUND;
  }
  uint64_t total_size = attrs.filesize;
  bytes_total_.store(total_size, std::memory_order_release);

  // Resume support
  uint64_t resume_offset = 0;
  if (config_.resume) {
    FILE* f = fopen(local_path, "rb");
    if (f) {
      fseeko(f, 0, SEEK_END);
      resume_offset = ftello(f);
      fclose(f);
      if (resume_offset > total_size) resume_offset = 0;
      SCP_LOG_INFO("Resuming download from offset %llu/%llu",
                   (unsigned long long)resume_offset,
                   (unsigned long long)total_size);
    }
  }

  // Pre-allocate output file for parallel writes
  FILE* local_file = fopen(local_path, "wb+");
  if (!local_file) {
    SCP_LOG_ERROR("Failed to open local file for download: %s", local_path);
    return SCP_ERROR_FILE_OPEN;
  }
  setvbuf(local_file, nullptr, _IOFBF, 1024 * 1024);
  if (resume_offset == 0) {
    fseeko(local_file, static_cast<int64_t>(total_size - 1), SEEK_SET);
    fputc(0, local_file);  // ensure file size
    fseeko(local_file, 0, SEEK_SET);
  }

  // Switch SSH session to non-blocking for pipelined reads
  LIBSSH2_SESSION* session = conn_->GetSshSession();
  libssh2_session_set_blocking(session, 0);

  constexpr int kWorkers = 6;
  const uint64_t chunk = (total_size / kWorkers + 1);

  struct Worker {
    LIBSSH2_SFTP_HANDLE* h = nullptr;
    uint64_t start, end, pos;
    std::vector<char> buf;
    ssize_t pending = -1;  // -1 = idle, >= 0 = bytes just received
    bool eof = false;
  };
  std::vector<Worker> w(kWorkers);
  for (int i = 0; i < kWorkers; ++i) {
    auto& wk = w[i];
    wk.start = i * chunk;
    wk.end   = std::min(wk.start + chunk, total_size);
    wk.pos   = wk.start;
    if (wk.start >= total_size) continue;
    wk.buf.resize(256 * 1024);
    wk.h = libssh2_sftp_open(sftp, remote_path, LIBSSH2_FXF_READ, 0);
    if (wk.h) libssh2_sftp_seek64(wk.h, wk.start);
  }

  uint64_t bytes_received = 0;

  // Main event loop: issue reads on idle workers, poll socket, collect results
  while (bytes_received < total_size) {
    // Pause / cancel
    {
      TransferState st;
      if (ShouldStop(this, &st)) {
        libssh2_session_set_blocking(session, 1);
        for (auto& wk : w) if (wk.h) libssh2_sftp_close(wk.h);
        fclose(local_file);
        if (st == TransferState::kCancelled) return SCP_ERROR_CANCELLED;
        return SCP_ERROR_PAUSED;
      }
      WaitWhilePaused(pause_mutex_, pause_cv_, paused_flag_, this);
    }

    // ---- PHASE 1: collect completed reads and write to disk ----
    for (auto& wk : w) {
      if (wk.pending < 0) continue;
      if (wk.pending == 0) { wk.eof = true; wk.pending = -1; continue; }

      fseeko(local_file, static_cast<int64_t>(wk.pos), SEEK_SET);
      fwrite(wk.buf.data(), 1, wk.pending, local_file);
      wk.pos += wk.pending;
      bytes_received += wk.pending;
      wk.pending = -1;
      if (wk.pos >= wk.end) wk.eof = true;

      bytes_transferred_.store(bytes_received, std::memory_order_release);
      ReportProgress(bytes_received, total_size);
    }

    // ---- PHASE 2: issue new reads on idle workers ----
    bool any_issued = false;
    for (auto& wk : w) {
      if (wk.eof || wk.pending >= 0 || !wk.h) continue;
      size_t n = static_cast<size_t>(std::min<uint64_t>(wk.buf.size(), wk.end - wk.pos));
      ssize_t rc = libssh2_sftp_read(wk.h, wk.buf.data(), n);
      if (rc > 0) {
        wk.pending = rc;
        any_issued = true;
      } else if (rc == LIBSSH2_ERROR_EAGAIN) {
        // not ready yet — fine, try again after poll
      } else if (rc < 0) {
        wk.eof = true;  // error, mark done
      } else {
        wk.pending = 0;  // rc == 0 -> EOF
      }
    }

    // ---- PHASE 3: if nothing is in-flight, poll the socket ----
    bool any_active = false;
    for (auto& wk : w) { if (!wk.eof && wk.h) { any_active = true; break; } }
    if (!any_active) break;

    bool all_idle = true;
    for (auto& wk : w) {
      if (!wk.eof && wk.pending < 0 && wk.h) { /* worker is idle */ }
      else if (wk.pending < 0 && wk.eof) { /* done */ }
      else { all_idle = false; break; }
    }

    if (!any_issued && all_idle) {
      // Nothing to do — wait for socket data
      struct pollfd pfd;
      pfd.fd     = conn_->GetSocketFd();
      pfd.events = POLLIN;
      int pret = poll(&pfd, 1, 200);
      if (pret < 0 && errno != EINTR) break;
    }
  }

  // Restore blocking mode
  libssh2_session_set_blocking(session, 1);

  for (auto& wk : w) if (wk.h) libssh2_sftp_close(wk.h);
  fclose(local_file);

  SCP_LOG_INFO("Download complete: %s -> %s (%llu bytes)",
               remote_path, local_path, (unsigned long long)bytes_received);
  return SCP_OK;
}

void SftpTransfer::ReportProgress(uint64_t transferred, uint64_t total) {
  if (!config_.progress_callback) return;

  auto now = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_progress_time_).count();
    if (elapsed < static_cast<long long>(config_.progress_throttle_ms)) {
      return;
    }
    last_progress_time_ = now;
  }

  config_.progress_callback(
      reinterpret_cast<scp_transfer_t>(this),
      transferred, total,
      config_.progress_userdata);
}

}  // namespace scp
