/// Low-level dart:ffi bindings for the crossscp native library.
/// Maps every `extern "C"` function in scp_api.h to a Dart FFI call.

import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

// Native function typedefs (C-side, used for lookupFunction native type).
typedef _ScpInitC = Int32 Function();
typedef _ScpCleanupC = Void Function();
typedef _ScpGetVersionC = Pointer<Utf8> Function();
typedef _ScpErrorStringC = Pointer<Utf8> Function(Int32 error);
typedef _ScpConnectC = Int32 Function(Pointer<ConnectConfig> config, Pointer<IntPtr> outSession);
typedef _ScpConnect2C = Int32 Function(Pointer<Utf8> host, Uint16 port, Pointer<Utf8> user, Pointer<Utf8> pwd, Pointer<Utf8> key, Pointer<Utf8> pass, Uint32 timeout, Pointer<IntPtr> out);
typedef _ScpDisconnectC = Int32 Function(IntPtr session);
typedef _ScpIsConnectedC = Int8 Function(IntPtr session);
typedef _ScpSessionGetHostC = Pointer<Utf8> Function(IntPtr session);
typedef _ScpKeepaliveC = Int32 Function(IntPtr session);
typedef _ScpSessionGetLastErrorC = Pointer<Utf8> Function(IntPtr session);
typedef _ScpListDirC = Int32 Function(IntPtr session, Pointer<Utf8> path, Pointer<Pointer<FileInfo>> outFiles, Pointer<Uint32> outCount);
typedef _ScpStatC = Int32 Function(IntPtr session, Pointer<Utf8> path, Pointer<FileInfo> outInfo);
typedef _ScpMkdirC = Int32 Function(IntPtr session, Pointer<Utf8> path, Uint32 permissions);
typedef _ScpRmdirC = Int32 Function(IntPtr session, Pointer<Utf8> path);
typedef _ScpUnlinkC = Int32 Function(IntPtr session, Pointer<Utf8> path);
typedef _ScpRenameC = Int32 Function(IntPtr session, Pointer<Utf8> oldPath, Pointer<Utf8> newPath);
typedef _ScpChmodC = Int32 Function(IntPtr session, Pointer<Utf8> path, Uint32 permissions);
typedef _ScpTouchC = Int32 Function(IntPtr session, Pointer<Utf8> path);
typedef _ScpDfC = Int32 Function(IntPtr session, Pointer<Utf8> path, Pointer<Uint64> outTotal, Pointer<Uint64> outFree);
typedef _ScpFreeFileListC = Void Function(Pointer<FileInfo> files, Uint32 count);
typedef _ScpTransferStartC = Int32 Function(IntPtr session, Pointer<TransferConfig> config, Pointer<IntPtr> outTransfer);
typedef _ScpBatchTransferC = Int32 Function(IntPtr session, Pointer<BatchConfig> config);
typedef _ScpTransferPauseC = Int32 Function(IntPtr transfer);
typedef _ScpTransferResumeC = Int32 Function(IntPtr transfer);
typedef _ScpTransferCancelC = Int32 Function(IntPtr transfer);
typedef _ScpTransferWaitC = Int32 Function(IntPtr transfer);
typedef _ScpTransferGetProgressC = Int32 Function(IntPtr transfer, Pointer<Uint64> transferred, Pointer<Uint64> total);
typedef _ScpTransferFreeC = Void Function(IntPtr transfer);

// Dart-side function types (use `int` for pointer-sized integers, not IntPtr).
typedef _ScpInitD = int Function();
typedef _ScpCleanupD = void Function();
typedef _ScpGetVersionD = Pointer<Utf8> Function();
typedef _ScpErrorStringD = Pointer<Utf8> Function(int error);
typedef _ScpConnectD = int Function(Pointer<ConnectConfig> config, Pointer<IntPtr> outSession);
typedef _ScpConnect2D = int Function(Pointer<Utf8> host, int port, Pointer<Utf8> user, Pointer<Utf8> pwd, Pointer<Utf8> key, Pointer<Utf8> pass, int timeout, Pointer<IntPtr> out);
typedef _ScpDisconnectD = int Function(int session);
typedef _ScpIsConnectedD = int Function(int session);
typedef _ScpSessionGetHostD = Pointer<Utf8> Function(int session);
typedef _ScpKeepaliveD = int Function(int session);
typedef _ScpSessionGetLastErrorD = Pointer<Utf8> Function(int session);
typedef _ScpListDirD = int Function(int session, Pointer<Utf8> path, Pointer<Pointer<FileInfo>> outFiles, Pointer<Uint32> outCount);
typedef _ScpStatD = int Function(int session, Pointer<Utf8> path, Pointer<FileInfo> outInfo);
typedef _ScpMkdirD = int Function(int session, Pointer<Utf8> path, int permissions);
typedef _ScpRmdirD = int Function(int session, Pointer<Utf8> path);
typedef _ScpUnlinkD = int Function(int session, Pointer<Utf8> path);
typedef _ScpRenameD = int Function(int session, Pointer<Utf8> oldPath, Pointer<Utf8> newPath);
typedef _ScpChmodD = int Function(int session, Pointer<Utf8> path, int permissions);
typedef _ScpTouchD = int Function(int session, Pointer<Utf8> path);
typedef _ScpDfD = int Function(int session, Pointer<Utf8> path, Pointer<Uint64> outTotal, Pointer<Uint64> outFree);
typedef _ScpFreeFileListD = void Function(Pointer<FileInfo> files, int count);
typedef _ScpTransferStartD = int Function(int session, Pointer<TransferConfig> config, Pointer<IntPtr> outTransfer);
typedef _ScpBatchTransferD = int Function(int session, Pointer<BatchConfig> config);
typedef _ScpTransferPauseD = int Function(int transfer);
typedef _ScpTransferResumeD = int Function(int transfer);
typedef _ScpTransferCancelD = int Function(int transfer);
typedef _ScpTransferWaitD = int Function(int transfer);
typedef _ScpTransferGetProgressD = int Function(int transfer, Pointer<Uint64> transferred, Pointer<Uint64> total);
typedef _ScpTransferFreeD = void Function(int transfer);

// Callback types
typedef _ProgressCallbackC = Void Function(
    IntPtr transfer, Uint64 transferred, Uint64 total, Pointer<Void> userdata);
typedef _LogCallbackC = Void Function(
    Int32 level, Pointer<Utf8> message, Pointer<Void> userdata);

// ---------------------------------------------------------------------------
// C struct definitions (must match scp_types.h layout exactly)
// ---------------------------------------------------------------------------

final class FileInfo extends Struct {
  external Pointer<Utf8> filename;

  external Pointer<Utf8> longname;

  @Uint64()
  external int filesize;

  @Uint32()
  external int permissions;

  @Uint32()
  external int uid;

  @Uint32()
  external int gid;

  @Uint64()
  external int mtime;

  @Uint8()
  external int isDir;
}

final class ConnectConfig extends Struct {
  external Pointer<Utf8> host;

  @Uint16()
  external int port;

  external Pointer<Utf8> username;

  @Int32()
  external int authType;

  external Pointer<Utf8> password;

  external Pointer<Utf8> keyData;

  external Pointer<Utf8> keyPath;

  external Pointer<Utf8> passphrase;

  @Uint32()
  external int connectTimeoutS;

  @Uint32()
  external int keepaliveS;

  @Uint8()
  external int tcpNodelay;

  @Int32()
  external int preferredCipher;

  external Pointer<Void> kbdintCallback;
  external Pointer<Void> kbdintUserdata;
  external Pointer<Void> logCallback;
  external Pointer<Void> logUserdata;
}

final class TransferConfig extends Struct {
  @Int32()
  external int direction;

  external Pointer<Utf8> localPath;

  external Pointer<Utf8> remotePath;

  @Uint8()
  external int resume;

  @Uint8()
  external int verifyChecksum;

  @Uint32()
  external int maxConcurrent;

  @Uint32()
  external int blockSize;

  @Uint32()
  external int progressThrottleMs;

  external Pointer<NativeFunction<_ProgressCallbackC>> progressCallback;
  external Pointer<Void> progressUserdata;
}

final class BatchConfig extends Struct {
  @Uint32()
  external int count;

  external Pointer<TransferConfig> transfers;

  @Uint32()
  external int maxConcurrent;

  external Pointer<NativeFunction<Void Function(
      Int32, Pointer<Utf8>, Uint64, Uint64, Pointer<Void>)>>
      progressCallback;

  external Pointer<Void> progressUserdata;
}

// ---------------------------------------------------------------------------
// Dynamic library loading
// ---------------------------------------------------------------------------

DynamicLibrary _loadLibrary() {
  if (Platform.isAndroid) {
    return DynamicLibrary.open('libscp_native.so');
  } else if (Platform.isIOS || Platform.isMacOS) {
    return DynamicLibrary.process(); // statically linked
  } else if (Platform.isLinux) {
    return DynamicLibrary.open('libscp_native.so');
  } else if (Platform.isWindows) {
    return DynamicLibrary.open('scp_native.dll');
  }
  throw UnsupportedError('Unsupported platform: ${Platform.operatingSystem}');
}

final DynamicLibrary _lib = _loadLibrary();

// ---------------------------------------------------------------------------
// Bound functions
// ---------------------------------------------------------------------------

final scpInit = _lib.lookupFunction<_ScpInitC, _ScpInitD>('scp_init');
final scpCleanup = _lib.lookupFunction<_ScpCleanupC, _ScpCleanupD>('scp_cleanup');
final scpGetVersion = _lib.lookupFunction<_ScpGetVersionC, _ScpGetVersionD>('scp_get_version');
final scpErrorString = _lib.lookupFunction<_ScpErrorStringC, _ScpErrorStringD>('scp_error_string');
final scpConnect = _lib.lookupFunction<_ScpConnectC, _ScpConnectD>('scp_connect');
final scpConnect2 = _lib.lookupFunction<_ScpConnect2C, _ScpConnect2D>('scp_connect2');
final scpDisconnect = _lib.lookupFunction<_ScpDisconnectC, _ScpDisconnectD>('scp_disconnect');
final scpIsConnected = _lib.lookupFunction<_ScpIsConnectedC, _ScpIsConnectedD>('scp_is_connected');
final scpSessionGetHost = _lib.lookupFunction<_ScpSessionGetHostC, _ScpSessionGetHostD>('scp_session_get_host');
final scpKeepalive = _lib.lookupFunction<_ScpKeepaliveC, _ScpKeepaliveD>('scp_keepalive');
final scpSessionGetLastError = _lib.lookupFunction<_ScpSessionGetLastErrorC, _ScpSessionGetLastErrorD>('scp_session_get_last_error');
final scpListDir = _lib.lookupFunction<_ScpListDirC, _ScpListDirD>('scp_list_dir');
final scpStat = _lib.lookupFunction<_ScpStatC, _ScpStatD>('scp_stat');
final scpMkdir = _lib.lookupFunction<_ScpMkdirC, _ScpMkdirD>('scp_mkdir');
final scpRmdir = _lib.lookupFunction<_ScpRmdirC, _ScpRmdirD>('scp_rmdir');
final scpUnlink = _lib.lookupFunction<_ScpUnlinkC, _ScpUnlinkD>('scp_unlink');
final scpRename = _lib.lookupFunction<_ScpRenameC, _ScpRenameD>('scp_rename');
final scpChmod = _lib.lookupFunction<_ScpChmodC, _ScpChmodD>('scp_chmod');
final scpTouch = _lib.lookupFunction<_ScpTouchC, _ScpTouchD>('scp_touch');
final scpDf = _lib.lookupFunction<_ScpDfC, _ScpDfD>('scp_df');
final scpFreeFileList = _lib.lookupFunction<_ScpFreeFileListC, _ScpFreeFileListD>('scp_free_file_list');
final scpTransferStart = _lib.lookupFunction<_ScpTransferStartC, _ScpTransferStartD>('scp_transfer_start');
final scpBatchTransfer = _lib.lookupFunction<_ScpBatchTransferC, _ScpBatchTransferD>('scp_batch_transfer');
final scpTransferPause = _lib.lookupFunction<_ScpTransferPauseC, _ScpTransferPauseD>('scp_transfer_pause');
final scpTransferResume = _lib.lookupFunction<_ScpTransferResumeC, _ScpTransferResumeD>('scp_transfer_resume');
final scpTransferCancel = _lib.lookupFunction<_ScpTransferCancelC, _ScpTransferCancelD>('scp_transfer_cancel');
final scpTransferWait = _lib.lookupFunction<_ScpTransferWaitC, _ScpTransferWaitD>('scp_transfer_wait');
final scpTransferGetProgress = _lib.lookupFunction<_ScpTransferGetProgressC, _ScpTransferGetProgressD>('scp_transfer_get_progress');
final scpTransferFree = _lib.lookupFunction<_ScpTransferFreeC, _ScpTransferFreeD>('scp_transfer_free');
