#ifndef SCP_TYPES_H_
#define SCP_TYPES_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle types
typedef void* scp_session_t;
typedef void* scp_transfer_t;
typedef void* scp_listing_t;

typedef enum {
  SCP_AUTH_PASSWORD = 0,
  SCP_AUTH_PUBLICKEY = 1,
  SCP_AUTH_KEYBOARD_INTERACTIVE = 2,
  SCP_AUTH_NONE = 3,            // no auth, just username (e.g. panel SFTP)
} scp_auth_type_t;

typedef enum {
  SCP_UPLOAD = 0,
  SCP_DOWNLOAD = 1,
} scp_direction_t;

// File entry information returned by list operations
typedef struct {
  char* filename;
  char* longname;
  uint64_t filesize;
  uint32_t permissions;
  uint32_t uid;
  uint32_t gid;
  uint64_t mtime;
  bool is_dir;
} scp_file_info_t;

// Encryption algorithm hint for performance tuning
typedef enum {
  SCP_CIPHER_AUTO = 0,
  SCP_CIPHER_AES128_CTR = 1,
  SCP_CIPHER_AES256_CTR = 2,
  SCP_CIPHER_CHACHA20_POLY1305 = 3,
} scp_cipher_t;

// Keyboard-interactive auth callback.
// Called when server requests keyboard-interactive authentication.
// The callback must fill `responses` with allocated strings (freed by caller).
typedef void (*scp_kbdint_cb)(const char* name,
                               const char* instruction,
                               int num_prompts,
                               const char* prompts[],
                               char* responses[],
                               void* userdata);

// Progress callback for transfer operations.
// `transferred` and `total` are in bytes.
// This callback is throttled (default 100ms) to avoid flooding.
typedef void (*scp_progress_cb)(scp_transfer_t transfer,
                                 uint64_t transferred,
                                 uint64_t total,
                                 void* userdata);

// Log callback.
// `level`: 0=debug, 1=info, 2=warn, 3=error
typedef void (*scp_log_cb)(int level, const char* message, void* userdata);

// Per-file progress in batch transfers.
typedef void (*scp_batch_progress_cb)(int file_index,
                                       const char* filename,
                                       uint64_t transferred,
                                       uint64_t total,
                                       void* userdata);

// Connection configuration
typedef struct {
  const char* host;
  uint16_t port;                    // 0 = default 22
  const char* username;
  scp_auth_type_t auth_type;

  const char* password;             // SCP_AUTH_PASSWORD
  const char* key_data;             // PEM/PPK key content or NULL
  const char* key_path;             // path to key file or NULL
  const char* passphrase;           // key passphrase or NULL

  uint32_t connect_timeout_s;       // connection timeout in seconds
  uint32_t keepalive_s;             // keepalive interval (0 = disable)
  bool tcp_nodelay;                 // enable TCP_NODELAY
  scp_cipher_t preferred_cipher;    // preferred encryption algorithm

  scp_kbdint_cb kbdint_callback;
  void* kbdint_userdata;

  scp_log_cb log_callback;
  void* log_userdata;
} scp_connect_config_t;

// Single transfer configuration
typedef struct {
  scp_direction_t direction;
  const char* local_path;           // local filesystem path
  const char* remote_path;          // remote SFTP path
  bool resume;                      // enable resume from partial transfer
  bool verify_checksum;             // verify integrity after transfer (SHA256)
  uint32_t max_concurrent;          // concurrent SFTP requests (0 = default 32)
  uint32_t block_size;              // transfer block size (0 = default 256KB)
  uint32_t progress_throttle_ms;    // progress callback throttle (0 = default 100ms)

  scp_progress_cb progress_callback;
  void* progress_userdata;
} scp_transfer_config_t;

// Batch transfer configuration
typedef struct {
  uint32_t count;                   // number of transfers
  scp_transfer_config_t* transfers; // array of individual transfer configs
  uint32_t max_concurrent;          // max concurrent transfers (0 = default 4)

  scp_batch_progress_cb progress_callback;
  void* progress_userdata;
} scp_batch_config_t;

#ifdef __cplusplus
}
#endif

#endif  // SCP_TYPES_H_
