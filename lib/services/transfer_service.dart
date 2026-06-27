import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:crossscp/ffi/scp_client.dart';
import 'package:crossscp/models/scp_models.dart';

class TransferService extends ChangeNotifier {
  final _client = ScpClient();
  final List<TransferTask> _queue = [];
  final Set<String> _active = {};
  Timer? _pollTimer;

  List<TransferTask> get queue => List.unmodifiable(_queue);
  int get activeCount => _active.length;
  bool get hasActive => _active.isNotEmpty;

  Future<TransferTask> enqueue({
    required int session,
    required String local,
    required String remote,
    required bool isUpload,
  }) async {
    final task = TransferTask(
      id: DateTime.now().microsecondsSinceEpoch.toString(),
      localPath: local,
      remotePath: remote,
      isUpload: isUpload,
      sessionHandle: session,
    );
    _queue.add(task);
    notifyListeners();
    _start(task);
    _ensurePolling();
    return task;
  }

  Future<void> _start(TransferTask task) async {
    _active.add(task.id);
    task.status = 'running';
    task.startedAt = DateTime.now();
    notifyListeners();

    try {
      final h = await _client.startTransfer(
        session: task.sessionHandle,
        local: task.localPath,
        remote: task.remotePath,
        isUpload: task.isUpload,
      );
      task.transferHandle = h;
      // Poll immediately for 0-byte files
      await _pollOnce(task);
    } catch (e) {
      task.status = 'error';
      task.errorMessage = e.toString();
      _active.remove(task.id);
      notifyListeners();
    }
  }

  Future<void> _pollOnce(TransferTask task) async {
    try {
      final p = await _client.getProgress(task.transferHandle!);
      task.bytesTransferred = p[0];
      task.bytesTotal = p[1];
      // Only complete when total > 0 (native has stat'd the file) AND transferred >= total.
      if (p[1] > 0 && p[0] >= p[1]) {
        task.status = 'completed';
        task.completedAt = DateTime.now();
        _active.remove(task.id);
        await _cleanupTransfer(task);
        notifyListeners();
      }
    } catch (e) {
      debugPrint('pollOnce error: $e');
    }
  }

  void _ensurePolling() {
    _pollTimer ??= Timer.periodic(const Duration(milliseconds: 300), (_) => _poll());
  }

  Future<void> _poll() async {
    if (_active.isEmpty) {
      _pollTimer?.cancel();
      _pollTimer = null;
      return;
    }
    bool changed = false;
    for (final task in _queue.where((t) => t.status == 'running' && t.transferHandle != null)) {
      try {
        final p = await _client.getProgress(task.transferHandle!);
        final transferred = p[0], total = p[1];
        if (transferred != task.bytesTransferred || total != task.bytesTotal) {
          final dt = DateTime.now().difference(task.startedAt ?? DateTime.now()).inSeconds;
          task.bytesTransferred = transferred;
          task.bytesTotal = total;
          if (dt > 0) task.speedBps = (transferred / dt).roundToDouble();
          changed = true;
          if (total > 0 && transferred >= total) {
            task.status = 'completed';
            task.completedAt = DateTime.now();
            _active.remove(task.id);
            _cleanupTransfer(task);
          }
        }
      } catch (e) {
        debugPrint('poll error: $e');
        task.status = 'error';
        task.errorMessage = e.toString();
        _active.remove(task.id);
        changed = true;
      }
    }
    if (changed) notifyListeners();
    if (_active.isEmpty) {
      _pollTimer?.cancel();
      _pollTimer = null;
    }
  }

  Future<void> _cleanupTransfer(TransferTask task) async {
    if (task.transferHandle == null) return;
    try { await _client.waitTransfer(task.transferHandle!); } catch (_) {}
  }

  Future<void> pause(TransferTask task) async {
    if (task.transferHandle == null) return;
    await _client.controlTransfer(task.transferHandle!, 'pause');
    task.status = 'paused';
    notifyListeners();
  }

  Future<void> resume(TransferTask task) async {
    if (task.transferHandle == null) return;
    await _client.controlTransfer(task.transferHandle!, 'resume');
    task.status = 'running';
    _ensurePolling();
    notifyListeners();
  }

  Future<void> cancel(TransferTask task) async {
    if (task.transferHandle == null) return;
    await _client.controlTransfer(task.transferHandle!, 'cancel');
    task.status = 'cancelled';
    _active.remove(task.id);
    notifyListeners();
  }

  void clearCompleted() {
    _queue.removeWhere((t) =>
        t.status == 'completed' || t.status == 'cancelled' || t.status == 'error');
    notifyListeners();
  }
}
