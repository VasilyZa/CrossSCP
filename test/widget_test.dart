import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:crossscp/main.dart';

void main() {
  testWidgets('App launches and shows site manager', (tester) async {
    await tester.pumpWidget(const CrossScpApp());
    await tester.pumpAndSettle();

    // Verify the app shell is rendered
    expect(find.text('crossscp'), findsOneWidget);

    // Verify tabs are present
    expect(find.text('Sites'), findsOneWidget);
    expect(find.text('Files'), findsOneWidget);

    // Verify the "No sites configured" placeholder or add button
    expect(find.text('Add Site'), findsWidgets);
  });

  testWidgets('Site list add button opens dialog', (tester) async {
    await tester.pumpWidget(const CrossScpApp());
    await tester.pumpAndSettle();

    // Tap the "Add Site" button
    final addButtons = find.text('Add Site');
    if (addButtons.evaluate().isNotEmpty) {
      await tester.tap(addButtons.first);
      await tester.pumpAndSettle();

      // Verify dialog appears
      expect(find.text('New Site'), findsOneWidget);
      expect(find.text('Host'), findsOneWidget);
      expect(find.text('Username'), findsOneWidget);
      expect(find.text('Save'), findsOneWidget);
    }
  });
}
