#ifndef SCP_API_H_
#define SCP_API_H_

// =============================================================================
// crossscp  Public C API
// =============================================================================
// All functions are thread-safe unless noted otherwise.
// All returned handles must be released via the corresponding `scp_*_free`
// function to avoid resource leaks.
//
// Error handling: most functions return `scp_error_t`.  SCP_OK (0) means
// success; negative values indicate an error (see scp_error.h).
// =============================================================================

#include "scp_types.h"
#include "scp_error.h"
#include "scp_logger.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Library lifecycle
// ---------------------------------------------------------------------------

// Initialize the library. Must be called once before any other API.
// Idempotent (subsequent calls are no-ops).
scp_error_t scp_init(void);

// Deinitialize the library. Frees all global resources.
// Any open session will be forcibly closed.
void scp_cleanup(void);

// Return the library version string (e.g. "1.0.0").
const char* scp_get_version(void);

// Return a human-readable string for the given error code.
const char* scp_error_string(scp_error_t error);

// ---------------------------------------------------------------------------
// Cipher configuration
// ---------------------------------------------------------------------------

// Enable/disable specific ciphers for the next connections.
// By default all supported ciphers are enabled except the weakest ones.
scp_error_t scp_set_cipher_enabled(scp_cipher_t cipher, bool enabled);

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------

// Connect to a remote host and authenticate.
// On success `out_session` receives an opaque session handle.
scp_error_t scp_connect(const scp_connect_config_t* config,
                        scp_session_t* out_session);

// Simplified connect API: avoids struct layout issues from dart:ffi.
// All string params can be NULL except host and username.
scp_error_t scp_connect2(
    const char* host, uint16_t port, const char* username,
    const char* password,
    const char* key_path, const char* passphrase,
    uint32_t timeout_s,
    scp_session_t* out_session);

// Gracefully disconnect and release all session resources.
scp_error_t scp_disconnect(scp_session_t session);

// Check whether the session is currently connected and authenticated.
bool scp_is_connected(scp_session_t session);

// Get the remote host string.
const char* scp_session_get_host(scp_session_t session);

// Send a keepalive request. Useful before a long idle period.
scp_error_t scp_keepalive(scp_session_t session);

// Get last SSH error message from libssh2.
const char* scp_session_get_last_error(scp_session_t session);

// ---------------------------------------------------------------------------
// File / directory operations
// ---------------------------------------------------------------------------

// List contents of a remote directory.
// `out_files` and `out_count` receive the result.
// Free the result with `scp_free_file_list()`.
scp_error_t scp_list_dir(scp_session_t session,
                         const char* path,
                         scp_file_info_t** out_files,
                         uint32_t* out_count);

// Stat a remote path (file or directory).
scp_error_t scp_stat(scp_session_t session,
                     const char* path,
                     scp_file_info_t* out_info);

// Create a directory on the remote host.
scp_error_t scp_mkdir(scp_session_t session,
                      const char* path,
                      uint32_t permissions);

// Remove an empty directory on the remote host.
scp_error_t scp_rmdir(scp_session_t session, const char* path);

// Delete a file on the remote host.
scp_error_t scp_unlink(scp_session_t session, const char* path);

// Rename / move a remote file or directory.
scp_error_t scp_rename(scp_session_t session,
                       const char* old_path,
                       const char* new_path);

// Change permissions of a remote file or directory.
scp_error_t scp_chmod(scp_session_t session,
                      const char* path,
                      uint32_t permissions);

// Create an empty file on the remote host.
scp_error_t scp_touch(scp_session_t session, const char* path);

// Get filesystem information (total / free bytes) for the given remote path.
scp_error_t scp_df(scp_session_t session,
                   const char* path,
                   uint64_t* out_total,
                   uint64_t* out_free);

// Free a file list returned by `scp_list_dir`.
void scp_free_file_list(scp_file_info_t* files, uint32_t count);

// ---------------------------------------------------------------------------
// Transfer operations
// ---------------------------------------------------------------------------

// Start a single-file transfer (upload or download).
// Returns immediately; use `scp_transfer_wait` to block until completion
// or poll progress with `scp_transfer_get_progress`.
scp_error_t scp_transfer_start(scp_session_t session,
                               const scp_transfer_config_t* config,
                               scp_transfer_t* out_transfer);

// Start a batch transfer. Returns immediately.
// Progress is reported via the batch_progress_callback.
scp_error_t scp_batch_transfer(scp_session_t session,
                               const scp_batch_config_t* config);

// Pause a running transfer. Data written so far is preserved for resume.
scp_error_t scp_transfer_pause(scp_transfer_t transfer);

// Resume a paused transfer.
scp_error_t scp_transfer_resume(scp_transfer_t transfer);

// Cancel a transfer. Partial data may be removed (use pause for resumable stop).
scp_error_t scp_transfer_cancel(scp_transfer_t transfer);

// Block until a transfer completes or fails.
scp_error_t scp_transfer_wait(scp_transfer_t transfer);

// Poll the current progress of a transfer.
// Both `transferred` and `total` are in bytes.
scp_error_t scp_transfer_get_progress(scp_transfer_t transfer,
                                      uint64_t* transferred,
                                      uint64_t* total);

// Release transfer handle resources.
void scp_transfer_free(scp_transfer_t transfer);

#ifdef __cplusplus
}
#endif

#endif  // SCP_API_H_
