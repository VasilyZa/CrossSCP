/// High-level Dart client for SCP operations.
/// Wraps the native FFI layer behind a clean asynchronous API.



/// Represents a remote or local file entry.
class FileEntry {
  final String name;
  final String longName;
  final int size;
  final int permissions;
  final int uid;
  final int gid;
  final DateTime modTime;
  final bool isDirectory;

  const FileEntry({
    required this.name,
    required this.longName,
    required this.size,
    required this.permissions,
    required this.uid,
    required this.gid,
    required this.modTime,
    required this.isDirectory,
  });

  String get sizeFormatted {
    if (size < 1024) return '$size B';
    if (size < 1024 * 1024) return '${(size / 1024).toStringAsFixed(1)} KB';
    if (size < 1024 * 1024 * 1024) {
      return '${(size / (1024 * 1024)).toStringAsFixed(1)} MB';
    }
    return '${(size / (1024 * 1024 * 1024)).toStringAsFixed(1)} GB';
  }

  String get permissionsFormatted {
    final buf = StringBuffer();
    buf.write(isDirectory ? 'd' : '-');
    buf.write((permissions & 256) != 0 ? 'r' : '-');
    buf.write((permissions & 128) != 0 ? 'w' : '-');
    buf.write((permissions & 64) != 0 ? 'x' : '-');
    buf.write((permissions & 32) != 0 ? 'r' : '-');
    buf.write((permissions & 16) != 0 ? 'w' : '-');
    buf.write((permissions & 8) != 0 ? 'x' : '-');
    buf.write((permissions & 4) != 0 ? 'r' : '-');
    buf.write((permissions & 2) != 0 ? 'w' : '-');
    buf.write((permissions & 1) != 0 ? 'x' : '-');
    return buf.toString();
  }
}

/// Site configuration stored locally.
class SiteConfig {
  final String id;
  final String name;
  final String host;
  final int port;
  final String username;
  final String? password;
  final String? keyPath;
  final String group;

  const SiteConfig({
    required this.id,
    required this.name,
    required this.host,
    this.port = 22,
    required this.username,
    this.password,
    this.keyPath,
    this.group = 'Default',
  });

  Map<String, dynamic> toJson() => {
        'id': id,
        'name': name,
        'host': host,
        'port': port,
        'username': username,
        'password': password,
        'keyPath': keyPath,
        'group': group,
      };

  factory SiteConfig.fromJson(Map<String, dynamic> json) => SiteConfig(
        id: json['id'] as String,
        name: json['name'] as String,
        host: json['host'] as String,
        port: (json['port'] as int?) ?? 22,
        username: json['username'] as String,
        password: json['password'] as String?,
        keyPath: json['keyPath'] as String?,
        group: (json['group'] as String?) ?? 'Default',
      );
}

/// Transfer task representing a single upload or download.
class TransferTask {
  final String id;
  final String localPath;
  final String remotePath;
  final bool isUpload;
  final int sessionHandle;
  int? transferHandle; // assigned by native after start

  int bytesTransferred = 0;
  int bytesTotal = 0;
  String status = 'pending'; // pending, running, paused, completed, error, cancelled
  String? errorMessage;
  DateTime? startedAt;
  DateTime? completedAt;
  double speedBps = 0;

  TransferTask({
    required this.id,
    required this.localPath,
    required this.remotePath,
    required this.isUpload,
    required this.sessionHandle,
  });

  double get progress => bytesTotal > 0 ? bytesTransferred / bytesTotal : 0;

  String get speedFormatted {
    if (speedBps < 1024) return '${speedBps.toStringAsFixed(0)} B/s';
    if (speedBps < 1024 * 1024) {
      return '${(speedBps / 1024).toStringAsFixed(1)} KB/s';
    }
    return '${(speedBps / (1024 * 1024)).toStringAsFixed(1)} MB/s';
  }

  String get etaFormatted {
    if (speedBps <= 0 || bytesTotal <= 0) return '--';
    final remaining = bytesTotal - bytesTransferred;
    final etaSec = (remaining / speedBps).ceil();
    if (etaSec < 60) return '${etaSec}s';
    if (etaSec < 3600) {
      return '${etaSec ~/ 60}m ${etaSec % 60}s';
    }
    return '${etaSec ~/ 3600}h ${(etaSec % 3600) ~/ 60}m';
  }

  String get filename {
    final parts = isUpload ? localPath : remotePath;
    return parts.split('/').last;
  }
}
