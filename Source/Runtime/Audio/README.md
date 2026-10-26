# 게임 사운드

`UGameInstance::GetSoundManager()`가 공용 `FSoundManager`를 반환합니다. 게임 스레드에서 호출하세요. 각 GameInstance가 AudioEngine, WAV 캐시, 재생 instance를 소유하므로 코인 액터가 삭제되어도 소리가 유지됩니다. GameInstance 초기화에서 기존 WAV 9개를 preload하며 파일은 변경/복사하지 않습니다. 경로는 기존 엔진처럼 저장소 루트 작업 디렉터리 기준 UTF-8입니다.

```cpp
#include "Engine/GameInstance.h"
#include "Audio/SoundManager.h"

if (auto* Instance = GetWorld()->GetGameInstance())
    if (auto* Sound = Instance->GetSoundManager())
    {
        Sound->PlaySFX("Assets/Sounds/coin.wav");
        Sound->SetSFXVolume(0.7f);
        Sound->SetMaxSFXVoices(32);
    }
```

새 BGM WAV를 준비한 후 호출하는 예시입니다(현재 BGM 에셋과 자동 재생은 없습니다).

```cpp
Sound->PlayBGM(MusicPath, true); // WAV 경로 전달, true = 반복
Sound->SetBGMVolume(0.5f);
Sound->PauseBGM();
Sound->ResumeBGM();
Sound->StopBGM();
```

동일한 정규화 경로의 BGM이 재생/일시정지 중이면 중복 요청을 무시합니다. 반복 여부를 바꾸려면 StopBGM 후 재생합니다. 다른 곡은 로드·재생에 성공한 뒤 교체하므로 파일 오류로 기존 곡을 잃지 않습니다. `PauseBGM`은 음악만 멈추고 월드 일시정지는 AudioEngine 전체를 suspend합니다. 볼륨은 0~1로 제한하며 BGM/SFX 모두 **현재 재생 중인 소리와 이후 소리**에 적용합니다. 개별 SFX Volume과 공용 SFXVolume을 곱합니다.

SFX 기본 상한은 `SoundManager.cpp`의 `MaxSFXVoices = 32`이며 초과 시 새 요청을 버립니다. 동일 WAV의 instance 여러 개로 중첩 재생합니다. 매 Update와 새 요청 시 STOPPED instance를 회수하며 WAV 데이터는 세션 동안 캐시합니다. 상한을 낮추면 초과 instance는 즉시 정리합니다. `StopAll`은 현재 소리만 정리하고 캐시는 유지합니다.

연결한 이벤트:

| 음원 (`Assets/Sounds/`) | 호출 위치/조건 |
| --- | --- |
| `coin.wav` | CoinActor OnOverlapBegin, 플레이어/컨트롤러 확인 및 bCollected 설정 뒤, 점수·HUD·Destroy 전 단발 |
| `footstep00.wav`~`footstep04.wav` | PlayerPawn, sweep 및 wall slide 이후 실제 XY 이동을 합산해 PostPhysics에서 프레임당 한 번 판정, 매 걸음 균등 선택(연속 선택 허용) |
| `Damaged.wav` | PlayerPawn SetLife에서 생명 값이 실제 감소하고 BeginPlay한 경우. 현재 이 함수를 호출하는 피해 규칙은 발견되지 않았으며 새 피해 규칙은 추가하지 않음 |
| `GameClear.wav`, `GameOver.wav` | PacmanGameMode ShowGameResult의 기존 bResultShown 확정 지점에서 한 번. 기존 Lua 승패 규칙 유지 |

발걸음 간격은 `FootstepCadence.h`의 기본 `IntervalSeconds = 0.35f`, 또는 `Player->SetFootstepInterval(Seconds)`로 조절합니다. 입력 전후 위치가 아닌 **충돌 처리 전후 실제 위치 차이**를 수집하므로 완전한 벽 막힘/회전/프레임 사이 텔레포트는 걸음이 아닙니다. 이동 callback 중 요청 이동보다 큰 위치 변경도 제외합니다. 이동 입력이 여러 번 있어도 PostPhysics에서 합산한 XY 변위로 한 번 판단합니다. 정지하면 타이머를 리셋하며, 지연 프레임에서 최대 한 번 재생하고 누적된 걸음을 몰아서 재생하지 않습니다. 실제 유효 이동이 계속될 때 FPS에 관계없이 약 0.35초 간격입니다. 일시정지 시간은 타이머에 들어가지 않습니다.

AudioEngine Update는 LaunchEngineLoop → UEngine::UpdateAudio에서 실행하여 PIE/월드가 일시정지해도 자원을 관리합니다. `UWorld::SetPaused`에서 suspend/resume하고 `EndPlay`(씬 교체 포함)에서 StopAll합니다. GameInstance 종료는 instance → WAV cache → AudioEngine → COM 순서로 정리합니다. Play→Stop→Play는 새 관리자와 새 발걸음 상태를 사용합니다.

파일 누락/잘못된 WAV/재생 실패는 false를 반환하고 경로별 한 번 로그를 남깁니다(Editor Output Log, standalone Game은 VS 디버거 출력). 해당 세션의 실패 경로는 다시 로딩하지 않으므로 수정 후 PIE를 다시 시작하세요. 오디오 장치 초기화 실패/제거 시 게임은 계속되며 장치 문제를 로그로 알립니다. 출력 장치를 복구한 뒤 세션/앱을 다시 시작하세요. 설정 시 라이브러리 누락은 이 런타임 정책과 별개로 명시적으로 실패합니다.

공식 근거: [DirectXTK Audio](https://github.com/microsoft/DirectXTK/wiki/Audio), [AudioEngine](https://github.com/microsoft/DirectXTK/wiki/AudioEngine), [vcpkg classic install 및 기능 변경](https://learn.microsoft.com/en-us/vcpkg/commands/install). 현재 vcpkg의 `ports/directxtk/vcpkg.json`, `portfile.cmake`, 설치된 CMake export와 Audio.h를 함께 확인했습니다.

## 검증

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File Tests\Run-AudioTests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tests\Run-AudioTests.ps1 -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File Tests\Test-AudioDependency.ps1
```

먼저 해당 구성의 Editor/Game을 빌드하세요. 실제 DLL/WAV를 사용한 음원 로딩, 동일 효과음 중첩, 상한, 일시정지 중 Update, 완료 자원 회수, 오류 중복 로그, 세션 재초기화와 30/60/144 FPS 간격을 검사합니다. 테스트는 SFX 볼륨을 0으로 두며 음악은 Build 아래 테스트 전용 무음 WAV를 생성합니다. 의존성 테스트는 임시 fixture로 feature·구성·아키텍처 누락을 검사하며 공유 패키지를 변경하지 않습니다. 청취 검증과 구분하세요.

수동 확인: Editor에서 Pacman 씬을 열고 Play, 코인을 연속 획득해 중첩 및 삭제 후 재생 지속을 듣습니다. 기본 Lua는 현재 1개 코인으로 클리어하므로 승패 규칙을 바꾸지 않고 연속 획득 확인이 제한될 수 있습니다. 이동/정지/벽 정면/벽을 따라 이동/회전, Pause→Resume, Play→Stop→Play를 반복하세요. 다섯 발걸음의 종류·음량·체감 간격은 청취로 최종 확인해야 합니다. 피해음은 실제 피해 규칙이 아직 없어 SetLife 감소 호출 시점에서 확인하세요. BGM은 실제 음악 WAV를 추가하고 위 API로 곡 교체/반복을 확인하세요.

### 2026-10-26 확인 결과

- 기존 기본 설치에는 Audio 없는 DirectXTK가 있었고, 비어 있던 별도 Audio 트리에 `directxtk[xaudio2-9]` 설치 성공. 기본 설치의 패키지 삭제/업데이트 없음.
- 설정 재실행 및 `-Build -SkipBuild`에서 재설치 없이 프로젝트 생성 성공. 누락된 bootstrap을 지정한 격리 fixture에서는 실패 단계/원인/재실행 명령을 출력하고 종료 코드 1로 중단함.
- Editor/Game Debug·Release 빌드 성공. 기존 렌더링의 형 변환 및 Lua 정적 라이브러리 중복 심볼 경고는 남아 있음. DirectXTK 관련 새 링크 오류 없음.
- Debug/Release 각각 오디오 검사 52개 통과(실제 장치/실제 WAV, SFX 음소거). 의존성 fixture 검사 8개 통과. 기본 0.35초 간격은 30/60/144 FPS에서 7초에 20회.
- 두 구성의 출력 DirectXTK DLL은 각 설치 DLL과 SHA-256 일치. Editor 실행, Pacman PIE Pause→Resume 및 Play→Stop→Play, Release Game의 MainMenu→Pacman 진입과 종료를 화면에서 확인함.
- 완전히 깨끗한 다른 PC/Windows 10, vcpkg 최초 clone·bootstrap, ImGui 최초 submodule clone은 실제 재현하지 않음. ObjViewer/Benchmark는 생성된 구성의 include/lib/DLL 배치를 검토했으며 전체 빌드는 수행하지 않음.
- 실제 청취, 실제 장치 제거/초기화 실패, 다섯 발걸음 랜덤 재생의 청취, 충돌·벽 슬라이드·회전·텔레포트 상황별 사운드, 코인 삭제 후 소리 지속은 코드 검토 및 관리자 테스트까지 확인했으며 게임 내 청취 확인은 남음. 지원 UI 입력 API로 지속적인 키 누름을 제어할 수 없어 이동 상황별 청취 검증을 자동화하지 못함. 위 수동 절차로 확인할 것.

로그: `Build/Logs/Build-Debug-Final.log`, `Build-Release.log`, `AudioTests-Debug.log`, `AudioTests-Release.log`, `Setup-Rerun.log`, `Setup-Failure-Test.log` (모두 로컬 Build 산출물).
