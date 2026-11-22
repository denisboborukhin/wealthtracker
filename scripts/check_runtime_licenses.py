#!/usr/bin/env python3
"""Check resolved Conan host-runtime licenses; Qt is reviewed separately.

A license expression must offer a branch without GPL/AGPL. LGPL is permitted.
Build-context tools and test-only nodes are not distributed runtime dependencies.
Multiple Conan license entries are checked individually; ambiguous/missing
metadata fails for review rather than silently passing.
"""
import argparse
import json
import re
from pathlib import Path


def allowed_expression(expression):
    tokens = re.findall(r"\(|\)|[A-Za-z0-9.+:-]+", expression)
    if "".join(tokens) != re.sub(r"\s+", "", expression) or not tokens:
        raise ValueError("Invalid license expression")
    position = 0

    def term():
        nonlocal position
        if position >= len(tokens):
            raise ValueError("Incomplete license expression")
        token = tokens[position]
        position += 1
        if token == "(":
            result = either()
            if position >= len(tokens) or tokens[position] != ")":
                raise ValueError("Unclosed license expression")
            position += 1
            return result
        if token in {"AND", "OR", "WITH", ")", "NOASSERTION", "NONE"}:
            raise ValueError("Missing usable license identifier")
        result = not re.match(r"^(?:GPL|AGPL)(?:-|$)", token)
        if position < len(tokens) and tokens[position] == "WITH":
            position += 1
            if position >= len(tokens) or tokens[position] in {"AND", "OR", "WITH", "(", ")"}:
                raise ValueError("Missing license exception")
            position += 1
        return result

    def together():
        nonlocal position
        result = term()
        while position < len(tokens) and tokens[position] == "AND":
            position += 1
            right = term()
            result = result and right
        return result

    def either():
        nonlocal position
        result = together()
        while position < len(tokens) and tokens[position] == "OR":
            position += 1
            right = together()
            result = result or right
        return result

    result = either()
    if position != len(tokens):
        raise ValueError("Unexpected license token")
    return bool(result)


def findings(document):
    nodes = document["graph"]["nodes"]
    if not isinstance(nodes, dict) or not nodes:
        raise ValueError("Resolved Conan graph has no nodes")
    errors = []
    for key, node in nodes.items():
        if str(key) == "0" or node.get("recipe") == "Consumer":
            continue
        if node.get("context") == "build" or node.get("test") is True:
            continue
        reference = node.get("ref") or str(key)
        if node.get("context") != "host":
            errors.append(f"{reference}: missing/unknown dependency context")
            continue
        licenses = node.get("license")
        if isinstance(licenses, str):
            licenses = [licenses]
        if not isinstance(licenses, list) or not licenses:
            errors.append(f"{reference}: missing runtime license metadata")
            continue
        for expression in licenses:
            try:
                if not isinstance(expression, str) or not allowed_expression(expression):
                    errors.append(f"{reference}: forbidden runtime license {expression!r}")
            except ValueError as error:
                errors.append(f"{reference}: {error}: {expression!r}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("graph", type=Path, help="Conan graph info --format=json output")
    arguments = parser.parse_args()
    try:
        errors = findings(json.loads(arguments.graph.read_text(encoding="utf-8")))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"Cannot check resolved runtime licenses: {error}\n")
    if errors:
        parser.exit(1, "\n".join(errors) + "\n")
    print("Resolved Conan runtime license check passed; Qt notices require separate review.")


if __name__ == "__main__":
    main()
