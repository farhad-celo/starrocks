#!/usr/bin/env python3
"""Fastlane-aware StarRocks fork rebase tooling.

The commands in this module are intentionally usable both from GitHub Actions
and from a developer checkout.  Git is the source of truth for commit content;
GitHub is used only for PR metadata and orchestration.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Dict, List, Mapping, Optional, Sequence, Tuple


SCHEMA_VERSION = 1
STATUS_BASE_SHA_LENGTH = 12
FASTLANE_RE = re.compile(r"\[FASTLANE-([1-9][0-9]*)\]")
FASTLANE_PREFIX = "[FASTLANE-"
REBASE_TOKEN = "[FASTLANE-REBASE]"
COAUTHOR_RE = re.compile(r"^Co-authored-by:\s*", re.IGNORECASE | re.MULTILINE)
INTERNAL_PR_RE = re.compile(r"\(#([1-9][0-9]*)\)\s*$")
STATE_RE = re.compile(r"<!--\s*fastlane-rebase-state\s+(\{.*?\})\s*-->", re.DOTALL)
WRITABLE_PERMISSIONS = {"admin", "maintain", "write"}
ALLOWED_UPSTREAM_BASES = {"main", "branch-3.5-cc"}


class FastlaneError(RuntimeError):
    """An expected, user-actionable fastlane error."""


def _run(
    args: Sequence[str],
    *,
    check: bool = True,
    input_text: Optional[str] = None,
    env: Optional[Mapping[str, str]] = None,
) -> subprocess.CompletedProcess[str]:
    process_env = os.environ.copy()
    if env:
        process_env.update(env)
    result = subprocess.run(
        list(args),
        check=False,
        input=input_text,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=process_env,
    )
    if check and result.returncode:
        detail = result.stderr.strip() or result.stdout.strip()
        raise FastlaneError(f"Command failed ({shlex.join(args)}): {detail}")
    return result


def git(*args: str, check: bool = True) -> str:
    return _run(("git",) + args, check=check).stdout.strip()


def fetch_origin_branch(branch: str) -> None:
    git(
        "fetch",
        "origin",
        f"+refs/heads/{branch}:refs/remotes/origin/{branch}",
    )


def gh_api(
    path: str,
    *,
    method: str = "GET",
    payload: Optional[Mapping[str, Any]] = None,
    fields: Optional[Mapping[str, str]] = None,
) -> Any:
    args: List[str] = ["gh", "api", "--method", method, path]
    if fields:
        for key, value in fields.items():
            args.extend(("-f", f"{key}={value}"))
    input_text = None
    if payload is not None:
        args.extend(("--input", "-"))
        input_text = json.dumps(payload)
    result = _run(args, input_text=input_text)
    if not result.stdout.strip():
        return None
    return json.loads(result.stdout)


def gh_api_optional(path: str) -> Optional[Any]:
    result = _run(("gh", "api", "--method", "GET", path), check=False)
    if result.returncode:
        return None
    return json.loads(result.stdout) if result.stdout.strip() else None


def _canonical_json(value: Mapping[str, Any]) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def _manifest_hash(manifest: Mapping[str, Any]) -> str:
    unsigned = dict(manifest)
    unsigned.pop("manifest_sha256", None)
    return hashlib.sha256(_canonical_json(unsigned).encode("utf-8")).hexdigest()


def child_status_context(base_sha: str, manifest_sha256: str) -> str:
    return f"fastlane-child/{base_sha[:STATUS_BASE_SHA_LENGTH]}/{manifest_sha256}"


def write_manifest(path: Path, manifest: Dict[str, Any]) -> None:
    manifest["manifest_sha256"] = _manifest_hash(manifest)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def load_manifest(path: Path) -> Dict[str, Any]:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != SCHEMA_VERSION:
        raise FastlaneError(
            f"Unsupported manifest schema {manifest.get('schema_version')!r}; expected {SCHEMA_VERSION}"
        )
    expected = manifest.get("manifest_sha256")
    actual = _manifest_hash(manifest)
    if not expected or expected != actual:
        raise FastlaneError(f"Manifest checksum mismatch: expected {expected}, calculated {actual}")
    return manifest


def write_output(name: str, value: Any) -> None:
    rendered = str(value).lower() if isinstance(value, bool) else str(value)
    output_path = os.environ.get("GITHUB_OUTPUT")
    if output_path:
        with open(output_path, "a", encoding="utf-8") as output:
            output.write(f"{name}={rendered}\n")
    print(f"{name}={rendered}")


def marker_numbers(text: str) -> List[int]:
    return [int(match.group(1)) for match in FASTLANE_RE.finditer(text)]


def has_malformed_fastlane_marker(text: str) -> bool:
    return FASTLANE_PREFIX in FASTLANE_RE.sub("", text)


def commit_subject(sha: str) -> str:
    return git("show", "-s", "--format=%s", sha)


def commit_message(sha: str) -> str:
    return git("show", "-s", "--format=%B", sha)


def patch_id(sha: str) -> str:
    patch = _run(("git", "show", "--pretty=format:", "--binary", sha)).stdout
    result = _run(("git", "patch-id", "--stable"), input_text=patch)
    return result.stdout.split()[0] if result.stdout.split() else ""


def is_ancestor(ancestor: str, descendant: str) -> bool:
    return _run(("git", "merge-base", "--is-ancestor", ancestor, descendant), check=False).returncode == 0


def permission_for(repo: str, login: str) -> Optional[str]:
    data = gh_api_optional(f"repos/{repo}/collaborators/{login}/permission")
    if not data:
        return None
    return data.get("user", {}).get("permissions", {}).get("admin") and "admin" or data.get("permission")


def has_write_permission(repo: str, login: Optional[str]) -> bool:
    return bool(login and permission_for(repo, login) in WRITABLE_PERMISSIONS)


def parse_state(body: str) -> Dict[str, Any]:
    match = STATE_RE.search(body or "")
    if not match:
        raise FastlaneError("PR is missing fastlane orchestration metadata")
    try:
        return json.loads(match.group(1))
    except json.JSONDecodeError as error:
        raise FastlaneError(f"Invalid fastlane orchestration metadata: {error}") from error


def state_comment(state: Mapping[str, Any]) -> str:
    return f"<!-- fastlane-rebase-state {_canonical_json(state)} -->"


def _default_branch(repo: str) -> str:
    return str(gh_api(f"repos/{repo}")["default_branch"])


def _all_pr_commits(repo: str, number: int) -> List[Dict[str, Any]]:
    commits: List[Dict[str, Any]] = []
    page = 1
    while True:
        batch = list(
            gh_api(
                f"repos/{repo}/pulls/{number}/commits",
                fields={"per_page": "100", "page": str(page)},
            )
        )
        commits.extend(batch)
        if len(batch) < 100:
            return commits
        page += 1


def validate_pr(args: argparse.Namespace) -> None:
    pr = gh_api(f"repos/{args.repo}/pulls/{args.pr_number}")
    title = str(pr["title"])
    if REBASE_TOKEN in title:
        print(f"Skipping normal fastlane validation for distributed rebase PR #{args.pr_number}")
        return

    commits = _all_pr_commits(args.repo, args.pr_number)
    subjects = [str(commit["commit"]["message"]).splitlines()[0] for commit in commits]
    title_markers = marker_numbers(title)
    commit_markers = [number for subject in subjects for number in marker_numbers(subject)]
    malformed_locations = []
    if has_malformed_fastlane_marker(title):
        malformed_locations.append("PR title")
    malformed_locations.extend(
        f"commit subject {index}"
        for index, subject in enumerate(subjects, start=1)
        if has_malformed_fastlane_marker(subject)
    )
    if malformed_locations:
        raise FastlaneError(
            "Malformed fastlane marker in "
            f"{', '.join(malformed_locations)}; expected [FASTLANE-<positive PR number>]"
        )

    if not title_markers and not commit_markers:
        print(f"PR #{args.pr_number} is not a fastlane PR")
        return

    if len(commits) != 1:
        raise FastlaneError(f"Fastlane PRs must contain exactly one commit; found {len(commits)}")
    if len(title_markers) != 1 or len(commit_markers) != 1:
        raise FastlaneError("Fastlane PR title and sole commit subject must each contain exactly one marker")
    if title_markers[0] != commit_markers[0]:
        raise FastlaneError(
            f"Fastlane marker mismatch: title references #{title_markers[0]}, commit references #{commit_markers[0]}"
        )

    default_branch = args.default_branch or _default_branch(args.repo)
    if pr["base"]["ref"] != default_branch:
        raise FastlaneError(
            f"Fastlane PR must target current default branch {default_branch!r}, not {pr['base']['ref']!r}"
        )

    upstream_number = title_markers[0]
    upstream = gh_api_optional(f"repos/{args.upstream_repo}/pulls/{upstream_number}")
    if not upstream:
        raise FastlaneError(f"{args.upstream_repo}#{upstream_number} does not exist or is not a pull request")
    upstream_base = upstream["base"]["ref"]
    if upstream_base not in ALLOWED_UPSTREAM_BASES:
        raise FastlaneError(
            f"Upstream PR #{upstream_number} targets {upstream_base!r}; expected main or branch-3.5-cc"
        )
    if upstream["state"] == "closed" and not upstream.get("merged_at"):
        raise FastlaneError(f"Upstream PR #{upstream_number} is closed without being merged")

    history = git("log", "--format=%s", pr["base"]["sha"])
    if any(upstream_number in marker_numbers(subject) for subject in history.splitlines()):
        raise FastlaneError(
            f"Upstream PR #{upstream_number} is already referenced by a fastlane commit in {default_branch}"
        )
    print(
        f"Validated fastlane PR #{args.pr_number}: {args.upstream_repo}#{upstream_number} "
        f"targets {upstream_base} and is {upstream['state']}"
    )


def _associated_internal_pr(repo: str, sha: str, upstream_number: int) -> Dict[str, Any]:
    subject_match = INTERNAL_PR_RE.search(commit_subject(sha))
    if subject_match:
        candidate = gh_api_optional(f"repos/{repo}/pulls/{subject_match.group(1)}")
        if (
            candidate
            and candidate.get("merged_at")
            and upstream_number in marker_numbers(candidate.get("title") or "")
            and REBASE_TOKEN not in candidate.get("title", "")
        ):
            return candidate
    prs = gh_api(f"repos/{repo}/commits/{sha}/pulls?per_page=100")
    merged = [
        pr
        for pr in prs
        if pr.get("merged_at")
        and upstream_number in marker_numbers(pr.get("title") or "")
        and REBASE_TOKEN not in pr.get("title", "")
    ]
    if len(merged) != 1:
        numbers = [pr.get("number") for pr in merged]
        raise FastlaneError(
            f"Commit {sha} must map to exactly one original merged fastlane PR; found {numbers}"
        )
    return merged[0]


def _resolve_author(repo: str, sha: str, internal_pr: Mapping[str, Any], actor: str) -> Tuple[str, str]:
    commit = gh_api(f"repos/{repo}/commits/{sha}")
    github_author = (commit.get("author") or {}).get("login")
    coauthored = bool(COAUTHOR_RE.search(commit["commit"]["message"]))
    if github_author and not coauthored and has_write_permission(repo, github_author):
        return github_author, "commit-author"
    pr_author = internal_pr["user"]["login"]
    if has_write_permission(repo, pr_author):
        return pr_author, "internal-pr-author"
    if has_write_permission(repo, actor):
        return actor, "workflow-actor"
    raise FastlaneError(
        f"No writable owner for {sha}: commit author={github_author}, PR author={pr_author}, actor={actor}"
    )


def _references_pr_number(text: str, number: int) -> bool:
    return re.search(rf"(?<![0-9])#{number}(?![0-9])", text) is not None


def _merged_backport(
    upstream_repo: str, upstream_number: int, new_head: str
) -> Optional[Dict[str, Any]]:
    query = f"repo:{upstream_repo} is:pr base:branch-3.5-cc {upstream_number}"
    search = gh_api("search/issues", fields={"q": query, "per_page": "100"})
    for item in search.get("items", []):
        candidate = gh_api(f"repos/{upstream_repo}/pulls/{item['number']}")
        provenance = f"{candidate.get('title') or ''}\n{candidate.get('body') or ''}"
        if (
            candidate.get("merged_at")
            and candidate["base"]["ref"] == "branch-3.5-cc"
            and _references_pr_number(provenance, upstream_number)
            and candidate.get("merge_commit_sha")
            and is_ancestor(candidate["merge_commit_sha"], new_head)
        ):
            return candidate
    return None


def _classify_upstream(
    upstream_repo: str, upstream_number: int, new_head: str, internal_sha: str
) -> Dict[str, Any]:
    upstream = gh_api(f"repos/{upstream_repo}/pulls/{upstream_number}")
    base = upstream["base"]["ref"]
    if base not in ALLOWED_UPSTREAM_BASES:
        raise FastlaneError(f"Upstream PR #{upstream_number} now targets unsupported branch {base!r}")
    result: Dict[str, Any] = {
        "number": upstream_number,
        "url": upstream["html_url"],
        "title": upstream["title"],
        "base": base,
        "state": upstream["state"],
        "merged_at": upstream.get("merged_at"),
        "classification": "pending",
        "evidence": "Upstream containment has not been proven",
        "warning": None,
        "accepted_sha": None,
        "accepted_patch_id": None,
        "patch_differs": None,
    }
    if upstream["state"] == "closed" and not upstream.get("merged_at"):
        result["evidence"] = "The upstream PR is closed without a merge"
        result["warning"] = "Upstream PR is closed unmerged; preserving internal fastlane"
        return result
    if not upstream.get("merged_at"):
        result["evidence"] = "The upstream PR is still open"
        return result

    accepted: Optional[Dict[str, Any]] = None
    if base == "branch-3.5-cc":
        merge_sha = upstream.get("merge_commit_sha")
        if merge_sha and is_ancestor(merge_sha, new_head):
            accepted = upstream
    else:
        accepted = _merged_backport(upstream_repo, upstream_number, new_head)

    if not accepted:
        if base == "main":
            result["evidence"] = (
                "No provenance-linked branch-3.5-cc backport is contained in the recorded upstream head"
            )
        else:
            result["evidence"] = "The merged commit is not contained in the recorded upstream head"
        return result
    accepted_sha = accepted.get("merge_commit_sha")
    result["classification"] = "upstreamed"
    result["evidence"] = f"Accepted commit {accepted_sha} is contained in the recorded upstream head"
    result["accepted_sha"] = accepted_sha
    result["accepted_pr"] = accepted.get("number")
    if accepted_sha:
        try:
            internal_patch_id = patch_id(internal_sha)
            accepted_patch_id = patch_id(accepted_sha)
            result["accepted_patch_id"] = accepted_patch_id
            result["patch_differs"] = internal_patch_id != accepted_patch_id
        except FastlaneError:
            result["patch_differs"] = None
            result["warning"] = "Could not compare internal and accepted upstream patches"
    return result


def snapshot(args: argparse.Namespace) -> None:
    merge_base = git("merge-base", args.old_head, args.new_head)
    shas = git("rev-list", "--reverse", f"{merge_base}..{args.old_head}").splitlines()
    commits: List[Dict[str, Any]] = []
    entries: List[Dict[str, Any]] = []
    seen_upstream: Dict[int, str] = {}

    for index, sha in enumerate(shas):
        subject = commit_subject(sha)
        markers = marker_numbers(subject)
        if has_malformed_fastlane_marker(subject):
            raise FastlaneError(f"Malformed fastlane marker in {sha}: {subject}")
        if len(markers) > 1:
            raise FastlaneError(f"Commit {sha} contains multiple fastlane markers")
        commit_record = {"sha": sha, "subject": subject, "order": index, "fastlane": bool(markers)}
        commits.append(commit_record)
        if not markers:
            continue
        upstream_number = markers[0]
        if upstream_number in seen_upstream:
            raise FastlaneError(
                f"Repeated upstream reference #{upstream_number} in "
                f"{seen_upstream[upstream_number]} and {sha} is unsupported"
            )
        seen_upstream[upstream_number] = sha
        internal_pr = _associated_internal_pr(args.repo, sha, upstream_number)
        author, author_source = _resolve_author(args.repo, sha, internal_pr, args.actor)
        upstream = _classify_upstream(args.upstream_repo, upstream_number, args.new_head, sha)
        entries.append(
            {
                "sha": sha,
                "patch_id": patch_id(sha),
                "subject": subject,
                "order": index,
                "upstream_pr": upstream,
                "internal_pr": {
                    "number": internal_pr["number"],
                    "title": internal_pr["title"],
                    "url": internal_pr["html_url"],
                    "author": internal_pr["user"]["login"],
                },
                "author": author,
                "author_source": author_source,
            }
        )

    run_id = str(args.run_id)
    manifest: Dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "run_id": run_id,
        "repo": args.repo,
        "upstream_repo": args.upstream_repo,
        "actor": args.actor,
        "old_head": git("rev-parse", args.old_head),
        "old_default_branch": args.old_default_branch,
        "old_upstream_base": merge_base,
        "new_head": git("rev-parse", args.new_head),
        "base_branch": args.base_branch,
        "recovery_branch": f"fastlane-rebase/{run_id}/recovery",
        "central_branch": f"fastlane-rebase/{run_id}/central",
        "commits": commits,
        "entries": entries,
    }
    write_manifest(Path(args.output), manifest)
    pending = [entry for entry in entries if entry["upstream_pr"]["classification"] == "pending"]
    upstreamed = [entry for entry in entries if entry["upstream_pr"]["classification"] == "upstreamed"]
    lines = [
        f"Fastlane rebase run {run_id}",
        f"Old fork head: {manifest['old_head']}",
        f"New upstream head: {manifest['new_head']}",
        f"Upstream-aligned base branch: {manifest['base_branch']}",
        f"Pending: {len(pending)}; already upstreamed: {len(upstreamed)}",
        "",
    ]
    for entry in entries:
        upstream = entry["upstream_pr"]
        lines.append(
            f"- {upstream['classification'].upper()} {entry['sha']} {entry['internal_pr']['title']} "
            f"(internal {entry['internal_pr']['url']}, upstream {upstream['url']}, owner @{entry['author']})"
        )
        lines.append(f"  Evidence: {upstream['evidence']}")
        if upstream.get("warning"):
            lines.append(f"  WARNING: {upstream['warning']}")
        if upstream.get("patch_differs"):
            lines.append(
                "  WARNING: accepted upstream patch differs from the internal fastlane patch "
                f"({entry['patch_id']} -> {upstream.get('accepted_patch_id')})"
            )
    Path(args.report).write_text("\n".join(lines) + "\n", encoding="utf-8")
    write_output("manifest_sha256", manifest["manifest_sha256"])
    write_output("pending_count", len(pending))
    write_output("fastlane_count", len(entries))
    write_output("recovery_branch", manifest["recovery_branch"])
    write_output("central_branch", manifest["central_branch"])


def _write_sequence_editor(manifest: Mapping[str, Any], directory: Path) -> Path:
    all_shas = [entry["sha"] for entry in manifest["entries"]]
    pending = [
        entry["sha"]
        for entry in manifest["entries"]
        if entry["upstream_pr"]["classification"] == "pending"
    ]
    script = directory / "sequence_editor.py"
    script.write_text(
        "#!/usr/bin/env python3\n"
        "import pathlib, sys\n"
        f"all_shas = {all_shas!r}\n"
        f"pending = {pending!r}\n"
        "todo = pathlib.Path(sys.argv[1])\n"
        "lines = todo.read_text().splitlines()\n"
        "found = set()\n"
        "out = []\n"
        "for line in lines:\n"
        "    fields = line.split()\n"
        "    if len(fields) >= 2 and fields[0] in {'pick', 'p', 'reword', 'r', 'edit', 'e'}:\n"
        "        matches = [sha for sha in all_shas if sha.startswith(fields[1])]\n"
        "        if len(matches) == 1:\n"
        "            found.add(matches[0])\n"
        "            fields[0] = 'drop'\n"
        "            line = ' '.join(fields)\n"
        "    out.append(line)\n"
        "missing = set(pending) - found\n"
        "if missing:\n"
        "    raise SystemExit('Pending fastlanes missing from rebase todo: ' + ', '.join(sorted(missing)))\n"
        "todo.write_text('\\n'.join(out) + '\\n')\n",
        encoding="utf-8",
    )
    script.chmod(0o755)
    return script


def validate_rebase_data(manifest: Mapping[str, Any], candidate: str) -> str:
    new_head = manifest["new_head"]
    candidate_sha = git("rev-parse", candidate)
    if not is_ancestor(new_head, candidate_sha):
        raise FastlaneError(f"{candidate_sha} is not based on recorded upstream head {new_head}")
    subjects = git("log", "--reverse", "--format=%s", f"{new_head}..{candidate_sha}").splitlines()
    if any(marker_numbers(subject) for subject in subjects):
        raise FastlaneError("Rebased clean branch still contains fastlane markers")
    range_diff = _run(
        (
            "git",
            "range-diff",
            f"{manifest['old_upstream_base']}..{manifest['old_head']}",
            f"{new_head}..{candidate_sha}",
        ),
        check=False,
    )
    return range_diff.stdout + range_diff.stderr


def rebase_branch(args: argparse.Namespace) -> None:
    manifest = load_manifest(Path(args.manifest))
    with tempfile.TemporaryDirectory(prefix="fastlane-rebase-") as temp_dir:
        editor = _write_sequence_editor(manifest, Path(temp_dir))
        result = _run(
            ("git", "rebase", "--interactive", manifest["new_head"]),
            check=False,
            env={"GIT_SEQUENCE_EDITOR": str(editor)},
        )
    if result.returncode:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise FastlaneError(
            "Rebase stopped. Resolve it locally and continue with git rebase --continue, "
            "or abort with git rebase --abort."
        )
    report = validate_rebase_data(manifest, "HEAD")
    if args.report:
        Path(args.report).write_text(report, encoding="utf-8")
    print("Fastlane-free rebase completed and validated")


def validate_rebase_command(args: argparse.Namespace) -> None:
    manifest = load_manifest(Path(args.manifest))
    report = validate_rebase_data(manifest, args.candidate)
    if args.report:
        Path(args.report).write_text(report, encoding="utf-8")
    print("Rebased candidate validated")


def _find_pr(repo: str, head: str, base: str, state: str = "open") -> Optional[Dict[str, Any]]:
    owner = repo.split("/", 1)[0]
    prs = gh_api(f"repos/{repo}/pulls?state={state}&head={owner}:{head}&base={base}&per_page=100")
    return prs[0] if prs else None


def _create_or_reuse_pr(
    repo: str, *, head: str, base: str, title: str, body: str, draft: bool
) -> Dict[str, Any]:
    existing = _find_pr(repo, head, base)
    if existing:
        return existing
    return gh_api(
        f"repos/{repo}/pulls",
        method="POST",
        payload={"head": head, "base": base, "title": title, "body": body, "draft": draft},
    )


def _child_branch(run_id: str, author: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9_.-]+", "-", author).strip("-.").lower()
    if not slug:
        raise FastlaneError(f"Cannot derive a safe branch component from author {author!r}")
    return f"fastlane-rebase/{run_id}/author/{slug}"


def _restore_command(run_id: str, repo: str, branch: str) -> str:
    return (
        f"gh pr checkout {shlex.quote(branch)} --repo {shlex.quote(repo)}\n"
        f"gh run download {shlex.quote(run_id)} --repo {shlex.quote(repo)} "
        f"-n fastlane-manifest-{shlex.quote(run_id)} -D .fastlane-rebase-state\n"
        f"python3 celonis/tools/fastlane_rebase.py restore "
        f"--manifest .fastlane-rebase-state/fastlane-manifest.json "
        f"--repo {shlex.quote(repo)} --pr-number \"$(gh pr view {shlex.quote(branch)} "
        f"--repo {shlex.quote(repo)} --json number --jq .number)\" --push"
    )


def _expected_child_entries(manifest: Mapping[str, Any], author: str) -> List[Dict[str, Any]]:
    return [
        {"sha": entry["sha"], "upstream_pr": entry["upstream_pr"]["number"]}
        for entry in manifest["entries"]
        if entry["upstream_pr"]["classification"] == "pending" and entry["author"] == author
    ]


def _manifest_pr_report(manifest: Mapping[str, Any]) -> str:
    lines = ["### Fastlane snapshot", ""]
    for entry in manifest["entries"]:
        upstream = entry["upstream_pr"]
        lines.append(
            f"- **{upstream['classification'].upper()}** "
            f"[{entry['internal_pr']['title']}]({entry['internal_pr']['url']}) → "
            f"[upstream #{upstream['number']}]({upstream['url']}) (owner @{entry['author']})"
        )
        lines.append(f"  - Evidence: {upstream['evidence']}")
        if upstream.get("warning"):
            lines.append(f"  - **Warning:** {upstream['warning']}")
        if upstream.get("patch_differs"):
            lines.append(
                "  - **Warning: the authoritative upstream patch differs from the internal patch; "
                f"the internal commit was dropped.** Patch IDs: `{entry['patch_id']}` → "
                f"`{upstream.get('accepted_patch_id')}`."
            )
    return "\n".join(lines)


def _validate_child_state(
    manifest: Mapping[str, Any], pr: Mapping[str, Any], state: Mapping[str, Any]
) -> None:
    author = state.get("author")
    if (
        state.get("role") != "child"
        or str(state.get("run_id")) != str(manifest["run_id"])
        or state.get("manifest_sha256") != manifest["manifest_sha256"]
        or state.get("central_branch") != manifest["central_branch"]
        or not isinstance(author, str)
        or not isinstance(state.get("parent_pr"), int)
        or not re.fullmatch(r"[0-9a-f]{40}", str(state.get("bootstrap_sha", "")))
    ):
        raise FastlaneError("Child PR metadata does not match this rebase manifest")
    expected_entries = _expected_child_entries(manifest, author)
    if not expected_entries or state.get("entries") != expected_entries:
        raise FastlaneError(f"Child PR assignments for @{author} do not match the immutable manifest")
    expected_branch = _child_branch(str(manifest["run_id"]), author)
    if pr["head"]["ref"] != expected_branch or pr["base"]["ref"] != manifest["central_branch"]:
        raise FastlaneError(
            f"Child PR must merge {expected_branch} into {manifest['central_branch']}"
        )
    expected_title = f"{REBASE_TOKEN} {manifest['central_branch']} - {author}"
    if pr["title"] != expected_title:
        raise FastlaneError(f"Child PR title must be exactly {expected_title!r}")


def distribute(args: argparse.Namespace) -> None:
    manifest = load_manifest(Path(args.manifest))
    repo = manifest["repo"]
    central = args.central_branch or manifest["central_branch"]
    if args.base_branch != manifest.get("base_branch"):
        raise FastlaneError(
            f"Requested base branch {args.base_branch!r} does not match immutable manifest "
            f"base branch {manifest.get('base_branch')!r}"
        )
    run_id = manifest["run_id"]
    pending = [
        entry
        for entry in manifest["entries"]
        if entry["upstream_pr"]["classification"] == "pending"
    ]
    if not pending:
        print("No pending fastlanes to distribute")
        return
    if git("status", "--porcelain"):
        raise FastlaneError("distribute requires a clean working tree")
    remote_base = git("ls-remote", "--heads", "origin", args.base_branch).split()
    remote_base_sha = remote_base[0] if remote_base else None
    if remote_base_sha != manifest["new_head"]:
        raise FastlaneError(
            f"Snapshot base branch {args.base_branch} moved from {manifest['new_head']} "
            f"to {remote_base_sha or '<missing>'}"
        )

    parent_state: Dict[str, Any] = {
        "role": "parent",
        "run_id": run_id,
        "manifest_sha256": manifest["manifest_sha256"],
        "central_branch": central,
        "base_branch": args.base_branch,
        "clean_head": git("rev-parse", central),
        "children": [],
    }
    parent_body = (
        f"Distributed fastlane rebase for run `{run_id}`.\n\n"
        "This PR remains draft until every child PR is rebase-merged and final integration CI passes.\n\n"
        f"{_manifest_pr_report(manifest)}\n\n"
        f"{state_comment(parent_state)}"
    )
    parent = _create_or_reuse_pr(
        repo,
        head=central,
        base=args.base_branch,
        title=f"[Tool] Integration rebase: {central} → {args.base_branch} (sync #{run_id})",
        body=parent_body,
        draft=True,
    )
    existing_parent_state = parse_state(parent.get("body") or "")
    if (
        existing_parent_state.get("role") != "parent"
        or str(existing_parent_state.get("run_id")) != str(run_id)
        or existing_parent_state.get("manifest_sha256") != manifest["manifest_sha256"]
        or existing_parent_state.get("central_branch") != central
        or existing_parent_state.get("base_branch") != args.base_branch
        or existing_parent_state.get("clean_head") != parent_state["clean_head"]
    ):
        raise FastlaneError(f"Existing parent PR #{parent['number']} has unexpected orchestration metadata")
    gh_api(
        f"repos/{repo}/issues/{parent['number']}/labels",
        method="POST",
        payload={"labels": ["sync"]},
    )

    grouped: Dict[str, List[Dict[str, Any]]] = defaultdict(list)
    for entry in pending:
        grouped[entry["author"]].append(entry)

    original_branch = git("branch", "--show-current")
    for author, entries in sorted(grouped.items()):
        branch = _child_branch(run_id, author)
        existing = _find_pr(repo, branch, central, state="open")
        if existing:
            state = parse_state(existing.get("body") or "")
            _validate_child_state(manifest, existing, state)
            parent_state["children"].append(existing["number"])
            continue

        remote_branch = git("ls-remote", "--heads", "origin", branch)
        if remote_branch:
            raise FastlaneError(
                f"Branch {branch} already exists without a matching PR; refusing to overwrite it"
            )

        git("switch", "--force-create", branch, central)
        instruction_path = Path(".fastlane-rebase") / run_id / f"{author}.txt"
        instruction_path.parent.mkdir(parents=True, exist_ok=True)
        restore_command = _restore_command(run_id, repo, branch)
        lines = [
            f"Fastlane rebase assignments for @{author}",
            f"Run: {run_id}",
            "",
            "Cherry-pick in this order:",
        ]
        for entry in entries:
            lines.append(
                f"- {entry['sha']} | {entry['internal_pr']['title']} | {entry['internal_pr']['url']}"
            )
            lines.append(
                f"  Upstream: {entry['upstream_pr']['title']} | {entry['upstream_pr']['url']}"
            )
            lines.append(f"  Evidence: {entry['upstream_pr']['evidence']}")
        lines.extend(("", "Restore command:", restore_command, ""))
        instruction_path.write_text("\n".join(lines), encoding="utf-8")
        git("add", str(instruction_path))
        git("commit", "-m", f"[FASTLANE-REBASE] Assign pending fastlanes to @{author}")
        bootstrap_sha = git("rev-parse", "HEAD")
        git("push", "--set-upstream", "origin", branch)
        child_state = {
            "role": "child",
            "run_id": run_id,
            "manifest_sha256": manifest["manifest_sha256"],
            "parent_pr": parent["number"],
            "central_branch": central,
            "author": author,
            "bootstrap_sha": bootstrap_sha,
            "entries": [
                {"sha": entry["sha"], "upstream_pr": entry["upstream_pr"]["number"]}
                for entry in entries
            ],
        }
        links = "\n".join(
            f"- {entry['internal_pr']['url']} → {entry['upstream_pr']['url']} (`{entry['sha']}`)"
            for entry in entries
        )
        body = (
            f"@{author}, restore and resolve your pending fastlanes for parent PR #{parent['number']}.\n\n"
            f"{links}\n\n"
            f"Run:\n```shell\n{restore_command}\n```\n\n"
            "After pushing, mark this PR ready. When current-base checks pass, comment `/merge`.\n\n"
            f"{state_comment(child_state)}"
        )
        child = _create_or_reuse_pr(
            repo,
            head=branch,
            base=central,
            title=f"[FASTLANE-REBASE] {central} - {author}",
            body=body,
            draft=True,
        )
        gh_api(
            f"repos/{repo}/issues/{child['number']}/assignees",
            method="POST",
            payload={"assignees": [author]},
        )
        parent_state["children"].append(child["number"])

    if original_branch:
        git("switch", original_branch)
    checklist = "\n".join(f"- [ ] #{number}" for number in parent_state["children"])
    parent_body = (
        f"Distributed fastlane rebase for run `{run_id}`.\n\n"
        "This PR remains draft until every child PR is rebase-merged and final integration CI passes.\n\n"
        f"{checklist}\n\n{_manifest_pr_report(manifest)}\n\n{state_comment(parent_state)}"
    )
    gh_api(
        f"repos/{repo}/pulls/{parent['number']}",
        method="PATCH",
        payload={
            "title": (
                f"[Tool] Integration rebase: {central} → {args.base_branch} (sync #{run_id})"
            ),
            "body": parent_body,
        },
    )
    write_output("parent_pr", parent["number"])
    write_output("child_count", len(parent_state["children"]))


def restore(args: argparse.Namespace) -> None:
    manifest = load_manifest(Path(args.manifest))
    pr = gh_api(f"repos/{args.repo}/pulls/{args.pr_number}")
    state = parse_state(pr.get("body") or "")
    _validate_child_state(manifest, pr, state)
    branch = pr["head"]["ref"]
    central = state["central_branch"]
    for remote_branch in (
        central,
        branch,
        manifest["recovery_branch"],
        manifest["old_default_branch"],
    ):
        fetch_origin_branch(remote_branch)
    git("switch", branch)
    if not args.finish:
        shas = [entry["sha"] for entry in state["entries"]]
        result = _run(("git", "cherry-pick") + tuple(shas), check=False)
        if result.returncode:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
            print(
                "Resolve the conflict, run git cherry-pick --continue until complete, "
                "then rerun this command with --finish.",
                file=sys.stderr,
            )
            raise FastlaneError("Cherry-pick stopped for conflict resolution")
    rebase_result = _run(
        ("git", "rebase", "--onto", f"origin/{central}", state["bootstrap_sha"], branch),
        check=False,
    )
    if rebase_result.returncode:
        sys.stderr.write(rebase_result.stdout)
        sys.stderr.write(rebase_result.stderr)
        print(
            "Resolve the conflict and run git rebase --continue until complete, then push with "
            f"git push --force-with-lease origin HEAD:{branch}",
            file=sys.stderr,
        )
        raise FastlaneError("Rebase onto the current central branch stopped for conflict resolution")
    if is_ancestor(state["bootstrap_sha"], "HEAD"):
        raise FastlaneError("Bootstrap commit is still present after restore")
    if git("ls-tree", "-r", "--name-only", "HEAD", ".fastlane-rebase"):
        raise FastlaneError("Bootstrap instruction file is still present after restore")
    if args.push:
        git("push", "--force-with-lease", "origin", f"HEAD:{branch}")
    print(f"Restored {len(state['entries'])} fastlane commit(s) on {branch}")


def validate_child(args: argparse.Namespace) -> None:
    manifest = load_manifest(Path(args.manifest))
    pr = gh_api(f"repos/{args.repo}/pulls/{args.pr_number}")
    state = parse_state(pr.get("body") or "")
    _validate_child_state(manifest, pr, state)
    base_sha = pr["base"]["sha"]
    head_sha = pr["head"]["sha"]
    fetch_origin_branch(pr["base"]["ref"])
    fetch_origin_branch(pr["head"]["ref"])
    if not is_ancestor(base_sha, head_sha):
        raise FastlaneError("Child branch does not contain the current central branch; rebase and rerun CI")
    if is_ancestor(state["bootstrap_sha"], head_sha):
        raise FastlaneError("Bootstrap commit is still in child history")
    if git("ls-tree", "-r", "--name-only", head_sha, ".fastlane-rebase"):
        raise FastlaneError("Bootstrap instruction file is still present")

    commits = git("log", "--reverse", "--format=%H%x09%s", f"{base_sha}..{head_sha}").splitlines()
    actual: List[Tuple[str, int]] = []
    for line in commits:
        sha, subject = line.split("\t", 1)
        markers = marker_numbers(subject)
        if len(markers) != 1:
            raise FastlaneError(f"Restored commit {sha} must contain exactly one fastlane marker")
        actual.append((sha, markers[0]))
    expected = [int(entry["upstream_pr"]) for entry in state["entries"]]
    if [number for _, number in actual] != expected:
        raise FastlaneError(
            f"Restored marker order mismatch: expected {expected}, got {[number for _, number in actual]}"
        )

    write_output("base_sha", base_sha)
    write_output("head_sha", head_sha)
    write_output(
        "status_context",
        child_status_context(base_sha, manifest["manifest_sha256"]),
    )
    print(f"Validated child PR #{args.pr_number} against central {base_sha}")


def _run_prs(repo: str, run_id: str, state: str = "all") -> List[Dict[str, Any]]:
    result = []
    page = 1
    while True:
        prs = list(
            gh_api(
                f"repos/{repo}/pulls",
                fields={"state": state, "per_page": "100", "page": str(page)},
            )
        )
        for pr in prs:
            try:
                metadata = parse_state(pr.get("body") or "")
            except FastlaneError:
                continue
            if str(metadata.get("run_id")) == str(run_id):
                result.append(pr)
        if len(prs) < 100:
            break
        page += 1
    return result


def _find_parent_pr(repo: str, manifest: Mapping[str, Any]) -> Dict[str, Any]:
    owner = repo.split("/", 1)[0]
    prs = gh_api(
        f"repos/{repo}/pulls",
        fields={
            "state": "all",
            "head": f"{owner}:{manifest['central_branch']}",
            "per_page": "100",
        },
    )
    parents = []
    for pr in prs:
        try:
            metadata = parse_state(pr.get("body") or "")
        except FastlaneError:
            continue
        if (
            metadata.get("role") == "parent"
            and str(metadata.get("run_id")) == str(manifest["run_id"])
        ):
            parents.append(pr)
    if len(parents) != 1:
        raise FastlaneError(
            f"Expected exactly one parent PR for run {manifest['run_id']}; found {len(parents)}"
        )
    return gh_api(f"repos/{repo}/pulls/{parents[0]['number']}")


def status(args: argparse.Namespace) -> None:
    manifest = load_manifest(Path(args.manifest))
    prs = _run_prs(args.repo, manifest["run_id"])
    summary = [
        {
            "number": pr["number"],
            "title": pr["title"],
            "state": pr["state"],
            "merged_at": pr.get("merged_at"),
            "draft": pr.get("draft"),
        }
        for pr in prs
    ]
    print(json.dumps(summary, indent=2, sort_keys=True))


def _log_commits(revision_range: str) -> List[Tuple[str, str]]:
    lines = git("log", "--reverse", "--format=%H%x09%s", revision_range).splitlines()
    commits = []
    for line in lines:
        sha, subject = line.split("\t", 1)
        commits.append((sha, subject))
    return commits


def validate_old_default_unchanged(args: argparse.Namespace) -> None:
    manifest = load_manifest(Path(args.manifest))
    old_default_branch = manifest["old_default_branch"]
    old_head = manifest["old_head"]
    fetch_origin_branch(old_default_branch)
    current_old_head = git("rev-parse", f"origin/{old_default_branch}")
    if current_old_head == old_head:
        print(f"Old default branch {old_default_branch} remains at {old_head}")
        return

    lines = [
        "### Clean rebase snapshot is stale",
        "",
        f"`{old_default_branch}` moved after this rebase was snapshotted:",
        "",
        f"- Snapshotted head: `{old_head}`",
        f"- Current head: `{current_old_head}`",
        "",
    ]
    if is_ancestor(old_head, current_old_head):
        lines.append("Commits added since the snapshot:")
        lines.append("")
        lines.extend(
            f"- `{sha}` {subject}"
            for sha, subject in _log_commits(f"{old_head}..{current_old_head}")
        )
        lines.append("")
    else:
        lines.extend(
            (
                "The old default branch no longer contains the snapshotted head, so its "
                "appended commits cannot be determined safely.",
                "",
            )
        )
    lines.extend(
        (
            "Start a new fastlane rebase from the current default branch:",
            "",
            "```shell",
            f"gh workflow run celonis_fastlane_rebase.yml --repo {args.repo} -f mode=start",
            "```",
            "",
        )
    )
    report = "\n".join(lines)
    print(report)
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with open(summary_path, "a", encoding="utf-8") as summary:
            summary.write(report)
    raise FastlaneError(
        f"Old default branch {old_default_branch} moved from {old_head} to {current_old_head}; "
        "start a new rebase"
    )


def _validate_old_default_commits(
    repo: str,
    manifest: Mapping[str, Any],
    parent_number: int,
    central_sha: str,
) -> List[int]:
    old_default_branch = manifest["old_default_branch"]
    old_head = manifest["old_head"]
    fetch_origin_branch(old_default_branch)
    current_old_head = git("rev-parse", f"origin/{old_default_branch}")
    if current_old_head == old_head:
        return []

    if not is_ancestor(old_head, current_old_head):
        body = (
            "### Old default branch diverged from the rebase snapshot\n\n"
            f"`{old_default_branch}` no longer contains the snapshotted commit `{old_head}` "
            f"(current head: `{current_old_head}`). The appended commits cannot be determined "
            "safely. Investigate the branch change and start a new fastlane rebase if necessary."
        )
        gh_api(
            f"repos/{repo}/issues/{parent_number}/comments",
            method="POST",
            payload={"body": body},
        )
        raise FastlaneError(
            f"Old default branch {old_default_branch} diverged from snapshotted head {old_head}"
        )

    added_commits = _log_commits(f"{old_head}..{current_old_head}")
    required_commits: List[Tuple[str, str]] = []
    additional_markers: List[int] = []
    skipped_fastlanes: List[Tuple[str, str, str]] = []
    for sha, subject in added_commits:
        markers = marker_numbers(subject)
        if has_malformed_fastlane_marker(subject):
            raise FastlaneError(f"Malformed fastlane marker in post-snapshot commit {sha}: {subject}")
        if len(markers) > 1:
            raise FastlaneError(f"Post-snapshot commit {sha} contains multiple fastlane markers")
        if not markers:
            required_commits.append((sha, subject))
            continue

        upstream = _classify_upstream(
            manifest["upstream_repo"], markers[0], manifest["new_head"], sha
        )
        if upstream["classification"] == "upstreamed":
            skipped_fastlanes.append((sha, subject, upstream["evidence"]))
            continue
        required_commits.append((sha, subject))
        additional_markers.extend(markers)

    if skipped_fastlanes:
        report = ["Post-snapshot fastlanes already contained in the rebase base:"]
        report.extend(
            f"- {sha} {subject}: {evidence}"
            for sha, subject, evidence in skipped_fastlanes
        )
        print("\n".join(report))
        summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
        if summary_path:
            with open(summary_path, "a", encoding="utf-8") as summary:
                summary.write("### Post-snapshot fastlanes already upstreamed\n\n")
                summary.writelines(
                    f"- `{sha}` {subject}: {evidence}\n"
                    for sha, subject, evidence in skipped_fastlanes
                )
                summary.write("\n")

    central_subjects = Counter(
        subject for _, subject in _log_commits(f"{manifest['new_head']}..{central_sha}")
    )
    missing: List[Tuple[str, str]] = []
    for sha, subject in required_commits:
        if central_subjects[subject]:
            central_subjects[subject] -= 1
        else:
            missing.append((sha, subject))
    if not missing:
        return additional_markers

    central_branch = manifest["central_branch"]
    cherry_pick_lines = " \\\n".join(f"  {sha}" for sha, _ in missing)
    missing_lines = "\n".join(f"- `{sha}` {subject}" for sha, subject in missing)
    body = (
        "### Commits from the old default branch are missing\n\n"
        f"`{old_default_branch}` advanced from `{old_head}` to `{current_old_head}` after this "
        "rebase was snapshotted. The following commits are not represented on the central "
        "branch by an exact commit-subject match:\n\n"
        f"{missing_lines}\n\n"
        "Cherry-pick them onto the central branch in this order, push the result, and rerun "
        "finalization:\n\n"
        "```shell\n"
        f"git fetch origin {old_default_branch} {central_branch}\n"
        f"git switch -C {central_branch} origin/{central_branch}\n"
        f"git cherry-pick \\\n{cherry_pick_lines}\n"
        f"git push origin HEAD:{central_branch}\n"
        f"gh workflow run celonis_fastlane_rebase.yml --repo {repo} "
        f"-f mode=finalize -f run_id={manifest['run_id']}\n"
        "```"
    )
    gh_api(
        f"repos/{repo}/issues/{parent_number}/comments",
        method="POST",
        payload={"body": body},
    )
    raise FastlaneError(
        f"Old default branch {old_default_branch} has {len(missing)} commit(s) missing from central"
    )


def validate_final(args: argparse.Namespace) -> None:
    manifest = load_manifest(Path(args.manifest))
    pending = [
        entry
        for entry in manifest["entries"]
        if entry["upstream_pr"]["classification"] == "pending"
    ]
    if not pending:
        raise FastlaneError("This run has no distributed fastlanes and cannot be finalized")
    parent = _find_parent_pr(args.repo, manifest)
    parent_state = parse_state(parent.get("body") or "")
    recorded_base_branch = manifest.get("base_branch")
    if (
        parent_state.get("role") != "parent"
        or str(parent_state.get("run_id")) != str(manifest["run_id"])
        or parent_state.get("manifest_sha256") != manifest["manifest_sha256"]
        or parent_state.get("central_branch") != manifest["central_branch"]
        or not isinstance(parent_state.get("base_branch"), str)
        or not parent_state["base_branch"]
        or (
            recorded_base_branch is not None
            and parent_state["base_branch"] != recorded_base_branch
        )
    ):
        raise FastlaneError("Parent PR metadata does not match the immutable snapshot")
    if parent.get("head", {}).get("ref") != manifest["central_branch"]:
        raise FastlaneError("Parent PR head branch does not match the recorded central branch")
    if parent.get("base", {}).get("ref") != parent_state["base_branch"]:
        raise FastlaneError("Parent PR no longer targets the recorded snapshot base branch")
    if parent.get("base", {}).get("sha") != manifest["new_head"]:
        raise FastlaneError("Parent PR base branch moved from the snapshotted upstream head")
    child_numbers = parent_state.get("children") or []
    if not child_numbers:
        raise FastlaneError("Parent PR has no recorded child PRs")
    if len(child_numbers) != len(set(child_numbers)):
        raise FastlaneError("Parent PR records a child PR more than once")
    child_authors = set()
    for number in child_numbers:
        child = gh_api(f"repos/{args.repo}/pulls/{number}")
        if not child.get("merged_at"):
            raise FastlaneError(f"Child PR #{number} has not been merged")
        child_state = parse_state(child.get("body") or "")
        _validate_child_state(manifest, child, child_state)
        if child_state.get("parent_pr") != parent["number"]:
            raise FastlaneError(f"Child PR #{number} points to a different parent PR")
        child_authors.add(child_state["author"])
    expected_authors = {entry["author"] for entry in pending}
    if child_authors != expected_authors:
        raise FastlaneError(
            f"Distributed authors differ: expected {sorted(expected_authors)}, found {sorted(child_authors)}"
        )

    central = parent_state["central_branch"]
    fetch_origin_branch(central)
    central_sha = git("rev-parse", f"origin/{central}")
    if not is_ancestor(manifest["new_head"], central_sha):
        raise FastlaneError("Central branch is no longer based on the recorded upstream head")
    additional_markers = _validate_old_default_commits(
        args.repo, manifest, parent["number"], central_sha
    )
    clean_head = parent_state.get("clean_head")
    if not isinstance(clean_head, str) or not re.fullmatch(r"[0-9a-f]{40}", clean_head):
        raise FastlaneError("Parent PR has an invalid recorded clean central head")
    if not is_ancestor(clean_head, central_sha):
        raise FastlaneError("Recorded clean central head is not in current central history")
    lines = git(
        "log", "--reverse", "--format=%H%x09%s", f"{manifest['new_head']}..{central_sha}"
    ).splitlines()
    actual: List[int] = []
    for line in lines:
        sha, subject = line.split("\t", 1)
        markers = marker_numbers(subject)
        if len(markers) > 1:
            raise FastlaneError(f"Central commit {sha} combines multiple fastlane markers")
        actual.extend(markers)
    expected = [int(entry["upstream_pr"]["number"]) for entry in pending]
    expected.extend(additional_markers)
    if Counter(actual) != Counter(expected):
        raise FastlaneError(f"Central fastlane markers differ: expected {expected}, found {actual}")
    extra_commits = []
    for line in git("log", "--reverse", "--format=%H%x09%s", f"{clean_head}..{central_sha}").splitlines():
        sha, subject = line.split("\t", 1)
        if not marker_numbers(subject):
            extra_commits.append((sha, subject))
    if extra_commits:
        report = ["Additional non-fastlane commits on central:"]
        report.extend(f"- {sha} {subject}" for sha, subject in extra_commits)
        rendered_report = "\n".join(report)
        print(rendered_report)
        summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
        if summary_path:
            with open(summary_path, "a", encoding="utf-8") as summary:
                summary.write("### Additional non-fastlane commits\n\n")
                summary.writelines(f"- `{sha}` {subject}\n" for sha, subject in extra_commits)
                summary.write("\n")
    write_output("parent_pr", parent["number"])
    write_output("central_branch", central)
    write_output("base_branch", parent_state["base_branch"])
    write_output("central_sha", central_sha)
    write_output("extra_commit_count", len(extra_commits))
    print(f"Final state validated for parent PR #{parent['number']}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate = subparsers.add_parser("validate-pr", help="Validate a normal fastlane PR reference")
    validate.add_argument("--repo", required=True)
    validate.add_argument("--upstream-repo", default="StarRocks/starrocks")
    validate.add_argument("--pr-number", required=True, type=int)
    validate.add_argument("--default-branch")
    validate.set_defaults(func=validate_pr)

    snapshot_parser = subparsers.add_parser("snapshot", help="Snapshot and classify fastlane commits")
    snapshot_parser.add_argument("--repo", required=True)
    snapshot_parser.add_argument("--upstream-repo", default="StarRocks/starrocks")
    snapshot_parser.add_argument("--old-head", required=True)
    snapshot_parser.add_argument("--old-default-branch", required=True)
    snapshot_parser.add_argument("--new-head", required=True)
    snapshot_parser.add_argument("--base-branch", required=True)
    snapshot_parser.add_argument("--run-id", required=True)
    snapshot_parser.add_argument("--actor", required=True)
    snapshot_parser.add_argument("--output", required=True)
    snapshot_parser.add_argument("--report", required=True)
    snapshot_parser.set_defaults(func=snapshot)

    rebase_parser = subparsers.add_parser("rebase", help="Drop snapshotted fastlanes during rebase")
    rebase_parser.add_argument("--manifest", required=True)
    rebase_parser.add_argument("--report")
    rebase_parser.set_defaults(func=rebase_branch)

    validate_rebase_parser = subparsers.add_parser("validate-rebase", help="Validate a completed clean rebase")
    validate_rebase_parser.add_argument("--manifest", required=True)
    validate_rebase_parser.add_argument("--candidate", default="HEAD")
    validate_rebase_parser.add_argument("--report")
    validate_rebase_parser.set_defaults(func=validate_rebase_command)

    distribute_parser = subparsers.add_parser("distribute", help="Create parent and author PRs")
    distribute_parser.add_argument("--manifest", required=True)
    distribute_parser.add_argument("--central-branch")
    distribute_parser.add_argument("--base-branch", required=True)
    distribute_parser.set_defaults(func=distribute)

    restore_parser = subparsers.add_parser("restore", help="Restore an author's pending fastlanes")
    restore_parser.add_argument("--manifest", required=True)
    restore_parser.add_argument("--repo", required=True)
    restore_parser.add_argument("--pr-number", required=True, type=int)
    restore_parser.add_argument("--finish", action="store_true")
    restore_parser.add_argument("--push", action="store_true")
    restore_parser.set_defaults(func=restore)

    child_parser = subparsers.add_parser("validate-child", help="Validate a distributed author PR")
    child_parser.add_argument("--manifest", required=True)
    child_parser.add_argument("--repo", required=True)
    child_parser.add_argument("--pr-number", required=True, type=int)
    child_parser.set_defaults(func=validate_child)

    final_parser = subparsers.add_parser("validate-final", help="Validate a distributed run before final CI")
    final_parser.add_argument("--manifest", required=True)
    final_parser.add_argument("--repo", required=True)
    final_parser.set_defaults(func=validate_final)

    old_default_parser = subparsers.add_parser(
        "validate-old-default-unchanged",
        help="Reject a clean rebase when its snapshotted default branch moved",
    )
    old_default_parser.add_argument("--manifest", required=True)
    old_default_parser.add_argument("--repo", required=True)
    old_default_parser.set_defaults(func=validate_old_default_unchanged)

    status_parser = subparsers.add_parser("status", help="Report PRs associated with a fastlane run")
    status_parser.add_argument("--manifest", required=True)
    status_parser.add_argument("--repo", required=True)
    status_parser.set_defaults(func=status)
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        args.func(args)
    except FastlaneError as error:
        print(f"fastlane-rebase: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
