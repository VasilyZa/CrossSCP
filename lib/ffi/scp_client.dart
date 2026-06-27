/// Singleton SCP client that executes all native calls in a dedicated Isolate.
///
/// Protocol: uses List<int|String|List> for isolate messages to avoid
/// class serialization issues. Worker sends [id, ok, data], main sends [id, cmd, ...args].

import 'dart:async';
import 'dart:ffi';
import 'dart:isolate';
import 'package:ffi/ffi.dart';
import 'package:crossscp/ffi/scp_bindings.dart' as bind;
import 'package:crossscp/ffi/scp_error.dart';

// --- Isolate entry -----------------------------------------------------------

void _worker(SendPort mainPort) {
  final recv = ReceivePort();
  try {
    bind.scpInit();
    mainPort.send(recv.sendPort);  // only send after init succeeds
  } catch (e) {
    mainPort.send([-1, 0, 'scpInit failed: $e']);
    return;
  }

  recv.listen((msg) {
    if (msg is! List) return;
    final id = msg[0] as int;
    final cmd = msg[1] as int;
    try {
      switch (cmd) {
        case 0: _wInit(id, mainPort); break;
        case 1: _wCleanup(id, mainPort); break;
        case 2: _wConnect(id, mainPort, msg); break;
        case 3: _wDisconnect(id, mainPort, msg); break;
        case 4: _wListDir(id, mainPort, msg); break;
        case 5: _wTransferStart(id, mainPort, msg); break;
        case 6: _wTransferControl(id, mainPort, msg); break;
        case 7: _wMkdir(id, mainPort, msg); break;
        case 8: _wProgress(id, mainPort, msg); break;
        case 9: _wTransferWait(id, mainPort, msg); break;
        case 10: _wFileOp(id, mainPort, msg); break;
        case 11: _wTouch(id, mainPort, msg); break;
      }
    } catch (e) {
      mainPort.send([id, 0, e.toString()]);
    }
  });
}

void _wInit(int id, SendPort r) { r.send([id, bind.scpInit() == 0 ? 1 : 0]); }
void _wCleanup(int id, SendPort r) { bind.scpCleanup(); r.send([id, 1]); }

void _wConnect(int id, SendPort r, List msg) {
  final host = (msg[2] as String).toNativeUtf8();
  final user = (msg[4] as String).toNativeUtf8();
  final pwd = msg[5] as String?;
  final out = calloc<IntPtr>();
  final pwdPtr = (pwd != null && pwd.isNotEmpty) ? pwd.toNativeUtf8() : nullptr;

  final code = bind.scpConnect2(
      host.cast(), msg[3] as int, user.cast(),
      pwdPtr.cast(), nullptr, nullptr,
      30, out);

  final session = code == 0 ? out.value : -1;
  calloc.free(host); calloc.free(user);
  if (pwdPtr != nullptr) calloc.free(pwdPtr);
  calloc.free(out);
  r.send([id, code == 0 ? 1 : 0, session]);
}

void _wDisconnect(int id, SendPort r, List msg) {
  bind.scpDisconnect(msg[2] as int);
  r.send([id, 1]);
}

void _wListDir(int id, SendPort r, List msg) {
  final s = msg[2] as int;
  final path = (msg[3] as String).toNativeUtf8();
  final outFiles = calloc<Pointer<bind.FileInfo>>();
  final outCount = calloc<Uint32>();
  final code = bind.scpListDir(s, path.cast(), outFiles, outCount);
  calloc.free(path);

  if (code != 0) {
    calloc.free(outFiles); calloc.free(outCount);
    r.send([id, 0, code]);
    return;
  }

  final count = outCount.value;
  final ptr = outFiles.value;
  final files = <String>[];
  for (int i = 0; i < count; i++) {
    final info = ptr.elementAt(i).ref;
    files.add('${info.filename.toDartString()}|'
        '${info.longname.toDartString()}|'
        '${info.filesize}|${info.permissions}|'
        '${info.uid}|${info.gid}|${info.mtime}|'
        '${info.isDir != 0}');
  }
  bind.scpFreeFileList(ptr, count);
  calloc.free(outFiles); calloc.free(outCount);
  r.send([id, 1, files]);
}

void _wTransferStart(int id, SendPort r, List msg) {
  final cfg = calloc<bind.TransferConfig>();
  final local = (msg[3] as String).toNativeUtf8();
  final remote = (msg[4] as String).toNativeUtf8();
  cfg.ref.direction = (msg[5] as bool) ? 0 : 1;
  cfg.ref.localPath = local.cast();
  cfg.ref.remotePath = remote.cast();
  cfg.ref.maxConcurrent = 32;
  cfg.ref.blockSize = 256 * 1024;
  cfg.ref.progressThrottleMs = 100;
  final out = calloc<IntPtr>();
  final code = bind.scpTransferStart(msg[2] as int, cfg, out);
  calloc.free(local); calloc.free(remote); calloc.free(cfg);
  final t = out.value; calloc.free(out);
  r.send([id, code == 0 ? 1 : 0, t]);
}

void _wTransferControl(int id, SendPort r, List msg) {
  final h = msg[2] as int;
  int code;
  switch (msg[3] as String) {
    case 'pause': code = bind.scpTransferPause(h); break;
    case 'resume': code = bind.scpTransferResume(h); break;
    case 'cancel': code = bind.scpTransferCancel(h); break;
    default: code = -1; break;
  }
  r.send([id, code == 0 ? 1 : 0]);
}

void _wMkdir(int id, SendPort r, List msg) {
  final p = (msg[3] as String).toNativeUtf8();
  final code = bind.scpMkdir(msg[2] as int, p.cast(), 493);
  calloc.free(p);
  r.send([id, code == 0 ? 1 : 0]);
}

void _wFileOp(int id, SendPort r, List msg) {
  final s = msg[2] as int;
  final path = (msg[3] as String).toNativeUtf8();
  int code;
  switch (msg[4] as int) {
    case 1: code = bind.scpRmdir(s, path.cast()); break;
    case 2: code = bind.scpUnlink(s, path.cast()); break;
    case 3:
      final np = (msg[5] as String).toNativeUtf8();
      code = bind.scpRename(s, path.cast(), np.cast());
      calloc.free(np);
      break;
    case 4:
      code = bind.scpChmod(s, path.cast(), msg[5] as int);
      break;
    default: code = -1;
  }
  calloc.free(path);
  r.send([id, code == 0 ? 1 : 0, code]);
}

void _wTouch(int id, SendPort r, List msg) {
  final p = (msg[3] as String).toNativeUtf8();
  final code = bind.scpTouch(msg[2] as int, p.cast());
  calloc.free(p);
  r.send([id, code == 0 ? 1 : 0]);
}

void _wProgress(int id, SendPort r, List msg) {
  final h = msg[2] as int;
  final transferred = calloc<Uint64>();
  final total = calloc<Uint64>();
  final code = bind.scpTransferGetProgress(h, transferred, total);
  final t = transferred.value, tt = total.value;
  calloc.free(transferred); calloc.free(total);
  r.send([id, code == 0 ? 1 : 0, t, tt]);
}

void _wTransferWait(int id, SendPort r, List msg) {
  final h = msg[2] as int;
  final code = bind.scpTransferWait(h);
  r.send([id, code == 0 ? 1 : 0, code]);
}

// ---------------------------------------------------------------------------
// ScpClient
// ---------------------------------------------------------------------------

class ScpClient {
  static final ScpClient _instance = ScpClient._();
  factory ScpClient() => _instance;
  ScpClient._();

  Isolate? _isolate;
  SendPort? _port;
  final _pending = <int, Completer<List>>{};
  int _seq = 0;
  bool _ready = false;
  String? _initError;

  Future<void> ensureReady() async {
    if (_ready) return;
    final rp = ReceivePort();
    rp.listen((msg) {
      if (msg is List && msg.length >= 2) {
        final id = msg[0] as int;
        if (id == -1) {
          // Worker reported a fatal init error
          _initError = '${msg[2]}';
          _port = null;
          return;
        }
        _pending.remove(id)?.complete(msg);
      } else if (msg is SendPort) {
        _port = msg;
      }
    });
    _isolate = await Isolate.spawn(_worker, rp.sendPort);
    // Wait for worker's SendPort with a timeout
    final sw = Stopwatch()..start();
    while (_port == null && _initError == null) {
      if (sw.elapsedMilliseconds > 5000) {
        rp.close();
        _isolate?.kill(priority: Isolate.immediate);
        _isolate = null;
        throw Exception(_initError ?? 'Isolate startup timed out (5s)');
      }
      await Future.delayed(const Duration(milliseconds: 10));
    }
    if (_initError != null) {
      rp.close();
      _isolate?.kill(priority: Isolate.immediate);
      _isolate = null;
      throw Exception(_initError);
    }
    _ready = true;
  }

  Future<List> _send(int cmd, List args) async {
    final id = ++_seq;
    final c = Completer<List>();
    _pending[id] = c;
    _port!.send([id, cmd, ...args]);
    return c.future.timeout(
      const Duration(seconds: 60),
      onTimeout: () => [id, 0, 'timeout'],
    );
  }

  // ---- public API ----

  Future<int> connect({
    required String host,
    int port = 22,
    required String username,
    String? password,
  }) async {
    await ensureReady();
    final res = await _send(2, [host, port, username, password ?? '']);
    if (res[1] == 0) throw ScpException.fromCode(-1100, detail: 'connect failed');
    return res[2] as int;
  }

  Future<void> disconnect(int session) async {
    await _send(3, [session]);
  }

  Future<List<Map<String, dynamic>>> listDir(int session, String path) async {
    final res = await _send(4, [session, path]);
    if (res[1] == 0) throw ScpException.fromCode(res[2] as int? ?? -1400);
    final raw = (res[2] as List);
    final result = <Map<String, dynamic>>[];
    for (final s in raw) {
      if (s is! String) throw ScpException.fromCode(-1400, detail: 'bad entry: ${s.runtimeType}');
      final p = s.split('|');
      result.add({
        'name': p[0], 'longName': p[1],
        'size': int.parse(p[2]), 'permissions': int.parse(p[3]),
        'uid': int.parse(p[4]), 'gid': int.parse(p[5]),
        'mtime': int.parse(p[6]), 'isDirectory': p[7] == 'true',
      });
    }
    return result;
  }

  /// Direct FFI call — bypasses Isolate (debug only, blocks UI).
  List<Map<String, dynamic>> listDirDirect(int session, String path) {
    final pathPtr = path.toNativeUtf8();
    final outFiles = calloc<Pointer<bind.FileInfo>>();
    final outCount = calloc<Uint32>();
    final code = bind.scpListDir(session, pathPtr.cast(), outFiles, outCount);
    calloc.free(pathPtr);
    if (code != 0) throw ScpException.fromCode(code);
    final count = outCount.value;
    final ptr = outFiles.value;
    final result = <Map<String, dynamic>>[];
    for (int i = 0; i < count; i++) {
      final info = ptr.elementAt(i).ref;
      result.add({
        'name': info.filename.toDartString(),
        'longName': info.longname.toDartString(),
        'size': info.filesize,
        'permissions': info.permissions,
        'uid': info.uid,
        'gid': info.gid,
        'mtime': info.mtime,
        'isDirectory': info.isDir != 0,
      });
    }
    bind.scpFreeFileList(ptr, count);
    calloc.free(outFiles); calloc.free(outCount);
    return result;
  }

  Future<int> startTransfer({
    required int session,
    required String local,
    required String remote,
    required bool isUpload,
  }) async {
    final res = await _send(5, [session, local, remote, isUpload]);
    if (res[1] == 0) throw ScpException.fromCode(-1500);
    return res[2] as int;
  }

  Future<void> controlTransfer(int transfer, String op) async {
    await _send(6, [transfer, op]);
  }

  Future<void> mkdir(int session, String path) async {
    await _send(7, [session, path]);
  }

  Future<void> rmdir(int session, String path) async {
    await _send(10, [session, path, 1]);
  }

  Future<void> unlink(int session, String path) async {
    await _send(10, [session, path, 2]);
  }

  Future<void> rename(int session, String oldPath, String newPath) async {
    await _send(10, [session, oldPath, 3, newPath]);
  }

  Future<void> chmod(int session, String path, int permissions) async {
    await _send(10, [session, path, 4, permissions]);
  }

  Future<void> touch(int session, String path) async {
    await _send(11, [session, path]);
  }

  /// Poll transfer progress. Returns [transferred, total] in bytes.
  Future<List<int>> getProgress(int transfer) async {
    final res = await _send(8, [transfer]);
    if (res[1] == 0) return [0, 0];
    return [res[2] as int, res[3] as int];
  }

  /// Wait for transfer to finish.
  Future<int> waitTransfer(int transfer) async {
    final res = await _send(9, [transfer]);
    return res[1] as int;
  }

  Future<void> shutdown() async {
    if (!_ready) return;
    await _send(1, []);
    _isolate?.kill(priority: Isolate.immediate);
    _ready = false;
  }
}
