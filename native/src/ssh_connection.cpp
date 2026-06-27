#include "ssh_connection.h"
#include "memory_guard.h"
#include "crossscp/scp_logger.h"

#include <libssh2.h>
#include <libssh2_sftp.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#define close closesocket
using socklen_t = int;
// Non-blocking socket I/O via ioctlsocket instead of fcntl
#define SET_NONBLOCK(fd) do { u_long mode = 1; ioctlsocket(fd, FIONBIO, &mode); } while(0)
#define SET_BLOCK(fd)    do { u_long mode = 0; ioctlsocket(fd, FIONBIO, &mode); } while(0)
#define strdup _strdup
#pragma warning(disable: 4244 4267)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#define SET_NONBLOCK(fd) do { int flags = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, flags | O_NONBLOCK); } while(0)
#define SET_BLOCK(fd)    do { int flags = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, flags & ~O_NONBLOCK); } while(0)
#endif

#include <cstring>
#include <cerrno>

#ifdef _WIN32
#else
#include <signal.h>
#endif

namespace scp {

SshConnection::SshConnection()
    : ssh_session_(nullptr), sftp_session_(nullptr) {}

SshConnection::~SshConnection() {
  Disconnect();
}

void SshConnection::SetLastError(const char* msg) {
  last_error_ = msg ? msg : "";
  SCP_LOG_ERROR("%s", msg);
}

void SshConnection::SetSocketOptions() {
  if (!socket_.IsValid()) return;

  if (tcp_nodelay_) {
    int flag = 1;
    setsockopt(socket_.fd, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&flag), sizeof(flag));
    SCP_LOG_DEBUG("TCP_NODELAY set on socket %d", socket_.fd);
  }

  // Set socket buffer sizes for high-throughput bulk transfer.
  // Larger buffers allow the TCP window to grow and keep the pipe full on
  // high-bandwidth / high-latency links.
  int sndbuf = 4 * 1024 * 1024;
  int rcvbuf = 4 * 1024 * 1024;
  setsockopt(socket_.fd, SOL_SOCKET, SO_SNDBUF,
             reinterpret_cast<const char*>(&sndbuf), sizeof(sndbuf));
  setsockopt(socket_.fd, SOL_SOCKET, SO_RCVBUF,
             reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf));
}

scp_error_t SshConnection::CreateSocket(const char* host, uint16_t port,
                                         uint32_t timeout_s) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char port_str[8];
  snprintf(port_str, sizeof(port_str), "%u", port > 0 ? port : 22);

  struct addrinfo* result = nullptr;
  int ret = getaddrinfo(host, port_str, &hints, &result);
  if (ret != 0) {
    SetLastError("DNS resolution failed");
    return SCP_ERROR_HOST_NOT_FOUND;
  }

  // RAII cleanup for addrinfo
  auto free_addrinfo = [](struct addrinfo* r) { if (r) freeaddrinfo(r); };
  UniqueResource<struct addrinfo*, decltype(free_addrinfo)> guard(result, free_addrinfo);

  // Try each address
  int addr_idx = 0;
  for (struct addrinfo* rp = result; rp; rp = rp->ai_next, addr_idx++) {
    char ipstr[INET6_ADDRSTRLEN];
    void* addr = nullptr;
    if (rp->ai_family == AF_INET) {
      addr = &reinterpret_cast<struct sockaddr_in*>(rp->ai_addr)->sin_addr;
    } else if (rp->ai_family == AF_INET6) {
      addr = &reinterpret_cast<struct sockaddr_in6*>(rp->ai_addr)->sin6_addr;
    }
    if (addr) inet_ntop(rp->ai_family, addr, ipstr, sizeof(ipstr));
    else snprintf(ipstr, sizeof(ipstr), "(unknown family %d)", rp->ai_family);

    socket_.fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (!socket_.IsValid()) {
      SCP_LOG_DEBUG("[%s]:%u  addr#%d %s: socket() failed (errno=%d)",
                    host, port, addr_idx, ipstr,
#ifdef _WIN32
                    WSAGetLastError()
#else
                    errno
#endif
                    );
      continue;
    }

    // Set non-blocking for connect with timeout
    if (timeout_s > 0) {
      SET_NONBLOCK(socket_.fd);
    }

    ret = connect(socket_.fd, rp->ai_addr, rp->ai_addrlen);
    if (ret < 0 && timeout_s > 0) {
#ifdef _WIN32
      if (WSAGetLastError() == WSAEWOULDBLOCK) {
#else
      if (errno == EINPROGRESS) {
#endif
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(socket_.fd, &wset);
        struct timeval tv;
        tv.tv_sec = timeout_s;
        tv.tv_usec = 0;
        ret = select(0, nullptr, &wset, nullptr, &tv);
        if (ret <= 0) {
          SCP_LOG_DEBUG("[%s]:%u  addr#%d %s: connect timeout after %us",
                        host, port, addr_idx, ipstr, timeout_s);
          close(socket_.fd);
          socket_.fd = INVALID_SOCKET_VALUE;
          continue;
        }
        // Check connect result
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        getsockopt(socket_.fd, SOL_SOCKET, SO_ERROR,
                   reinterpret_cast<char*>(&so_error), &len);
        if (so_error != 0) {
          SCP_LOG_DEBUG("[%s]:%u  addr#%d %s: connect error (errno=%d: %s)",
                        host, port, addr_idx, ipstr, so_error,
                        strerror(so_error));
          close(socket_.fd);
          socket_.fd = INVALID_SOCKET_VALUE;
          continue;
        }
      } else {
        SCP_LOG_DEBUG("[%s]:%u  addr#%d %s: connect immediate error (errno=%d: %s)",
                      host, port, addr_idx, ipstr,
#ifdef _WIN32
                      WSAGetLastError()
#else
                      errno
#endif
                      ,
#ifdef _WIN32
                      ""
#else
                      strerror(errno)
#endif
                      );
        close(socket_.fd);
        socket_.fd = INVALID_SOCKET_VALUE;
        continue;
      }
    } else if (ret < 0) {
      SCP_LOG_DEBUG("[%s]:%u  addr#%d %s: connect failed (errno=%d: %s)",
                    host, port, addr_idx, ipstr,
#ifdef _WIN32
                    WSAGetLastError()
#else
                    errno
#endif
                    ,
#ifdef _WIN32
                    ""
#else
                    strerror(errno)
#endif
                    );
      close(socket_.fd);
      socket_.fd = INVALID_SOCKET_VALUE;
      continue;
    }

    // Restore blocking mode
    if (timeout_s > 0) {
      SET_BLOCK(socket_.fd);
    }

    // Connected successfully
    host_ = host;
    SetSocketOptions();
    return SCP_OK;
  }

  char errbuf[256];
  snprintf(errbuf, sizeof(errbuf), "Failed to connect to %s:%u", host, port);
  SetLastError(errbuf);
  return SCP_ERROR_CONNECT;
}

scp_error_t SshConnection::Handshake() {
  if (!socket_.IsValid()) return SCP_ERROR_NETWORK;

  ssh_session_ = libssh2_session_init();
  if (!ssh_session_) {
    SetLastError("Failed to initialize libssh2 session");
    return SCP_ERROR_CONNECT;
  }

  // Disable default blocking behavior; we handle it ourselves
  libssh2_session_set_blocking(ssh_session_, 1);

  int ret = libssh2_session_handshake(ssh_session_, socket_.fd);
  if (ret) {
    char* errmsg = nullptr;
    libssh2_session_last_error(ssh_session_, &errmsg, nullptr, 0);
    char buf[512];
    snprintf(buf, sizeof(buf), "SSH handshake failed: %s",
             errmsg ? errmsg : "unknown error");
    SetLastError(buf);
    return SCP_ERROR_CONNECT;
  }

  SCP_LOG_INFO("SSH handshake completed with %s", host_.c_str());
  return SCP_OK;
}

scp_error_t SshConnection::AuthenticatePassword(const char* username, const char* password) {
  int ret = libssh2_userauth_password(ssh_session_, username, password);
  if (ret) {
    char* errmsg = nullptr;
    libssh2_session_last_error(ssh_session_, &errmsg, nullptr, 0);
    char buf[512];
    snprintf(buf, sizeof(buf), "Password authentication failed: %s",
             errmsg ? errmsg : "access denied");
    SetLastError(buf);
    return SCP_ERROR_AUTH_DENIED;
  }
  SCP_LOG_INFO("Password authentication succeeded");
  return SCP_OK;
}

scp_error_t SshConnection::AuthenticatePublicKey(const scp_connect_config_t* config) {
  const char* key_data   = config->key_data;
  const char* key_path   = config->key_path;
  const char* passphrase = config->passphrase;

  int ret = 0;
  if (key_data && key_data[0]) {
    // Authenticate with in-memory key data
    ret = libssh2_userauth_publickey_frommemory(
        ssh_session_,
        config->username,
        strlen(config->username),
        nullptr,  // publickey (NULL for auto-derive)
        0,
        key_data,
        strlen(key_data),
        passphrase);
  } else if (key_path && key_path[0]) {
    // Authenticate with key file
    ret = libssh2_userauth_publickey_fromfile(
        ssh_session_,
        config->username,
        nullptr,  // publickey file (NULL for auto-derive from private key)
        key_path,
        passphrase);
  } else {
    // Try default key locations
    ret = libssh2_userauth_publickey_fromfile(
        ssh_session_,
        config->username,
        nullptr,
        nullptr,  // default ~/.ssh/id_rsa etc.
        passphrase);
    // If default key doesn't exist, try explicit ones
    if (ret) {
      const char* home = getenv("HOME");
      if (home) {
        char keyfile[1024];
        snprintf(keyfile, sizeof(keyfile), "%s/.ssh/id_rsa", home);
        ret = libssh2_userauth_publickey_fromfile(
            ssh_session_, config->username, nullptr, keyfile, passphrase);
      }
      if (ret) {
        if (home) {
          char keyfile[1024];
          snprintf(keyfile, sizeof(keyfile), "%s/.ssh/id_ed25519", home);
          ret = libssh2_userauth_publickey_fromfile(
              ssh_session_, config->username, nullptr, keyfile, passphrase);
        }
      }
    }
  }

  if (ret) {
    char* errmsg = nullptr;
    libssh2_session_last_error(ssh_session_, &errmsg, nullptr, 0);
    char buf[512];
    snprintf(buf, sizeof(buf), "Public key authentication failed: %s",
             errmsg ? errmsg : "invalid key or passphrase");
    SetLastError(buf);
    if (ret == LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED ||
        ret == LIBSSH2_ERROR_AUTHENTICATION_FAILED) {
      return SCP_ERROR_AUTH_DENIED;
    }
    return SCP_ERROR_AUTH_KEY;
  }
  SCP_LOG_INFO("Public key authentication succeeded");
  return SCP_OK;
}

// Helper: keyboard-interactive response callback for libssh2
namespace {
struct KbdintContext {
  scp_kbdint_cb callback;
  void* userdata;
  char** responses;  // filled by callback, freed by us
  int num_responses;
};
}

static void KbdintLibssh2Callback(const char* name, int name_len,
                                   const char* instruction, int instruction_len,
                                   int num_prompts,
                                   const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
                                   LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses,
                                   void** abstract) {
  KbdintContext* ctx = static_cast<KbdintContext*>(*abstract);
  if (!ctx || !ctx->callback) return;

  // Build arrays for user callback
  const char** prompt_texts = new const char*[num_prompts];
  char** response_texts = new char*[num_prompts]();
  for (int i = 0; i < num_prompts; i++) {
    prompt_texts[i] = reinterpret_cast<const char*>(prompts[i].text);
  }

  ctx->callback(name, instruction, num_prompts, prompt_texts, response_texts, ctx->userdata);
  ctx->responses = response_texts;
  ctx->num_responses = num_prompts;

  for (int i = 0; i < num_prompts; i++) {
    if (response_texts[i]) {
      responses[i].text = strdup(response_texts[i]);
      responses[i].length = strlen(response_texts[i]);
    } else {
      responses[i].text = strdup("");
      responses[i].length = 0;
    }
  }

  delete[] prompt_texts;
}

scp_error_t SshConnection::AuthenticateKeyboardInteractive(
    const char* username, scp_kbdint_cb callback, void* userdata) {

  KbdintContext ctx;
  ctx.callback = callback;
  ctx.userdata = userdata;
  ctx.responses = nullptr;
  ctx.num_responses = 0;

  int ret = libssh2_userauth_keyboard_interactive(
      ssh_session_, username, KbdintLibssh2Callback);

  // Free responses allocated by the callback
  if (ctx.responses) {
    for (int i = 0; i < ctx.num_responses; i++) {
      free(ctx.responses[i]);
    }
    delete[] ctx.responses;
  }

  if (ret) {
    char* errmsg = nullptr;
    libssh2_session_last_error(ssh_session_, &errmsg, nullptr, 0);
    char buf[512];
    snprintf(buf, sizeof(buf), "Keyboard-interactive auth failed: %s",
             errmsg ? errmsg : "denied");
    SetLastError(buf);
    return SCP_ERROR_AUTH_DENIED;
  }
  SCP_LOG_INFO("Keyboard-interactive authentication succeeded");
  return SCP_OK;
}

scp_error_t SshConnection::StartSftp() {
  if (!ssh_session_) return SCP_ERROR_INVALID_HANDLE;

  sftp_session_ = libssh2_sftp_init(ssh_session_);
  if (!sftp_session_) {
    char* errmsg = nullptr;
    libssh2_session_last_error(ssh_session_, &errmsg, nullptr, 0);
    char buf[512];
    snprintf(buf, sizeof(buf), "Failed to initialize SFTP: %s",
             errmsg ? errmsg : "unknown error");
    SetLastError(buf);
    return SCP_ERROR_SFTP_INIT;
  }
  SCP_LOG_INFO("SFTP session initialized");
  return SCP_OK;
}

scp_error_t SshConnection::Connect(const scp_connect_config_t* config) {
  if (!config) return SCP_ERROR_INVALID_ARG;

  // Disconnect existing session if any
  Disconnect();

  tcp_nodelay_ = config->tcp_nodelay;

  // 1. Create socket
  scp_error_t err = CreateSocket(config->host, config->port,
                                  config->connect_timeout_s);
  if (err != SCP_OK) return err;

  // 2. SSH handshake
  err = Handshake();
  if (err != SCP_OK) return err;

  // 3. Set preferred cipher if specified
  if (config->preferred_cipher != SCP_CIPHER_AUTO) {
    const char* cipher = nullptr;
    switch (config->preferred_cipher) {
      case SCP_CIPHER_AES128_CTR:
        cipher = "aes128-ctr";
        break;
      case SCP_CIPHER_AES256_CTR:
        cipher = "aes256-ctr";
        break;
      case SCP_CIPHER_CHACHA20_POLY1305:
        cipher = "chacha20-poly1305@openssh.com";
        break;
      default:
        break;
    }
    if (cipher) {
      libssh2_session_method_pref(ssh_session_, LIBSSH2_METHOD_CRYPT_CS, cipher);
      libssh2_session_method_pref(ssh_session_, LIBSSH2_METHOD_CRYPT_SC, cipher);
      SCP_LOG_INFO("Preferred cipher set: %s", cipher);
    }
  }

  // 4. Check available auth methods
  char* userauthlist = libssh2_userauth_list(ssh_session_, config->username,
                                              strlen(config->username));
  SCP_LOG_INFO("Available auth methods: %s",
               userauthlist ? userauthlist : "(none)");

  // 5. Authenticate
  bool authenticated = false;

  if (config->auth_type == SCP_AUTH_KEYBOARD_INTERACTIVE &&
      config->kbdint_callback) {
    err = AuthenticateKeyboardInteractive(config->username, config->kbdint_callback,
                                           config->kbdint_userdata);
    authenticated = (err == SCP_OK);
  }

  if (!authenticated && config->auth_type == SCP_AUTH_PUBLICKEY) {
    err = AuthenticatePublicKey(config);
    if (err == SCP_OK) authenticated = true;
    else if (err == SCP_ERROR_AUTH_PARTIAL && config->kbdint_callback) {
      SCP_LOG_INFO("Partial auth success, trying keyboard-interactive...");
      err = AuthenticateKeyboardInteractive(config->username, config->kbdint_callback,
                                             config->kbdint_userdata);
      authenticated = (err == SCP_OK);
    }
  }

  if (!authenticated && config->auth_type == SCP_AUTH_PASSWORD) {
    err = AuthenticatePassword(config->username, config->password);
    authenticated = (err == SCP_OK);
  }

  // SCP_AUTH_NONE: try empty password first, then none-auth method
  if (!authenticated && config->auth_type == SCP_AUTH_NONE) {
    err = AuthenticatePassword(config->username, "");
    if (err == SCP_OK) {
      authenticated = true;
      SCP_LOG_INFO("Empty password auth succeeded");
    }
  }

  if (!authenticated) {
    SCP_LOG_ERROR("All authentication methods failed");
    return SCP_ERROR_AUTH;
  }

  // 6. Start SFTP session
  err = StartSftp();
  if (err != SCP_OK) return err;

  connected_.store(true, std::memory_order_release);
  SCP_LOG_INFO("Connected to %s", config->host);

  return SCP_OK;
}

scp_error_t SshConnection::Disconnect() {
  connected_.store(false, std::memory_order_release);

  if (sftp_session_) {
    libssh2_sftp_shutdown(sftp_session_);
    sftp_session_ = nullptr;
  }

  if (ssh_session_) {
    libssh2_session_disconnect(ssh_session_, "Client disconnecting");
    libssh2_session_free(ssh_session_);
    ssh_session_ = nullptr;
  }

  if (socket_.IsValid()) {
#ifdef _WIN32
    shutdown(socket_.fd, SD_BOTH);
#else
    shutdown(socket_.fd, SHUT_RDWR);
#endif
    close(socket_.fd);
    socket_.fd = INVALID_SOCKET_VALUE;
  }

  host_.clear();
  last_error_.clear();
  return SCP_OK;
}

scp_error_t SshConnection::Keepalive() {
  if (!ssh_session_ || !connected_.load(std::memory_order_acquire)) {
    return SCP_ERROR_DISCONNECTED;
  }

  // Send a keepalive via the global request mechanism
  int ret = libssh2_keepalive_send(ssh_session_, nullptr);
  if (ret) {
    char* errmsg = nullptr;
    libssh2_session_last_error(ssh_session_, &errmsg, nullptr, 0);
    char buf[256];
    snprintf(buf, sizeof(buf), "Keepalive failed: %s",
             errmsg ? errmsg : "connection lost");
    SetLastError(buf);
    connected_.store(false, std::memory_order_release);
    return SCP_ERROR_NETWORK;
  }
  return SCP_OK;
}

}  // namespace scp
