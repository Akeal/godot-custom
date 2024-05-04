#!/bin/bash
<<<<<<< HEAD
<<<<<<< HEAD
=======
>>>>>>> 5241b0105b (	modified:   NugetSource/Godot.NET.Sdk.4.3.100-dev.nupkg)

clean="false"

while (( $# >= 1 )); do 
    case $1 in
    --clean) clean="true";;
    *) break;
    esac;
    shift
done

<<<<<<< HEAD
=======
>>>>>>> d76c8750af (post_process and build)
=======
>>>>>>> 5241b0105b (	modified:   NugetSource/Godot.NET.Sdk.4.3.100-dev.nupkg)
scriptPath=$(realpath .)
nugetPath="/usr/local/bin/nuget.exe"
nugetConfigPath="/home/nick/.nuget/NuGet/NuGet.Config"
nugetSource="${scriptPath}/NugetSource"
nugetSources=$(eval "mono ${nugetPath} sources list -ConfigFile ${nugetConfigPath}")
<<<<<<< HEAD
<<<<<<< HEAD

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
=======
#echo "$scriptPath"
#echo "$nugetSource"
#echo "$nugetSources"
scons platform=linuxbsd module_mono_enabled=yes
#echo "${scriptPath}/bin/godot.linuxbsd.editor.x86_64.mono --generate-mono-glue ${scriptPath}/modules/mono/glue" | bash
>>>>>>> d76c8750af (post_process and build)
=======

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
>>>>>>> 5241b0105b (	modified:   NugetSource/Godot.NET.Sdk.4.3.100-dev.nupkg)
if ! echo "${nugetSources}" | grep "${nugetSource}"; then
  $(eval "dotnet nuget add source ${nugetSource} --name GodotCppNugetSource")
fi
./modules/mono/build_scripts/build_assemblies.py --godot-output-dir ./bin --push-nupkgs-local ./NugetSource
<<<<<<< HEAD
<<<<<<< HEAD
#fi
=======
>>>>>>> d76c8750af (post_process and build)
=======
#fi
>>>>>>> 5241b0105b (	modified:   NugetSource/Godot.NET.Sdk.4.3.100-dev.nupkg)
