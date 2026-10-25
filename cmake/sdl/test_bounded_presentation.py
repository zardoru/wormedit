#!/usr/bin/env python3
"""Source-override checks and an opt-in, isolated-Xvfb Vulkan fault run.

Default: unit checks only. Integration uses an already built application/shim;
this script never compiles dependencies or opens the user's display.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import secrets
import sys
import subprocess
import tempfile
import time
import unittest

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("sdl_bounded_patch", HERE / "patch_bounded_presentation.py")
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)
SOURCE = None


def generate(source, output, platform="linux"):
    subprocess.run(["python3", str(HERE / "patch_bounded_presentation.py"),
        "--source", str(source), "--output", str(output), "--platform", platform,
        "--helpers", str(HERE / "bounded_present_vulkan.inc")], check=True, capture_output=True)


class OverrideTests(unittest.TestCase):
    def copy_source(self, destination):
        for relative in PATCH.AUDITED_SHA256:
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(SOURCE / relative, target)

    def test_selected_source_is_unchanged_and_generation_is_repeatable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / "selected-SDL", root / "private-overrides"
            self.copy_source(source)
            before = {path: (source / path).read_bytes() for path in PATCH.AUDITED_SHA256}
            generate(source, output)
            first = {path: (output / path).read_bytes() for path in before}
            modified = {path: (output / path).stat().st_mtime_ns for path in before}
            generate(source, output)
            self.assertEqual(before, {path: (source / path).read_bytes() for path in before})
            self.assertEqual(first, {path: (output / path).read_bytes() for path in before})
            self.assertEqual(modified, {path: (output / path).stat().st_mtime_ns for path in before})
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual(set(manifest), set(before))

    def test_metal_timeout_guard_preserves_source_and_handles_nil_before_present(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / "selected-SDL", root / "private-overrides"
            relative = next(iter(PATCH.METAL_SHA256))
            original = (SOURCE / relative).read_bytes()
            target = source / relative
            target.parent.mkdir(parents=True)
            target.write_bytes(original)
            generate(source, output, "metal")
            generated = (output / relative).read_bytes()
            modified = (output / relative).stat().st_mtime_ns
            generate(source, output, "metal")
            self.assertEqual(target.read_bytes(), original)
            self.assertEqual((output / relative).read_bytes(), generated)
            self.assertEqual((output / relative).stat().st_mtime_ns, modified)
            text = generated.decode()
            self.assertIn("allowsNextDrawableTimeout = YES", text)
            start = text.index("windowData->drawable = [windowData->layer nextDrawable];")
            self.assertLess(text.index("if (windowData->drawable == nil)", start),
                text.index("// Set up presentation", start))
            self.assertNotIn("wormeditBoundedPresent", text)

    def test_unaudited_source_fails_before_any_override_is_published(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / "selected-SDL", root / "private-overrides"
            self.copy_source(source)
            path = source / next(iter(PATCH.AUDITED_SHA256))
            path.write_bytes(path.read_bytes() + b"\n/* local modification */\n")
            with self.assertRaises(subprocess.CalledProcessError):
                generate(source, output)
            self.assertFalse(output.exists())

    def test_vsync_checks_active_and_recovery_swapchains_not_startup_probes(self):
        recorded = [{"event": "present_mode", "value": value} for value in (2, 1, 1, 2)]
        recorded += [{"event": "present_fence", "value": 1}, {"event": "present_mode", "value": 2}]
        self.assertEqual(effective_present_modes(recorded), [2, 2])
        recorded.append({"event": "present_mode", "value": 1})
        self.assertEqual(effective_present_modes(recorded), [2, 2, 1])

    def test_ambiguous_or_missing_patch_anchor_is_rejected(self):
        with self.assertRaises(ValueError):
            PATCH.replace_once("two two", "two", "new")
        with self.assertRaises(ValueError):
            PATCH.replace_once("none", "two", "new")


def events(log):
    if not log.exists():
        return []
    return [json.loads(line) for line in log.read_text().splitlines() if line.startswith("{")]


def effective_present_modes(recorded):
    first_present = next((i for i, event in enumerate(recorded) if event["event"] == "present_fence"), None)
    if first_present is None:
        return []
    startup = [event["value"] for event in recorded[:first_present] if event["event"] == "present_mode"]
    recovery = [event["value"] for event in recorded[first_present:] if event["event"] == "present_mode"]
    return startup[-1:] + recovery


def wait_for(predicate, process, seconds=10):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        if process.poll() is not None:
            raise RuntimeError(f"application exited during presentation fault test: {process.returncode}")
        if predicate():
            return
        time.sleep(0.02)
    raise RuntimeError("presentation fault test timed out")


def write_mode(control, mode):
    temporary = control.with_suffix(".new")
    temporary.write_text(mode + "\n")
    temporary.replace(control)


def quit_through_application(window, environment):
    # The custom titlebar Close button routes through the application's Quit
    # action. This needs no window manager/EWMH support on the private X server.
    geometry = subprocess.check_output(["xdotool", "getwindowgeometry", "--shell", window],
        env=environment, text=True)
    fields = dict(line.split("=", 1) for line in geometry.splitlines() if "=" in line)
    subprocess.run(["xdotool", "windowfocus", "--sync", window], env=environment, check=True)
    subprocess.run(["xdotool", "mousemove", "--window", window, str(int(fields["WIDTH"]) - 16), "20"],
        env=environment, check=True)
    subprocess.run(["xdotool", "mousedown", "1"], env=environment, check=True)
    time.sleep(0.08)
    subprocess.run(["xdotool", "mouseup", "1"], env=environment, check=True)


def watchdog_texts(state):
    return {path: path.read_text() for path in state.rglob("display-stalls.log")}


def check_watchdog_resume(application, application_log, state, stderr_before, crash_before):
    wait_for(lambda: "wormedit: display resumed:" in application_log.read_text()[stderr_before:], application)
    detail = application_log.read_text()[stderr_before:]
    if detail.count("wormedit: display stalled:") != 1 or detail.count("wormedit: display resumed:") != 1:
        raise RuntimeError("watchdog did not report exactly one presentation stall/resume")
    current = watchdog_texts(state)
    logged = "".join(text[len(crash_before.get(path, "")):] for path, text in current.items())
    if logged.count("wormedit: display stalled:") != 1 or logged.count("wormedit: display resumed:") != 1:
        raise RuntimeError("private recovery crash log lacks the single stall/resume pair")


def integration(executable, shim, icd, artifacts, unsupported=False, stall_seconds=0):
    for command in ("Xvfb", "xdotool", "xdpyinfo", "xauth"):
        if not shutil.which(command):
            raise RuntimeError(f"integration requires {command}")
    artifacts.mkdir(parents=True, exist_ok=True)
    control, log = artifacts / "fault-control", artifacts / "vulkan-events.jsonl"
    write_mode(control, "unsupported" if unsupported else "resume")
    log.write_text("")
    authority = artifacts / "Xauthority"
    authority.touch(mode=0o600, exist_ok=True)
    authority.chmod(0o600)
    environment = dict(os.environ, XAUTHORITY=str(authority.resolve()), SDL_VIDEODRIVER="x11",
        SDL_AUDIODRIVER="dummy", SDL_GPU_DRIVER="vulkan")
    for name in ("WAYLAND_DISPLAY", "WAYLAND_SOCKET", "NIRI_SOCKET", "SWAYSOCK", "HYPRLAND_INSTANCE_SIGNATURE",
                 "DBUS_SESSION_BUS_ADDRESS", "DBUS_STARTER_ADDRESS", "DBUS_STARTER_BUS_TYPE"):
        environment.pop(name, None)
    state = Path(tempfile.mkdtemp(prefix="isolated-state-", dir=artifacts))
    for key, child in (("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                       ("XDG_STATE_HOME", "state"), ("XDG_CACHE_HOME", "cache"), ("XDG_RUNTIME_DIR", "runtime")):
        directory = state / child
        directory.mkdir(mode=0o700)
        environment[key] = str(directory.resolve())
    xvfb = None
    display = None
    for number in range(140, 160):
        candidate = f"localhost:{number}"
        probe = subprocess.run(["xdpyinfo", "-display", candidate], env=environment,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if probe.returncode == 0:
            continue
        subprocess.run(["xauth", "-f", str(authority), "add", candidate, ".", secrets.token_hex(16)],
            check=True, stdout=subprocess.DEVNULL)
        xvfb = subprocess.Popen(["Xvfb", f":{number}", "-screen", "0", "1280x1024x24", "-nolisten", "unix",
            "-listen", "tcp", "-auth", str(authority.resolve())], stdout=subprocess.DEVNULL,
            stderr=(artifacts / "xvfb.log").open("w"))
        for _ in range(20):
            if xvfb.poll() is not None:
                break
            probe = subprocess.run(["xdpyinfo", "-display", candidate], env=environment,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if probe.returncode == 0:
                display = candidate
                break
            time.sleep(0.1)
        if display:
            break
        if xvfb.poll() is None:
            xvfb.terminate()
        xvfb.wait(timeout=5)
        xvfb = None
    if not display:
        raise RuntimeError("could not create an authenticated, owned Xvfb display")
    environment["DISPLAY"] = display
    bus = None
    if shutil.which("dbus-daemon"):
        bus = subprocess.Popen(["dbus-daemon", "--session", "--nofork", "--print-address=1", "--nopidfile"],
            env=environment, stdout=subprocess.PIPE, stderr=(artifacts / "dbus.log").open("w"), text=True)
        address = bus.stdout.readline().strip()
        if not address:
            bus.terminate()
            xvfb.terminate()
            raise RuntimeError("private session D-Bus did not start")
        environment["DBUS_SESSION_BUS_ADDRESS"] = address
    application = None
    try:
        environment.update(LD_PRELOAD=str(shim.resolve()),
            WORMEDIT_PRESENT_FAULT_CONTROL=str(control.resolve()),
            WORMEDIT_PRESENT_FAULT_LOG=str(log.resolve()))
        if icd:
            environment["VK_ICD_FILENAMES"] = str(icd.resolve())
        with (artifacts / "application.log").open("w") as output:
            application = subprocess.Popen([str(executable.resolve())], env=environment,
                stdout=output, stderr=subprocess.STDOUT)
            if unsupported:
                application.wait(timeout=15)
                detail = (artifacts / "application.log").read_text()
                if application.returncode == 0 or "VK_EXT_swapchain_maintenance1" not in detail:
                    raise RuntimeError("unsupported device did not fail startup with the explicit driver requirement")
                if not any(event["event"] == "maintenance_hidden" for event in events(log)):
                    raise RuntimeError("unsupported-device injection was not exercised")
                (artifacts / "result.json").write_text(json.dumps({"passed": True,
                    "unsupported_startup": True, "exit_code": application.returncode}, indent=2) + "\n")
                return
            wait_for(lambda: sum(event["event"] == "present_fence" for event in events(log)) >= 3, application)
            windows = subprocess.check_output(["xdotool", "search", "--onlyvisible", "--pid", str(application.pid)],
                env=environment, text=True).splitlines()
            if not windows:
                raise RuntimeError("application has no isolated test window")
            window = windows[0]
            for fault in ("withhold", "fences", "out_of_date", "surface_lost_once"):
                before = len(events(log))
                stderr_before = len((artifacts / "application.log").read_text())
                crash_before = watchdog_texts(state)
                write_mode(control, fault)
                held_since = time.monotonic()
                if fault == "fences":
                    subprocess.run(["xdotool", "windowsize", window, "1000", "700"], env=environment, check=True)
                expected = {"withhold": "withheld", "fences": "presentation_pending",
                    "out_of_date": "out_of_date", "surface_lost_once": "surface_lost"}[fault]
                wait_for(lambda: sum(event["event"] == expected for event in events(log)[before:]) >= (1 if fault == "surface_lost_once" else 8), application)
                if fault == "withhold" and stall_seconds:
                    wait_for(lambda: time.monotonic() - held_since >= stall_seconds, application,
                        seconds=stall_seconds + 5)
                # Multiple acquisitions/fence polls prove the application loop continues
                # while presentation is withheld. Frame/input/audio owners have separate tests.
                write_mode(control, "resume")
                resumed = sum(event["event"] == "present_fence" for event in events(log))
                wait_for(lambda: sum(event["event"] == "present_fence" for event in events(log)) > resumed + 2, application)
                if fault == "withhold" and stall_seconds:
                    check_watchdog_resume(application, artifacts / "application.log", state, stderr_before, crash_before)
            recorded = events(log)
            forbidden = {"unbounded_acquire", "forbidden_device_idle", "missing_present_fence"}
            if any(event["event"] in forbidden for event in recorded):
                raise RuntimeError("bounded presentation contract was violated; inspect Vulkan event log")
            modes = effective_present_modes(recorded)
            if not modes or any(value != 2 for value in modes):
                raise RuntimeError("default VSync/FIFO presentation was not preserved")
            quit_through_application(window, environment)
            application.wait(timeout=15)
            if application.returncode:
                raise RuntimeError(f"application did not close cleanly: {application.returncode}")
            (artifacts / "result.json").write_text(json.dumps({"passed": True,
                "faults": ["withhold", "fences", "out_of_date", "surface_lost_once"],
                "events": len(recorded), "present_modes": modes, "stall_seconds": stall_seconds,
                "watchdog_checked": bool(stall_seconds)}, indent=2) + "\n")
    finally:
        write_mode(control, "resume")
        if application and application.poll() is None:
            application.terminate()
            try:
                application.wait(timeout=5)
            except subprocess.TimeoutExpired:
                application.kill()
                application.wait()
        xvfb.terminate()
        xvfb.wait(timeout=5)
        if bus and bus.poll() is None:
            bus.terminate()
            bus.wait(timeout=5)


def main():
    global SOURCE
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--integration", action="store_true")
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--expect-unsupported", action="store_true")
    parser.add_argument("--stall-seconds", type=float, default=0)
    parser.add_argument("--shim", type=Path)
    parser.add_argument("--icd", type=Path)
    parser.add_argument("--artifacts", type=Path, default=Path("/tmp/wormedit-t293-presentation"))
    args = parser.parse_args()
    SOURCE = args.source
    if args.integration:
        if not args.executable or not args.shim:
            parser.error("--integration requires --executable and --shim")
        integration(args.executable, args.shim, args.icd, args.artifacts, args.expect_unsupported, args.stall_seconds)
    else:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(OverrideTests))
        raise SystemExit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
