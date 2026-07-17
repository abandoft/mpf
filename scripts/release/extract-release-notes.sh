#!/usr/bin/env bash

set -euo pipefail

if (( $# != 3 )); then
  echo "usage: $0 <changelog> <version> <output>" >&2
  exit 64
fi

changelog=$1
version=$2
output=$3

if [[ ! -f "$changelog" ]]; then
  echo "release changelog does not exist: $changelog" >&2
  exit 66
fi
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "release version must use MAJOR.MINOR.PATCH without a prefix: $version" >&2
  exit 65
fi

output_directory=$(dirname -- "$output")
mkdir -p -- "$output_directory"
temporary=$(mktemp "${output}.tmp.XXXXXX")
trap 'rm -f -- "$temporary"' EXIT

awk -v heading="## ${version}" -v source="$changelog" '
  {
    sub(/\r$/, "")
  }
  !found {
    if ($0 == heading) {
      found = 1
    }
    next
  }
  /^## / {
    exit
  }
  {
    if (!emitted && $0 == "") {
      next
    }
    emitted = 1
    if ($0 ~ /^- /) {
      entries++
    }
    print
  }
  END {
    if (!found) {
      printf "release section %s was not found in %s\n", heading, source > "/dev/stderr"
      exit 1
    }
    if (entries == 0) {
      printf "release section %s has no changelog entries\n", heading > "/dev/stderr"
      exit 1
    }
  }
' "$changelog" > "$temporary"

mv -- "$temporary" "$output"
trap - EXIT
