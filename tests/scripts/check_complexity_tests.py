"""Failure contracts of the complexity ratchet (no compiler subprocesses)."""

import importlib.util
from dataclasses import replace
import json
from pathlib import Path
import sys
import subprocess
import shutil
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("complexity_check", ROOT / "scripts/check-complexity.py")
check = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = check
spec.loader.exec_module(check)


def finding(score=29, function="usr:int", file="src/example.cpp"):
    return check.Finding(file, function, "example(int)", score)


class ComplexityTests(unittest.TestCase):
    def test_unchanged_and_lower_scores(self):
        previous = finding()
        for score in (29, 28, 26):
            current = finding(score)
            failures, decreases = check.comparison(
                {current.key: current}, {previous.key: previous}, {previous.file})
            self.assertEqual(failures, [])
            self.assertEqual(bool(decreases), score < previous.score)

    def test_new_or_increased_function_fails(self):
        for previous, current in ((None, finding(26)), (finding(29), finding(30))):
            baseline = {previous.key: previous} if previous else {}
            failures, _ = check.comparison({current.key: current}, baseline, {current.file})
            self.assertEqual(len(failures), 1)

    def test_same_display_name_cannot_borrow_another_function_budget(self):
        previous = finding()
        for current in (finding(26, function="usr:double"),
                        finding(26, function="usr:renamed"),
                        finding(26, file="src/moved.cpp")):
            failures, _ = check.comparison(
                {current.key: current}, {previous.key: previous}, {current.file})
            self.assertEqual(len(failures), 1)

    def test_partial_run_does_not_retire_unchecked_allowances(self):
        previous = finding()
        _, partial = check.comparison({}, {previous.key: previous}, {"src/other.cpp"})
        _, checked = check.comparison({}, {previous.key: previous}, {previous.file})
        _, complete = check.comparison({}, {previous.key: previous}, set(), complete=True)
        self.assertEqual(partial, [])
        self.assertEqual(len(checked), 1)
        self.assertEqual(complete, checked)

    def test_repeated_template_diagnostics_take_the_maximum(self):
        low, high = finding(27), finding(29)
        merged = check.merge_findings([(low, 100), (high, 100), (low, 100)])
        self.assertEqual(merged, {high.key: high})

    def test_colliding_declarations_fail_instead_of_sharing_an_allowance(self):
        with self.assertRaisesRegex(ValueError, "Ambiguous"):
            check.merge_findings([(finding(27), 100), (finding(29), 200)])

    def read_rows(self, rows, metadata=None):
        if metadata is None:
            metadata = {"format": 1, "threshold": 25, "clang_tidy_version": "23.1.1"}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "baseline.txt"
            path.write_text("# metadata: " + json.dumps(metadata) + "\n" +
                            "\n".join(json.dumps(row) for row in rows) + "\n")
            return check.read_baseline(path)

    def test_baseline_keeps_overloads_with_the_same_display_name(self):
        first, second = finding(), finding(function="usr:double")
        _, rows = self.read_rows([check.asdict(first), check.asdict(second)])
        self.assertEqual(set(rows), {first.key, second.key})

    def test_malformed_or_duplicate_baseline_fails_closed(self):
        valid = check.asdict(finding())
        bad = [dict(valid, score=25), dict(valid, score=True), dict(valid, file="../outside.cpp"),
               dict(valid, file="/outside.cpp"), dict(valid, function=[]),
               {"file": "src/example.cpp"}]
        for row in bad:
            with self.subTest(row=row), self.assertRaises(ValueError):
                self.read_rows([row])
        with self.assertRaises(ValueError):
            self.read_rows([valid, valid])

    def test_wrong_threshold_or_missing_metadata_fails_closed(self):
        for metadata in ({}, {"format": 1, "threshold": 30}, {"format": 2, "threshold": 25}):
            with self.subTest(metadata=metadata), self.assertRaises(ValueError):
                self.read_rows([], metadata)

    def test_missing_explicit_source_does_not_pass_as_a_deleted_file(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "compile_commands.json").write_text("[]")
            result = subprocess.run(
                [sys.executable, str(ROOT / "scripts/check-complexity.py"),
                 "--build-dir", str(build), str(ROOT / "src/__complexity_missing_input__.cpp")],
                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing source file", result.stderr)


class ClangIdentityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tidy, cls.driver = shutil.which("clang-tidy"), shutil.which("clang")
        if not cls.tidy or not cls.driver:
            raise unittest.SkipTest("Clang tools are needed for identity integration cases")
        try:
            import yaml
            from clang import cindex
        except (ImportError, OSError) as error:
            raise unittest.SkipTest(str(error))
        try:
            cindex.Index.create()
        except cindex.LibclangError as error:
            raise unittest.SkipTest(str(error))
        cls.yaml, cls.clang = yaml, cindex
        cls.resource = subprocess.check_output([cls.driver, "-print-resource-dir"], text=True).strip()

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        root_patch = patch.object(check, "ROOT", self.root)
        root_patch.start()
        self.addCleanup(root_patch.stop)
        self.source = self.root / "src/probe.cpp"
        self.source.parent.mkdir()
        self.build = self.root / "build"
        self.build.mkdir()
        self.entry = {"directory": str(self.root), "file": str(self.source),
                      "arguments": [self.driver, "-std=c++23", "-c", str(self.source),
                                    "-o", str(self.build / "probe.o")]}
        (self.build / "compile_commands.json").write_text(json.dumps([self.entry]))
        self.inspections = 0

    def inspect(self, text):
        self.source.write_text(text)
        self.inspections += 1
        logs = self.root / f"logs-{self.inspections}"
        logs.mkdir()
        rows = check.inspect_file((0, self.source), self.build, {self.source: [self.entry]},
                                  self.resource, logs, self.tidy, self.yaml, self.clang)
        return check.merge_findings(rows)

    @staticmethod
    def branches():
        return " ".join(f"if (value == {i}) ++value;" for i in range(26))

    def test_real_overloads_have_separate_budgets(self):
        body = self.branches() + " return value;"
        rows = self.inspect("int same_name(int value) {" + body + "}\n" +
                            "int same_name(double value) {" + body + "}\n")
        self.assertEqual(len(rows), 2)
        integer = next(row for row in rows.values() if row.name == "same_name(int)")
        failures, _ = check.comparison(rows, {integer.key: replace(integer, score=29)}, {integer.file})
        self.assertEqual(len(failures), 1)
        self.assertIn("same_name(double)", failures[0])

    def test_generated_macro_counters_and_line_moves_do_not_change_identity(self):
        macros = ("#define JOIN_RAW(a,b) a##b\n#define JOIN(a,b) JOIN_RAW(a,b)\n"
                  "#define TEST_CASE(name) void JOIN(test_, __COUNTER__)()\n")
        existing = 'TEST_CASE("existing case") { int value = 0; ' + self.branches() + "}\n"
        before = self.inspect(macros + existing)
        after = self.inspect(macros + '\nTEST_CASE("inserted case") {}\n' + existing)
        self.assertEqual(len(before), 1)
        self.assertEqual(before, after)

    def test_below_threshold_callback_still_makes_a_repeated_context_ambiguous(self):
        header = "template<class F> int invoke(F callback) {return callback(0);}\n"
        over = "invoke([](int value) {" + self.branches() + " return value;});"
        self.assertTrue(self.inspect(header + "int run() {" + over + " return 0;}"))
        below = "invoke([](int value) {return value;});"
        with self.assertRaisesRegex(ValueError, "ambiguous lambda context"):
            self.inspect(header + "int run() {" + over + below + " return 0;}")


if __name__ == "__main__":
    unittest.main()
