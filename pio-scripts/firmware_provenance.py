"""Git identities shared by compiled firmware and rolling update manifests."""
import json
import os
import re


def provenance(build_commit, environ=None):
    environ = os.environ if environ is None else environ
    source_commit = build_commit
    if environ.get("GITHUB_EVENT_NAME") == "pull_request":
        with open(environ["GITHUB_EVENT_PATH"], encoding="utf-8") as fp:
            source_commit = json.load(fp)["pull_request"]["head"]["sha"]
        if not isinstance(source_commit, str) or not re.fullmatch(r"[0-9a-fA-F]{40}", source_commit):
            raise ValueError("Invalid PR source commit")
    return {
        "commit": build_commit,
        "source_commit": source_commit,
        "tested_merge_commit": build_commit,
    }
