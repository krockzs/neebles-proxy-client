#!/usr/bin/env python3
"""Guardas de fuente F19; NO prueban runtime/Bridge ni certifican un ELF."""
import json
import pathlib
import re
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class BridgeTimeoutSourceContract(unittest.TestCase):
    def test_candidate_version(self):
        self.assertEqual(json.loads((ROOT/'manifest.json').read_text())['version'],'1.0.2')
    def test_bridge_wait_is_bounded(self):
        source=(ROOT/'src/platform_exit_node.cpp').read_text()
        self.assertIn('kTotalBudgetMs = 8500',source)
        self.assertEqual(len(re.findall(r'remaining\(5000\)',source)),1)
        self.assertNotIn('remaining(1000)',source)
        self.assertIn('waitForReadyRead(ms)',source)
    def test_authentication_and_limits_still_present(self):
        source=(ROOT/'src/platform_exit_node.cpp').read_text()
        for token in ('SO_PEERCRED','trustedCanonical(cred.pid)','kMaxReplyBytes',
                      'server-ip.get','--exit-node='):
            self.assertIn(token,source)
if __name__=='__main__': unittest.main()
