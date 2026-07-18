#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 4 ]]; then
  echo "usage: $0 <repository> <tag> <output-directory> <source-directory>" >&2
  exit 64
fi

repository=$1
tag=$2
output_directory=$3
source_directory=$4
artifacts=(mpf-linux-x64 mpf-macos mpf-windows-x64)

rm -rf "${output_directory}"
mkdir -p "${output_directory}"

release_state=$(gh release view "${tag}" --repo "${repository}" \
  --json tagName,isDraft,isPrerelease \
  --jq '[.tagName, .isDraft, .isPrerelease] | @tsv')
IFS=$'\t' read -r actual_tag is_draft is_prerelease <<< "${release_state}"
if [[ "${actual_tag}" != "${tag}" || "${is_draft}" != "false" ||
      "${is_prerelease}" != "false" ]]; then
  echo "release ${tag} is missing, draft, or prerelease: ${release_state}" >&2
  exit 1
fi

actual_assets=$(gh release view "${tag}" --repo "${repository}" \
  --json assets --jq '.assets[].name' | sort)
expected_assets=()
for artifact in "${artifacts[@]}"; do
  expected_assets+=("${artifact}.zip" "${artifact}.zip.sha256")
done
expected_asset_list=$(printf '%s\n' "${expected_assets[@]}" | sort)
if [[ "${actual_assets}" != "${expected_asset_list}" ]]; then
  echo "release assets do not match the required platform set" >&2
  printf 'expected:\n%s\n' "${expected_asset_list}" >&2
  printf 'actual:\n%s\n' "${actual_assets}" >&2
  exit 1
fi

gh release download "${tag}" --repo "${repository}" --dir "${output_directory}"
for artifact in "${artifacts[@]}"; do
  cmake \
    "-DARCHIVE=${output_directory}/${artifact}.zip" \
    "-DCHECKSUM=${output_directory}/${artifact}.zip.sha256" \
    "-DARTIFACT=${artifact}" \
    "-DVERSION=${tag}" \
    "-DLICENSE_FILE=${source_directory}/LICENSE" \
    "-DVERIFY_DIR=${output_directory}/verify-${artifact}" \
    -P "${source_directory}/cmake/verify_release_archive.cmake"
  gh attestation verify "${output_directory}/${artifact}.zip" --repo "${repository}"
done

echo "Release ${tag} passed published-asset verification."
