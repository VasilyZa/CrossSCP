import 'package:flutter/material.dart';
import 'package:uuid/uuid.dart';
import 'package:crossscp/l10n/app_localizations.dart';
import '../../services/connection_service.dart';
import '../../models/scp_models.dart';

class SiteListWidget extends StatelessWidget {
  final ConnectionService service;
  final void Function(SiteConfig) onSiteDoubleTap;

  const SiteListWidget({super.key, required this.service, required this.onSiteDoubleTap});

  void _showAddDialog(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final hostCtrl = TextEditingController();
    final userCtrl = TextEditingController();
    final passCtrl = TextEditingController();
    final nameCtrl = TextEditingController();
    final portCtrl = TextEditingController(text: '22');
    int port = 22;

    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(l10n.newSite),
        content: SingleChildScrollView(
          child: Column(mainAxisSize: MainAxisSize.min, children: [
            TextField(controller: nameCtrl, decoration: InputDecoration(labelText: l10n.siteName, hintText: '云麦')),
            const SizedBox(height: 8),
            TextField(controller: hostCtrl, decoration: InputDecoration(labelText: l10n.siteHost, hintText: 'nodeup1.yunmc.vip')),
            const SizedBox(height: 8),
            TextField(controller: portCtrl, decoration: InputDecoration(labelText: l10n.sitePort),
                keyboardType: TextInputType.number, onChanged: (v) => port = int.tryParse(v) ?? 22),
            const SizedBox(height: 8),
            TextField(controller: userCtrl, decoration: InputDecoration(labelText: l10n.siteUser)),
            const SizedBox(height: 8),
            TextField(controller: passCtrl, decoration: InputDecoration(labelText: l10n.sitePassword), obscureText: true),
          ]),
        ),
        actions: [
          OutlinedButton(onPressed: () => Navigator.pop(ctx), child: Text(l10n.cancel)),
          const SizedBox(width: 8),
          FilledButton(onPressed: () {
            if (hostCtrl.text.isEmpty || userCtrl.text.isEmpty) return;
            service.addSite(SiteConfig(
              id: const Uuid().v4(),
              name: nameCtrl.text.isNotEmpty ? nameCtrl.text : hostCtrl.text,
              host: hostCtrl.text, port: port, username: userCtrl.text,
              password: passCtrl.text.isNotEmpty ? passCtrl.text : null,
            ));
            Navigator.pop(ctx);
          }, child: Text(l10n.save)),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final sites = service.sites;
    final groups = <String, List<SiteConfig>>{};
    for (final s in sites) { groups.putIfAbsent(s.group, () => []).add(s); }

    if (sites.isEmpty) {
      return Center(child: Column(mainAxisSize: MainAxisSize.min, children: [
        Icon(Icons.dns_outlined, size: 64, color: Colors.grey.shade600),
        const SizedBox(height: 16),
        Text(l10n.noSites, style: TextStyle(color: Colors.grey.shade500, fontSize: 16)),
        const SizedBox(height: 16),
        FilledButton.icon(onPressed: () => _showAddDialog(context),
            icon: const Icon(Icons.add), label: Text(l10n.addSite)),
      ]));
    }

    return Column(children: [
      Padding(
        padding: const EdgeInsets.all(8.0),
        child: Row(children: [
          const Spacer(),
          FilledButton.icon(onPressed: () => _showAddDialog(context),
              icon: const Icon(Icons.add, size: 18), label: Text(l10n.addSite)),
        ]),
      ),
      Expanded(
        child: ListView.builder(
          padding: const EdgeInsets.only(bottom: 8),
          itemCount: groups.length,
          itemBuilder: (context, index) {
            final group = groups.keys.elementAt(index);
            final groupSites = groups[group]!;
            return Padding(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
              child: Card(
                child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                  Padding(padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
                      child: Text(group, style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 14))),
                  ...groupSites.map((site) => ListTile(
                    leading: const Icon(Icons.dns),
                    title: Text(site.name),
                    subtitle: Text('${site.username}@${site.host}:${site.port}'),
                    trailing: IconButton(icon: const Icon(Icons.delete_outline, size: 20),
                        onPressed: () => service.removeSite(site.id)),
                    onTap: () => onSiteDoubleTap(site),
                  )),
                ]),
              ),
            );
          },
        ),
      ),
    ]);
  }
}
