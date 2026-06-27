#include "connection_pool.h"
#include "ssh_connection.h"
#include "sftp_transfer.h"
#include "crossscp/scp_logger.h"

namespace scp {

// SessionPool implementation
std::atomic<uint64_t> SessionPool::next_handle_{1};

SessionPool& SessionPool::Instance() {
  static SessionPool pool;
  return pool;
}

scp_session_t SessionPool::NextHandle() {
  return reinterpret_cast<scp_session_t>(
      static_cast<uintptr_t>(next_handle_.fetch_add(1, std::memory_order_relaxed)));
}

scp_session_t SessionPool::Register(std::unique_ptr<SshConnection> conn,
                                     const std::string& host,
                                     const std::string& username,
                                     uint16_t port) {
  std::lock_guard<std::mutex> lock(mutex_);
  scp_session_t handle = NextHandle();
  SessionEntry entry;
  entry.host = host;
  entry.username = username;
  entry.port = port;
  entry.connection = std::move(conn);
  sessions_[reinterpret_cast<uint64_t>(handle)] = std::move(entry);
  SCP_LOG_DEBUG("Registered session %p for %s@%s:%u",
                handle, username.c_str(), host.c_str(), port);
  return handle;
}

SshConnection* SessionPool::Find(scp_session_t session) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(reinterpret_cast<uint64_t>(session));
  if (it != sessions_.end()) {
    return it->second.connection.get();
  }
  return nullptr;
}

scp_error_t SessionPool::Remove(scp_session_t session) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(reinterpret_cast<uint64_t>(session));
  if (it == sessions_.end()) {
    return SCP_ERROR_INVALID_HANDLE;
  }
  it->second.connection->Disconnect();
  sessions_.erase(it);
  SCP_LOG_DEBUG("Removed session %p", session);
  return SCP_OK;
}

scp_session_t SessionPool::FindExisting(const std::string& host,
                                         uint16_t port,
                                         const std::string& username) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& kv : sessions_) {
    auto& entry = kv.second;
    if (entry.host == host && entry.port == port &&
        entry.username == username &&
        entry.connection->IsConnected()) {
      return reinterpret_cast<scp_session_t>(kv.first);
    }
  }
  return nullptr;
}

void SessionPool::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& kv : sessions_) {
    kv.second.connection->Disconnect();
  }
  sessions_.clear();
}

// TransferRegistry implementation
std::atomic<uint64_t> TransferRegistry::next_handle_{1};

TransferRegistry& TransferRegistry::Instance() {
  static TransferRegistry reg;
  return reg;
}

scp_transfer_t TransferRegistry::Register(SftpTransfer* transfer) {
  std::lock_guard<std::mutex> lock(mutex_);
  scp_transfer_t handle = reinterpret_cast<scp_transfer_t>(
      static_cast<uintptr_t>(next_handle_.fetch_add(1, std::memory_order_relaxed)));
  transfers_[reinterpret_cast<uint64_t>(handle)] = transfer;
  return handle;
}

SftpTransfer* TransferRegistry::Find(scp_transfer_t handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = transfers_.find(reinterpret_cast<uint64_t>(handle));
  if (it != transfers_.end()) return it->second;
  return nullptr;
}

void TransferRegistry::Remove(scp_transfer_t handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  transfers_.erase(reinterpret_cast<uint64_t>(handle));
}

void TransferRegistry::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  transfers_.clear();
}

}  // namespace scp
