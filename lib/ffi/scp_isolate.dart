/// Isolate-based execution wrapper for blocking native SCP operations.
///
/// All blocking I/O (connect, transfer, file operations) MUST be executed
/// in a separate Isolate to avoid blocking the Flutter UI thread.
///
/// Communication pattern:
/// - SendRequest + ReceivePort for request-response calls
/// - Capability + stream for progress/event callbacks
///
/// Memory strategy: Dart sends file paths (strings) to native via FFI;
/// native directly reads/writes local disk. Zero large-memory copies
/// cross the Dart/native boundary.

import 'dart:async';
import 'dart:ffi';
import 'dart:isolate';
import 'package:ffi/ffi.dart';

import 'scp_bindings.dart' as bindings;
import 'scp_error.dart';

/// Message types for Isolate communication.
sealed class ScpMessage {}

class ConnectRequest extends ScpMessage {
  final SendPort replyTo;
  final String host;
  final int port;
  final String username;
  final String? password;
  final String? keyPath;
  final String? passphrase;
  final int timeoutS;

  ConnectRequest({
    required this.replyTo,
    required this.host,
    this.port = 22,
    required this.username,
    this.password,
    this.keyPath,
    this.passphrase,
    this.timeoutS = 30,
  });
}

class DisconnectRequest extends ScpMessage {
  final SendPort replyTo;
  final int sessionHandle;
  DisconnectRequest({required this.replyTo, required this.sessionHandle});
}

class ListDirRequest extends ScpMessage {
  final SendPort replyTo;
  final int sessionHandle;
  final String path;
  ListDirRequest(
      {required this.replyTo,
      required this.sessionHandle,
      required this.path});
}

class TransferRequest extends ScpMessage {
  final SendPort replyTo;
  final int sessionHandle;
  final String localPath;
  final String remotePath;
  final bool isUpload;
  final bool resume;
  final int maxConcurrent;
  final int blockSize;

  TransferRequest({
    required this.replyTo,
    required this.sessionHandle,
    required this.localPath,
    required this.remotePath,
    required this.isUpload,
    this.resume = false,
    this.maxConcurrent = 32,
    this.blockSize = 256 * 1024,
  });
}

class TransferControlRequest extends ScpMessage {
  final SendPort replyTo;
  final int transferHandle;
  final String command; // 'pause', 'resume', 'cancel', 'wait'
  TransferControlRequest(
      {required this.replyTo,
      required this.transferHandle,
      required this.command});
}

class ProgressRequest extends ScpMessage {
  final SendPort replyTo;
  final int transferHandle;
  ProgressRequest({required this.replyTo, required this.transferHandle});
}

class FileOpRequest extends ScpMessage {
  final SendPort replyTo;
  final int sessionHandle;
  final String op; // 'mkdir', 'rmdir', 'unlink', 'rename', 'chmod'
  final String path;
  final String? path2;
  final int? permissions;
  FileOpRequest({
    required this.replyTo,
    required this.sessionHandle,
    required this.op,
    required this.path,
    this.path2,
    this.permissions,
  });
}

class InitRequest extends ScpMessage {
  final SendPort replyTo;
  InitRequest({required this.replyTo});
}

class ShutdownRequest extends ScpMessage {
  final SendPort replyTo;
  ShutdownRequest({required this.replyTo});
}

/// Response from Isolate.
class ScpResponse {
  final bool success;
  final dynamic data;
  final int? errorCode;
  final String? errorMessage;

  ScpResponse(this.success, {this.data, this.errorCode, this.errorMessage});

  Map<String, dynamic> toMap() => {
        'success': success,
        'data': data,
        'errorCode': errorCode,
        'errorMessage': errorMessage,
      };
}

/// Progress update from Isolate.
class ScpProgress {
  final int transferHandle;
  final int transferred;
  final int total;
  final int speedBytesPerSec; // estimated

  ScpProgress({
    required this.transferHandle,
    required this.transferred,
    required this.total,
    this.speedBytesPerSec = 0,
  });

  double get percent =>
      total > 0 ? (transferred / total) * 100.0 : 0.0;
}

// ---------------------------------------------------------------------------
// Isolate entry point
// ---------------------------------------------------------------------------

void _scpIsolateEntry(SendPort mainSendPort) {
  final receivePort = ReceivePort();
  mainSendPort.send(receivePort.sendPort);

  // Map from transfer handle (native int) to timer for progress polling
  final progressTimers = <int, Timer>{};

  receivePort.listen((message) async {
    if (message is InitRequest) {
      final code = bindings.scpInit();
      message.replyTo.send(ScpResponse(code == 0,
          errorCode: code == 0 ? null : code, errorMessage: ''));

    } else if (message is ShutdownRequest) {
      bindings.scpCleanup();
      // Cancel all progress timers
      for (final timer in progressTimers.values) {
        timer.cancel();
      }
      progressTimers.clear();
      message.replyTo.send(ScpResponse(true));

    } else if (message is ConnectRequest) {
      // Build native ConnectConfig
      final config = calloc<bindings.ConnectConfig>();
      final hostPtr = message.host.toNativeUtf8();
      final userPtr = message.username.toNativeUtf8();

      config.ref.host = hostPtr.cast();
      config.ref.port = message.port;
      config.ref.username = userPtr.cast();
      config.ref.connectTimeoutS = message.timeoutS;
      config.ref.tcpNodelay = 1;
      config.ref.keepaliveS = 30;
      config.ref.preferredCipher = 1; // aes128-ctr

      if (message.password != null && message.password!.isNotEmpty) {
        config.ref.authType = 0; // SCP_AUTH_PASSWORD
        final passPtr = message.password!.toNativeUtf8();
        config.ref.credential.password = passPtr.cast();
      } else if (message.keyPath != null) {
        config.ref.authType = 1; // SCP_AUTH_PUBLICKEY
        final keyPtr = message.keyPath!.toNativeUtf8();
        config.ref.credential.rawKey[0] = nullptr;
        config.ref.credential.rawKey[1] = keyPtr.cast();
        config.ref.credential.rawKey[2] = message.passphrase?.toNativeUtf8().cast() ?? nullptr;
      } else {
        config.ref.authType = 3; // SCP_AUTH_NONE (panel SFTP, no password)
      }

      final outSession = calloc<IntPtr>();
      final code = bindings.scpConnect(config, outSession);

      calloc.free(hostPtr);
      calloc.free(userPtr);

      if (code == 0) {
        message.replyTo.send(ScpResponse(true, data: outSession.value));
      } else {
        final errMsg = bindings.scpErrorString(code).toDartString();
        message.replyTo.send(
            ScpResponse(false, errorCode: code, errorMessage: errMsg));
      }
      calloc.free(outSession);
      calloc.free(config);
    }
  });
}

// ---------------------------------------------------------------------------
// High-level Dart API
// ---------------------------------------------------------------------------

/// Manages an isolate dedicated to native SCP operations.
/// All blocking calls are dispatched to this isolate.
///
/// Usage:
/// ```dart
/// final scp = ScpIsolateClient();
/// await scp.init();
/// final session = await scp.connect(host: 'example.com', username: 'user', password: 'pass');
/// final files = await scp.listDir(session, '/home/user');
/// await scp.disconnect(session);
/// await scp.shutdown();
/// ```
class ScpIsolateClient {
  Isolate? _isolate;
  SendPort? _sendPort;
  ReceivePort? _receivePort;
  final Map<int, Completer<ScpResponse>> _pendingRequests = {};
  int _requestId = 0;
  bool _initialized = false;

  // Progress stream
  final _progressController = StreamController<ScpProgress>.broadcast();
  Stream<ScpProgress> get progressStream => _progressController.stream;

  /// Initialize the native library in a background isolate.
  Future<void> init() async {
    if (_initialized) return;

    _receivePort = ReceivePort();
    _isolate = await Isolate.spawn(_scpIsolateEntry, _receivePort!.sendPort);

    // Get the isolate's SendPort
    final completer = Completer<SendPort>();
    _receivePort!.listen((message) {
      if (message is SendPort) {
        completer.complete(message);
      } else if (message is ScpResponse) {
        // Handle incoming responses
        final reqId = message.data is Map ? (message.data as Map)['reqId'] : null;
        if (reqId != null) {
          final c = _pendingRequests.remove(reqId);
          c?.complete(message);
        }
      }
    });

    _sendPort = await completer.future;

    final response = await _sendRequest(InitRequest(replyTo: _receivePort!.sendPort));
    if (!response.success) {
      throw ScpException.fromCode(response.errorCode ?? -1000,
          detail: 'Failed to initialize native library');
    }
    _initialized = true;
  }

  /// Connect to a remote host.
  Future<int> connect({
    required String host,
    int port = 22,
    required String username,
    String? password,
    String? keyPath,
    String? passphrase,
    int timeoutS = 30,
  }) async {
    final response = await _sendRequest(ConnectRequest(
      replyTo: _receivePort!.sendPort,
      host: host,
      port: port,
      username: username,
      password: password,
      keyPath: keyPath,
      passphrase: passphrase,
      timeoutS: timeoutS,
    ));

    if (!response.success) {
      throw ScpException.fromCode(response.errorCode ?? -1100,
          detail: response.errorMessage);
    }
    return response.data as int;
  }

  /// Disconnect a session.
  Future<void> disconnect(int sessionHandle) async {
    await _sendRequest(DisconnectRequest(
        replyTo: _receivePort!.sendPort, sessionHandle: sessionHandle));
  }

  /// List directory contents.
  Future<List<Map<String, dynamic>>> listDir(
      int sessionHandle, String path) async {
    final response = await _sendRequest(ListDirRequest(
        replyTo: _receivePort!.sendPort,
        sessionHandle: sessionHandle,
        path: path));

    if (!response.success) {
      throw ScpException.fromCode(response.errorCode ?? -1400,
          detail: response.errorMessage);
    }
    return (response.data as List).cast<Map<String, dynamic>>();
  }

  /// Start a file transfer.
  Future<int> startTransfer({
    required int sessionHandle,
    required String localPath,
    required String remotePath,
    required bool isUpload,
    bool resume = false,
    int maxConcurrent = 32,
    int blockSize = 256 * 1024,
  }) async {
    final response = await _sendRequest(TransferRequest(
      replyTo: _receivePort!.sendPort,
      sessionHandle: sessionHandle,
      localPath: localPath,
      remotePath: remotePath,
      isUpload: isUpload,
      resume: resume,
      maxConcurrent: maxConcurrent,
      blockSize: blockSize,
    ));

    if (!response.success) {
      throw ScpException.fromCode(response.errorCode ?? -1500,
          detail: response.errorMessage);
    }
    return response.data as int;
  }

  /// Control a transfer (pause/resume/cancel/wait).
  Future<bool> controlTransfer(int transferHandle, String command) async {
    final response = await _sendRequest(TransferControlRequest(
        replyTo: _receivePort!.sendPort,
        transferHandle: transferHandle,
        command: command));

    return response.success;
  }

  /// Shutdown the native library and isolate.
  Future<void> shutdown() async {
    if (!_initialized) return;
    await _sendRequest(ShutdownRequest(replyTo: _receivePort!.sendPort));
    _isolate?.kill(priority: Isolate.immediate);
    _isolate = null;
    _initialized = false;
    _receivePort?.close();
    _progressController.close();
  }

  Future<ScpResponse> _sendRequest(ScpMessage message) {
    final completer = Completer<ScpResponse>();
    final id = ++_requestId;
    _pendingRequests[id] = completer;
    _sendPort?.send(message);
    return completer.future.timeout(
      const Duration(seconds: 120),
      onTimeout: () {
        _pendingRequests.remove(id);
        return ScpResponse(false,
            errorCode: -1102, errorMessage: 'Operation timed out');
      },
    );
  }
}
