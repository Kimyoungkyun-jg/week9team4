# Techlab4_Week07_Team1

## Windows team setup

Windows 10/11 x64 기준입니다. Git, Visual Studio 2022/2026의 **Desktop development with C++**(MSVC x64 도구, MSBuild), Windows 10/11 SDK를 설치하세요. 최초 의존성 설치에는 인터넷 연결이 필요합니다. legacy DirectX SDK, NuGet GUI 설정, 전역 vcpkg integration은 필요하지 않습니다.

저장소 루트의 명령 프롬프트/PowerShell에서 실행합니다.

```bat
SlnGenerator.bat
SlnGenerator.bat -Build
SlnGenerator.bat -Build -Configuration Debug
```

첫 명령은 의존성 확인·설치와 솔루션 생성을 수행합니다. 두 번째는 Release Editor/Game, 세 번째는 Debug Editor/Game까지 빌드합니다. 기존 `-SkipBuild`와 `-Configuration ObjViewer|Benchmark|Benchmark_Debug`도 지원합니다.

- `VCPKG_ROOT`를 우선 사용하며 미설정 시 `%USERPROFILE%\vcpkg`를 사용/최초 clone·bootstrap합니다. 환경 변수는 해당 설정 프로세스에만 적용합니다.
- 기존 classic LuaJIT·sol2는 `installed\x64-windows`에 설치합니다.
- ImGui 서브모듈이 비어 있으면 저장소에 기록된 커밋으로 초기화합니다.
- Audio는 classic `directxtk[xaudio2-9]:x64-windows`입니다. 기본 `installed`에 정상 설치되어 있으면 재사용합니다. 그렇지 않으면 같은 vcpkg의 **`installed-hitori-audio`** 별도 트리에 `--x-install-root`로 설치합니다. Audio 없는 기존 DirectXTK를 제거하거나 공유 설치를 재빌드하지 않습니다. Git pull, 패키지 삭제, 전역 VS 설정 변경도 하지 않습니다.
- 헤더뿐 아니라 `directxtk` core/`xaudio2-9` 설치 기록, Debug/Release import library 및 DLL을 확인합니다. 정상 설치 후 재실행하면 재설치하지 않습니다. 손상된 설치로 install이 복구되지 않으면 실패를 보고하며 임의 삭제하지 않습니다.
- 검증한 포트: **DirectXTK 2026-05-07#1 (may2026), DirectXMath 2026-06-12**, `x64-windows`, 동적 CRT 및 DirectXTK DLL. classic 방식이므로 공유 vcpkg checkout의 포트 버전을 사용하며 버전을 강제 업데이트/고정하지 않습니다. 실제 선택 버전은 설정 로그에 출력됩니다.
- Premake가 `DIRECTX_TOOLKIT_IMPORT`, Windows 10 대상 정의, 구성별 include/lib 및 DirectXTK DLL 복사를 설정합니다. Debug/Benchmark_Debug는 debug 라이브러리, 나머지는 release 라이브러리를 사용합니다. XAudio2 2.9는 Windows 내장 런타임이며 별도 redistributable 설치가 없습니다. LuaJIT DLL 자동 복사는 기존 방식대로 유지됩니다.

실패 시 `Build\Logs\SetupTeam.log`의 **FAILED stage**, vcpkg가 출력한 원인과 `<vcpkg>\buildtrees\directxtk` 로그를 확인하고 출력된 재실행 명령을 사용하세요. 실패 후에는 다음 단계로 넘어가거나 성공 메시지를 출력하지 않습니다. 소스에 파일을 추가한 경우 `SlnGenerator.bat`을 다시 실행하세요. 외부 설치를 이동/삭제한 경우에도 설정부터 재실행합니다.

이후에는 생성된 `Hitori.sln`(VS 2026에서는 `Hitori.slnx`)에서 구성 선택 후 빌드/F5를 사용할 수 있습니다. 실행 파일 출력 폴더에 필요한 DirectXTK·LuaJIT DLL이 자동 배치됩니다. 에셋·셰이더는 저장소 루트를 작업 디렉터리로 사용합니다(VS F5는 자동 설정).

```bat
rem 저장소 루트에서 실행
Build\Bin\Release-windows-x86_64\HitoriEditor.exe
Build\Bin\Release-windows-x86_64\HitoriGame.exe
```

사운드 API·설계·검증 방법은 [SoundManager 사용 문서](Source/Runtime/Audio/README.md)를 참고하세요.
