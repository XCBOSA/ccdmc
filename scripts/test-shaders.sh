#!/bin/sh
set -eu
if [ "$#" -ne 1 ]; then
    printf 'Usage: sh %s /absolute/path/to/xcsdl-msl-metal-test\n' "$0" >&2
    exit 2
fi
case "$1" in /*) tool=$1 ;; *) printf 'Provide an absolute test executable path.\n' >&2; exit 2 ;; esac
cd "$(dirname "$0")/../Contents/Shaders"

# The CCD shader sandbox does not import private headers. Keep the two
# independent include-free address helpers byte-identical.
material_abi() { sed -n '/^\/\/ Material atlas ABI:/,/^\/\/ End material atlas ABI\./p' "$1"; }
abi=$(material_abi textured.metal)
if [ -z "$abi" ] || [ "$abi" != "$(material_abi world_lighting.metal)" ]; then
    printf 'Material atlas address helpers do not match.\n' >&2
    exit 1
fi

# Use the SDK's real parser/re-emitter and Metal compiler, not a local
# allowlist. Compile every stage, including the fixed renderer ports.
"$tool" \
    present.metal vertex xcSDLVertex present.metal fragment xcSDLFragment \
    geometry.metal vertex xcSDLGeometryVertex geometry.metal fragment xcSDLGeometryFragment \
    textured.metal vertex xcSDLTexturedVertex textured.metal fragment xcSDLTexturedFragment \
    textured.metal fragment xcSDLResidentFragment \
    canvas.metal vertex xcSDLCanvasVertex canvas.metal fragment xcSDLCanvasFragment \
    world_lighting.metal vertex shadowCaster world_lighting.metal vertex worldVertex \
    world_lighting.metal fragment worldFragment world_lighting.metal fragment worldUnshadowedFragment \
    world_lighting.metal fragment reflectionFragment \
    world_lighting.metal fragment underwaterFragment \
    atmosphere.metal vertex skyVertex atmosphere.metal fragment skyFragment \
    atmosphere.metal fragment skyComposite atmosphere.metal fragment skyReflection atmosphere.metal fragment basicMoon \
    water.metal vertex waterVertex water.metal fragment waterFragment water.metal vertex waterFlatVertex \
    water.metal fragment waterBelow
"$tool" --world world_lighting.metal
