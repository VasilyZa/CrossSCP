import 'package:flutter/material.dart';
import 'package:crossscp/l10n/app_localizations.dart';
import '../../services/transfer_service.dart';
import '../../models/scp_models.dart';

class TransferQueueWidget extends StatelessWidget {
  final TransferService transferService;
  final VoidCallback? onClose;

  const TransferQueueWidget({
    super.key,
    required this.transferService,
    this.onClose,
  });

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final queue = transferService.queue;
    final hasActive = transferService.hasActive;

    return Container(
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.surfaceContainerHighest,
        border: Border(top: BorderSide(color: Colors.grey.shade700)),
      ),
      child: Column(
        children: [
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
            child: Row(
              children: [
                const Icon(Icons.swap_vert, size: 16),
                const SizedBox(width: 8),
                Text('${l10n.transfers} (${queue.length})',
                    style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 13)),
                const Spacer(),
                if (hasActive) ...[
                  Text('${transferService.activeCount} ${l10n.active}',
                      style: TextStyle(fontSize: 11, color: Colors.green.shade400)),
                  const SizedBox(width: 8),
                ],
                IconButton(icon: const Icon(Icons.delete_sweep, size: 18),
                    tooltip: l10n.clearCompleted, onPressed: transferService.clearCompleted),
                if (onClose != null)
                  IconButton(icon: const Icon(Icons.close, size: 18), onPressed: onClose),
              ],
            ),
          ),
          Expanded(
            child: queue.isEmpty
                ? Center(child: Text(l10n.noTransfers,
                    style: TextStyle(color: Colors.grey.shade500, fontSize: 12)))
                : ListView.builder(
                    itemCount: queue.length,
                    itemBuilder: (context, index) {
                      final task = queue[index];
                      return _TransferTile(
                        task: task,
                        onPause: () => transferService.pause(task),
                        onResume: () => transferService.resume(task),
                        onCancel: () => transferService.cancel(task),
                        l10n: l10n,
                      );
                    },
                  ),
          ),
          if (queue.isNotEmpty)
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
              child: Row(children: [
                _StatusDot(hasActive: hasActive),
                const SizedBox(width: 8),
                Text(hasActive ? l10n.transferring : l10n.idle, style: const TextStyle(fontSize: 11)),
              ]),
            ),
        ],
      ),
    );
  }
}

class _TransferTile extends StatelessWidget {
  final TransferTask task;
  final VoidCallback onPause;
  final VoidCallback onResume;
  final VoidCallback onCancel;
  final AppLocalizations l10n;

  const _TransferTile({
    required this.task,
    required this.onPause,
    required this.onResume,
    required this.onCancel,
    required this.l10n,
  });

  @override
  Widget build(BuildContext context) {
    final isRunning = task.status == 'running';
    final isPaused = task.status == 'paused';
    final isComplete = task.status == 'completed';
    final isError = task.status == 'error' || task.status == 'cancelled';

    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
      child: Padding(
        padding: const EdgeInsets.all(8),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(children: [
              Icon(task.isUpload ? Icons.upload : Icons.download, size: 16,
                  color: task.isUpload ? Colors.orange.shade400 : Colors.green.shade400),
              const SizedBox(width: 6),
              Expanded(child: Text(task.filename, overflow: TextOverflow.ellipsis,
                  style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w500))),
              if (isRunning)
                IconButton(icon: const Icon(Icons.pause, size: 16), onPressed: onPause,
                    tooltip: l10n.pause, constraints: const BoxConstraints(minWidth: 28, minHeight: 28)),
              if (isPaused)
                IconButton(icon: const Icon(Icons.play_arrow, size: 16), onPressed: onResume,
                    tooltip: l10n.resume, constraints: const BoxConstraints(minWidth: 28, minHeight: 28)),
              if (isRunning || isPaused)
                IconButton(icon: const Icon(Icons.close, size: 16), onPressed: onCancel,
                    tooltip: l10n.cancel, constraints: const BoxConstraints(minWidth: 28, minHeight: 28)),
              if (isComplete) Icon(Icons.check_circle, size: 16, color: Colors.green.shade400),
              if (isError) Icon(Icons.error, size: 16, color: Colors.red.shade400),
            ]),
            if (isRunning || isPaused) ...[
              const SizedBox(height: 4),
              ClipRRect(
                borderRadius: BorderRadius.circular(4),
                child: LinearProgressIndicator(
                  value: task.progress, minHeight: 4,
                  backgroundColor: Colors.grey.shade800,
                  valueColor: AlwaysStoppedAnimation(
                      isPaused ? Colors.orange.shade400 : Colors.blue.shade400),
                ),
              ),
              const SizedBox(height: 2),
              Row(children: [
                Text('${(task.progress * 100).toStringAsFixed(1)}%', style: const TextStyle(fontSize: 10)),
                const SizedBox(width: 8),
                Text(task.speedFormatted, style: TextStyle(fontSize: 10, color: Colors.grey.shade400)),
                const Spacer(),
                Text('${l10n.eta}: ${task.etaFormatted}',
                    style: TextStyle(fontSize: 10, color: Colors.grey.shade400)),
              ]),
            ],
            if (isComplete) Text(l10n.completed,
                style: TextStyle(fontSize: 10, color: Colors.green.shade400)),
            if (isError && task.errorMessage != null)
              Text(task.errorMessage!, style: TextStyle(fontSize: 10, color: Colors.red.shade400),
                  maxLines: 1, overflow: TextOverflow.ellipsis),
          ],
        ),
      ),
    );
  }
}

class _StatusDot extends StatelessWidget {
  final bool hasActive;
  const _StatusDot({required this.hasActive});

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 8, height: 8,
      decoration: BoxDecoration(
        shape: BoxShape.circle,
        color: hasActive ? Colors.green.shade400 : Colors.grey.shade600,
      ),
    );
  }
}
