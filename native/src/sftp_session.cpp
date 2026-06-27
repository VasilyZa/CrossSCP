#include "sftp_session.h"
#include "ssh_connection.h"
#include "memory_guard.h"
#include "crossscp/scp_logger.h"

#include <libssh2.h>
#include <libssh2_sftp.h>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>

namespace scp {

SftpSession::SftpSession(SshConnection* conn) : conn_(conn) {}

SftpSession::~SftpSession() {}

scp_file_info_t* SftpSession::ParseAttrsToFileInfo(
    const char* filename, size_t filename_len,
    uint64_t filesize, uint32_t permissions,
    uint32_t uid, uint32_t gid, uint64_t mtime,
    bool is_dir) {

  scp_file_info_t* info = static_cast<scp_file_info_t*>(
      calloc(1, sizeof(scp_file_info_t)));
  if (!info) return nullptr;

  info->filename = static_cast<char*>(malloc(filename_len + 1));
  if (!info->filename) {
    free(info);
    return nullptr;
  }
  memcpy(info->filename, filename, filename_len);
  info->filename[filename_len] = '\0';

  // Build human-readable longname (simulating ls -l)
  char perms[16];
  snprintf(perms, sizeof(perms), "%c%c%c%c%c%c%c%c%c%c",
           is_dir ? 'd' : '-',
           (permissions & S_IRUSR) ? 'r' : '-',
           (permissions & S_IWUSR) ? 'w' : '-',
           (permissions & S_IXUSR) ? 'x' : '-',
           (permissions & S_IRGRP) ? 'r' : '-',
           (permissions & S_IWGRP) ? 'w' : '-',
           (permissions & S_IXGRP) ? 'x' : '-',
           (permissions & S_IROTH) ? 'r' : '-',
           (permissions & S_IWOTH) ? 'w' : '-',
           (permissions & S_IXOTH) ? 'x' : '-');

  char timebuf[32];
  time_t t = static_cast<time_t>(mtime);
  struct tm tm_buf;
#ifdef _WIN32
  gmtime_s(&tm_buf, &t);
#else
  gmtime_r(&t, &tm_buf);
#endif
  strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", &tm_buf);

  uint64_t sz = filesize;
  char sizebuf[16];
  if (sz < 1024ULL) {
    snprintf(sizebuf, sizeof(sizebuf), "%llu", (unsigned long long)sz);
  } else if (sz < 1024ULL * 1024) {
    snprintf(sizebuf, sizeof(sizebuf), "%.1fK", sz / 1024.0);
  } else if (sz < 1024ULL * 1024 * 1024) {
    snprintf(sizebuf, sizeof(sizebuf), "%.1fM", sz / (1024.0 * 1024));
  } else {
    snprintf(sizebuf, sizeof(sizebuf), "%.1fG", sz / (1024.0 * 1024 * 1024));
  }

  char longname[512];
  snprintf(longname, sizeof(longname), "%s %3u %5u %5u %10s %s %s",
           perms, 1U, uid, gid, sizebuf, timebuf, filename);

  info->longname = strdup(longname);
  info->filesize = filesize;
  info->permissions = permissions;
  info->uid = uid;
  info->gid = gid;
  info->mtime = mtime;
  info->is_dir = is_dir;

  return info;
}

scp_error_t SftpSession::ListDir(const char* path,
                                  scp_file_info_t** out_files,
                                  uint32_t* out_count) {
  if (!conn_ || !conn_->IsConnected()) return SCP_ERROR_DISCONNECTED;
  if (!path || !out_files || !out_count) return SCP_ERROR_INVALID_ARG;

  LIBSSH2_SFTP* sftp = conn_->GetSftpSession();
  if (!sftp) return SCP_ERROR_SFTP;

  LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_opendir(
      sftp, path);
  if (!handle) {
    SCP_LOG_ERROR("Failed to open directory: %s", path);
    return SCP_ERROR_NOT_FOUND;
  }

  std::vector<scp_file_info_t*> entries;

  // Buffer for directory entry data
  char filename[512];
  LIBSSH2_SFTP_ATTRIBUTES attrs;
  char longentry[1024];

  int rc;
  while ((rc = libssh2_sftp_readdir_ex(handle, filename, sizeof(filename),
                                         longentry, sizeof(longentry),
                                         &attrs)) > 0) {
    // Skip "." and ".." 
    if (filename[0] == '.' && (filename[1] == '\0' ||
        (filename[1] == '.' && filename[2] == '\0'))) {
      continue;
    }

    scp_file_info_t* info = ParseAttrsToFileInfo(
        filename, strlen(filename),
        attrs.filesize, attrs.permissions,
        attrs.uid, attrs.gid, attrs.mtime,
        LIBSSH2_SFTP_S_ISDIR(attrs.permissions));

    if (info) {
      entries.push_back(info);
    }
  }

  libssh2_sftp_closedir(handle);

  if (rc < 0) {
    // Clean up on error
    for (auto* e : entries) FreeFileList(e, 1);
    SCP_LOG_ERROR("Failed to read directory: %s", path);
    return SCP_ERROR_SFTP;
  }

  // Allocate output array and copy structs (entries stores pointers, not inline structs)
  if (entries.empty()) {
    *out_files = nullptr;
    *out_count = 0;
  } else {
    *out_files = static_cast<scp_file_info_t*>(
        calloc(entries.size(), sizeof(scp_file_info_t)));
    if (!*out_files) {
      for (auto* e : entries) FreeFileList(e, 1);
      return SCP_ERROR_MEMORY;
    }
    for (size_t i = 0; i < entries.size(); i++) {
      memcpy(&(*out_files)[i], entries[i], sizeof(scp_file_info_t));
      free(entries[i]);  // free the ptr wrapper, not the filename/longname strings
    }
    *out_count = static_cast<uint32_t>(entries.size());
  }

  SCP_LOG_INFO("Listed %u entries in %s", *out_count, path);
  return SCP_OK;
}

scp_error_t SftpSession::Stat(const char* path, scp_file_info_t* out_info) {
  if (!conn_ || !conn_->IsConnected()) return SCP_ERROR_DISCONNECTED;
  if (!path || !out_info) return SCP_ERROR_INVALID_ARG;

  LIBSSH2_SFTP* sftp = conn_->GetSftpSession();
  if (!sftp) return SCP_ERROR_SFTP;

  LIBSSH2_SFTP_ATTRIBUTES attrs;
  int ret = libssh2_sftp_stat(sftp, path, &attrs);
  if (ret != 0) {
    SCP_LOG_ERROR("Failed to stat: %s", path);
    return SCP_ERROR_NOT_FOUND;
  }

  // Extract filename from path
  const char* fname = strrchr(path, '/');
  if (fname) fname++;
  else fname = path;

  memset(out_info, 0, sizeof(scp_file_info_t));
  out_info->filename = strdup(fname);
  out_info->longname = strdup(fname);
  out_info->filesize = attrs.filesize;
  out_info->permissions = attrs.permissions;
  out_info->uid = attrs.uid;
  out_info->gid = attrs.gid;
  out_info->mtime = attrs.mtime;
  out_info->is_dir = LIBSSH2_SFTP_S_ISDIR(attrs.permissions);

  return SCP_OK;
}

scp_error_t SftpSession::Mkdir(const char* path, uint32_t permissions) {
  if (!conn_ || !conn_->IsConnected()) return SCP_ERROR_DISCONNECTED;
  if (!path) return SCP_ERROR_INVALID_ARG;

  LIBSSH2_SFTP* sftp = conn_->GetSftpSession();
  if (!sftp) return SCP_ERROR_SFTP;

  if (permissions == 0) permissions = 0755;

  int ret = libssh2_sftp_mkdir(sftp, path, permissions);
  if (ret != 0) {
    SCP_LOG_ERROR("Failed to create directory: %s", path);
    return SCP_ERROR_PERMISSION;
  }
  SCP_LOG_INFO("Created directory: %s", path);
  return SCP_OK;
}

scp_error_t SftpSession::Rmdir(const char* path) {
  if (!conn_ || !conn_->IsConnected()) return SCP_ERROR_DISCONNECTED;
  if (!path) return SCP_ERROR_INVALID_ARG;

  LIBSSH2_SFTP* sftp = conn_->GetSftpSession();
  if (!sftp) return SCP_ERROR_SFTP;

  int ret = libssh2_sftp_rmdir(sftp, path);
  if (ret != 0) {
    SCP_LOG_ERROR("Failed to remove directory: %s", path);
    return SCP_ERROR_PERMISSION;
  }
  SCP_LOG_INFO("Removed directory: %s", path);
  return SCP_OK;
}

scp_error_t SftpSession::Unlink(const char* path) {
  if (!conn_ || !conn_->IsConnected()) return SCP_ERROR_DISCONNECTED;
  if (!path) return SCP_ERROR_INVALID_ARG;

  LIBSSH2_SFTP* sftp = conn_->GetSftpSession();
  if (!sftp) return SCP_ERROR_SFTP;

  int ret = libssh2_sftp_unlink(sftp, path);
  if (ret != 0) {
    SCP_LOG_ERROR("Failed to delete file: %s", path);
    return SCP_ERROR_PERMISSION;
  }
  SCP_LOG_INFO("Deleted file: %s", path);
  return SCP_OK;
}

scp_error_t SftpSession::Rename(const char* old_path, const char* new_path) {
  if (!conn_ || !conn_->IsConnected()) return SCP_ERROR_DISCONNECTED;
  if (!old_path || !new_path) return SCP_ERROR_INVALID_ARG;

  LIBSSH2_SFTP* sftp = conn_->GetSftpSession();
  if (!sftp) return SCP_ERROR_SFTP;

  int ret = libssh2_sftp_rename(sftp, old_path, new_path);
  if (ret != 0) {
    SCP_LOG_ERROR("Failed to rename %s -> %s", old_path, new_path);
    return SCP_ERROR_PERMISSION;
  }
  SCP_LOG_INFO("Renamed %s -> %s", old_path, new_path);
  return SCP_OK;
}

scp_error_t SftpSession::Chmod(const char* path, uint32_t permissions) {
  if (!conn_ || !conn_->IsConnected()) return SCP_ERROR_DISCONNECTED;
  if (!path) return SCP_ERROR_INVALID_ARG;

  LIBSSH2_SFTP* sftp = conn_->GetSftpSession();
  if (!sftp) return SCP_ERROR_SFTP;

  LIBSSH2_SFTP_ATTRIBUTES attrs;
  memset(&attrs, 0, sizeof(attrs));
  attrs.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
  attrs.permissions = permissions;

  int ret = libssh2_sftp_stat(sftp, path, &attrs);
  // Note: libssh2 doesn't have a direct chmod; we use setstat
  // Actually libssh2_sftp_setstat can set attributes.
  // Let's use the open + fsetstat pattern for simplicity.
  ret = libssh2_sftp_setstat(sftp, path, &attrs);
  if (ret != 0) {
    SCP_LOG_ERROR("Failed to chmod %s", path);
    return SCP_ERROR_PERMISSION;
  }
  SCP_LOG_INFO("Changed permissions of %s to %o", path, permissions);
  return SCP_OK;
}

scp_error_t SftpSession::Df(const char* path, uint64_t* out_total,
                             uint64_t* out_free) {
  // libssh2 does not have a direct statvfs/df call.
  // We extend it via the SFTP statvfs extension (statvfs@openssh.com or fstatvfs@openssh.com).
  // Many servers support this extension; we try and fall back gracefully.
  if (!conn_ || !conn_->IsConnected()) return SCP_ERROR_DISCONNECTED;

  LIBSSH2_SFTP* sftp = conn_->GetSftpSession();
  if (!sftp) return SCP_ERROR_SFTP;

  LIBSSH2_SFTP_STATVFS st;
  int ret = libssh2_sftp_statvfs(sftp, path ? path : "/", strlen(path ? path : "/"), &st);
  if (ret == 0) {
    if (out_total) *out_total = st.f_frsize * st.f_blocks;
    if (out_free)  *out_free  = st.f_frsize * st.f_bavail;
    return SCP_OK;
  }

  // Fallback: try fstatvfs on root
  LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_open(
      sftp, path ? path : "/", LIBSSH2_FXF_READ, 0);
  if (handle) {
    ret = libssh2_sftp_fstatvfs(handle, &st);
    libssh2_sftp_close(handle);
    if (ret == 0) {
      if (out_total) *out_total = st.f_frsize * st.f_blocks;
      if (out_free)  *out_free  = st.f_frsize * st.f_bavail;
      return SCP_OK;
    }
  }

  // Extension not supported
  SCP_LOG_WARN("statvfs not supported by remote server");
  if (out_total) *out_total = 0;
  if (out_free)  *out_free = 0;
  return SCP_ERROR_SFTP;
}

void SftpSession::FreeFileList(scp_file_info_t* files, uint32_t count) {
  if (!files) return;
  for (uint32_t i = 0; i < count; i++) {
    free(files[i].filename);
    free(files[i].longname);
  }
  free(files);
}

}  // namespace scp
