#!/usr/bin/env python3
"""Static F16 certification gates for Proxy Client 1.0.1 candidate.

Read-only: this suite never contacts Tailscale, Secrets, Boss IPC or root OS.
It validates source contracts only. It does not certify compiled Qt/Esbirro ELF.
"""
import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def document(relative):
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


class ProxyClientCandidateContract(unittest.TestCase):
    def test_governed_install_and_open_unchanged(self):
        manifest = document("manifest.json")
        lifecycle = document("lifecycle.json")
        self.assertEqual(manifest["schema"], 4)
        self.assertEqual(manifest["name"], "neebles-proxy-client")
        self.assertEqual(manifest["entrypoint"], "runtime/neebles-proxy-client")
        self.assertEqual(manifest["platform_operations"],
                         "contracts/platform-operations.json")
        self.assertEqual(
            lifecycle["transitions"]["open"]["operations"]["open-config"]
                     ["munition"]["step"], "open-config")
        self.assertEqual(
            lifecycle["transitions"]["install"]["operations"]["start-config-bridge"]
                     ["artillery"], "boss.user_service")

    def test_privileged_policy_has_only_fixed_exit_node_actions(self):
        policy = document("contracts/platform-operations.json")
        self.assertEqual(set(policy), {"schema", "module", "operations"})
        self.assertEqual(policy["schema"], "1")
        self.assertEqual(policy["module"], "neebles-proxy-client")
        self.assertEqual(set(policy["operations"]), {"activate", "deactivate"})
        for action in ("activate", "deactivate"):
            entry = policy["operations"][action]
            self.assertEqual(
                entry["executable"],
                "/usr/lib/neebles/module-operations/neebles-proxy-client-platform")
            self.assertEqual(entry["argv"], [
                "--" + action, "--tailnet=olate.pablo@gmail.com"
            ])
            self.assertNotIn("user-uid", " ".join(entry["argv"]))

    def test_single_governed_switch_on_each_boss_surface(self):
        lifecycle = document("lifecycle.json")
        surfaces = document("surfaces.json")["items"]
        obj = lifecycle["objects"]["proxy-exit-node"]
        self.assertIs(obj["initial_active"], False)
        self.assertEqual(obj["transition_active"],
                         {"activate": True, "deactivate": False})
        for action in ("activate", "deactivate"):
            operation = next(iter(obj["transitions"][action]["operations"].values()))
            self.assertEqual(operation["artillery"], "boss.platform_operation")
            self.assertEqual(operation["objective"], "execute")
            self.assertEqual(operation["munition"]["require_privilege"], "true")
            self.assertEqual(operation["munition"]["operation"], action)
        for surface in ("launcher", "tray"):
            controls = [entry for entry in surfaces.values()
                        if entry["surface"] == surface and
                        entry["data"].get("action_on", entry["data"].get("action"))
                        in ("activate", "deactivate")]
            self.assertEqual(len(controls), 1)
            item = controls[0]
            self.assertEqual(item["object_id"], "proxy-exit-node")
            self.assertEqual(item["data"]["control"], "switch")
            self.assertEqual(item["data"]["transition_on"], "activate")
            self.assertEqual(item["data"]["transition_off"], "deactivate")
            self.assertEqual(item["data"]["action_on"], "activate")
            self.assertEqual(item["data"]["action_off"], "deactivate")
            self.assertEqual(item["require"]["self"], "active")
            config = [entry for entry in surfaces.values()
                      if entry["surface"] == surface and
                      entry["data"].get("action") == "open"]
            self.assertEqual(len(config), 1)

    def test_translations_and_assets_exist(self):
        surfaces = document("surfaces.json")
        for language in ("es_CL", "en_US"):
            entries = document("languages/" + language + ".json")
            for entry in surfaces["items"].values():
                label = entry["data"]["label_key"]
                self.assertTrue(entries.get(label), (language, label))
        for name in ("neebles-client-logo-on.png",
                     "neebles-client-logo-off.png"):
            path = ROOT / "assets" / name
            self.assertTrue(path.is_file(), path)
            self.assertEqual(path.open("rb").read(8),
                             bytes.fromhex("89504e470d0a1a0a"))

    def test_config_theme_and_confirmed_splash(self):
        ui = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        bridge = (ROOT / "src/config_bridge.cpp").read_text(encoding="utf-8")
        self.assertIn('QApplication::setStyle(', ui)
        self.assertIn('app.setStyleSheet(neeblesTheme())', ui)
        for marker in ("#10131d", "#22d3ee", "#a78bfa",
                       "neebles-client-logo-on.png",
                       "neebles-client-logo-off.png"):
            self.assertIn(marker, ui)
        self.assertIn('3000.0', ui)
        self.assertIn('ownActive == currentActive', bridge)
        self.assertIn('QProcess::startDetached(installed, {action}', bridge)

    def test_native_helper_uses_os_desktop_identity_not_fixed_uid(self):
        source = (ROOT / "src/platform_exit_node.cpp").read_text(
            encoding="utf-8")
        self.assertIn('desktopUidFromOs()', source)
        self.assertIn('"runtime.env"', source)
        self.assertIn('O_NOFOLLOW', source)
        self.assertIn('NEEBLES_DESKTOP_UID=', source)
        self.assertIn('NEEBLES_DESKTOP_GID=', source)
        self.assertNotIn('cli.addOption({"user-uid"', source)
        self.assertIn('if (::geteuid() != 0)', source)
        self.assertIn('if (!approved(before, ip))', source)
        self.assertIn('if (!configuredServer(*user, ip))', source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
