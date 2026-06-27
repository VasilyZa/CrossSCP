#ifndef SCP_CONNECTION_POOL_H_
#define SCP_CONNECTION_POOL_H_

#include "crossscp/scp_api.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace scp {

class SshConnection;

// Session entry tracking a single connection.
struct SessionEntry {
  std::string host;
  std::string username;
  uint16_t port = 22;
  std::unique_ptr<SshConnection> connection;
};

// A simple session registry that maps opaque session_t handles to connections.
// Supports session reuse across multiple tabs.
class SessionPool {
 public:
  static SessionPool& Instance();

  // Register a new connection, returns an opaque handle.
  // Takes ownership of the connection.
  scp_session_t Register(std::unique_ptr<SshConnection> conn,
                         const std::string& host,
                         const std::string& username,
                         uint16_t port);

  // Look up a session handle. Returns nullptr if not found.
  SshConnection* Find(scp_session_t session);

  // Remove and disconnect a session.
  scp_error_t Remove(scp_session_t session);

  // Find an existing connected session matching host:port:username.
  scp_session_t FindExisting(const std::string& host,
                             uint16_t port,
                             const std::string& username);

  // Disconnect all sessions.
  void Clear();

  // Number of active sessions.
  size_t Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
  }

 private:
  SessionPool() = default;
  static scp_session_t NextHandle();

  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, SessionEntry> sessions_;
  static std::atomic<uint64_t> next_handle_;
};

// Per-session transfer registry.
class TransferRegistry {
 public:
  static TransferRegistry& Instance();

  scp_transfer_t Register(class SftpTransfer* transfer);
  class SftpTransfer* Find(scp_transfer_t handle);
  void Remove(scp_transfer_t handle);
  void Clear();

 private:
  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, class SftpTransfer*> transfers_;
  static std::atomic<uint64_t> next_handle_;
};

}  // namespace scp

#endif  // SCP_CONNECTION_POOL_H_
