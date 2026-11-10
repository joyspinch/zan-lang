#!/usr/bin/env python3
"""Regression test suite for Zan multi-platform mobile packaging and certificate signing scaffold.

Validates scripts/package_mobile.ps1 across:
  1. Toolchain health check (-Doctor)
  2. Android release gate enforcement (requires -Cert)
  3. Android custom keystore and alias wiring
  4. iOS release gate enforcement (requires -Cert)
  5. iOS privacy descriptions injection in Info.plist
  6. HarmonyOS module.json5 permission declarations and pack.info structure
  7. End-to-end multi-platform dispatch (-Platform all)
  8. Static security & environment variable audit on src/compiler/apk.c
  9. App Store privacy audit on src/compiler/ipa.c
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent


def run_ps1(args: list[str]) -> tuple[int, str]:
    cmd = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(REPO_ROOT / "scripts" / "package_mobile.ps1"),
    ] + args
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    raw = res.stdout
    # Try decoding utf-8, fallback to gbk / cp936
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        text = raw.decode("gbk", errors="replace")
    return res.returncode, text


def test_doctor():
    print("[1/9] Testing package_mobile.ps1 -Doctor...")
    code, out = run_ps1(["-Doctor"])
    assert code == 0, f"Doctor failed with code {code}: {out}"
    assert "Zan 多端移动与折叠屏打包脚手架工具链体检 (Doctor)" in out, "Header missing"
    assert "Java Runtime" in out, "Java runtime probe missing"
    assert "Android apksigner" in out, "Android apksigner probe missing"
    print("  PASS: Doctor reported healthy toolchain.")


def test_android_release_guard():
    print("[2/9] Testing Android release gate (must fail without -Cert)...")
    code, out = run_ps1(["-Platform", "android", "-Release", "-DryRun"])
    assert code != 0, "Release mode without -Cert must fail"
    assert "必须指定 -Cert 商业签名证书路径" in out, f"Unexpected error message: {out}"
    print("  PASS: Release gate blocked unsigned release.")


def test_android_custom_keystore():
    print("[3/9] Testing Android release with custom Keystore and alias...")
    code, out = run_ps1([
        "-Platform", "android",
        "-Release",
        "-Cert", "prod_release.keystore",
        "-KeyPass", "secret123",
        "-KeyAlias", "enterprise_key",
        "-DryRun"
    ])
    assert code == 0, f"Android custom keystore DryRun failed: {out}"
    assert "使用自定义签名证书: prod_release.keystore (Alias: enterprise_key)" in out
    assert "Android APK 打包与签名计划校验通过" in out
    print("  PASS: Android custom keystore and alias wired successfully.")


def test_ios_release_guard():
    print("[4/9] Testing iOS release gate (must fail without -Cert)...")
    code, out = run_ps1(["-Platform", "ios", "-Release", "-DryRun"])
    assert code != 0, "iOS Release mode without -Cert must fail"
    assert "必须指定 -Cert 苹果开发者证书" in out, f"Unexpected error message: {out}"
    print("  PASS: iOS release gate blocked unsigned release.")


def test_ios_privacy_declarations():
    print("[5/9] Testing iOS IPA packaging plan and App Store privacy check...")
    code, out = run_ps1([
        "-Platform", "ios",
        "-Release",
        "-Cert", "Apple Distribution: Acme Inc",
        "-Profile", "embedded.mobileprovision",
        "-DryRun"
    ])
    assert code == 0, f"iOS DryRun failed: {out}"
    assert "NSLocationWhenInUseUsageDescription" in out
    assert "NSFaceIDUsageDescription" in out
    assert "iOS IPA 打包计划校验通过" in out
    print("  PASS: iOS privacy declarations validated.")


def test_ohos_module_and_pack_info():
    print("[6/9] Testing HarmonyOS NEXT packaging structure (module.json / pack.info)...")
    code, out = run_ps1(["-Platform", "ohos", "-DryRun"])
    assert code == 0, f"OHOS DryRun failed: {out}"
    assert "module.json (含 7 项核心鸿蒙敏感权限与 EntryAbility)" in out
    assert "pack.info" in out
    assert "HarmonyOS HAP 打包结构推演通过" in out
    print("  PASS: HarmonyOS NEXT manifests and structure verified.")


def test_all_platforms_dry_run():
    print("[7/9] Testing end-to-end multi-platform dispatch (-Platform all)...")
    code, out = run_ps1(["-Platform", "all", "-DryRun"])
    assert code == 0, f"All platforms DryRun failed: {out}"
    assert "[Android] 正在构建与签署 APK 安装包" in out
    assert "[iOS] 正在构建与签署 IPA 安装包" in out
    assert "[HarmonyOS] 正在构建与签署 HAP 安装包" in out
    print("  PASS: Multi-platform orchestration succeeded.")


def test_compiler_apk_source_audit():
    print("[8/9] Auditing src/compiler/apk.c keystore environment variables...")
    apk_c = REPO_ROOT / "src" / "compiler" / "apk.c"
    text = apk_c.read_text(encoding="utf-8")
    assert "ZAN_ANDROID_KEYSTORE" in text, "ZAN_ANDROID_KEYSTORE missing"
    assert "ZAN_ANDROID_KEYSTORE_PASS" in text, "ZAN_ANDROID_KEYSTORE_PASS missing"
    assert "ZAN_ANDROID_KEY_ALIAS" in text, "ZAN_ANDROID_KEY_ALIAS missing"
    print("  PASS: apk.c supports commercial keystore environment configuration.")


def test_compiler_ipa_source_audit():
    print("[9/9] Auditing src/compiler/ipa.c App Store privacy strings...")
    ipa_c = REPO_ROOT / "src" / "compiler" / "ipa.c"
    text = ipa_c.read_text(encoding="utf-8")
    assert "NSLocationWhenInUseUsageDescription" in text
    assert "NSFaceIDUsageDescription" in text
    assert "NSPhotoLibraryUsageDescription" in text
    assert "NSCameraUsageDescription" in text
    assert "NSMicrophoneUsageDescription" in text
    print("  PASS: ipa.c declares all mandatory App Store privacy usage descriptions.")


def main():
    print("==========================================================")
    print(" Zan Mobile & Multi-Platform Packaging Regression Tests   ")
    print("==========================================================")
    test_doctor()
    test_android_release_guard()
    test_android_custom_keystore()
    test_ios_release_guard()
    test_ios_privacy_declarations()
    test_ohos_module_and_pack_info()
    test_all_platforms_dry_run()
    test_compiler_apk_source_audit()
    test_compiler_ipa_source_audit()
    print("==========================================================")
    print("ALL 9 PACKAGING & SIGNING REGRESSION TESTS PASSED.")
    print("==========================================================")


if __name__ == "__main__":
    main()
