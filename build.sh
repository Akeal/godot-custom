#!/bin/bash
scriptPath=$(realpath .)
nugetPath="/usr/local/bin/nuget.exe"
nugetConfigPath="/home/nick/.nuget/NuGet/NuGet.Config"
nugetSource="${scriptPath}/NugetSource"
nugetSources=$(eval "mono ${nugetPath} sources list -ConfigFile ${nugetConfigPath}")
#echo "$scriptPath"
#echo "$nugetSource"
#echo "$nugetSources"
scons platform=linuxbsd module_mono_enabled=yes
#echo "${scriptPath}/bin/godot.linuxbsd.editor.x86_64.mono --generate-mono-glue ${scriptPath}/modules/mono/glue" | bash
if ! echo "${nugetSources}" | grep "${nugetSource}"; then
  $(eval "dotnet nuget add source ${nugetSource} --name GodotCppNugetSource")
fi
./modules/mono/build_scripts/build_assemblies.py --godot-output-dir ./bin --push-nupkgs-local ./NugetSource
