#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    printf 'Usage: sh %s /path/to/new/CCDMC-Tests.ccdproj\n' "$0" >&2
    exit 2
fi
case "$1" in
    /*.ccdproj) target=$1 ;;
    *) printf 'Use an absolute path ending in .ccdproj.\n' >&2; exit 2 ;;
esac
command -v ruby >/dev/null || { printf 'Ruby is required.\n' >&2; exit 2; }
cd "$(dirname "$0")/.."

# Reserve a separate package and deliberately exclude both saves and build cache.
# Never add testing code or change settings in the playable project.
mkdir "$target"
mkdir "$target/Contents"
cp -R Contents/game Contents/render Contents/save Contents/ui Contents/world "$target/Contents/"
cp -R Tests/core "$target/Contents/tests"
cp Tests/core/ccdmc_mesh_publication_test.h Tests/core/ccdmc_entity_view_test.h \
    Tests/core/ccdmc_shadow_receivers_test.h "$target/Contents/render/"
cp Tests/engine_main.c "$target/Contents/main.c"
ruby -rjson -rsecurerandom -e '
    project = JSON.parse(File.read(ARGV.fetch(0)))
    project["projectId"] = SecureRandom.uuid.upcase
    project["mainArguments"] = "--world-test"
    File.write(ARGV.fetch(1), JSON.pretty_generate(project) + "\n")
' project.json "$target/project.json"
printf 'Open %s in CCD. Default run: --world-test.\n' "$target"
