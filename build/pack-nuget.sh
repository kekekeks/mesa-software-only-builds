#!/usr/bin/env bash
# Package the built single-file software Mesa binaries into NuGet packages.
#
# Produces, for every RID directory found under artifacts/:
#   <id>.<rid>   -> runtimes/<rid>/native/libsoftmesa.<ext>
# plus a meta package:
#   <id>         -> depends on every per-RID package
#
# Mirrors the layout of ~/Projects/Avalonia.Controls.Keyboard.NativeAssets but
# without its private build-common/Nuke dependency: plain `dotnet pack`.
#
# Env:
#   PKG_ID      package id             (default: unofficial.mesa.softwarerenderer)
#   VERSION     package version        (default: 0.1.0-local)
#   OUTPUT      output dir for .nupkg   (default: artifacts/packages)
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$SCRIPT_DIR/.." && pwd)

PKG_ID="${PKG_ID:-unofficial.mesa.softwarerenderer}"
VERSION="${VERSION:-0.1.0-local}"
OUTPUT="${OUTPUT:-$REPO/artifacts/packages}"
NATIVE_ROOT="$REPO/artifacts"
WORK="$REPO/artifacts/_pack"
AUTHORS="${PKG_AUTHORS:-mesa-software-only-builds}"
PROJECT_URL="https://github.com/kekekeks/mesa-software-only-builds"
DESC="Self-contained software Mesa (llvmpipe OpenGL + lavapipe Vulkan) exposing only eglGetProcAddress and vkGetInstanceProcAddr."

rm -rf "$WORK"
mkdir -p "$WORK" "$OUTPUT"

# Map an artifact filename glob per-RID. Linux only for now.
rids=()
for d in "$NATIVE_ROOT"/*/; do
    rid=$(basename "$d")
    case "$rid" in
        _pack|packages) continue ;;
    esac
    # Must contain at least one shared object / dylib / dll.
    if compgen -G "$d"'*.so' >/dev/null || compgen -G "$d"'*.dylib' >/dev/null || compgen -G "$d"'*.dll' >/dev/null; then
        rids+=("$rid")
    fi
done

if [[ ${#rids[@]} -eq 0 ]]; then
    echo "No native artifacts found under $NATIVE_ROOT" >&2
    exit 1
fi

pack_rid() {
    local rid="$1"
    local id="$PKG_ID.$rid"
    local proj="$WORK/$id"
    mkdir -p "$proj"
    cat > "$proj/$id.csproj" <<EOF
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>netstandard2.0</TargetFramework>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <IncludeBuildOutput>false</IncludeBuildOutput>
    <NoBuild>true</NoBuild>
    <GenerateAssemblyInfo>false</GenerateAssemblyInfo>
    <SuppressDependenciesWhenPacking>true</SuppressDependenciesWhenPacking>
    <NoWarn>\$(NoWarn);NU5128;NU5100;NU5127</NoWarn>
    <PackageId>$id</PackageId>
    <Version>$VERSION</Version>
    <Authors>$AUTHORS</Authors>
    <Description>$DESC Native binary for $rid.</Description>
    <PackageProjectUrl>$PROJECT_URL</PackageProjectUrl>
  </PropertyGroup>
  <ItemGroup>
    <None Include="$NATIVE_ROOT/$rid/**/*" Pack="true" PackagePath="runtimes/$rid/native/%(RecursiveDir)%(Filename)%(Extension)" />
  </ItemGroup>
</Project>
EOF
    echo "==> pack $id"
    dotnet pack "$proj/$id.csproj" -c Release -o "$OUTPUT" -p:NuGetAudit=false --nologo -v quiet
}

pack_meta() {
    local id="$PKG_ID"
    local proj="$WORK/$id"
    mkdir -p "$proj"
    local deps=""
    for rid in "${rids[@]}"; do
        deps="$deps    <PackageReference Include=\"$PKG_ID.$rid\" Version=\"$VERSION\" />
"
    done
    cat > "$proj/$id.csproj" <<EOF
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>netstandard2.0</TargetFramework>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <IncludeBuildOutput>false</IncludeBuildOutput>
    <NoBuild>true</NoBuild>
    <GenerateAssemblyInfo>false</GenerateAssemblyInfo>
    <NoWarn>\$(NoWarn);NU5128;NU5100;NU5127</NoWarn>
    <PackageId>$id</PackageId>
    <Version>$VERSION</Version>
    <Authors>$AUTHORS</Authors>
    <Description>$DESC Meta-package referencing every per-RID native binary package.</Description>
    <PackageProjectUrl>$PROJECT_URL</PackageProjectUrl>
  </PropertyGroup>
  <ItemGroup>
$deps  </ItemGroup>
</Project>
EOF
    echo "==> pack $id (meta)"
    dotnet pack "$proj/$id.csproj" -c Release -o "$OUTPUT" -p:NuGetAudit=false \
        --source "https://api.nuget.org/v3/index.json" --source "$OUTPUT" \
        --nologo -v quiet
}

for rid in "${rids[@]}"; do
    pack_rid "$rid"
done
pack_meta

echo "==> Produced packages:"
ls -la "$OUTPUT"/*.nupkg
