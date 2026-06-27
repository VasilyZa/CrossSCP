#ifndef SCP_SSH_CONNECTION_H_
#define SCP_SSH_CONNECTION_H_

#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <cstdint>

#include <cstdint>

// Forward-declare libssh2 types (no need to include the heavy headers here)
typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;
typedef struct _LIBSSH2_SFTP LIBSSH2_SFTP;

#include "crossscp/scp_api.h"

namespace scp {

#ifdef _WIN32
using socket_t = intptr_t;
#define INVALID_SOCKET_VALUE static_cast<intptr_t>(-1)
#else
using socket_t = int;
#define INVALID_SOCKET_VALUE (-1)
#endif

struct SshSocket {
  socket_t fd = INVALID_SOCKET_VALUE;

  bool IsValid() const { return fd != INVALID_SOCKET_VALUE; }
};

// Encapsulates a single SSH connection: socket + libssh2 session + SFTP session.
// Thread-compatible but not thread-safe; external synchronization required.
class SshConnection {
 public:
  SshConnection();
  ~SshConnection();

  SshConnection(const SshConnection&) = delete;
  SshConnection& operator=(const SshConnection&) = delete;

  // Connect and authenticate in one call.
  scp_error_t Connect(const scp_connect_config_t* config);

  // Disconnect gracefully.
  scp_error_t Disconnect();

  bool IsConnected() const { return connected_.load(std::memory_order_acquire); }

  // Raw access for advanced operations.
  LIBSSH2_SESSION* GetSshSession() const { return ssh_session_; }
  LIBSSH2_SFTP*    GetSftpSession() const { return sftp_session_; }
  const std::string& GetHost() const { return host_; }
  const std::string& GetLastError() const { return last_error_; }
  intptr_t GetSocketFd() const { return socket_.fd; }

  // Send a keepalive to prevent connection timeout.
  scp_error_t Keepalive();

 private:
  scp_error_t CreateSocket(const char* host, uint16_t port, uint32_t timeout_s);
  scp_error_t Handshake();
  scp_error_t AuthenticatePassword(const char* username, const char* password);
  scp_error_t AuthenticatePublicKey(const scp_connect_config_t* config);
  scp_error_t AuthenticateKeyboardInteractive(const char* username,
                                               scp_kbdint_cb callback, void* userdata);
  scp_error_t StartSftp();

  void SetLastError(const char* msg);
  void SetSocketOptions();

  SshSocket socket_;
  LIBSSH2_SESSION* ssh_session_;
  LIBSSH2_SFTP* sftp_session_;

  std::string host_;
  std::string last_error_;
  std::atomic<bool> connected_{false};

  bool tcp_nodelay_ = true;
};

}  // namespace scp

#endif  // SCP_SSH_CONNECTION_H_
