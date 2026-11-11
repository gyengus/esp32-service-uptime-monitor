#!/usr/bin/env python3
"""
Extracts @openapi YAML blocks from C/C++ doc comments across include/, src/, and lib/
and generates docs/api/openapi.yaml.
"""

import argparse
import copy
import os
import re
import sys
import textwrap
from pathlib import Path

try:
    import yaml
except ImportError:
    yaml = None

COMMENT_BLOCK_RE = re.compile(r"/\*\*(.*?)\*/", re.DOTALL)
VERSION_DEFINE_RE = re.compile(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"')
SERVER_ON_RE = re.compile(
    r'server\.on\(\s*"([^"]+)"\s*,\s*(HTTP_[A-Z]+)\s*,',
    re.MULTILINE,
)

SCAN_DIRS = ("include", "src", "lib")
SOURCE_EXTENSIONS = {".h", ".hpp", ".cpp"}
ROUTE_CHECK_FILES = (
    os.path.join("src", "Api.cpp"),
    os.path.join("lib", "FileManager", "src", "FileManager.cpp"),
)


def strip_cpp_comments(source: str) -> str:
    """Remove // line comments and /* ... */ block comments for route scanning."""
    no_block = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", no_block)


def normalize_route_path(raw_path: str) -> str:
    """Normalize C++ regex or OpenAPI templated path to a canonical pattern for drift checking."""
    path = raw_path
    if path.startswith("^") and path.endswith("$"):
        path = path[1:-1]
    path = path.replace(r"\\/", "/").replace(r"\/", "/")
    path = re.sub(r"\([^)]+\)", "{param}", path)
    path = re.sub(r"\{[^}]+\}", "{param}", path)
    return path


def extract_openapi_blocks(file_path: Path) -> list[str]:
    """Extract YAML payloads from /** ... @openapi ... */ comments in a C/C++ file."""
    content = file_path.read_text(encoding="utf-8")
    blocks: list[str] = []

    for match in COMMENT_BLOCK_RE.finditer(content):
        raw_block = match.group(1)
        if "@openapi" not in raw_block:
            continue

        cleaned_lines: list[str] = []
        found_marker = False
        for line in raw_block.splitlines():
            stripped = re.sub(r"^\s*\* ?", "", line)
            if not found_marker:
                marker_match = re.match(r"^\s*@openapi\s*(.*)$", stripped)
                if marker_match:
                    found_marker = True
                    trailing = marker_match.group(1).strip()
                    if trailing:
                        cleaned_lines.append(trailing)
                continue
            cleaned_lines.append(stripped)

        yaml_text = textwrap.dedent("\n".join(cleaned_lines)).strip()
        if yaml_text:
            blocks.append(yaml_text)

    return blocks


def deep_merge(target: dict, source: dict) -> dict:
    """Recursively merge source dict into target dict."""
    for key, value in source.items():
        if key == "tags" and isinstance(value, list) and isinstance(target.get(key), list):
            existing_names = {t.get("name") for t in target[key] if isinstance(t, dict)}
            for item in value:
                if isinstance(item, dict) and item.get("name") not in existing_names:
                    target[key].append(item)
                    existing_names.add(item.get("name"))
        elif isinstance(value, dict) and isinstance(target.get(key), dict):
            deep_merge(target[key], value)
        else:
            target[key] = copy.deepcopy(value)
    return target


def read_firmware_version(project_dir: Path) -> str:
    """Read FIRMWARE_VERSION from include/version.h if present."""
    version_file = project_dir / "include" / "version.h"
    if not version_file.is_file():
        return "1.0.0"
    match = VERSION_DEFINE_RE.search(version_file.read_text(encoding="utf-8"))
    return match.group(1) if match else "1.0.0"


def find_unannotated_routes(project_dir: Path, spec: dict) -> list[tuple[str, str, str]]:
    """Return (file, method, path) tuples for server.on routes missing from OpenAPI paths."""
    documented: set[tuple[str, str]] = set()
    for path, methods in (spec.get("paths") or {}).items():
        if not isinstance(methods, dict):
            continue
        norm_path = normalize_route_path(path)
        for method in methods:
            if method.lower() in {"get", "post", "put", "delete", "patch", "options", "head"}:
                documented.add((method.lower(), norm_path))

    missing: list[tuple[str, str, str]] = []
    for rel_path in ROUTE_CHECK_FILES:
        file_path = project_dir / rel_path
        if not file_path.is_file():
            continue
        code = strip_cpp_comments(file_path.read_text(encoding="utf-8"))
        for match in SERVER_ON_RE.finditer(code):
            raw_route = match.group(1)
            http_method = match.group(2).replace("HTTP_", "").lower()
            norm_route = normalize_route_path(raw_route)
            if (http_method, norm_route) not in documented:
                missing.append((rel_path, http_method.upper(), raw_route))

    return missing


def build_openapi_spec(project_dir: Path) -> dict:
    """Scan project sources and assemble the complete OpenAPI specification dict."""
    if yaml is None:
        raise RuntimeError("PyYAML is required to generate openapi.yaml (pip install pyyaml)")

    merged: dict = {}
    for scan_dir in SCAN_DIRS:
        root = project_dir / scan_dir
        if not root.is_dir():
            continue
        files = sorted(
            p for p in root.rglob("*")
            if p.is_file() and p.suffix in SOURCE_EXTENSIONS
        )
        for file_path in files:
            for block in extract_openapi_blocks(file_path):
                try:
                    parsed = yaml.safe_load(block)
                except yaml.YAMLError as exc:
                    raise ValueError(f"Invalid @openapi YAML in {file_path}: {exc}") from exc
                if isinstance(parsed, dict):
                    path_entries = {k: v for k, v in parsed.items() if isinstance(k, str) and k.startswith("/")}
                    other_entries = {k: v for k, v in parsed.items() if not (isinstance(k, str) and k.startswith("/"))}
                    if other_entries:
                        deep_merge(merged, other_entries)
                    if path_entries:
                        deep_merge(merged.setdefault("paths", {}), path_entries)

    if not merged:
        raise RuntimeError("No @openapi blocks found in source files.")

    info = merged.setdefault("info", {})
    if info.get("version") in (None, "", "auto"):
        info["version"] = read_firmware_version(project_dir)

    return merged


def collect_schema_refs(node: object, refs: set[str]) -> None:
    """Recursively collect schema names referenced via $ref."""
    if isinstance(node, dict):
        ref = node.get("$ref")
        if isinstance(ref, str) and ref.startswith("#/components/schemas/"):
            refs.add(ref.rsplit("/", 1)[-1])
        for value in node.values():
            collect_schema_refs(value, refs)
    elif isinstance(node, list):
        for item in node:
            collect_schema_refs(item, refs)


def filter_internal_operations(spec: dict) -> dict:
    """Remove operations marked with x-internal: true along with orphaned tags/schemas."""
    filtered = copy.deepcopy(spec)
    paths = filtered.get("paths") or {}
    used_tags: set[str] = set()
    kept_paths: dict = {}

    for path, methods in paths.items():
        if not isinstance(methods, dict):
            continue
        kept_methods: dict = {}
        for method, operation in methods.items():
            if isinstance(operation, dict) and operation.get("x-internal"):
                continue
            kept_methods[method] = operation
            if isinstance(operation, dict):
                for tag in operation.get("tags") or []:
                    used_tags.add(tag)
        if kept_methods:
            kept_paths[path] = kept_methods

    filtered["paths"] = kept_paths

    if isinstance(filtered.get("tags"), list):
        filtered["tags"] = [
            tag for tag in filtered["tags"]
            if isinstance(tag, dict) and tag.get("name") in used_tags
        ]

    schemas = (filtered.get("components") or {}).get("schemas")
    if isinstance(schemas, dict):
        referenced: set[str] = set()
        collect_schema_refs(kept_paths, referenced)
        # Follow transitive $ref chains between schemas until fixed point
        expanded = set(referenced)
        while expanded:
            next_refs: set[str] = set()
            for name in expanded:
                if name in schemas:
                    collect_schema_refs(schemas[name], next_refs)
            expanded = next_refs - referenced
            referenced |= expanded
        filtered["components"]["schemas"] = {
            name: schema for name, schema in schemas.items() if name in referenced
        }

    return filtered


def render_openapi_yaml(spec: dict) -> str:
    """Serialize OpenAPI spec dict to a deterministic YAML string with header comment."""
    if yaml is None:
        raise RuntimeError("PyYAML is required to render openapi.yaml")

    header = (
        "# AUTO-GENERATED FILE - DO NOT EDIT MANUALLY\n"
        "# Generated from @openapi blocks in include/, src/, and lib/ by scripts/openapi_generator.py\n"
    )
    body = yaml.safe_dump(
        spec,
        sort_keys=False,
        allow_unicode=True,
        width=120,
        default_flow_style=False,
    )
    return header + body


def ensure_openapi_spec(
    project_dir: str | Path,
    output_rel: str = os.path.join("docs", "api", "openapi.yaml"),
    strict: bool = False,
    include_internal: bool = False,
) -> bool:
    """Generate docs/api/openapi.yaml if changed. Returns True if file was written."""
    if yaml is None:
        print("[OpenAPI] PyYAML not installed; skipping OpenAPI generation.")
        return False

    root = Path(project_dir).resolve()
    spec = build_openapi_spec(root)

    missing = find_unannotated_routes(root, spec)
    for rel_file, method, route in missing:
        print(
            f"[OpenAPI] WARNING: Route '{method} {route}' in {rel_file} has no @openapi documentation.",
            file=sys.stderr,
        )
    if strict and missing:
        raise RuntimeError(f"{len(missing)} route(s) are missing @openapi documentation.")

    output_spec = spec if include_internal else filter_internal_operations(spec)
    rendered = render_openapi_yaml(output_spec)
    output_path = root / output_rel
    output_path.parent.mkdir(parents=True, exist_ok=True)

    if output_path.is_file() and output_path.read_text(encoding="utf-8") == rendered:
        return False

    output_path.write_text(rendered, encoding="utf-8")
    print(f"[OpenAPI] Updated {output_rel}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate OpenAPI YAML from C++ @openapi blocks.")
    parser.add_argument(
        "--project-dir",
        default=str(Path(__file__).resolve().parent.parent),
        help="Project root directory",
    )
    parser.add_argument(
        "--output",
        default=os.path.join("docs", "api", "openapi.yaml"),
        help="Output YAML path relative to project root",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Exit non-zero if any server.on route lacks an @openapi block",
    )
    parser.add_argument(
        "--include-internal",
        action="store_true",
        help="Include endpoints marked with x-internal: true in the generated YAML",
    )
    args = parser.parse_args()

    try:
        ensure_openapi_spec(
            args.project_dir,
            output_rel=args.output,
            strict=args.strict,
            include_internal=args.include_internal,
        )
    except Exception as exc:
        print(f"[OpenAPI] ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
