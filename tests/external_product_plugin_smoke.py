"""Build ScoreView from a copied product tree using only the installed SDK."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import sys
import tempfile


def _bootstrap_shared_support() -> None:
    """The shared smoke helpers live in the Draxul checkout this driver is
    pointed at (tests/support/sdk_smoke.py); --source-root locates it."""
    probe = argparse.ArgumentParser(add_help=False)
    probe.add_argument("--source-root", type=pathlib.Path)
    known, _ = probe.parse_known_args()
    if known.source_root is None:
        raise SystemExit("--source-root is required")
    sys.path.insert(
        0, str(known.source_root.resolve() / "tests" / "support"))


_bootstrap_shared_support()

import sdk_smoke  # noqa: E402

PLUGIN_ID = "dev.draxul.scoreview"


def verovio_name() -> str:
    if sys.platform.startswith("win"):
        return "verovio.dll"
    if sys.platform == "darwin":
        return "libverovio.dylib"
    return "libverovio.so"


def copy_draxul_runtime_assets(draxul: pathlib.Path,
                               executable_dir: pathlib.Path) -> None:
    if sys.platform == "darwin":
        source_resources = draxul.parent.parent / "Resources"
        destination = executable_dir.parent / "Resources"
        if source_resources.is_dir():
            shutil.copytree(source_resources, destination)
    # On every platform the core runtime payloads (shaders, fonts, assets)
    # live beside the executable — in Contents/MacOS inside the bundle — so
    # copy them there too; Resources alone leaves the renderer shaderless.
    for name in ("assets", "fonts", "shaders"):
        source = draxul.parent / name
        if source.is_dir():
            shutil.copytree(source, executable_dir / name)


def main() -> int:
    args = sdk_smoke.make_argument_parser().parse_args()

    source_root = args.source_root.resolve()
    # Keeping the isolated checkout beside, rather than beneath, the repository
    # makes accidental app/libs/modules access visible and avoids MSBuild's
    # restrictions on the system temporary directory.
    with tempfile.TemporaryDirectory(
            prefix=".draxul-product-extraction-",
            dir=source_root.parent) as raw_temp:
        temp = pathlib.Path(raw_temp)
        sdk_prefix = temp / "sdk"
        copied_plugins = temp / "plugins"
        source = copied_plugins / "scoreview"
        external_build = temp / "build"

        sdk_smoke.install_sdk(args, sdk_prefix)
        shutil.copytree(source_root / "plugins" / "scoreview", source)
        shutil.copytree(source_root / "plugins" / "support" / "imgui",
                        copied_plugins / "support" / "imgui")
        # The support ImGui target consumes the shared scancode/IImGuiHost
        # leaf; standalone builds carry a copy of it beside support/imgui.
        shutil.copytree(source_root / "libs" / "draxul-imgui-core",
                        copied_plugins / "support" / "imgui-core")
        # The NanoVG core + Vulkan/Metal backends (and their GLSL shaders)
        # come from the shared Draxul NanoVG tree; standalone builds compile
        # its backend half from a copy staged beside support/imgui.
        shutil.copytree(source_root / "libs" / "draxul-nanovg",
                        copied_plugins / "support" / "nanovg")

        forbidden_roots = [temp / name for name in ("app", "libs", "modules")]
        if any(path.exists() for path in forbidden_roots):
            raise RuntimeError(
                "isolated product tree contains a Draxul core tree")

        sdk_smoke.configure_external(args, source, external_build, sdk_prefix,
                                     timeout=600)
        # Verovio presents hundreds of translation units to MSVC in one
        # target. Bound parallelism so this acceptance test remains usable
        # on developer workstations instead of saturating input/UI work.
        sdk_smoke.build_external(
            args, external_build, "draxul-scoreview-plugin",
            parallel=min(12, max(2, os.cpu_count() or 2)), timeout=2400)

        # The standalone build may emit CMake's default MODULE suffix (.so on
        # macOS); stage the module under the manifest's canonical platform
        # library name so the plugin loader finds it.
        module = sdk_smoke.find_shared_module(
            external_build, "draxul-scoreview")
        module_name = sdk_smoke.shared_module_name("draxul-scoreview")
        executable_dir, plugin_dir = sdk_smoke.stage_app_layout(
            temp, args.draxul, PLUGIN_ID)
        clean_draxul = executable_dir / args.draxul.name
        copy_draxul_runtime_assets(args.draxul, executable_dir)
        shutil.copy2(source / "plugin.toml", plugin_dir / "plugin.toml")
        shutil.copy2(module, plugin_dir / module_name)
        verovio = sdk_smoke.find_one(external_build, verovio_name())
        shutil.copy2(verovio, plugin_dir / verovio.name)
        verovio_source = external_build / "_deps" / "verovio-src"
        shutil.copytree(verovio_source / "data", plugin_dir / "verovio-data")
        shutil.copy2(verovio_source / "fonts" / "Leipzig" / "Leipzig.ttf",
                     plugin_dir / "verovio-data" / "Leipzig.ttf")
        soundfont_source = external_build / "_deps" / "ydp_grand_piano-src"
        shutil.copytree(soundfont_source, plugin_dir / "soundfonts")
        shader_dir = plugin_dir / "shaders"
        shader_dir.mkdir()
        if sys.platform.startswith("win"):
            for name in ("nanovg.vert.spv", "nanovg.frag.spv"):
                shutil.copy2(sdk_smoke.find_one(external_build, name),
                             shader_dir / name)

        env = sdk_smoke.isolated_env(temp)
        sdk_smoke.assert_plugin_loads(
            clean_draxul, PLUGIN_ID, plugin_dir / module_name, env,
            timeout=30)

        if args.render:
            fixture_dir = plugin_dir / "fixtures"
            fixture_dir.mkdir()
            fixture = fixture_dir / "grieg.musicxml"
            shutil.copy2(source / "tests" / "fixtures" / "musicxml"
                         / "grieg-waltz-op-12-no-2.musicxml", fixture)
            scenario = temp / "scoreview-extracted.toml"
            config_json = json.dumps({
                "source": str(fixture),
                "mode": "paged",
            })
            scenario.write_text(
                "\n".join([
                    'name = "scoreview-extracted"',
                    'host = "plugin"',
                    f'plugin_id = "{PLUGIN_ID}"',
                    f"plugin_config_json = '{config_json}'",
                    "width = 960",
                    "height = 640",
                    "settle_ms = 1200",
                    "timeout_ms = 60000",
                    'commands = [""]',
                    "",
                ]), encoding="utf-8")
            rendered = temp / "scoreview-extracted.bmp"
            sdk_smoke.run_render(clean_draxul, scenario, rendered, env=env,
                                 cwd=executable_dir, timeout=90,
                                 what="extracted ScoreView render")
            sdk_smoke.validate_rendered_bmp(
                rendered, min_colors=16, what="extracted ScoreView render")
            print("Extracted ScoreView Grieg render: PASS", flush=True)

        print("ScoreView extraction boundary: PASS", flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
