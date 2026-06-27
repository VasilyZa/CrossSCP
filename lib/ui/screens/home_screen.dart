import 'dart:io';
import 'package:flutter/material.dart' show Icons;
import 'package:fluent_ui/fluent_ui.dart';
import 'package:crossscp/l10n/app_localizations.dart';
import 'package:crossscp/services/connection_service.dart';
import 'package:crossscp/services/transfer_service.dart';
import 'package:crossscp/models/scp_models.dart';
import 'package:crossscp/app_info.dart';
import '../widgets/file_browser.dart';
import '../widgets/transfer_queue.dart';

class HomeScreen extends StatefulWidget {
  final ConnectionService connectionService;
  final TransferService transferService;
  const HomeScreen({super.key, required this.connectionService, required this.transferService});
  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  int _connectedSessionHandle = -1;
  String _leftPath = Platform.environment['HOME'] ?? '/home';
  String _rightPath = '/';
  bool _showTransferPanel = true;
  bool _userClosedPanel = false;
  final _browserKey = GlobalKey<FileBrowserWidgetState>();

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final hasActive = widget.transferService.hasActive;
    if (hasActive) _userClosedPanel = false;
    final connected = _connectedSessionHandle >= 0;
    final sites = widget.connectionService.sites;

    return Column(children: [
      CommandBar(mainAxisAlignment: MainAxisAlignment.start, primaryItems: [
        CommandBarButton(icon: const Icon(Icons.add), label: Text(l10n.addSite),
            onPressed: () => _addSite(context)),
        if (sites.isNotEmpty) ...[
          CommandBarButton(icon: Icon(connected ? Icons.link_off : Icons.link),
              label: Text(connected ? l10n.disconnect : l10n.connect),
              onPressed: connected ? _disconnect : () => _pickSite(context)),
          CommandBarButton(icon: const Icon(Icons.settings), label: const Text('管理'),
              onPressed: () => _manageSites(context)),
        ],
        const CommandBarSeparator(),
        CommandBarButton(icon: const Icon(Icons.refresh), label: const Text('刷新'),
            onPressed: () => _browserKey.currentState?.reloadRemote()),
        CommandBarButton(icon: const Icon(Icons.info), label: Text('关于 v$appVersion'),
            onPressed: () => _showAbout(context)),
      ]),
      Expanded(child: FileBrowserWidget(
          connected: connected, leftPath: _leftPath, rightPath: _rightPath,
          onLeftPathChanged: (p) => setState(() => _leftPath = p),
          onRightPathChanged: (p) => setState(() => _rightPath = p),
          connectionService: widget.connectionService,
          transferService: widget.transferService,
          sessionHandle: _connectedSessionHandle)),
      if (!_userClosedPanel && (_showTransferPanel || hasActive || widget.transferService.queue.isNotEmpty))
        SizedBox(height: 180, child: TransferQueueWidget(
            transferService: widget.transferService,
            onClose: () => setState(() { _showTransferPanel = false; _userClosedPanel = true; }))),
    ]);
  }

  void _connect(SiteConfig site) async {
    var host = site.host.trim();
    if (host.startsWith('sftp://')) host = host.substring(7);
    else if (host.startsWith('ssh://')) host = host.substring(6);
    final fixed = SiteConfig(id: site.id, name: site.name, host: host, port: site.port,
        username: site.username, password: site.password, keyPath: site.keyPath, group: site.group);
    try {
      final h = await widget.connectionService.connect(fixed);
      setState(() { _connectedSessionHandle = h; _rightPath = '/'; });
    } catch (e) {
      if (mounted) displayInfoBar(context, builder: (_, close) =>
          InfoBar(title: Text('连接失败: $e'), severity: InfoBarSeverity.error));
    }
  }

  void _disconnect() async {
    if (_connectedSessionHandle >= 0) {
      await widget.connectionService.disconnect(_connectedSessionHandle);
      setState(() => _connectedSessionHandle = -1);
    }
  }

  void _addSite(BuildContext ctx) {
    final l10n = AppLocalizations.of(ctx);
    final nc = TextEditingController(), hc = TextEditingController();
    final uc = TextEditingController(), pc = TextEditingController();
    final portC = TextEditingController(text: '22');
    int port = 22;

    showDialog(context: ctx, builder: (c) => ContentDialog(
      title: Text(l10n.newSite),
      content: Column(mainAxisSize: MainAxisSize.min, children: [
        _textBox(l10n.siteName, nc, '云麦'),
        const SizedBox(height: 8),
        _textBox(l10n.siteHost, hc, 'nodeup1.yunmc.vip'),
        const SizedBox(height: 8),
        _textBox(l10n.sitePort, portC, '22', onChanged: (v) => port = int.tryParse(v) ?? 22),
        const SizedBox(height: 8),
        _textBox(l10n.siteUser, uc, ''),
        const SizedBox(height: 8),
        _textBox(l10n.sitePassword, pc, '', obscure: true),
      ]),
      actions: _dialogActions(c, l10n, onSave: () {
        if (hc.text.isEmpty || uc.text.isEmpty) return;
        widget.connectionService.addSite(SiteConfig(
            id: DateTime.now().microsecondsSinceEpoch.toString(),
            name: nc.text.isNotEmpty ? nc.text : hc.text,
            host: hc.text, port: port, username: uc.text,
            password: pc.text.isNotEmpty ? pc.text : null));
        Navigator.pop(c);
        _connect(SiteConfig(id: '', name: hc.text, host: hc.text, port: port,
            username: uc.text, password: pc.text.isNotEmpty ? pc.text : null));
      }),
    ));
  }

  void _pickSite(BuildContext ctx) {
    final l10n = AppLocalizations.of(ctx);
    final sites = widget.connectionService.sites;
    showDialog(context: ctx, builder: (c) => ContentDialog(
      title: Text(l10n.connect),
      content: SizedBox(width: 300, child: ListView.builder(shrinkWrap: true,
        itemCount: sites.length + 1,
        itemBuilder: (_, i) {
          if (i < sites.length) {
            final s = sites[i];
            return ListTile(leading: const Icon(Icons.dns, size: 20),
                title: Text(s.name), subtitle: Text('${s.username}@${s.host}:${s.port}'),
                onPressed: () { Navigator.pop(c); _connect(s); });
          }
          return ListTile(leading: const Icon(Icons.add, size: 20),
              title: const Text('快速连接...'),
              onPressed: () { Navigator.pop(c); _quickConnect(ctx); });
        },
      )),
      actions: [Button(child: Text(l10n.cancel), onPressed: () => Navigator.pop(c))],
    ));
  }

  void _quickConnect(BuildContext ctx) {
    final l10n = AppLocalizations.of(ctx);
    final hc = TextEditingController(), uc = TextEditingController(), pc = TextEditingController();
    final portC = TextEditingController(text: '22');
    int port = 22;
    showDialog(context: ctx, builder: (c) => ContentDialog(
      title: const Text('快速连接'),
      content: Column(mainAxisSize: MainAxisSize.min, children: [
        _textBox(l10n.siteHost, hc, 'nodeup1.yunmc.vip'),
        const SizedBox(height: 8),
        _textBox(l10n.sitePort, portC, '22', onChanged: (v) => port = int.tryParse(v) ?? 22),
        const SizedBox(height: 8),
        _textBox(l10n.siteUser, uc, ''),
        const SizedBox(height: 8),
        _textBox(l10n.sitePassword, pc, '', obscure: true),
      ]),
      actions: _dialogActions(c, l10n, onSave: () {
        Navigator.pop(c);
        _connect(SiteConfig(id: '', name: hc.text, host: hc.text, port: port,
            username: uc.text, password: pc.text.isNotEmpty ? pc.text : null));
      }, saveLabel: l10n.connect),
    ));
  }

  void _manageSites(BuildContext ctx) {
    final l10n = AppLocalizations.of(ctx);
    final cs = widget.connectionService;
    showDialog(context: ctx, builder: (c) {
      return StatefulBuilder(builder: (ctx2, setDialogState) {
        final sites = cs.sites;
        final list = ListView.builder(
          shrinkWrap: true,
          itemCount: sites.length,
          itemBuilder: (_, i) {
            final s = sites[i];
            return ListTile(
              leading: const Icon(Icons.dns, size: 20),
              title: Text(s.name),
              subtitle: Text('${s.username}@${s.host}:${s.port}'),
              trailing: Row(mainAxisSize: MainAxisSize.min, children: [
                IconButton(icon: const Icon(Icons.edit, size: 16),
                    onPressed: () { Navigator.pop(c); _editSite(context, s); }),
                IconButton(icon: const Icon(Icons.delete, size: 16),
                    onPressed: () { cs.removeSite(s.id); setDialogState(() {}); }),
              ]),
            );
          },
        );
        if (sites.isEmpty) {
          return ContentDialog(title: const Text('站点管理'),
              content: Center(child: Text(l10n.noSites)),
              actions: [Button(child: Text(l10n.cancel), onPressed: () => Navigator.pop(c))]);
        }
        return ContentDialog(title: const Text('站点管理'),
            content: SizedBox(width: 400, height: 300, child: list),
            actions: [Button(child: Text(l10n.cancel), onPressed: () => Navigator.pop(c))]);
      });
    });
  }

  void _editSite(BuildContext ctx, SiteConfig site) {
    final l10n = AppLocalizations.of(ctx);
    final nc = TextEditingController(text: site.name);
    final hc = TextEditingController(text: site.host);
    final uc = TextEditingController(text: site.username);
    final pc = TextEditingController(text: site.password ?? '');
    final portC = TextEditingController(text: '${site.port}');
    int port = site.port;

    showDialog(context: ctx, builder: (c) => ContentDialog(
      title: const Text('编辑站点'),
      content: Column(mainAxisSize: MainAxisSize.min, children: [
        _textBox(l10n.siteName, nc, ''),
        const SizedBox(height: 8),
        _textBox(l10n.siteHost, hc, ''),
        const SizedBox(height: 8),
        _textBox(l10n.sitePort, portC, '', onChanged: (v) => port = int.tryParse(v) ?? 22),
        const SizedBox(height: 8),
        _textBox(l10n.siteUser, uc, ''),
        const SizedBox(height: 8),
        _textBox(l10n.sitePassword, pc, '', obscure: true),
      ]),
      actions: _dialogActions(c, l10n, onSave: () {
        widget.connectionService.removeSite(site.id);
        widget.connectionService.addSite(SiteConfig(
            id: site.id, name: nc.text, host: hc.text, port: port,
            username: uc.text, password: pc.text.isNotEmpty ? pc.text : null));
        Navigator.pop(c);
      }),
    ));
  }

  List<Widget> _dialogActions(BuildContext dialogCtx, AppLocalizations l10n,
      {required VoidCallback onSave, String? saveLabel}) {
    return [
      Button(child: Text(l10n.cancel), onPressed: () => Navigator.pop(dialogCtx)),
      const SizedBox(width: 8),
      FilledButton(child: Text(saveLabel ?? l10n.save), onPressed: onSave),
    ];
  }

  Widget _textBox(String label, TextEditingController ctrl, String placeholder,
      {bool obscure = false, ValueChanged<String>? onChanged}) {
    return InfoLabel(label: label, child: TextBox(
        controller: ctrl, placeholder: placeholder,
        obscureText: obscure, onChanged: onChanged));
  }

  void _showAbout(BuildContext ctx) {
    showDialog(context: ctx, builder: (c) => ContentDialog(
      title: const Text(appName),
      content: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(appDescription, style: const TextStyle(fontSize: 13)),
        const SizedBox(height: 12),
        _aboutRow('版本', appVersion),
        _aboutRow('架构', 'C++ libssh2 + Flutter + dart:ffi'),
        _aboutRow('协议', 'SFTP (SSH File Transfer Protocol)'),
        _aboutRow('加密', 'OpenSSL / mbedTLS'),
        _aboutRow('平台', 'Windows · macOS · Linux · Android · iOS'),
        const SizedBox(height: 12),
        const Text('© 2026 CrossSCP', style: TextStyle(fontSize: 11, color: Color(0xFF888899))),
      ]),
      actions: [FilledButton(child: const Text('确定'), onPressed: () => Navigator.pop(c))],
    ));
  }

  Widget _aboutRow(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(children: [
        SizedBox(width: 60, child: Text(label, style: const TextStyle(fontSize: 12, color: Color(0xFF888899)))),
        Expanded(child: Text(value, style: const TextStyle(fontSize: 12))),
      ]),
    );
  }
}
