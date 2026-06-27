import 'dart:convert';
import 'dart:io';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';
import 'package:crossscp/ffi/scp_client.dart';
import 'package:crossscp/ffi/scp_error.dart';
import 'package:crossscp/models/scp_models.dart';

class ConnectionService extends ChangeNotifier {
  final _client = ScpClient();

  final Map<int, SiteConfig> _sessions = {};
  List<SiteConfig> _sites = [];

  bool _ready = false;
  bool get isReady => _ready;

  List<SiteConfig> get sites => List.unmodifiable(_sites);
  Map<int, SiteConfig> get sessions => Map.unmodifiable(_sessions);

  Future<void> init() async {
    if (_ready) return;
    await _client.ensureReady();
    await _loadSites();
    _ready = true;
    notifyListeners();
  }

  Future<int> connect(SiteConfig config) async {
    await init();
    // Strip protocol prefix if user pasted a full URL
    var host = config.host.trim();
    if (host.startsWith('sftp://')) host = host.substring(7);
    else if (host.startsWith('ssh://')) host = host.substring(6);
    else if (host.startsWith('http://') || host.startsWith('https://'))
      host = host.substring(host.indexOf('://') + 3);
    final handle = await _client.connect(
      host: host,
      port: config.port,
      username: config.username,
      password: config.password,
    );
    _sessions[handle] = config;
    notifyListeners();
    return handle;
  }

  Future<void> disconnect(int handle) async {
    await _client.disconnect(handle);
    _sessions.remove(handle);
    notifyListeners();
  }

  Future<void> mkdir(int session, String path) async {
    await _client.mkdir(session, path);
  }

  Future<void> deleteFile(int session, String path) async {
    await _client.unlink(session, path);
  }

  Future<void> deleteDir(int session, String path) async {
    await _client.rmdir(session, path);
  }

  Future<void> renameFile(int session, String oldPath, String newPath) async {
    await _client.rename(session, oldPath, newPath);
  }

  Future<void> chmod(int session, String path, int permissions) async {
    await _client.chmod(session, path, permissions);
  }

  Future<void> touch(int session, String path) async {
    await _client.touch(session, path);
  }

  Future<List<FileEntry>> listDir(int session, String path) async {
    final raw = await _client.listDir(session, path);
    return raw.map((m) => FileEntry(
      name: m['name'] as String,
      longName: m['longName'] as String? ?? (m['name'] as String),
      size: (m['size'] as int?) ?? 0,
      permissions: (m['permissions'] as int?) ?? 0,
      uid: (m['uid'] as int?) ?? 0,
      gid: (m['gid'] as int?) ?? 0,
      modTime: DateTime.fromMillisecondsSinceEpoch(
          ((m['mtime'] as int?) ?? 0) * 1000),
      isDirectory: (m['isDirectory'] as bool?) ?? false,
    )).toList();
  }

  Future<void> saveSites() async {
    final dir = await getApplicationDocumentsDirectory();
    final f = File('${dir.path}/crossscp_sites.json');
    await f.writeAsString(jsonEncode(_sites.map((s) => s.toJson()).toList()));
  }

  Future<void> _loadSites() async {
    try {
      final dir = await getApplicationDocumentsDirectory();
      final f = File('${dir.path}/crossscp_sites.json');
      if (await f.exists()) {
        final list = jsonDecode(await f.readAsString()) as List;
        _sites = list
            .map((j) => SiteConfig.fromJson(j as Map<String, dynamic>))
            .toList();
      }
    } catch (e) {
      debugPrint('load sites: $e');
    }
  }

  Future<void> addSite(SiteConfig config) async {
    _sites.add(config);
    await saveSites();
    notifyListeners();
  }

  Future<void> removeSite(String id) async {
    _sites.removeWhere((s) => s.id == id);
    await saveSites();
    notifyListeners();
  }

  @override
  void dispose() {
    _client.shutdown();
    super.dispose();
  }
}
