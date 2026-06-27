#ifndef SCP_ERROR_H_
#define SCP_ERROR_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SCP_OK = 0,

  // General errors  -1000..-1009
  SCP_ERROR_GENERIC         = -1000,
  SCP_ERROR_NOT_INITIALIZED = -1001,
  SCP_ERROR_INVALID_ARG     = -1002,

  // Connection errors  -1100..-1199
  SCP_ERROR_CONNECT         = -1100, // failed to establish TCP connection
  SCP_ERROR_HOST_NOT_FOUND  = -1101, // DNS resolution failed
  SCP_ERROR_TIMEOUT         = -1102, // connect or handshake timeout
  SCP_ERROR_NETWORK         = -1103, // network I/O error
  SCP_ERROR_DISCONNECTED    = -1104, // session disconnected

  // Authentication errors  -1200..-1299
  SCP_ERROR_AUTH            = -1200, // generic auth failure
  SCP_ERROR_AUTH_METHOD     = -1201, // unsupported auth method
  SCP_ERROR_AUTH_DENIED     = -1202, // credentials rejected
  SCP_ERROR_AUTH_KEY        = -1203, // invalid or unreadable key file
  SCP_ERROR_AUTH_PASSPHRASE = -1204, // wrong passphrase
  SCP_ERROR_AUTH_PARTIAL    = -1205, // partial success, need further auth
  SCP_ERROR_AUTH_BANNER     = -1206, // server sent banner/prompt

  // SFTP errors  -1300..-1399
  SCP_ERROR_SFTP            = -1300, // generic SFTP error
  SCP_ERROR_SFTP_INIT       = -1301, // failed to init SFTP subsystem
  SCP_ERROR_SFTP_CHANNEL    = -1302, // failed to open SFTP channel

  // File operation errors  -1400..-1499
  SCP_ERROR_FILE            = -1400, // generic file error
  SCP_ERROR_NOT_FOUND       = -1401, // file or directory not found
  SCP_ERROR_PERMISSION      = -1402, // permission denied
  SCP_ERROR_ALREADY_EXISTS  = -1403, // file already exists
  SCP_ERROR_NOT_A_DIR       = -1404, // path is not a directory
  SCP_ERROR_IS_A_DIR        = -1405, // path is a directory (expected file)
  SCP_ERROR_FILE_OPEN       = -1406, // failed to open local file
  SCP_ERROR_FILE_WRITE      = -1407, // failed to write local file
  SCP_ERROR_FILE_READ       = -1408, // failed to read local file
  SCP_ERROR_DISK_FULL       = -1409, // insufficient disk space
  SCP_ERROR_QUOTA_EXCEEDED  = -1410, // quota exceeded

  // Transfer errors  -1500..-1599
  SCP_ERROR_TRANSFER        = -1500, // generic transfer error
  SCP_ERROR_CANCELLED       = -1501, // transfer cancelled by user
  SCP_ERROR_PAUSED          = -1502, // transfer is paused
  SCP_ERROR_CHECKSUM        = -1503, // integrity check failed
  SCP_ERROR_REMOTE_IO       = -1504, // remote I/O error during transfer

  // Resource errors  -1600..-1699
  SCP_ERROR_MEMORY          = -1600, // memory allocation failed
  SCP_ERROR_BUFFER_TOO_SMALL= -1601, // provided buffer too small
  SCP_ERROR_INVALID_HANDLE  = -1602, // invalid or corrupted handle
  SCP_ERROR_POOL_EXHAUSTED  = -1603, // connection pool exhausted
  SCP_ERROR_STATE_MACHINE   = -1604, // invalid state transition
} scp_error_t;

#ifdef __cplusplus
}
#endif

#endif  // SCP_ERROR_H_
