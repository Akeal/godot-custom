#!/bin/bash
clean="false"

while (( $# >= 1 )); do 
    case $1 in
      --clean) clean="true";;
      --proj=*) proj="${1#*=}";;
    *)
    esac;
    shift
done

scriptPath=$(realpath .)
# Assumes nuget.exe exists somewhere under /usr
nugetExePathCmd="find 2>/dev/null /usr -name nuget.exe"
echo "Finding nuget.exe..."
echo $nugetExePathCmd
nugetExePath=$(eval $nugetExePathCmd)
echo "Found ${nugetExePath}"

nugetSource="${scriptPath}/bin/GodotSharp/Tools/nupkgs"

nugetSourcesCmd="mono ${nugetExePath} sources list"

monoGlueModulesPath="${scriptPath}/modules/mono/glue"

build="scons platform=linuxbsd module_mono_enabled=yes"

if "$clean" == "true"; then
  echo "Cleaning ..."
  eval "${build} -c"
  eval "rm -R ${scriptPath}/bin"
fi

echo "Building..."
echo $build
eval $build

echo "Generating glue..."
glueCmd="${scriptPath}/bin/godot.linuxbsd.editor.x86_64.mono --headless --generate-mono-glue ${monoGlueModulesPath}"
echo $glueCmd
eval $glueCmd

echo "Checking current nuget sources..."
if [ -z ${proj+x} ]; then
  echo "Project not specified. Using default nuget config."
else
  echo "Updating project path ${proj} with newly built nuget packages."
  nugetSourcesCmd="${nugetSourcesCmd} -ConfigFile ${proj}/nuget.config"
fi
echo $nugetSourcesCmd
nugetSources=$(eval $nugetSourcesCmd)

# Add or update nuget package
echo "Checking nuget sources ..."
echo $nugetSources
echo "Looking for ${nugetSource}"
if ! [[ $nugetSources =~ "${nugetSource}" ]]; then
  echo "Adding new NuGet Source..."
  nugetSourceCmd="mono ${nugetExePath} sources add -name GodotCppNugetSource -source ${nugetSource}"
else
  echo "Updating existing NuGetSource..."
  nugetSourceCmd="mono ${nugetExePath} sources update -name GodotCppNugetSource"
fi

if [ -z ${proj+x} ]; then
  echo "Project not specified. Using default nuget config."
else
  echo "Updating project path ${proj} with newly built nuget packages."
  nugetSourceCmd="${nugetSourceCmd} -ConfigFile ${proj}/nuget.config"
fi

echo $nugetSourceCmd
eval $nugetSourceCmd

echo "Building managed libraries..."
buildManagedLibraries="${scriptPath}/modules/mono/build_scripts/build_assemblies.py --godot-output-dir ${scriptPath}/bin --push-nupkgs-local ${nugetSource}"
echo $buildManagedLibraries
eval $buildManagedLibraries