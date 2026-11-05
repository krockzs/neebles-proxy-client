#!/usr/bin/env python3
"""F38 static gates for the 1.0.3 candidate; no OS, Secrets or network actions."""
import json
from pathlib import Path
import re
import unittest
ROOT = Path(__file__).resolve().parents[1]

class Corrective103Contract(unittest.TestCase):
    def test_version_identity(self):
        self.assertEqual(json.loads((ROOT / 'manifest.json').read_text())['version'], '1.0.3')
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertRegex(cmake, r'project\(neebles_proxy_client VERSION 1\.0\.3 LANGUAGES CXX\)')

    def test_fresh_desktop_environment_without_hardcoding(self):
        source = (ROOT / 'src/config_bridge.cpp').read_text()
        self.assertIn('recoverGraphicalEnvironment', source)
        self.assertIn('show-environment', source)
        self.assertIn('setProcessEnvironment(desktop)', source)
        self.assertIn('QProcess::startDetached(installed, {action}', source)
        self.assertNotIn('Environment=DISPLAY=:0', source)
        self.assertIn('PROXY_CLIENT_ANIMATION_NO_DESKTOP_SESSION', source)

    def test_safe_secrets_diagnostics(self):
        source = (ROOT / 'src/config_bridge.cpp').read_text()
        self.assertIn('safeSecretsFailureCode(error)', source)
        self.assertIn('PROXY_CLIENT_SECRETS_DIAG phase=read_server_ip status=', source)
        self.assertIn('configuration_unavailable', source)
        self.assertNotIn('<< error <<', source)

    def test_lifecycle_uses_boss_generic_services(self):
        lifecycle = json.loads((ROOT / 'lifecycle.json').read_text())
        manifest = json.loads((ROOT / 'manifest.json').read_text())
        self.assertEqual(lifecycle['transitions']['update']['operations'], {})
        self.assertEqual(lifecycle['transitions']['uninstall']['operations'], {})
        self.assertEqual(manifest['uninstall']['extras'][0]['provider'], 'systemd-user')
        self.assertTrue(manifest['uninstall']['extras'][0]['remove'])

if __name__ == '__main__':
    unittest.main(verbosity=2)
