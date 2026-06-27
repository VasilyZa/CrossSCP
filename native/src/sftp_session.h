#ifndef SCP_SFTP_SESSION_H_
#define SCP_SFTP_SESSION_H_

#include "crossscp/scp_api.h"

#include <memory>
#include <string>
#include <vector>

typedef struct _LIBSSH2_SFTP LIBSSH2_SFTP;
typedef struct _LIBSSH2_SFTP_HANDLE LIBSSH2_SFTP_HANDLE;

namespace scp {

class SshConnection;

// Encapsulates all SFTP operations on an established connection.
// Not thread-safe; use from a single thread or with external synchronization.
class SftpSession {
 public:
  explicit SftpSession(SshConnection* conn);
  ~SftpSession();

  SftpSession(const SftpSession&) = delete;
  SftpSession& operator=(const SftpSession&) = delete;

  // Directory listing
  scp_error_t ListDir(const char* path,
                      scp_file_info_t** out_files,
                      uint32_t* out_count);

  // Stat single path
  scp_error_t Stat(const char* path, scp_file_info_t* out_info);

  // Create directory
  scp_error_t Mkdir(const char* path, uint32_t permissions);

  // Remove directory
  scp_error_t Rmdir(const char* path);

  // Delete file
  scp_error_t Unlink(const char* path);

  // Rename / move
  scp_error_t Rename(const char* old_path, const char* new_path);

  // Change permissions
  scp_error_t Chmod(const char* path, uint32_t permissions);

  // Disk free / total
  scp_error_t Df(const char* path, uint64_t* out_total, uint64_t* out_free);

  // Free a file list
  static void FreeFileList(scp_file_info_t* files, uint32_t count);

 private:
  scp_file_info_t* ParseAttrsToFileInfo(
      const char* filename, size_t filename_len,
      uint64_t filesize, uint32_t permissions,
      uint32_t uid, uint32_t gid, uint64_t mtime,
      bool is_dir);

  SshConnection* conn_;  // non-owning
};

}  // namespace scp

#endif  // SCP_SFTP_SESSION_H_
