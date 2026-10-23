param(
    [string]$Source='E:/ThorNative/research/eden-thor',
    [ValidateSet('native','apk')][string]$Stage='apk'
)
$ErrorActionPreference='Stop'
$env:JAVA_HOME='C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot'
$env:ANDROID_SDK_ROOT='E:/DevTools/Android/Sdk'
$env:GRADLE_USER_HOME='E:/DevTools/Gradle/UserHome'
$env:CMAKE_BUILD_PARALLEL_LEVEL='4'
$env:PATH='E:/DevTools/PortableGit-2.56.0.3/cmd;E:/DevTools/PortableGit-2.56.0.3/usr/bin;E:/DevTools/Android/Sdk/ndk/28.2.13676358/prebuilt/windows-x86_64/bin;'+$env:PATH
$cmake='-PYUZU_ANDROID_ARGS=-DPATCH_EXE=E:/DevTools/PortableGit-2.56.0.3/usr/bin/patch.exe -DGIT_EXECUTABLE=E:/DevTools/PortableGit-2.56.0.3/cmd/git.exe -DGLSLANGVALIDATOR=E:/DevTools/glslang-16.6.0/bin/glslang.exe -DCMAKE_JOB_POOLS=thor_compile=4 -DCMAKE_JOB_POOL_COMPILE=thor_compile'
$project=Join-Path $Source 'src/android'
$task=if($Stage -eq 'native'){'externalNativeBuildMainlineRelWithDebInfo'}else{'assembleMainlineRelWithDebInfo'}
& "$project/gradlew.bat" --project-dir $project --no-daemon --max-workers=2 $cmake $task
if($LASTEXITCODE -ne 0){throw "Eden $Stage build failed: $LASTEXITCODE"}
