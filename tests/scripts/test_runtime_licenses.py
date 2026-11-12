import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location("runtime_licenses", Path(__file__).parents[2] / "scripts/check_runtime_licenses.py")
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


def graph(*nodes):
    return {"graph": {"nodes": {"0": {"recipe": "Consumer", "context": "host"},
        **{str(index + 1): node for index, node in enumerate(nodes)}}}}


def package(license, **changes):
    return {"ref": "fixture/1.0", "context": "host", "license": license, **changes}


class RuntimeLicenseTests(unittest.TestCase):
    def test_transitive_runtime_dependency_is_checked(self):
        data = graph(package("MIT"), package("GPL-3.0-only", ref="transitive/1.0"))
        self.assertEqual(len(checker.findings(data)), 1)
        self.assertIn("transitive/1.0", checker.findings(data)[0])

    def test_lgpl_does_not_match_gpl(self):
        self.assertEqual(checker.findings(graph(package("LGPL-3.0-only"))), [])
        self.assertTrue(checker.findings(graph(package("AGPL-3.0-or-later"))))

    def test_build_and_test_tools_are_not_runtime(self):
        data = graph(package("GPL-3.0-only", context="build"), package("AGPL-3.0-only", test=True))
        self.assertEqual(checker.findings(data), [])

    def test_expression_branches_and_precedence(self):
        for expression, expected in [("GPL-3.0-only OR MIT", True),
                ("MIT AND GPL-3.0-only", False),
                ("(LGPL-3.0-only OR GPL-3.0-only) AND BSD-3-Clause", True),
                ("GPL-2.0-only WITH Classpath-exception-2.0", False)]:
            with self.subTest(expression=expression):
                self.assertEqual(checker.allowed_expression(expression), expected)

    def test_missing_and_malformed_metadata_fail(self):
        for expression in [None, [], "NOASSERTION", "MIT OR", "(MIT", "MIT GPL-3.0-only"]:
            with self.subTest(expression=expression):
                self.assertTrue(checker.findings(graph(package(expression))))
        with self.assertRaises(ValueError):
            checker.findings({"graph": {"nodes": {}}})

    def test_multiple_license_entries_are_all_checked(self):
        self.assertEqual(checker.findings(graph(package(["MIT", "BSD-3-Clause"]))), [])
        self.assertTrue(checker.findings(graph(package(["MIT", "GPL-3.0-only"]))))
