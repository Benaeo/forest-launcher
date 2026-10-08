"""Parse the shared Markdown changelog for release tooling and the launcher."""
import re

HEADING = re.compile(r"^##\s+(?:\[)?(?:v)?(\d+\.\d+\.\d+)(?:\])?(?:\s+-\s+[^\r\n]+)?\s*$", re.ASCII)
FENCE = re.compile(r"^\s{0,3}(`{3,}|~{3,})")


def entries(text):
    """Return version/body pairs without treating code-block examples as releases."""
    result, seen = [], set()
    version, body, fence = None, [], ""

    def flush():
        if version is not None:
            result.append({"version": version, "body": "".join(body).strip()})

    for line in text.splitlines(keepends=True):
        match = FENCE.match(line)
        if fence:
            if match and match[1][0] == fence[0] and len(match[1]) >= len(fence) and not line.strip().strip(fence[0]):
                fence = ""
            body.append(line)
            continue
        if match:
            fence = match[1]
            body.append(line)
            continue
        heading = HEADING.fullmatch(line.rstrip("\r\n"))
        if heading:
            flush()
            version, body = heading[1], []
            if version in seen:
                raise ValueError(f"Duplicate changelog version: {version}")
            seen.add(version)
        elif re.fullmatch(r"##\s+\[?Unreleased\]?\s*", line.rstrip("\r\n"), flags=re.IGNORECASE):
            flush()
            version, body = None, []
        else:
            body.append(line)
    flush()
    return result
