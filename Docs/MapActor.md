# MapActor 사용 및 팀 연동

## 생성과 편집

1. `Editor Controls > Actor Spawn`에서 `MapActor`를 선택하고 `Spawn`한다.
2. Outliner에서 생성된 Actor를 선택한다. 메시 기본값은 `Models/Map/Map.obj`이다.
3. Details의 `Map Tiles`에서 Columns/Rows, Local Tile Size, Local Grid Origin을 편집한다.
4. 격자에서 타일을 클릭하고 `Terrain`으로 Floor/Wall/Unused를 지정한다. 선택 타일은 월드에서 노란색으로 표시된다.
5. `Fill Floor`, `Fill Wall`, `Fill Unused`는 전체 배열을 한 종류로 채운다. **`Reset to Initial Grid`**는 생성 초기의 56×48 / TileSize 0.5 / GridOrigin 및 메시 기반 Wall/Floor 배열로 되돌린다. 리사이즈와 수동 편집 내용도 초기화하며 선택 타일은 해제한다. Actor Transform과 메시 참조는 유지한다.
6. `Show Map Overlay`로 맵 표시를 켜고 끈다. 기존 월드 그리드 설정과 독립이다. `Overlay Local Z Offset`은 바닥과 겹치는 표시를 띄운다.
7. 이동/회전/스케일은 `DefaultSceneRoot`의 Transform으로 편집한다. 메시 변경은 메시 컴포넌트의 기존 StaticMesh 슬롯을 사용한다.

초기값은 **56열 × 48행(2,688개), TileSize 0.5**, GridOrigin (-14,-12,-0.978149)이다. 기존과 동일한 로컬 28×24 영역을 덮는다. OBJ 변환은 (-z,x,y)이며 `Map.obj`의 전체 범위는 X [-14.000001,14.000002], Y [-12.000002,12.000002], Z [-1.209924,1.209924]이다. 기존 grounded 변형보다 Z가 약 1.209924 낮으므로 그리드 높이도 메시의 바닥 윗면에 맞춘다.

새 Actor 생성 시 `Map.obj`의 실제 삼각형 메시를 로컬 Z=GridOrigin.Z+0.1(약 -0.878149)에서 잘라 벽 단면을 얻는다. 단면의 내부와 벽 경계가 타일 내부를 지나가는 영역을 Wall로 지정하고 나머지 바닥 판 영역을 Floor로 채운다. 따라서 중심을 비껴가는 얇은 벽도 표시되며, 위쪽 면을 통째로 투영해 실내 바닥 전체가 Wall이 되는 것을 피한다. 벽을 정수 격자에 맞춘 것으로 가정하지 않고, 경계만 스치는 이웃 타일은 Wall로 확장하지 않는다. 몬스터 크기에 따른 여유 폭은 적용하지 않는다.

현재 에셋의 초기 배열은 **Wall 1,481칸 / Floor 1,207칸**이다. 원본 OBJ를 별도로 계산한 단면 기준 배열과 전체 2,688칸이 일치함을 확인했다. 테스트 실행 시 `Build/MapActorTests/default-terrain.json`으로 실제 생성 배열을 출력한다.

자동 분류는 **새로 생성할 때 또는 명시적으로 리셋할 때** 수행한다. 수동 편집, 리사이즈, 메시 교체 시 자동으로 재분류하지 않는다. 리셋은 현재 메시를 기준으로 재분류하며, 바닥 윗면은 이 맵 에셋의 `AABB.Min.Z + 0.231775`로 맞춘다. 메시 데이터가 없으면 기존 배열을 유지하고 실패한다. 저장된 배열과 PIE 데이터는 초기 분류보다 우선하므로 이전 28×24/TileSize 1 장면도 그대로 유지된다. 기존 장면에 명시적으로 저장된 grounded 메시 참조를 강제로 바꾸지는 않으며, 해당 메시에서 리셋해도 높이는 그 메시 바닥에 맞는다. 현재 에셋의 바닥 위 단면을 사용한 편집 출발점이며, 임의의 다른 메시나 다층 맵의 일반적인 판정은 아니다.

## 좌표와 Transform 계약

- 논리 좌표는 `(Column, Row)`이며 로컬 XY 기준 좌하단이 (0,0), +로컬 X가 열, +로컬 Y가 행, +로컬 Z가 높이이다. **Details 배열은 카메라와 무관한 고정 배치**이다. 보고된 맵 뷰와 좌우 방향을 맞추도록 **열은 왼쪽으로 증가, 행은 위쪽으로 증가**, 즉 (0,0)은 UI 오른쪽 아래에 표시한다. 축 숫자와 셀 툴팁, 선택 좌표는 항상 실제 논리 좌표를 표시한다.
- 배열 위 `Fixed layout`에 고정 방향을 안내한다. 카메라 이동/회전, 뷰포트 전환, Actor 회전으로 UI가 뒤집히거나 열·행이 교환되지 않는다. `DisplayColumn`/`DisplayRow`는 왼쪽 위에서 증가하는 화면 슬롯이며 실제 좌표는 `(Columns-1-DisplayColumn, Rows-1-DisplayRow)`이다.
- 색상, 클릭, 선택 하이라이트는 동일한 화면 슬롯→논리 타일 변환을 사용한다. 선택된 `(Column,Row)`와 저장 배열은 유지되며 렌더러·좌표 API·직렬화의 타일 순서는 바뀌지 않는다. 자유 카메라를 반대편으로 돌리면 월드의 화면 방향은 달라질 수 있지만 하이라이트는 계속 같은 논리 타일을 가리킨다.
- `GridOrigin`은 (0,0) 타일의 **좌하단 모서리**이고 Z는 타일 평면 높이이다. 중심은 Origin + ((Column+0.5)*Size,(Row+0.5)*Size,0)이다.
- Actor의 전체 루트 월드 행렬과 역행렬을 사용한다. 이동, yaw(Z축 회전), 양수 uniform scale을 지원한다. Scale 2는 같은 데이터와 같은 개수를 유지하면서 타일 변 길이를 2배, 면적을 4배로 만든다.
- 메시 상대 Transform은 identity여야 하며 루트의 직접 자식이어야 한다. 따로 변환하거나 메시 컴포넌트가 없거나 여러 개면 경고와 함께 월드 좌표 API/표시가 실패한다. 데이터 자체는 계속 편집 가능하다. 메시 상대 Transform을 identity로 되돌리면 복구된다.
- 0/음수/비균일 scale, 기울어진 평면, NaN/무한대 및 표현할 수 없는 역변환은 경고와 실패로 처리한다. 경고 문자열은 `GetGridTransformWarning()`으로 확인한다. 실패한 Try API는 출력 인자를 변경하지 않는다.
- 월드→타일 API는 **로컬 XY 평면 투영**이다. 월드 위치를 역변환한 뒤 로컬 Z를 무시한다. 바닥 접촉/높이/다층 판정이 아니다.
- 범위는 `[Origin.X, Origin.X+Columns*Size) × [Origin.Y, Origin.Y+Rows*Size)`이다. 최소 경계는 포함, 최대 경계는 제외하며 내부 경계는 +방향 타일에 속한다. 음수에는 floor 규칙을 적용하고 바깥 점은 보정하지 않는다. float 좌표의 표현 정밀도를 넘어서는 극단적인 위치/크기에서는 경계 근처의 수치 오차가 있을 수 있다.
- TileSize/개수를 바꾸어도 메시 크기는 바뀌지 않는다. 메시·그리드·추후 벽 컴포넌트는 공통 루트 아래에 둔다.

## 팀원이 사용할 공개 API

헤더: `Source/Runtime/GameFramework/Actor/MapActor.h`

| API | 용도 |
|---|---|
| `GetColumns()`, `GetRows()`, `GetTileSize()`, `GetGridOrigin()` | 그리드 설정 조회 |
| `IsValidTile(Column, Row)` | 좌표 범위 확인 |
| `TryWorldToTileXYProjected(World, OutColumn, OutRow)` | 월드 위치를 로컬 XY에 투영해 타일 조회 |
| `TryTileToWorldCenter(Column, Row, OutWorldCenter)` | 타일 중심을 실제 월드 위치로 변환 |
| `TryGetTile(Column, Row, OutTerrain)` | 지형 데이터 조회 |
| `SetTile(Column, Row, Terrain)` | 한 타일 변경 |
| `FillTiles(Terrain)` | 전체 채우기 |
| `ResetGridToInitial()` | 생성 초기의 그리드 설정과 메시 기반 배열 복원, 성공 여부 반환 |
| `ConfigureGrid(Columns, Rows, Size, Origin)` | 설정 변경, 겹치는 좌표 보존 |
| `GetGridTransformWarning()` | 정상은 nullptr, 지원하지 않는 상태는 설명 문자열 |
| `TryGetGridToWorld(OutMatrix)` | 검증된 그리드→월드 행렬 |
| `GetStaticMeshComponent()` | 상속한 단일 맵 메시 접근 |

`EMapTileTerrain::{Floor,Wall,Unused}`는 제작자가 지정한 **지형**이다. Floor가 특정 몬스터 크기의 통과를 보장하지 않는다. 배열의 수정 가능한 포인터/참조를 제공하지 않는다. AI는 이 API를 읽어 자신의 BFS/통과 정책을 적용하면 된다. 점유 정보, BFS, 추적/재탐색/몬스터 이동은 없다.

설정은 열/행 각각 1~256, 총 65,536개 이하, 유한한 양수 TileSize와 유한한 GridOrigin으로 제한된다. 잘못된 ConfigureGrid는 경고를 기록하고 기존 설정/데이터를 유지한다. 크기 변경은 기존/새 행 너비를 따로 사용해 좌표별로 복사한다. 새 영역은 Floor이며 축소로 잘려 나간 영역은 다시 확장해도 복구되지 않는다.

맵 메시의 기본 충돌은 꺼져 있다. 기존 StaticMeshComponent의 메시 전체 AABB 충돌이 플레이 영역 전체를 막는 것을 피하기 위함이다. 기존 Add Component로 벽 컴포넌트를 루트에 붙일 수 있으며, 이번 변경에서 벽 충돌 컴포넌트나 충돌 자동 생성은 추가하지 않았다. 필수 맵 메시의 삭제/이름 변경은 Details에서 막아 기본 서브오브젝트의 로드 이름을 유지한다.

## 저장·PIE·역할 분리

- `AActor::Serialize`의 기존 컴포넌트 경로가 메시 에셋 참조, 루트 Transform, 추가 컴포넌트를 저장한다.
- `AMapActor::Serialize(FStructuredArchive::FRecord)`가 `MapGrid` 버전 1, Columns/Rows, TileSize, OriginX/Y/Z, Terrain을 명시적으로 저장한다. Terrain 순서는 `Row*Columns+Column`이다.
- 같은 Serialize가 JSON 장면과 binary PIE 복제에 사용된다. 로드시 별도의 소유 배열을 만들고 검증 후 교체한다. 크기와 배열 개수/지형 값이 잘못되면 archive error를 설정한다.
- MapActor는 `AStaticMeshActor`를 재사용한다. 공통 루트 1개 + StaticMeshComponent 1개만 생성하며 Actor Tick을 켜지 않는다.
- 런타임 Actor에 ImGui/Editor 참조는 없다. `FMapTileEditor`는 세션 선택/표시 설정을 보유하고, `FMapTileRenderer`가 EditorViewPasses의 깊이 검사·알파 합성 패스에서 유한한 타일 면/선을 그린다. 메시 UV와 무관하다.
- 선택/호버, 표시 토글, 표시 높이는 지형 데이터에 포함되지 않으며 장면이나 PIE에 저장하지 않는다. 맵 표시 설정은 이 DetailsPanel 세션의 MapActor들에 공통으로 적용된다. PIE/독립 게임/SIE에는 맵 오버레이가 표시되지 않는다.
- 뷰포트 클릭 페인팅, 드래그 브러시, Undo 추가는 없다.

## 검증 재실행

프로젝트 파일은 기존 Premake의 런타임/에디터 파일 glob을 따르므로 새 체크아웃에서는 `SlnGenerator.bat -SkipBuild`로 다시 생성한다. VS 2026 환경의 기본 빌드는 `Setup/SetupTeam.ps1 -Build -Configuration Debug`를 사용할 수 있다.

에디터 Debug 빌드 후 저장소 루트에서 `./Tests/Run-MapActorTests.ps1`을 실행한다. 테스트는 실제 D3D11 장치/OBJ·MTL 로더/ImGui/scene serializer/PIE world duplication을 사용한다. 산출물과 검증 장면은 `Build/MapActorTests/`, 실행 로그는 `Build/MapActor-tests.log`에 둘 수 있다.

테스트 범위: 기본 생성과 메시 참조, 좌표 왕복, 경계/음수/출력 실패 규칙, 이동·여러 yaw·양수 scale, 타일 변 길이/면적, 잘못된 scale/tilt/메시 상대 변환, 리사이즈 보존, 잘못된 크기와 저장 데이터 거부, 실제 장면 파일 저장·로드, PIE 데이터 유지 및 수정 독립성. ImGui의 실제 타일 버튼 클릭과 D3D11 렌더 타깃 readback으로 선택 하이라이트, 지형 색상, 체크무늬, 유한 영역, 표시 토글, PIE 표시 제외도 검증한다. 리셋은 편집·리사이즈 후 초기 배열 전체 복원, 메시/Actor Transform 유지, 메시 누락 실패 시 데이터 보존, ImGui 버튼 활성화와 선택 해제, 로드 이후 복원, PIE 독립성까지 검증한다.

표시/선택 검증은 시험용 고정 투영 외에 **실제 `FEditorViewportClient::GetViewProjectionMatrix()`**도 사용한다. 보고된 맵 시점과 90도 회전 시점에서 실제 ImGui 클릭→논리 타일→GPU 하이라이트를 확인한다. Map yaw와 camera yaw/pitch/roll·옆면 시점 조합 36개에서도 화면 셀 배치, 4×3의 비정방형 크기, 일대일 좌표 대응, 선택/지형 데이터가 그대로 유지되는지 검증한다.

2026-10-25 최종 실행: 통합 검증 **9,372/9,372 통과**. `Map.obj` 바닥 높이, 0.5 기본 크기, 현재 에셋의 Wall/Floor 수와 대표 좌표, 중심을 비껴가는 얇은 벽, 초기 배열 전체 저장·로드·리셋, 이전 28×24/TileSize 1 형식 보존 및 카메라에 영향받지 않는 고정 편집 배치를 포함한다. 최종 HitoriEditor Debug x64 빌드는 오류 0개이며 기존 헤더/링크 설정의 경고는 남아 있다. HitoriGame은 앞선 런타임 변경에서 Debug x64 빌드 오류 0개를 확인했으며 이번 고정 표시 변경은 Editor에만 있다. `git diff --check --ignore-submodules`도 통과했다.

네이티브 에디터 창에서 생성 메뉴를 조작하는 수동 검증은 앱 실행 승인 대기 시간 만료로 수행하지 못했다. 실제 맵 메시가 포함된 에디터 화면의 육안 확인은 별도로 필요하다.

## 변경 파일

| 파일 | 변경 내용 |
|---|---|
| `Source/Runtime/GameFramework/Actor/MapActor.h`, `.cpp` | Actor, 캡슐화한 타일 배열, 좌표 변환, 전용 직렬화 |
| `Source/Runtime/Engine/EngineClassRegistry.cpp` | 장면 클래스 등록/링크 |
| `Source/Editor/Details/MapTileEditor.h`, `.cpp` | 전용 ImGui 격자 및 세션 선택 상태 |
| `Source/Editor/Details/DetailsPanel.h`, `.cpp` | Map UI 연결, 필수 맵 메시 이름/삭제 보호 |
| `Source/Editor/EditorControls/EditorControlsPanel.h` | MapActor 생성 메뉴 |
| `Source/Editor/Rendering/MapTileRenderer.h`, `.cpp` | 에디터 전용 타일 면/선 렌더러 |
| `Resources/Shader/MapTileOverlay.hlsl` | 타일 색상/알파 합성 셰이더 |
| `Source/Editor/Rendering/EditorViewPasses.h`, `.cpp` | 독립 맵 오버레이 패스 연결 |
| `Source/Editor/HitoriEd/EditorEngine.h`, `.cpp` | 렌더러 소유/초기화 및 UI 상태 전달 |
| `Tests/MapActorTests.cpp`, `Tests/Run-MapActorTests.ps1` | 통합 검증과 실행 도구 |
| `Docs/MapActor.md` | API 계약, 에디터 사용법, 검증 안내 |
