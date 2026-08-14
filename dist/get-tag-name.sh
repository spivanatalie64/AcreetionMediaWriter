#!/bin/bash
# Prints the full release tag (e.g. "v5.4.1") when the build runs on a tag ref.
# The "v" prefix is kept so that the release, its assets and the website's
# releases/latest link all target the SAME tag.

if [[ "$GITHUB_REF" == refs/tags/* ]]; then
    TAG="${GITHUB_REF#refs/tags/}"
    if [[ "$TAG" =~ ^v?[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        echo "$TAG"
        exit 0
    fi
fi

# Fallback: extract the first semver from any other ref (e.g. branch pushes)
echo "$GITHUB_REF" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1
