import 'package:flutter/material.dart';

class AppLocalizations {
  final Locale locale;
  AppLocalizations(this.locale);

  static AppLocalizations of(BuildContext ctx) =>
      Localizations.of<AppLocalizations>(ctx, AppLocalizations)!;

  static const _zh = {
    'appTitle': 'CrossSCP',
    'tabSites': '站点',
    'tabFiles': '文件',
    'addSite': '添加站点',
    'newSite': '新建站点',
    'siteName': '名称',
    'siteHost': '主机地址',
    'sitePort': '端口',
    'siteUser': '用户名',
    'sitePassword': '密码',
    'siteKeyPath': '私钥',
    'save': '保存',
    'cancel': '取消',
    'delete': '删除',
    'connect': '连接',
    'disconnect': '断开',
    'connected': '已连接',
    'notConnected': '未连接',
    'noSites': '暂无站点配置',
    'localFile': '本地',
    'remoteFile': '远程',
    'parentDir': '上级目录',
    'dir': '目录',
    'upload': '上传',
    'download': '下载',
    'transfers': '传输队列',
    'noTransfers': '无传输任务',
    'active': '活跃',
    'pause': '暂停',
    'resume': '继续',
    'clearCompleted': '清除已完成',
    'transferring': '传输中...',
    'idle': '空闲',
    'completed': '已完成',
    'failed': '失败',
    'connectFailed': '连接失败',
    'listFailed': '列出目录失败',
    'transferFailed': '传输失败',
    'unknownHost': '无法解析主机名',
    'authFailed': '认证失败',
    'permissionDenied': '权限不足',
    'notFound': '文件或目录不存在',
    'diskFull': '磁盘空间不足',
    'networkError': '网络错误',
    'timeout': '操作超时',
    'eta': '剩余时间',
    'cancelled': '已取消',
  };

  static const _localizedValues = {'zh': _zh};
  static const supportedLocales = [Locale('zh', 'CN')];

  String get appTitle => _zh['appTitle']!;
  String get tabSites => _zh['tabSites']!;
  String get tabFiles => _zh['tabFiles']!;
  String get addSite => _zh['addSite']!;
  String get newSite => _zh['newSite']!;
  String get siteName => _zh['siteName']!;
  String get siteHost => _zh['siteHost']!;
  String get sitePort => _zh['sitePort']!;
  String get siteUser => _zh['siteUser']!;
  String get sitePassword => _zh['sitePassword']!;
  String get save => _zh['save']!;
  String get cancel => _zh['cancel']!;
  String get delete => _zh['delete']!;
  String get connect => _zh['connect']!;
  String get disconnect => _zh['disconnect']!;
  String get connected => _zh['connected']!;
  String get notConnected => _zh['notConnected']!;
  String get noSites => _zh['noSites']!;
  String get localFile => _zh['localFile']!;
  String get remoteFile => _zh['remoteFile']!;
  String get parentDir => _zh['parentDir']!;
  String get dir => _zh['dir']!;
  String get upload => _zh['upload']!;
  String get download => _zh['download']!;
  String get transfers => _zh['transfers']!;
  String get noTransfers => _zh['noTransfers']!;
  String get active => _zh['active']!;
  String get pause => _zh['pause']!;
  String get resume => _zh['resume']!;
  String get clearCompleted => _zh['clearCompleted']!;
  String get transferring => _zh['transferring']!;
  String get idle => _zh['idle']!;
  String get completed => _zh['completed']!;
  String get failed => _zh['failed']!;
  String get connectFailed => _zh['connectFailed']!;
  String get unknownHost => _zh['unknownHost']!;
  String get authFailed => _zh['authFailed']!;
  String get notFound => _zh['notFound']!;
  String get diskFull => _zh['diskFull']!;
  String get eta => _zh['eta']!;
  String get cancelled => _zh['cancelled']!;

  String translateError(int code) {
    return switch (code) {
      -1100 => connectFailed,
      -1101 => unknownHost,
      -1102 => _zh['timeout']!,
      -1103 => _zh['networkError']!,
      -1200 => authFailed,
      -1401 => notFound,
      -1402 => _zh['permissionDenied']!,
      -1409 => diskFull,
      -1501 => cancelled,
      _ => '错误 ($code)',
    };
  }
}

class AppLocalizationsDelegate extends LocalizationsDelegate<AppLocalizations> {
  const AppLocalizationsDelegate();
  @override bool isSupported(Locale locale) => locale.languageCode == 'zh';
  @override Future<AppLocalizations> load(Locale locale) async => AppLocalizations(locale);
  @override bool shouldReload(covariant AppLocalizationsDelegate old) => false;
}
