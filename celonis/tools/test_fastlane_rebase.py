#!/usr/bin/env python3

import argparse
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock


MODULE_PATH = Path(__file__).with_name("fastlane_rebase.py")
SPEC = importlib.util.spec_from_file_location("fastlane_rebase", MODULE_PATH)
assert SPEC and SPEC.loader
fastlane = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fastlane)


class MarkerTest(unittest.TestCase):
    def test_marker_numbers_only_accepts_positive_numbers(self):
        self.assertEqual([12, 99], fastlane.marker_numbers("[FASTLANE-12] x [FASTLANE-99]"))
        self.assertEqual([], fastlane.marker_numbers("[FASTLANE-0] [FASTLANE-x]"))

    def test_state_round_trip(self):
        state = {"role": "child", "run_id": "42", "entries": [{"upstream_pr": 123}]}
        self.assertEqual(state, fastlane.parse_state(f"prefix\n{fastlane.state_comment(state)}\nsuffix"))


class ManifestTest(unittest.TestCase):
    def test_manifest_checksum_round_trip_and_tamper_detection(self):
        manifest = {"schema_version": fastlane.SCHEMA_VERSION, "run_id": "7", "entries": []}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            fastlane.write_manifest(path, manifest)
            loaded = fastlane.load_manifest(path)
            self.assertEqual("7", loaded["run_id"])

            loaded["run_id"] = "8"
            path.write_text(json.dumps(loaded), encoding="utf-8")
            with self.assertRaisesRegex(fastlane.FastlaneError, "checksum mismatch"):
                fastlane.load_manifest(path)


class ChildStatusContextTest(unittest.TestCase):
    def test_abbreviates_base_sha_to_fit_github_limit(self):
        base_sha = "a" * 40
        manifest_sha256 = "b" * 64

        context = fastlane.child_status_context(base_sha, manifest_sha256)

        self.assertEqual(f"fastlane-child/{'a' * 12}/{manifest_sha256}", context)
        self.assertLessEqual(len(context), 100)


class ValidatePrTest(unittest.TestCase):
    def setUp(self):
        self.pr = {
            "title": "[BugFix] fix it [FASTLANE-123]",
            "base": {"ref": "branch-3.5-celo", "sha": "base-sha"},
        }
        self.commit = {"commit": {"message": "fix it [FASTLANE-123]\n\nbody"}}
        self.upstream = {
            "base": {"ref": "main"},
            "state": "open",
            "merged_at": None,
        }
        self.args = argparse.Namespace(
            repo="celonis/celostar-starrocks",
            upstream_repo="StarRocks/starrocks",
            pr_number=7,
            default_branch="branch-3.5-celo",
        )

    def _api(self, path, **kwargs):
        if path.endswith("/pulls/7"):
            return self.pr
        if path.endswith("/pulls/7/commits"):
            self.assertEqual({"per_page": "100", "page": "1"}, kwargs["fields"])
            return [self.commit]
        raise AssertionError(path)

    @mock.patch.object(fastlane, "git", return_value="ordinary commit")
    @mock.patch.object(fastlane, "gh_api_optional")
    @mock.patch.object(fastlane, "gh_api")
    def test_valid_reference(self, api, optional, _git):
        api.side_effect = self._api
        optional.return_value = self.upstream
        fastlane.validate_pr(self.args)

    @mock.patch.object(fastlane, "gh_api")
    def test_distributed_title_bypasses_normal_validation(self, api):
        pr = dict(self.pr)
        pr["title"] = "repair [FASTLANE-REBASE] generated"
        api.return_value = pr
        fastlane.validate_pr(self.args)
        api.assert_called_once()

    @mock.patch.object(fastlane, "gh_api")
    def test_commit_marker_without_title_marker_is_rejected(self, api):
        pr = dict(self.pr)
        pr["title"] = "[BugFix] ordinary pull request"
        api.side_effect = self._api
        self.pr = pr

        with self.assertRaisesRegex(fastlane.FastlaneError, "must each contain exactly one marker"):
            fastlane.validate_pr(self.args)

    @mock.patch.object(fastlane, "gh_api")
    def test_ordinary_pr_loads_commits_and_passes_without_markers(self, api):
        self.pr["title"] = "[BugFix] ordinary pull request"
        self.commit["commit"]["message"] = "ordinary commit\n\nbody"
        api.side_effect = self._api

        fastlane.validate_pr(self.args)

        self.assertEqual(2, api.call_count)

    @mock.patch.object(fastlane, "gh_api")
    def test_malformed_commit_marker_is_rejected(self, api):
        self.pr["title"] = "[BugFix] ordinary pull request"
        self.commit["commit"]["message"] = "broken [FASTLANE-x]\n\nbody"
        api.side_effect = self._api

        with self.assertRaisesRegex(fastlane.FastlaneError, "commit subject 1"):
            fastlane.validate_pr(self.args)

    @mock.patch.object(fastlane, "gh_api")
    def test_malformed_marker_is_rejected_alongside_valid_marker(self, api):
        self.pr["title"] += " [FASTLANE-x]"
        api.side_effect = self._api

        with self.assertRaisesRegex(fastlane.FastlaneError, "PR title"):
            fastlane.validate_pr(self.args)

    @mock.patch.object(fastlane, "gh_api")
    def test_loads_all_commit_pages_before_treating_pr_as_ordinary(self, api):
        self.pr["title"] = "[BugFix] ordinary pull request"
        first_page = [
            {"commit": {"message": f"ordinary commit {number}"}}
            for number in range(100)
        ]
        marker_commit = {"commit": {"message": "hidden marker [FASTLANE-123]"}}

        def paginated_api(path, **kwargs):
            if path.endswith("/pulls/7"):
                return self.pr
            if path.endswith("/pulls/7/commits"):
                return first_page if kwargs["fields"]["page"] == "1" else [marker_commit]
            raise AssertionError(path)

        api.side_effect = paginated_api

        with self.assertRaisesRegex(fastlane.FastlaneError, "exactly one commit; found 101"):
            fastlane.validate_pr(self.args)

    @mock.patch.object(fastlane, "gh_api_optional")
    @mock.patch.object(fastlane, "gh_api")
    def test_marker_must_match_title_and_commit(self, api, optional):
        api.side_effect = self._api
        optional.return_value = self.upstream
        self.commit["commit"]["message"] = "fix it [FASTLANE-124]"
        with self.assertRaisesRegex(fastlane.FastlaneError, "marker mismatch"):
            fastlane.validate_pr(self.args)

    @mock.patch.object(fastlane, "gh_api_optional")
    @mock.patch.object(fastlane, "gh_api")
    def test_upstream_target_must_be_supported(self, api, optional):
        api.side_effect = self._api
        optional.return_value = dict(self.upstream, base={"ref": "branch-4.0"})
        with self.assertRaisesRegex(fastlane.FastlaneError, "expected main or branch-3.5-cc"):
            fastlane.validate_pr(self.args)

    @mock.patch.object(fastlane, "gh_api_optional")
    @mock.patch.object(fastlane, "gh_api")
    def test_closed_unmerged_reference_is_rejected(self, api, optional):
        api.side_effect = self._api
        optional.return_value = dict(self.upstream, state="closed")
        with self.assertRaisesRegex(fastlane.FastlaneError, "closed without being merged"):
            fastlane.validate_pr(self.args)

    @mock.patch.object(fastlane, "git", return_value="old [FASTLANE-123]")
    @mock.patch.object(fastlane, "gh_api_optional")
    @mock.patch.object(fastlane, "gh_api")
    def test_repeated_reference_is_rejected(self, api, optional, _git):
        api.side_effect = self._api
        optional.return_value = self.upstream
        with self.assertRaisesRegex(fastlane.FastlaneError, "already referenced"):
            fastlane.validate_pr(self.args)


class AuthorResolutionTest(unittest.TestCase):
    def setUp(self):
        self.internal_pr = {"user": {"login": "pr-owner"}}
        self.commit = {
            "author": {"login": "commit-owner"},
            "commit": {"message": "subject"},
        }

    @mock.patch.object(fastlane, "has_write_permission")
    @mock.patch.object(fastlane, "gh_api")
    def test_prefers_writable_commit_author(self, api, writable):
        api.return_value = self.commit
        writable.side_effect = lambda _repo, login: login == "commit-owner"
        self.assertEqual(
            ("commit-owner", "commit-author"),
            fastlane._resolve_author("org/repo", "sha", self.internal_pr, "actor"),
        )

    @mock.patch.object(fastlane, "has_write_permission")
    @mock.patch.object(fastlane, "gh_api")
    def test_coauthor_uses_pr_creator(self, api, writable):
        commit = dict(self.commit)
        commit["commit"] = {"message": "subject\n\nCo-authored-by: Someone <s@example.com>"}
        api.return_value = commit
        writable.side_effect = lambda _repo, login: login in {"commit-owner", "pr-owner"}
        self.assertEqual(
            ("pr-owner", "internal-pr-author"),
            fastlane._resolve_author("org/repo", "sha", self.internal_pr, "actor"),
        )

    @mock.patch.object(fastlane, "has_write_permission")
    @mock.patch.object(fastlane, "gh_api")
    def test_falls_back_to_workflow_actor(self, api, writable):
        api.return_value = self.commit
        writable.side_effect = lambda _repo, login: login == "actor"
        self.assertEqual(
            ("actor", "workflow-actor"),
            fastlane._resolve_author("org/repo", "sha", self.internal_pr, "actor"),
        )


class InternalPrResolutionTest(unittest.TestCase):
    @mock.patch.object(fastlane, "gh_api_optional")
    @mock.patch.object(fastlane, "commit_subject", return_value="change [FASTLANE-123] (#77)")
    def test_prefers_durable_pr_suffix(self, _subject, optional):
        optional.return_value = {
            "number": 77,
            "title": "[BugFix] change [FASTLANE-123]",
            "merged_at": "now",
        }
        result = fastlane._associated_internal_pr("org/repo", "sha", 123)
        self.assertEqual(77, result["number"])

    @mock.patch.object(fastlane, "gh_api")
    @mock.patch.object(fastlane, "gh_api_optional", return_value=None)
    @mock.patch.object(fastlane, "commit_subject", return_value="change [FASTLANE-123]")
    def test_filters_generated_associated_prs(self, _subject, _optional, api):
        api.return_value = [
            {
                "number": 10,
                "title": "[FASTLANE-REBASE] central - user",
                "merged_at": "now",
            },
            {
                "number": 77,
                "title": "[BugFix] change [FASTLANE-123]",
                "merged_at": "now",
            },
        ]
        result = fastlane._associated_internal_pr("org/repo", "sha", 123)
        self.assertEqual(77, result["number"])


class ClassificationTest(unittest.TestCase):
    @mock.patch.object(fastlane, "is_ancestor", return_value=True)
    @mock.patch.object(fastlane, "gh_api")
    def test_backport_uses_exact_original_pr_reference(self, api, ancestor):
        api.side_effect = [
            {"items": [{"number": 4}]},
            {
                "number": 4,
                "title": "change (backport #123)",
                "body": "No source commit SHA is required.",
                "base": {"ref": "branch-3.5-cc"},
                "merged_at": "now",
                "merge_commit_sha": "backport-sha",
            },
        ]

        result = fastlane._merged_backport("org/upstream", 123, "new-head")

        self.assertEqual(4, result["number"])
        ancestor.assert_called_once_with("backport-sha", "new-head")

    @mock.patch.object(fastlane, "is_ancestor")
    @mock.patch.object(fastlane, "gh_api")
    def test_backport_rejects_partial_pr_reference(self, api, ancestor):
        api.side_effect = [
            {"items": [{"number": 4}]},
            {
                "number": 4,
                "title": "change (backport #1234)",
                "body": "Not a backport of the requested PR.",
                "base": {"ref": "branch-3.5-cc"},
                "merged_at": "now",
                "merge_commit_sha": "backport-sha",
            },
        ]

        self.assertIsNone(fastlane._merged_backport("org/upstream", 123, "new-head"))
        ancestor.assert_not_called()

    @mock.patch.object(fastlane, "is_ancestor")
    @mock.patch.object(fastlane, "gh_api")
    def test_backport_requires_merge_commit_for_containment(self, api, ancestor):
        api.side_effect = [
            {"items": [{"number": 4}]},
            {
                "number": 4,
                "title": "change",
                "body": "Automatic backport of pull request #123.",
                "base": {"ref": "branch-3.5-cc"},
                "merged_at": "now",
                "merge_commit_sha": None,
            },
        ]

        self.assertIsNone(fastlane._merged_backport("org/upstream", 123, "new-head"))
        ancestor.assert_not_called()

    @mock.patch.object(fastlane, "gh_api")
    def test_open_pr_is_pending(self, api):
        api.return_value = {
            "html_url": "https://example/pr/1",
            "title": "open",
            "base": {"ref": "branch-3.5-cc"},
            "state": "open",
            "merged_at": None,
        }
        result = fastlane._classify_upstream("org/upstream", 1, "new", "internal")
        self.assertEqual("pending", result["classification"])

    @mock.patch.object(fastlane, "gh_api")
    def test_closed_unmerged_is_pending_with_warning(self, api):
        api.return_value = {
            "html_url": "https://example/pr/1",
            "title": "closed",
            "base": {"ref": "main"},
            "state": "closed",
            "merged_at": None,
        }
        result = fastlane._classify_upstream("org/upstream", 1, "new", "internal")
        self.assertEqual("pending", result["classification"])
        self.assertIn("closed unmerged", result["warning"])

    @mock.patch.object(fastlane, "patch_id", side_effect=["internal", "upstream"])
    @mock.patch.object(fastlane, "is_ancestor", return_value=True)
    @mock.patch.object(fastlane, "gh_api")
    def test_direct_merged_branch_pr_is_upstreamed_and_delta_reported(self, api, _ancestor, _patch):
        api.return_value = {
            "number": 2,
            "html_url": "https://example/pr/2",
            "title": "merged",
            "base": {"ref": "branch-3.5-cc"},
            "state": "closed",
            "merged_at": "now",
            "merge_commit_sha": "upstream-sha",
        }
        result = fastlane._classify_upstream("org/upstream", 2, "new", "internal")
        self.assertEqual("upstreamed", result["classification"])
        self.assertTrue(result["patch_differs"])

    @mock.patch.object(fastlane, "is_ancestor", return_value=False)
    @mock.patch.object(fastlane, "gh_api")
    def test_direct_merge_missing_from_exact_head_is_pending(self, api, _ancestor):
        api.return_value = {
            "number": 2,
            "html_url": "https://example/pr/2",
            "title": "merged elsewhere",
            "base": {"ref": "branch-3.5-cc"},
            "state": "closed",
            "merged_at": "now",
            "merge_commit_sha": "upstream-sha",
        }
        result = fastlane._classify_upstream("org/upstream", 2, "new", "internal")
        self.assertEqual("pending", result["classification"])

    @mock.patch.object(fastlane, "patch_id", side_effect=["same", "same"])
    @mock.patch.object(fastlane, "_merged_backport")
    @mock.patch.object(fastlane, "gh_api")
    def test_main_pr_with_verified_backport_is_upstreamed(self, api, backport, _patch):
        api.return_value = {
            "number": 3,
            "html_url": "https://example/pr/3",
            "title": "merged to main",
            "base": {"ref": "main"},
            "state": "closed",
            "merged_at": "now",
            "merge_commit_sha": "main-sha",
        }
        backport.return_value = {"number": 4, "merge_commit_sha": "backport-sha"}
        result = fastlane._classify_upstream("org/upstream", 3, "new", "internal")
        self.assertEqual("upstreamed", result["classification"])
        self.assertFalse(result["patch_differs"])
        backport.assert_called_once_with("org/upstream", 3, "new")


class SequenceEditorTest(unittest.TestCase):
    def test_drops_all_present_fastlanes_and_requires_pending(self):
        manifest = {
            "entries": [
                {"sha": "a" * 40, "upstream_pr": {"classification": "pending"}},
                {"sha": "b" * 40, "upstream_pr": {"classification": "upstreamed"}},
            ]
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            editor = fastlane._write_sequence_editor(manifest, root)
            todo = root / "todo"
            todo.write_text(f"pick {'a' * 12} pending\npick {'b' * 12} upstreamed\npick {'c' * 12} normal\n")
            result = fastlane._run((str(editor), str(todo)), check=False)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertEqual(
                f"drop {'a' * 12} pending\ndrop {'b' * 12} upstreamed\npick {'c' * 12} normal\n",
                todo.read_text(),
            )

    def test_missing_pending_commit_fails(self):
        manifest = {
            "entries": [{"sha": "a" * 40, "upstream_pr": {"classification": "pending"}}]
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            editor = fastlane._write_sequence_editor(manifest, root)
            todo = root / "todo"
            todo.write_text(f"pick {'c' * 12} normal\n")
            result = fastlane._run((str(editor), str(todo)), check=False)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("missing from rebase todo", result.stderr)


class RebaseValidationTest(unittest.TestCase):
    def setUp(self):
        self.manifest = {
            "new_head": "new-head",
            "old_upstream_base": "old-base",
            "old_head": "old-head",
        }

    @mock.patch.object(fastlane, "_run")
    @mock.patch.object(fastlane, "is_ancestor", return_value=True)
    @mock.patch.object(fastlane, "git")
    def test_allows_changed_non_fastlane_subjects(self, git_mock, _ancestor, run):
        git_mock.side_effect = ["candidate-sha", " renamed ordinary commit"]
        run.return_value = mock.Mock(stdout="range diff", stderr="")

        report = fastlane.validate_rebase_data(self.manifest, "candidate")

        self.assertEqual("range diff", report)

    @mock.patch.object(fastlane, "is_ancestor", return_value=True)
    @mock.patch.object(fastlane, "git")
    def test_rejects_remaining_fastlane_markers(self, git_mock, _ancestor):
        git_mock.side_effect = ["candidate-sha", "change [FASTLANE-123]"]

        with self.assertRaisesRegex(fastlane.FastlaneError, "still contains fastlane markers"):
            fastlane.validate_rebase_data(self.manifest, "candidate")


class ChildStateTest(unittest.TestCase):
    def setUp(self):
        self.manifest = {
            "run_id": "42",
            "manifest_sha256": "manifest-hash",
            "central_branch": "fastlane-rebase/42/central",
            "entries": [
                {
                    "sha": "assigned-sha",
                    "author": "alice",
                    "upstream_pr": {"number": 123, "classification": "pending"},
                },
                {
                    "sha": "other-sha",
                    "author": "bob",
                    "upstream_pr": {"number": 124, "classification": "pending"},
                },
            ],
        }
        self.state = {
            "role": "child",
            "run_id": "42",
            "manifest_sha256": "manifest-hash",
            "central_branch": "fastlane-rebase/42/central",
            "author": "alice",
            "parent_pr": 7,
            "bootstrap_sha": "a" * 40,
            "entries": [{"sha": "assigned-sha", "upstream_pr": 123}],
        }
        self.pr = {
            "title": "[FASTLANE-REBASE] fastlane-rebase/42/central - alice",
            "head": {"ref": "fastlane-rebase/42/author/alice"},
            "base": {"ref": "fastlane-rebase/42/central"},
        }

    def test_accepts_assignments_derived_from_manifest(self):
        fastlane._validate_child_state(self.manifest, self.pr, self.state)

    def test_rejects_mutable_assignment_changes(self):
        self.state["entries"] = [{"sha": "other-sha", "upstream_pr": 124}]
        with self.assertRaisesRegex(fastlane.FastlaneError, "immutable manifest"):
            fastlane._validate_child_state(self.manifest, self.pr, self.state)


class RestoreCommandTest(unittest.TestCase):
    def test_scopes_gh_commands_and_resolves_the_explicit_child_branch(self):
        command = fastlane._restore_command(
            "42", "celonis/celostar-starrocks", "fastlane-rebase/42/author/alice"
        )

        self.assertTrue(
            command.startswith(
                "gh pr checkout fastlane-rebase/42/author/alice "
                "--repo celonis/celostar-starrocks\n"
            )
        )
        self.assertIn(
            "gh run download 42 --repo celonis/celostar-starrocks", command
        )
        self.assertIn(
            "gh pr view fastlane-rebase/42/author/alice "
            "--repo celonis/celostar-starrocks",
            command,
        )


class DistributionTest(unittest.TestCase):
    @mock.patch.object(fastlane, "write_output")
    @mock.patch.object(fastlane, "gh_api")
    @mock.patch.object(fastlane, "_find_pr")
    @mock.patch.object(fastlane, "_create_or_reuse_pr")
    @mock.patch.object(fastlane, "git")
    @mock.patch.object(fastlane, "load_manifest")
    def test_only_reuses_open_child_prs(self, load, git_mock, create, find, _api, _output):
        manifest = {
            "repo": "org/repo",
            "run_id": "42",
            "manifest_sha256": "manifest-hash",
            "central_branch": "fastlane-rebase/42/central",
            "base_branch": "branch-3.5-cc-run-celo",
            "new_head": "n" * 40,
            "entries": [
                {
                    "sha": "assigned-sha",
                    "author": "alice",
                    "internal_pr": {"title": "change", "url": "https://example/internal"},
                    "upstream_pr": {
                        "number": 123,
                        "title": "upstream change",
                        "url": "https://example/upstream",
                        "classification": "pending",
                        "evidence": "still pending",
                    },
                }
            ],
        }
        load.return_value = manifest
        git_mock.side_effect = lambda *args, **_kwargs: {
            ("status", "--porcelain"): "",
            (
                "ls-remote",
                "--heads",
                "origin",
                manifest["base_branch"],
            ): f"{manifest['new_head']}\trefs/heads/{manifest['base_branch']}",
            ("rev-parse", manifest["central_branch"]): "c" * 40,
            ("branch", "--show-current"): "main",
            ("switch", "main"): "",
        }[args]
        create.side_effect = lambda _repo, **kwargs: {"number": 7, "body": kwargs["body"]}
        child_state = {
            "role": "child",
            "run_id": "42",
            "manifest_sha256": "manifest-hash",
            "central_branch": manifest["central_branch"],
            "author": "alice",
            "parent_pr": 7,
            "bootstrap_sha": "a" * 40,
            "entries": [{"sha": "assigned-sha", "upstream_pr": 123}],
        }
        find.return_value = {
            "number": 8,
            "title": f"[FASTLANE-REBASE] {manifest['central_branch']} - alice",
            "body": fastlane.state_comment(child_state),
            "head": {"ref": "fastlane-rebase/42/author/alice"},
            "base": {"ref": manifest["central_branch"]},
        }
        args = argparse.Namespace(
            manifest="manifest.json",
            central_branch=None,
            base_branch="branch-3.5-cc-run-celo",
        )

        fastlane.distribute(args)

        find.assert_called_once_with(
            "org/repo",
            "fastlane-rebase/42/author/alice",
            manifest["central_branch"],
            state="open",
        )

    @mock.patch.object(fastlane, "load_manifest")
    def test_rejects_base_branch_that_differs_from_manifest(self, load):
        load.return_value = {
            "repo": "org/repo",
            "central_branch": "fastlane-rebase/42/central",
            "base_branch": "recorded-base",
        }

        with self.assertRaisesRegex(fastlane.FastlaneError, "immutable manifest"):
            fastlane.distribute(
                argparse.Namespace(
                    manifest="manifest.json",
                    central_branch=None,
                    base_branch="different-base",
                )
            )

    @mock.patch.object(fastlane, "git")
    @mock.patch.object(fastlane, "load_manifest")
    def test_rejects_moved_or_missing_snapshot_base_before_creating_prs(self, load, git_mock):
        manifest = {
            "repo": "org/repo",
            "run_id": "42",
            "central_branch": "fastlane-rebase/42/central",
            "base_branch": "recorded-base",
            "new_head": "n" * 40,
            "entries": [
                {
                    "upstream_pr": {"classification": "pending"},
                }
            ],
        }
        load.return_value = manifest
        for remote_base in (f"{'m' * 40}\trefs/heads/recorded-base", ""):
            with self.subTest(remote_base=remote_base or "missing"):
                git_mock.reset_mock()
                git_mock.side_effect = ["", remote_base]
                with self.assertRaisesRegex(fastlane.FastlaneError, "Snapshot base branch.*moved"):
                    fastlane.distribute(
                        argparse.Namespace(
                            manifest="manifest.json",
                            central_branch=None,
                            base_branch="recorded-base",
                        )
                    )

                git_mock.assert_has_calls(
                    [
                        mock.call("status", "--porcelain"),
                        mock.call("ls-remote", "--heads", "origin", "recorded-base"),
                    ]
                )


class RunPrDiscoveryTest(unittest.TestCase):
    @mock.patch.object(fastlane, "gh_api")
    def test_status_discovery_paginates_all_pull_requests(self, api):
        first_page = [{"number": number, "body": ""} for number in range(100)]
        matching = {
            "number": 101,
            "body": fastlane.state_comment({"role": "parent", "run_id": "42"}),
        }
        api.side_effect = [first_page, [matching]]

        self.assertEqual([matching], fastlane._run_prs("org/repo", "42"))
        self.assertEqual("1", api.call_args_list[0].kwargs["fields"]["page"])
        self.assertEqual("2", api.call_args_list[1].kwargs["fields"]["page"])

    @mock.patch.object(fastlane, "gh_api")
    def test_parent_discovery_filters_by_deterministic_central_head(self, api):
        manifest = {
            "run_id": "42",
            "central_branch": "fastlane-rebase/42/central",
        }
        parent = {
            "number": 7,
            "body": fastlane.state_comment({"role": "parent", "run_id": "42"}),
        }
        full_parent = dict(parent, title="parent")
        api.side_effect = [[parent], full_parent]

        self.assertEqual(full_parent, fastlane._find_parent_pr("org/repo", manifest))
        self.assertEqual(
            "org:fastlane-rebase/42/central",
            api.call_args_list[0].kwargs["fields"]["head"],
        )
        api.assert_has_calls(
            [
                mock.call(
                    "repos/org/repo/pulls",
                    fields={
                        "state": "all",
                        "head": "org:fastlane-rebase/42/central",
                        "per_page": "100",
                    },
                ),
                mock.call("repos/org/repo/pulls/7"),
            ]
        )


class FinalValidationTest(unittest.TestCase):
    def setUp(self):
        self.manifest = {
            "schema_version": fastlane.SCHEMA_VERSION,
            "manifest_sha256": "manifest-hash",
            "run_id": "42",
            "old_head": "old-head",
            "old_default_branch": "branch-3.5-celo",
            "new_head": "new-head",
            "central_branch": "fastlane-rebase/42/central",
            "base_branch": "branch-3.5-cc-12345678-celo",
            "entries": [
                {
                    "sha": "original",
                    "author": "alice",
                    "upstream_pr": {"number": 123, "classification": "pending"},
                }
            ],
        }
        self.parent_state = {
            "role": "parent",
            "run_id": "42",
            "manifest_sha256": "manifest-hash",
            "central_branch": "fastlane-rebase/42/central",
            "base_branch": self.manifest["base_branch"],
            "clean_head": "b" * 40,
            "children": [8],
        }
        self.parent = {
            "number": 7,
            "body": fastlane.state_comment(self.parent_state),
            "head": {"ref": self.manifest["central_branch"]},
            "base": {
                "ref": self.parent_state["base_branch"],
                "sha": self.manifest["new_head"],
            },
        }
        self.child_state = {
            "role": "child",
            "run_id": "42",
            "manifest_sha256": "manifest-hash",
            "parent_pr": 7,
            "central_branch": "fastlane-rebase/42/central",
            "author": "alice",
            "bootstrap_sha": "a" * 40,
            "entries": [{"sha": "original", "upstream_pr": 123}],
        }
        self.args = argparse.Namespace(manifest="manifest.json", repo="org/repo")

    def _api(self, path, **_kwargs):
        if path.endswith("/pulls/7"):
            return self.parent
        if path.endswith("/pulls/8"):
            return {
                "number": 8,
                "merged_at": "now",
                "title": "[FASTLANE-REBASE] fastlane-rebase/42/central - alice",
                "body": fastlane.state_comment(self.child_state),
                "head": {"ref": "fastlane-rebase/42/author/alice"},
                "base": {"ref": "fastlane-rebase/42/central"},
            }
        raise AssertionError(path)

    def _git(self, *args, **_kwargs):
        if args[:2] == ("fetch", "origin"):
            return ""
        if args[:2] == ("rev-parse", "origin/branch-3.5-celo"):
            return "old-head"
        if args[:2] == ("rev-parse", "origin/fastlane-rebase/42/central"):
            return "central-sha"
        if args[:2] == ("log", "--reverse"):
            if args[-1] == f"{'b' * 40}..central-sha":
                return "fix-sha\tpost-CI fix\nfastlane-sha\tchange [FASTLANE-123]"
            return "fix-sha\tpost-CI fix\nfastlane-sha\tchange [FASTLANE-123]"
        raise AssertionError((args, kwargs))

    def _assert_parent_rejected(self, message):
        with (
            mock.patch.object(fastlane, "load_manifest", return_value=self.manifest),
            mock.patch.object(fastlane, "_find_parent_pr", return_value=self.parent),
        ):
            with self.assertRaisesRegex(fastlane.FastlaneError, message):
                fastlane.validate_final(self.args)

    def test_rejects_parent_with_wrong_head_branch(self):
        self.parent["head"]["ref"] = "other-central"
        self._assert_parent_rejected("head branch")

    def test_rejects_retargeted_parent(self):
        self.parent["base"]["ref"] = "other-base"
        self._assert_parent_rejected("recorded snapshot base branch")

    def test_rejects_force_pushed_snapshot_base(self):
        self.parent["base"]["sha"] = "different-base-sha"
        self._assert_parent_rejected("moved from the snapshotted upstream head")

    @mock.patch.object(fastlane, "write_output")
    @mock.patch.object(fastlane, "is_ancestor", return_value=True)
    @mock.patch.object(fastlane, "git")
    @mock.patch.object(fastlane, "gh_api")
    @mock.patch.object(fastlane, "_find_parent_pr")
    @mock.patch.object(fastlane, "load_manifest")
    def test_accepts_expected_marker_and_extra_non_fastlane_fix(
        self, load, find_parent, api, git_mock, _ancestor, output
    ):
        load.return_value = self.manifest
        find_parent.return_value = self.parent
        api.side_effect = self._api
        git_mock.side_effect = self._git
        fastlane.validate_final(self.args)
        output.assert_any_call("parent_pr", 7)
        output.assert_any_call("central_branch", "fastlane-rebase/42/central")
        output.assert_any_call("extra_commit_count", 1)

    @mock.patch.object(fastlane, "write_output")
    @mock.patch.object(
        fastlane, "_validate_old_default_commits", return_value=[789]
    )
    @mock.patch.object(fastlane, "is_ancestor", return_value=True)
    @mock.patch.object(fastlane, "git")
    @mock.patch.object(fastlane, "gh_api")
    @mock.patch.object(fastlane, "_find_parent_pr")
    @mock.patch.object(fastlane, "load_manifest")
    def test_accepts_fastlane_marker_from_post_snapshot_commit(
        self,
        load,
        find_parent,
        api,
        git_mock,
        _ancestor,
        validate_old_default,
        output,
    ):
        load.return_value = self.manifest
        find_parent.return_value = self.parent
        api.side_effect = self._api

        def git_with_new_fastlane(*args, **_kwargs):
            if args[:2] == ("fetch", "origin"):
                return ""
            if args[:2] == ("rev-parse", "origin/fastlane-rebase/42/central"):
                return "central-sha"
            if args[:2] == ("log", "--reverse"):
                return (
                    "fastlane-sha\tchange [FASTLANE-123]\n"
                    "new-fastlane-sha\tnew change [FASTLANE-789]"
                )
            raise AssertionError((args, _kwargs))

        git_mock.side_effect = git_with_new_fastlane

        fastlane.validate_final(self.args)

        validate_old_default.assert_called_once_with(
            "org/repo", self.manifest, 7, "central-sha"
        )
        output.assert_any_call("parent_pr", 7)

    @mock.patch.object(
        fastlane, "_validate_old_default_commits", return_value=[]
    )
    @mock.patch.object(fastlane, "is_ancestor", return_value=True)
    @mock.patch.object(fastlane, "git")
    @mock.patch.object(fastlane, "gh_api")
    @mock.patch.object(fastlane, "_find_parent_pr")
    @mock.patch.object(fastlane, "load_manifest")
    def test_rejects_marker_not_in_manifest_or_post_snapshot_commits(
        self,
        load,
        find_parent,
        api,
        git_mock,
        _ancestor,
        _validate_old_default,
    ):
        load.return_value = self.manifest
        find_parent.return_value = self.parent
        api.side_effect = self._api

        def git_with_unexpected_fastlane(*args, **_kwargs):
            if args[:2] == ("fetch", "origin"):
                return ""
            if args[:2] == ("rev-parse", "origin/fastlane-rebase/42/central"):
                return "central-sha"
            if args[:2] == ("log", "--reverse"):
                return (
                    "fastlane-sha\tchange [FASTLANE-123]\n"
                    "unexpected-sha\tunexpected [FASTLANE-789]"
                )
            raise AssertionError((args, _kwargs))

        git_mock.side_effect = git_with_unexpected_fastlane

        with self.assertRaisesRegex(fastlane.FastlaneError, "markers differ"):
            fastlane.validate_final(self.args)

    @mock.patch.object(fastlane, "is_ancestor", return_value=True)
    @mock.patch.object(fastlane, "git")
    @mock.patch.object(fastlane, "gh_api")
    @mock.patch.object(fastlane, "_find_parent_pr")
    @mock.patch.object(fastlane, "load_manifest")
    def test_rejects_combined_markers(self, load, find_parent, api, git_mock, _ancestor):
        load.return_value = self.manifest
        find_parent.return_value = self.parent
        api.side_effect = self._api

        def combined_git(*args, **kwargs):
            if args[:2] == ("log", "--reverse"):
                return "bad\tcombined [FASTLANE-123] [FASTLANE-124]"
            return self._git(*args, **kwargs)

        git_mock.side_effect = combined_git
        with self.assertRaisesRegex(fastlane.FastlaneError, "combines multiple"):
            fastlane.validate_final(self.args)


class OldDefaultValidationTest(unittest.TestCase):
    def setUp(self):
        self.manifest = {
            "run_id": "42",
            "old_head": "old-head",
            "old_default_branch": "branch-3.5-celo",
            "new_head": "new-head",
            "upstream_repo": "org/upstream",
            "central_branch": "fastlane-rebase/42/central",
        }

    @mock.patch.object(fastlane, "gh_api")
    @mock.patch.object(fastlane, "is_ancestor")
    @mock.patch.object(fastlane, "git", return_value="old-head")
    @mock.patch.object(fastlane, "fetch_origin_branch")
    def test_unchanged_old_default_passes_without_comment(
        self, fetch, _git, ancestor, api
    ):
        markers = fastlane._validate_old_default_commits(
            "org/repo", self.manifest, 7, "central-sha"
        )

        self.assertEqual([], markers)
        fetch.assert_called_once_with("branch-3.5-celo")
        ancestor.assert_not_called()
        api.assert_not_called()

    @mock.patch.object(fastlane, "gh_api")
    @mock.patch.object(fastlane, "_classify_upstream")
    @mock.patch.object(fastlane, "_log_commits")
    @mock.patch.object(fastlane, "is_ancestor", return_value=True)
    @mock.patch.object(fastlane, "git", return_value="current-old-head")
    @mock.patch.object(fastlane, "fetch_origin_branch")
    def test_advanced_old_default_passes_when_subjects_are_present(
        self, _fetch, _git, _ancestor, log_commits, classify, api
    ):
        classify.return_value = {
            "classification": "pending",
            "evidence": "The upstream PR is still open",
        }
        log_commits.side_effect = [
            [
                ("source-a", "first change"),
                ("source-b", "second change [FASTLANE-789]"),
            ],
            [
                ("central-a", "second change [FASTLANE-789]"),
                ("central-b", "first change"),
            ],
        ]

        markers = fastlane._validate_old_default_commits(
            "org/repo", self.manifest, 7, "central-sha"
        )

        self.assertEqual([789], markers)
        classify.assert_called_once_with(
            "org/upstream", 789, "new-head", "source-b"
        )
        api.assert_not_called()

    @mock.patch.object(fastlane, "gh_api")
    @mock.patch.object(fastlane, "_classify_upstream")
    @mock.patch.object(fastlane, "_log_commits")
    @mock.patch.object(fastlane, "is_ancestor", return_value=True)
    @mock.patch.object(fastlane, "git", return_value="current-old-head")
    @mock.patch.object(fastlane, "fetch_origin_branch")
    def test_upstreamed_post_snapshot_fastlane_does_not_require_cherry_pick(
        self, _fetch, _git, _ancestor, log_commits, classify, api
    ):
        classify.return_value = {
            "classification": "upstreamed",
            "evidence": "Accepted commit upstream-sha is contained in the recorded upstream head",
        }
        log_commits.side_effect = [
            [("source-a", "already upstream [FASTLANE-789]")],
            [],
        ]

        with tempfile.TemporaryDirectory() as directory:
            summary_path = Path(directory) / "summary.md"
            with mock.patch.dict(
                "os.environ", {"GITHUB_STEP_SUMMARY": str(summary_path)}
            ):
                markers = fastlane._validate_old_default_commits(
                    "org/repo", self.manifest, 7, "central-sha"
                )

            summary = summary_path.read_text(encoding="utf-8")

        self.assertEqual([], markers)
        self.assertIn("Post-snapshot fastlanes already upstreamed", summary)
        self.assertIn("source-a", summary)
        classify.assert_called_once_with(
            "org/upstream", 789, "new-head", "source-a"
        )
        api.assert_not_called()

    @mock.patch.object(fastlane, "gh_api")
    @mock.patch.object(fastlane, "_classify_upstream")
    @mock.patch.object(fastlane, "_log_commits")
    @mock.patch.object(fastlane, "is_ancestor", return_value=True)
    @mock.patch.object(fastlane, "git", return_value="current-old-head")
    @mock.patch.object(fastlane, "fetch_origin_branch")
    def test_missing_duplicate_subject_is_reported_with_cherry_pick_instructions(
        self, _fetch, _git, _ancestor, log_commits, classify, api
    ):
        classify.return_value = {
            "classification": "pending",
            "evidence": "The upstream PR is still open",
        }
        log_commits.side_effect = [
            [
                ("source-a", "same subject [FASTLANE-789]"),
                ("source-b", "same subject [FASTLANE-789]"),
            ],
            [("central-a", "same subject [FASTLANE-789]")],
        ]

        with self.assertRaisesRegex(fastlane.FastlaneError, "1 commit.*missing"):
            fastlane._validate_old_default_commits(
                "org/repo", self.manifest, 7, "central-sha"
            )

        api.assert_called_once()
        path = api.call_args.args[0]
        body = api.call_args.kwargs["payload"]["body"]
        self.assertEqual("repos/org/repo/issues/7/comments", path)
        self.assertNotIn("`source-a` same subject [FASTLANE-789]", body)
        self.assertIn("`source-b` same subject [FASTLANE-789]", body)
        self.assertIn("git cherry-pick", body)
        self.assertIn("source-b", body)
        self.assertIn("-f mode=finalize -f run_id=42", body)

    @mock.patch.object(fastlane, "gh_api")
    @mock.patch.object(fastlane, "is_ancestor", return_value=False)
    @mock.patch.object(fastlane, "git", return_value="diverged-head")
    @mock.patch.object(fastlane, "fetch_origin_branch")
    def test_diverged_old_default_is_reported(
        self, _fetch, _git, _ancestor, api
    ):
        with self.assertRaisesRegex(fastlane.FastlaneError, "diverged"):
            fastlane._validate_old_default_commits(
                "org/repo", self.manifest, 7, "central-sha"
            )

        body = api.call_args.kwargs["payload"]["body"]
        self.assertIn("Old default branch diverged", body)
        self.assertIn("start a new fastlane rebase", body)


class CleanOldDefaultValidationTest(unittest.TestCase):
    def setUp(self):
        self.manifest = {
            "old_head": "old-head",
            "old_default_branch": "branch-3.5-celo",
        }
        self.args = argparse.Namespace(manifest="manifest.json", repo="org/repo")

    @mock.patch.object(fastlane, "is_ancestor")
    @mock.patch.object(fastlane, "git", return_value="old-head")
    @mock.patch.object(fastlane, "fetch_origin_branch")
    @mock.patch.object(fastlane, "load_manifest")
    def test_unchanged_snapshot_passes(self, load, fetch, _git, ancestor):
        load.return_value = self.manifest

        fastlane.validate_old_default_unchanged(self.args)

        fetch.assert_called_once_with("branch-3.5-celo")
        ancestor.assert_not_called()

    @mock.patch.object(fastlane, "_log_commits")
    @mock.patch.object(fastlane, "is_ancestor", return_value=True)
    @mock.patch.object(fastlane, "git", return_value="current-head")
    @mock.patch.object(fastlane, "fetch_origin_branch")
    @mock.patch.object(fastlane, "load_manifest")
    def test_append_only_advance_lists_commits_and_restart_command(
        self, load, _fetch, _git, _ancestor, log_commits
    ):
        load.return_value = self.manifest
        log_commits.return_value = [
            ("first-sha", "first change"),
            ("second-sha", "second change"),
        ]

        with tempfile.TemporaryDirectory() as directory:
            summary_path = Path(directory) / "summary.md"
            with mock.patch.dict(
                "os.environ", {"GITHUB_STEP_SUMMARY": str(summary_path)}
            ):
                with self.assertRaisesRegex(fastlane.FastlaneError, "start a new rebase"):
                    fastlane.validate_old_default_unchanged(self.args)
            summary = summary_path.read_text(encoding="utf-8")

        self.assertLess(summary.index("first-sha"), summary.index("second-sha"))
        self.assertIn("first change", summary)
        self.assertIn("second change", summary)
        self.assertIn("--repo org/repo -f mode=start", summary)

    @mock.patch.object(fastlane, "_log_commits")
    @mock.patch.object(fastlane, "is_ancestor", return_value=False)
    @mock.patch.object(fastlane, "git", return_value="diverged-head")
    @mock.patch.object(fastlane, "fetch_origin_branch")
    @mock.patch.object(fastlane, "load_manifest")
    def test_diverged_snapshot_requires_restart(
        self, load, _fetch, _git, _ancestor, log_commits
    ):
        load.return_value = self.manifest

        with self.assertRaisesRegex(fastlane.FastlaneError, "start a new rebase"):
            fastlane.validate_old_default_unchanged(self.args)

        log_commits.assert_not_called()

    def test_command_is_registered(self):
        args = fastlane.build_parser().parse_args(
            [
                "validate-old-default-unchanged",
                "--manifest",
                "manifest.json",
                "--repo",
                "org/repo",
            ]
        )

        self.assertIs(fastlane.validate_old_default_unchanged, args.func)


if __name__ == "__main__":
    unittest.main()
