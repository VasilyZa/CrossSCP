/// Dual-pane file browser: local (left) + remote (right).

import 'dart:io';
import 'package:flutter/material.dart';
import 'package:crossscp/l10n/app_localizations.dart';
import '../../services/connection_service.dart';
import '../../services/transfer_service.dart';
import '../../models/scp_models.dart';

class FileBrowserWidget extends StatefulWidget {
  final bool connected;
  final String leftPath;
  final String rightPath;
  final ValueChanged<String> onLeftPathChanged;
  final ValueChanged<String> onRightPathChanged;
  final ConnectionService connectionService;
  final TransferService transferService;
  final int sessionHandle;

  const FileBrowserWidget({
    super.key,
    required this.connected,
    required this.leftPath,
    required this.rightPath,
    required this.onLeftPathChanged,
    required this.onRightPathChanged,
    required this.connectionService,
    required this.transferService,
    required this.sessionHandle,
  });

  @override
  State<FileBrowserWidget> createState() => FileBrowserWidgetState();
}

class FileBrowserWidgetState extends State<FileBrowserWidget> {
  List<FileSystemEntity> _localEntries = [];
  List<FileEntry> _remoteEntries = [];
  final Set<String> _selectedLocal = {};
  final Set<String> _selectedRemote = {};
  bool _loadingLocal = false;
  bool _loadingRemote = false;
  String? _remoteError;

  String? _editingPath;
  final _pathCtrl = TextEditingController();
  final _pathFocus = FocusNode();

  @override
  void initState() {
    super.initState();
    _pathFocus.addListener(() {
      if (!_pathFocus.hasFocus && _editingPath != null) {
        setState(() => _editingPath = null);
      }
    });
    _loadLocalDir();
  }

  @override
  void dispose() {
    _pathCtrl.dispose();
    _pathFocus.dispose();
    super.dispose();
  }

  @override
  void didUpdateWidget(FileBrowserWidget old) {
    super.didUpdateWidget(old);
    if (widget.leftPath != old.leftPath) { _editingPath = null; _loadLocalDir(); }
    if (widget.rightPath != old.rightPath || widget.connected != old.connected) {
      _editingPath = null;
      if (widget.connected) _loadRemoteDir();
    }
    if (widget.connected && !old.connected) _loadRemoteDir();
  }

  void reloadRemote() {
    if (widget.connected && widget.sessionHandle >= 0) _loadRemoteDir();
  }

  static String _staticJoin(String base, String name) {
    if (base.endsWith('/')) return '$base$name';
    return '$base/$name';
  }

  Future<void> _loadLocalDir() async {
    setState(() { _loadingLocal = true; _selectedLocal.clear(); });
    try {
      final dir = Directory(widget.leftPath);
      if (!await dir.exists()) { setState(() => _loadingLocal = false); return; }
      _localEntries = await dir.list().toList();
      _localEntries.sort((a, b) {
        final aIsDir = a is Directory, bIsDir = b is Directory;
        if (aIsDir && !bIsDir) return -1;
        if (!aIsDir && bIsDir) return 1;
        return a.path.compareTo(b.path);
      });
    } catch (e) {
      debugPrint('Error listing local dir: $e');
    }
    setState(() => _loadingLocal = false);
  }

  Future<void> _loadRemoteDir() async {
    if (!widget.connected || widget.sessionHandle < 0) return;
    setState(() { _loadingRemote = true; _selectedRemote.clear(); _remoteError = null; });
    try {
      _remoteEntries = await widget.connectionService.listDir(
          widget.sessionHandle, widget.rightPath);
    } catch (e) {
      _remoteEntries = [];
      _remoteError = e.toString();
    }
    setState(() => _loadingRemote = false);
  }

  void _navigateLocal(String path) => widget.onLeftPathChanged(path);
  void _navigateRemote(String path) => widget.onRightPathChanged(path);

  void _startTransfer(String localPath, String remoteName, bool isUpload) {
    final remotePath = _join(widget.rightPath, remoteName);
    widget.transferService.enqueue(
      session: widget.sessionHandle,
      local: localPath, remote: remotePath, isUpload: isUpload,
    );
  }

  String _join(String base, String name) {
    if (base.endsWith('/')) return '$base$name';
    return '$base/$name';
  }

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    return Row(
      children: [
        Expanded(
          child: _buildPane(
            title: l10n.localFile, path: widget.leftPath,
            isLoading: _loadingLocal,
            entries: _localEntries.map((e) {
              final name = e.path.split('/').last;
              return _PaneEntry(name: name, isDir: e is Directory,
                  size: e is File ? e.lengthSync() : 0);
            }).toList(),
            isLocal: true,
            onNavigate: (dir) => _navigateLocal(_join(widget.leftPath, dir)),
            onNavigateUp: () {
              final p = widget.leftPath;
              if (p == '/' || p.isEmpty) return;
              final idx = p.lastIndexOf('/');
              _navigateLocal(idx == 0 ? '/' : p.substring(0, idx));
            },
            onPathSubmit: (p) => _navigateLocal(p),
            onTransfer: (name) => _startTransfer(_join(widget.leftPath, name), name, true),
            l10n: l10n,
            sessionHandle: widget.sessionHandle,
            connectionService: widget.connectionService,
            onFileOp: () => _loadLocalDir(),
          ),
        ),
        const VerticalDivider(width: 2),
        Expanded(
          child: _buildPane(
            title: l10n.remoteFile, path: widget.rightPath,
            isLoading: _loadingRemote,
            entries: widget.connected
                ? _remoteEntries.map((e) => _PaneEntry(
                    name: e.name, isDir: e.isDirectory, size: e.size,
                    permissions: e.permissions)).toList()
                : [],
            isLocal: false,
            onNavigate: (dir) => _navigateRemote(_join(widget.rightPath, dir)),
            onNavigateUp: () {
              final p = widget.rightPath;
              if (p == '/' || p.isEmpty) return;
              final idx = p.lastIndexOf('/');
              _navigateRemote(idx == 0 ? '/' : p.substring(0, idx));
            },
            onPathSubmit: (p) => _navigateRemote(p),
            onTransfer: (name) => _startTransfer(_join(widget.leftPath, name), name, false),
            showPlaceholder: !widget.connected,
            l10n: l10n,
            errorMsg: _remoteError,
            sessionHandle: widget.sessionHandle,
            connectionService: widget.connectionService,
            onFileOp: () => _loadRemoteDir(),
          ),
        ),
      ],
    );
  }

  Widget _buildPane({
    required String title,
    required String path,
    required bool isLoading,
    required List<_PaneEntry> entries,
    required bool isLocal,
    required void Function(String) onNavigate,
    required VoidCallback onNavigateUp,
    required void Function(String) onTransfer,
    required void Function(String) onPathSubmit,
    required AppLocalizations l10n,
    required int sessionHandle,
    required ConnectionService connectionService,
    required VoidCallback onFileOp,
    bool showPlaceholder = false,
    String? errorMsg,
  }) {
    final isEditing = _editingPath == path;
    return Column(
      children: [
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
          color: Theme.of(context).colorScheme.surfaceContainerHighest,
          child: Row(
            children: [
              IconButton(icon: const Icon(Icons.arrow_upward, size: 18),
                  tooltip: l10n.parentDir, onPressed: onNavigateUp),
              Expanded(
                child: isEditing
                    ? SizedBox(
                        height: 28,
                        child: TextField(
                          controller: _pathCtrl..text = path,
                          focusNode: _pathFocus,
                          style: const TextStyle(fontSize: 12, fontFamily: 'monospace'),
                          decoration: const InputDecoration(
                            isDense: true,
                            contentPadding: EdgeInsets.symmetric(horizontal: 6, vertical: 4),
                            border: OutlineInputBorder(),
                          ),
                          textInputAction: TextInputAction.go,
                          onSubmitted: (val) {
                            final trimmed = val.trim();
                            setState(() => _editingPath = null);
                            if (trimmed.isNotEmpty && trimmed != path) {
                              onPathSubmit(trimmed);
                            }
                          },
                        ),
                      )
                    : GestureDetector(
                        onTap: () {
                          setState(() {
                            _editingPath = path;
                            _pathCtrl.text = path;
                          });
                          WidgetsBinding.instance.addPostFrameCallback((_) {
                            _pathFocus.requestFocus();
                          });
                        },
                        child: Text(path,
                            style: const TextStyle(fontSize: 12, fontFamily: 'monospace'),
                            overflow: TextOverflow.ellipsis),
                      ),
              ),
              if (isLoading)
                const SizedBox(width: 14, height: 14,
                    child: CircularProgressIndicator(strokeWidth: 2)),
              IconButton(
                icon: const Icon(Icons.refresh, size: 18),
                tooltip: '刷新',
                onPressed: onFileOp,
              ),
              if (!isLocal && sessionHandle >= 0) ...[
                IconButton(
                  icon: const Icon(Icons.note_add, size: 18),
                  tooltip: '新建文件',
                  onPressed: () => _showNewFileDialog(context, path, sessionHandle,
                      connectionService, l10n, onFileOp),
                ),
                IconButton(
                  icon: const Icon(Icons.create_new_folder, size: 18),
                  tooltip: '新建文件夹',
                  onPressed: () => _showNewFolderDialog(context, path, sessionHandle,
                      connectionService, l10n, onFileOp),
                ),
              ],
            ],
          ),
        ),
        Expanded(
          child: showPlaceholder
              ? Center(child: Column(mainAxisSize: MainAxisSize.min, children: [
                  Icon(Icons.cloud_off, size: 48, color: Colors.grey.shade600),
                  const SizedBox(height: 8),
                  Text(l10n.notConnected, style: TextStyle(color: Colors.grey.shade500)),
                ]))
              : errorMsg != null
              ? Center(child: Text(errorMsg!, style: TextStyle(color: Colors.red.shade400, fontSize: 12)))
              : ListView.builder(
                  itemCount: entries.length,
                  itemBuilder: (context, index) {
                    final entry = entries[index];
                    return _FileTile(
                      entry: entry,
                      isLocal: isLocal,
                      path: path,
                      sessionHandle: sessionHandle,
                      l10n: l10n,
                      connectionService: connectionService,
                      onNavigate: onNavigate,
                      onTransfer: onTransfer,
                      onFileOp: onFileOp,
                    );
                  },
                ),
        ),
      ],
    );
  }

  String _formatSize(int bytes) {
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
    return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
  }

  void _showNewFolderDialog(BuildContext ctx, String parentPath, int session,
      ConnectionService cs, AppLocalizations l10n, VoidCallback onDone) {
    final ctrl = TextEditingController();
    showDialog(
      context: ctx,
      builder: (ctx) => AlertDialog(
        title: const Text('新建文件夹'),
        content: TextField(controller: ctrl, autofocus: true,
            decoration: const InputDecoration(hintText: '文件夹名称')),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: Text(l10n.cancel)),
          FilledButton(onPressed: () async {
            if (ctrl.text.isEmpty) return;
            try {
              await cs.mkdir(session, _join(parentPath, ctrl.text));
              onDone();
            } catch (e) {
              if (ctx.mounted) ScaffoldMessenger.of(ctx).showSnackBar(
                  SnackBar(content: Text('创建失败: $e')));
            }
            Navigator.pop(ctx);
          }, child: Text(l10n.save)),
        ],
      ),
    );
  }

  void _showNewFileDialog(BuildContext ctx, String parentPath, int session,
      ConnectionService cs, AppLocalizations l10n, VoidCallback onDone) {
    final ctrl = TextEditingController();
    showDialog(
      context: ctx,
      builder: (ctx) => AlertDialog(
        title: const Text('新建文件'),
        content: TextField(controller: ctrl, autofocus: true,
            decoration: const InputDecoration(hintText: '文件名称')),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: Text(l10n.cancel)),
          FilledButton(onPressed: () async {
            if (ctrl.text.isEmpty) return;
            try {
              await cs.touch(session, _join(parentPath, ctrl.text));
              onDone();
            } catch (e) {
              if (ctx.mounted) ScaffoldMessenger.of(ctx).showSnackBar(
                  SnackBar(content: Text('创建失败: $e')));
            }
            Navigator.pop(ctx);
          }, child: Text(l10n.save)),
        ],
      ),
    );
  }
}

class _FileTile extends StatelessWidget {
  final _PaneEntry entry;
  final bool isLocal;
  final String path;
  final int sessionHandle;
  final AppLocalizations l10n;
  final ConnectionService connectionService;
  final void Function(String) onNavigate;
  final void Function(String) onTransfer;
  final VoidCallback onFileOp;

  const _FileTile({
    required this.entry,
    required this.isLocal,
    required this.path,
    required this.sessionHandle,
    required this.l10n,
    required this.connectionService,
    required this.onNavigate,
    required this.onTransfer,
    required this.onFileOp,
  });

  @override
  Widget build(BuildContext ctx) {
    final fullPath = FileBrowserWidgetState._staticJoin(path, entry.name);
    final tile = ListTile(
      dense: true,
      leading: Icon(entry.isDir ? Icons.folder : Icons.insert_drive_file,
          size: 20, color: entry.isDir ? Colors.amber.shade600 : Colors.blue.shade400),
      title: Text(entry.name, overflow: TextOverflow.ellipsis,
          style: const TextStyle(fontSize: 13)),
      subtitle: Text(entry.isDir ? '<目录>' : _formatSize(entry.size),
          style: TextStyle(fontSize: 11, color: Colors.grey.shade500)),
      trailing: entry.isDir ? null : IconButton(
          icon: Icon(isLocal ? Icons.upload : Icons.download, size: 16),
          onPressed: () => onTransfer(entry.name),
          tooltip: isLocal ? l10n.upload : l10n.download),
      onTap: entry.isDir ? () => onNavigate(entry.name) : () => onTransfer(entry.name),
    );

    if (isLocal) {
      // Local pane - limited context menu
      return GestureDetector(
        onSecondaryTapDown: (d) => _showLocalContextMenu(ctx, d.globalPosition, fullPath),
        child: tile,
      );
    }

    return GestureDetector(
      onSecondaryTapDown: (d) => _showRemoteContextMenu(ctx, d.globalPosition, fullPath),
      child: tile,
    );
  }

  void _showLocalContextMenu(BuildContext ctx, Offset pos, String fullPath) {
    final isDir = entry.isDir;
    showMenu(
      context: ctx, position: RelativeRect.fromLTRB(pos.dx, pos.dy, pos.dx, pos.dy),
      items: [
        if (!isDir)
          PopupMenuItem(value: 'upload', child: Row(children: [
            const Icon(Icons.upload, size: 18), const SizedBox(width: 8), Text(l10n.upload),
          ])),
        if (entry.name != '.' && entry.name != '..')
          PopupMenuItem(value: 'delete', child: Row(children: [
            const Icon(Icons.delete, size: 18), const SizedBox(width: 8), Text(l10n.delete),
          ])),
      ],
    ).then((value) async {
      if (value == 'upload') onTransfer(entry.name);
      if (value == 'delete') {
        final confirm = await _confirmDelete(ctx, entry.name);
        if (confirm == true) {
          try {
            if (isDir) await Directory(fullPath).delete(recursive: true);
            else await File(fullPath).delete();
            onFileOp();
          } catch (e) {
            if (ctx.mounted) _showSnack(ctx, '${l10n.delete} ${l10n.failed}: $e');
          }
        }
      }
    });
  }

  void _showRemoteContextMenu(BuildContext ctx, Offset pos, String fullPath) {
    showMenu(
      context: ctx, position: RelativeRect.fromLTRB(pos.dx, pos.dy, pos.dx, pos.dy),
      items: [
        if (!entry.isDir)
          PopupMenuItem(value: 'download', child: Row(children: [
            const Icon(Icons.download, size: 18), const SizedBox(width: 8), Text(l10n.download),
          ])),
        PopupMenuItem(value: 'delete', child: Row(children: [
          const Icon(Icons.delete, size: 18), const SizedBox(width: 8), Text(l10n.delete),
        ])),
        PopupMenuItem(value: 'rename', child: Row(children: [
          const Icon(Icons.edit, size: 18), const SizedBox(width: 8), const Text('重命名'),
        ])),
        if (!entry.isDir)
          PopupMenuItem(value: 'chmod', child: Row(children: [
            const Icon(Icons.lock, size: 18), const SizedBox(width: 8), const Text('权限'),
          ])),
        if (entry.isDir)
          PopupMenuItem(value: 'chmod', child: Row(children: [
            const Icon(Icons.lock, size: 18), const SizedBox(width: 8), const Text('权限'),
          ])),
        PopupMenuItem(value: 'properties', child: Row(children: [
          const Icon(Icons.info, size: 18), const SizedBox(width: 8), const Text('属性'),
        ])),
      ],
    ).then((value) async {
      if (value == 'download') onTransfer(entry.name);
      if (value == 'delete') {
        final confirm = await _confirmDelete(ctx, entry.name);
        if (confirm == true) {
          try {
            if (entry.isDir) await connectionService.deleteDir(sessionHandle, fullPath);
            else await connectionService.deleteFile(sessionHandle, fullPath);
            onFileOp();
          } catch (e) {
            if (ctx.mounted) _showSnack(ctx, '${l10n.delete} ${l10n.failed}: $e');
          }
        }
      }
      if (value == 'rename') await _showRenameDialog(ctx, fullPath);
      if (value == 'chmod') await _showChmodDialog(ctx, fullPath);
      if (value == 'properties') await _showPropertiesDialog(ctx, fullPath);
    });
  }

  Future<bool?> _confirmDelete(BuildContext ctx, String name) {
    return showDialog<bool>(
      context: ctx,
      builder: (c) => AlertDialog(
        title: Text('${l10n.delete} "$name"?'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(c, false), child: Text(l10n.cancel)),
          FilledButton(onPressed: () => Navigator.pop(c, true), child: Text(l10n.delete)),
        ],
      ),
    );
  }

  Future<void> _showRenameDialog(BuildContext ctx, String fullPath) async {
    final ctrl = TextEditingController(text: entry.name);
    final newName = await showDialog<String>(
      context: ctx,
      builder: (c) => AlertDialog(
        title: const Text('重命名'),
        content: TextField(controller: ctrl, autofocus: true),
        actions: [
          TextButton(onPressed: () => Navigator.pop(c), child: Text(l10n.cancel)),
          FilledButton(onPressed: () => Navigator.pop(c, ctrl.text), child: Text(l10n.save)),
        ],
      ),
    );
    if (newName != null && newName.isNotEmpty && newName != entry.name) {
      try {
        await connectionService.renameFile(sessionHandle, fullPath,
            FileBrowserWidgetState._staticJoin(path, newName));
        onFileOp();
      } catch (e) {
        if (ctx.mounted) _showSnack(ctx, '重命名 ${l10n.failed}: $e');
      }
    }
  }

  Future<void> _showChmodDialog(BuildContext ctx, String fullPath) async {
    final currentOctal = entry.permissions.toRadixString(8).padLeft(4, '0');
    final ctrl = TextEditingController(text: currentOctal.substring(currentOctal.length - 3));
    final result = await showDialog<String>(
      context: ctx,
      builder: (c) => AlertDialog(
        title: Text('权限 - ${entry.name}'),
        content: Column(mainAxisSize: MainAxisSize.min, children: [
          Text(currentOctal, style: const TextStyle(fontFamily: 'monospace', fontSize: 18)),
          const SizedBox(height: 8),
          TextField(controller: ctrl,
              decoration: const InputDecoration(labelText: '新权限 (八进制)', hintText: '755'),
              keyboardType: TextInputType.number),
        ]),
        actions: [
          TextButton(onPressed: () => Navigator.pop(c), child: Text(l10n.cancel)),
          FilledButton(onPressed: () => Navigator.pop(c, ctrl.text), child: Text(l10n.save)),
        ],
      ),
    );
    if (result != null) {
      final octalVal = int.tryParse(result, radix: 8);
      if (octalVal != null) {
        try {
          await connectionService.chmod(sessionHandle, fullPath, octalVal);
          onFileOp();
        } catch (e) {
          if (ctx.mounted) _showSnack(ctx, 'chmod ${l10n.failed}: $e');
        }
      }
    }
  }

  Future<void> _showPropertiesDialog(BuildContext ctx, String fullPath) async {
    // Show file info (name, size, permissions)
    showDialog(
      context: ctx,
      builder: (c) => AlertDialog(
        title: Text(entry.name),
        content: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
          _propRow('路径', fullPath),
          _propRow('大小', _formatSize(entry.size)),
          _propRow('类型', entry.isDir ? '目录' : '文件'),
        ]),
        actions: [
          FilledButton(onPressed: () => Navigator.pop(c), child: Text(l10n.save)),
        ],
      ),
    );
  }

  Widget _propRow(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(children: [
        SizedBox(width: 60, child: Text(label, style: TextStyle(color: Colors.grey.shade500, fontSize: 12))),
        Expanded(child: Text(value, style: const TextStyle(fontSize: 12))),
      ]),
    );
  }

  void _showSnack(BuildContext ctx, String msg) {
    ScaffoldMessenger.of(ctx).showSnackBar(SnackBar(content: Text(msg)));
  }

  static String _formatSize(int bytes) {
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
    return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
  }
}

class _PaneEntry {
  final String name;
  final bool isDir;
  final int size;
  final int permissions;
  const _PaneEntry({required this.name, required this.isDir, this.size = 0, this.permissions = 0});
}
