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
# Assumes nuget.exe exists somewhere under /usr
nugetPath=$(eval "find 2>/dev/null /usr -name nuget.exe")
nugetConfigPath="~/.nuget/NuGet/NuGet.Config" # If it aint here, add logic to find it or update it yerself
nugetSource="${scriptPath}/NugetSource"
nugetSources=$(eval "mono ${nugetPath} sources list -ConfigFile ${nugetConfigPath}")

build="scons platform=linuxbsd module_mono_enabled=yes"

if "$clean" == "true"; then
  echo "Cleaning ..."
  eval $"("${build} -c")"
  $(eval $"rm -R ${scriptPath}/bin")
fi

echo "Building..."
eval $"("${build}")"

echo "Generating glue..."
$(eval $"${scriptPath}/bin/godot.linuxbsd.editor.x86_64.mono --generate-mono-glue ${scriptPath}/modules/mono/glue") 

echo "Building managed libraries..."
$(eval $"${scriptPath}/modules/mono/build_scripts/build_assemblies.py --godot-output-dir=${scriptPath}/bin")

# Add or update nuget package
if ! echo "${nugetSources}" | grep "${nugetSource}"; then
  echo "Adding new NuGet Source..."
  $(eval "dotnet nuget add source ${nugetSource} --name GodotCppNugetSource")
else
  echo "Updating existing NuGetSource..."
  echo "dotnet nuget update source ${nugetSource} --name GodotCppNugetSource"
  $(eval "dotnet nuget update source ${nugetSource} --name GodotCppNugetSource")
fi