#!/bin/bash

clean="false"

while (( $# >= 1 )); do 
    case $1 in
    --clean) clean="true";;
    *) break;
    esac;
    shift
done

scriptPath=$(realpath .)
nugetPath="/usr/local/bin/nuget.exe"
nugetConfigPath="/home/nick/.nuget/NuGet/NuGet.Config"
nugetSource="${scriptPath}/NugetSource"
nugetSources=$(eval "mono ${nugetPath} sources list -ConfigFile ${nugetConfigPath}")

#echo "$scriptPath"
#echo "$nugetSource"
#echo "$nugetSources"

build="scons platform=linuxbsd module_mono_enabled=yes"

if "$clean" == "true"; then
  echo "Cleaning ..."
  build="$build -c"
fi
eval $"("${build}")"

#if "$clean" == "false"; then
echo "${scriptPath}/bin/godot.linuxbsd.editor.x86_64.mono --generate-mono-glue ${scriptPath}/modules/mono/glue" | bash
if ! echo "${nugetSources}" | grep "${nugetSource}"; then
  $(eval "dotnet nuget add source ${nugetSource} --name GodotCppNugetSource")
fi
./modules/mono/build_scripts/build_assemblies.py --godot-output-dir ./bin --push-nupkgs-local ./NugetSource
#fi
