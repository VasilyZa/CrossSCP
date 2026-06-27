/// Dart exception types mapped from native SCP error codes.
/// Provides structured error handling with actionable detail.

import 'scp_bindings.dart' as bindings;

/// Base class for all native SCP exceptions.
class ScpException implements Exception {
  final int code;
  final String message;
  final String? detail;

  const ScpException(this.code, this.message, {this.detail});

  factory ScpException.fromCode(int code, {String? detail}) {
    final msg = switch (code) {
      0 => 'Success',
      -1000 => 'Generic error',
      -1001 => 'Library not initialized',
      -1002 => 'Invalid argument',
      -1100 => 'Failed to connect to host',
      -1101 => 'Host not found (DNS resolution failed)',
      -1102 => 'Connection timed out',
      -1103 => 'Network I/O error',
      -1104 => 'Session disconnected',
      -1200 => 'Authentication failed',
      -1201 => 'Unsupported authentication method',
      -1202 => 'Access denied - invalid credentials',
      -1203 => 'Invalid or unreadable key file',
      -1204 => 'Wrong key passphrase',
      -1300 => 'SFTP protocol error',
      -1301 => 'Failed to initialize SFTP subsystem',
      -1400 => 'File operation error',
      -1401 => 'File or directory not found',
      -1402 => 'Permission denied',
      -1403 => 'File already exists',
      -1406 => 'Failed to open file',
      -1407 => 'Failed to write file',
      -1408 => 'Failed to read file',
      -1409 => 'Insufficient disk space',
      -1500 => 'Transfer error',
      -1501 => 'Transfer cancelled by user',
      -1502 => 'Transfer is paused',
      -1503 => 'Integrity check failed',
      -1600 => 'Memory allocation failed',
      -1602 => 'Invalid handle',
      _ => 'Unknown error ($code)',
    };

    return ScpException(code, msg, detail: detail);
  }

  /// Check if this error is recoverable (e.g., can retry).
  bool get isRecoverable =>
      code == -1102 || // timeout
      code == -1103 || // network
      code == -1104 || // disconnected
      code == -1502;   // paused

  /// Check if this is an authentication error.
  bool get isAuthError =>
      code >= -1206 && code <= -1200;

  @override
  String toString() {
    if (detail != null) {
      return 'ScpException($code): $message - $detail';
    }
    return 'ScpException($code): $message';
  }
}

/// Thrown when a native call returns an error code.
class ScpNativeException extends ScpException {
  final String functionName;

  ScpNativeException(super.code, super.message, this.functionName,
      {super.detail});

  @override
  String toString() {
    return 'ScpNativeException in $functionName($code): $message';
  }
}

/// Ensures a native call returned SCP_OK (0), otherwise throws.
void checkError(int code, String functionName) {
  if (code != 0) {
    throw ScpNativeException(code, '', functionName, detail: null);
  }
}
