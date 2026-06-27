/// CrossSCP - 跨平台 SFTP 客户端 (Fluent Design)
import 'package:flutter/material.dart' show Material, MaterialType;
import 'package:fluent_ui/fluent_ui.dart';
import 'ui/screens/home_screen.dart';
import 'services/connection_service.dart';
import 'services/transfer_service.dart';
import 'l10n/app_localizations.dart';
import 'app_info.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const CrossScpApp());
}

class CrossScpApp extends StatelessWidget {
  const CrossScpApp({super.key});

  @override
  Widget build(BuildContext context) {
    return FluentApp(
      title: appName,
      debugShowCheckedModeBanner: false,
      locale: const Locale('zh', 'CN'),
      supportedLocales: FluentLocalizations.supportedLocales,
      localizationsDelegates: [
        ...FluentLocalizations.localizationsDelegates,
        const AppLocalizationsDelegate(),
      ],
      builder: (context, child) {
        return Material(type: MaterialType.transparency, child: child!);
      },
      theme: FluentThemeData(
        brightness: Brightness.dark,
        accentColor: AccentColor.swatch(const <String, Color>{
          'darkest': Color(0xFF003D8F),
          'darker': Color(0xFF004FB0),
          'dark': Color(0xFF0063D1),
          'normal': Color(0xFF1677FF),
          'light': Color(0xFF4096FF),
          'lighter': Color(0xFF69B1FF),
          'lightest': Color(0xFF91CAFF),
        }),
        visualDensity: VisualDensity.compact,
        scaffoldBackgroundColor: const Color(0xFF1A1A2E),
        cardColor: const Color(0xFF252538),
        dividerTheme: DividerThemeData(decoration: BoxDecoration(color: Color(0xFF3A3A50))),
        inactiveColor: const Color(0xFF444460),
        typography: const Typography.raw(
          caption: TextStyle(fontSize: 12, color: Color(0xFFA0A0B0)),
          body: TextStyle(fontSize: 13, color: Color(0xFFD0D0E0)),
          bodyStrong: TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: Color(0xFFE0E0F0)),
          title: TextStyle(fontSize: 16, fontWeight: FontWeight.w600, color: Color(0xFFE8E8F8)),
        ),
      ),
      home: const AppShell(),
    );
  }
}

class AppShell extends StatefulWidget {
  const AppShell({super.key});
  @override
  State<AppShell> createState() => _AppShellState();
}

class _AppShellState extends State<AppShell> {
  final ConnectionService _connectionService = ConnectionService();
  final TransferService _transferService = TransferService();

  @override
  void initState() {
    super.initState();
    _connectionService.init();
  }

  @override
  void dispose() {
    _connectionService.dispose();
    _transferService.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: Listenable.merge([_connectionService, _transferService]),
      builder: (context, _) {
        return HomeScreen(
          connectionService: _connectionService,
          transferService: _transferService,
        );
      },
    );
  }
}
