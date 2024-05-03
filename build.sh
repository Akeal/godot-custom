#!/bin/bash
<<<<<<< HEAD

clean="false"

while (( $# >= 1 )); do 
    case $1 in
    --clean) clean="true";;
    *) break;
    esac;
    shift
done

=======
>>>>>>> d76c8750af (post_process and build)
scriptPath=$(realpath .)
nugetPath="/usr/local/bin/nuget.exe"
nugetConfigPath="/home/nick/.nuget/NuGet/NuGet.Config"
nugetSource="${scriptPath}/NugetSource"
nugetSources=$(eval "mono ${nugetPath} sources list -ConfigFile ${nugetConfigPath}")
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
if ! echo "${nugetSources}" | grep "${nugetSource}"; then
  $(eval "dotnet nuget add source ${nugetSource} --name GodotCppNugetSource")
fi
./modules/mono/build_scripts/build_assemblies.py --godot-output-dir ./bin --push-nupkgs-local ./NugetSource
<<<<<<< HEAD
#fi
=======
>>>>>>> d76c8750af (post_process and build)
