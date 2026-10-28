#include "EnginePCH.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/GameViewportClient.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraModifier.h"
#include "Camera/CameraModifier_HeadBob.h"
#include "Camera/SpringArmComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/LuaScriptComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "GameFramework/Actor/MapActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/PlayerStart.h"
#include "GameFramework/Actor/ExponentialHeightFogActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameActor/PacmanGameMode.h"
#include "GameActor/PacmanHUD.h"
#include "GameActor/GhostActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "GameActor/FlashlightActor.h"
#include "GameActor/FlashlightAttackComponent.h"
#include "GameActor/PlayerPawn.h"
#include "Lua/LuaManager.h"
#include "Sequencer/LevelSequenceActor.h"
#include "Asset/AssetManager.h"
#include "Render/RenderDevice.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/Mesh.h"
#include "UI/Widget.h"
#include "Serialization/JsonArchive.h"
#include "json.hpp"
#include <stdexcept>

namespace
{
int Checks = 0;
void Check(bool Value, const char* Message)
{
    ++Checks;
    if (!Value) throw std::runtime_error(Message);
}
bool Near(float A, float B) { return std::abs(A - B) < 0.0001f; }
void CheckLua(sol::protected_function_result& Result)
{
    if (!Result.valid())
    {
        sol::error Error = Result;
        throw std::runtime_error(Error.what());
    }
}

class ATickProbe : public APawn
{
    DECLARE_CLASS(ATickProbe, APawn)
public:
    float LastDelta = -1.f;
    int Actions = 0;
    ATickProbe() { PrimaryActorTick.bCanEverTick = true; }
    void Tick(float Delta) override { LastDelta = Delta; }
    void PrimaryAction() override { ++Actions; }
};
class ULuaTestEngine : public UEngine
{
public:
    void Tick(float) override {}
};
class UTickProbe : public UActorComponent
{
    DECLARE_CLASS(UTickProbe, UActorComponent)
public:
    float LastDelta = -1.f;
    UTickProbe() { PrimaryComponentTick.bCanEverTick = true; }
    void TickComponent(float Delta) override { LastDelta = Delta; }
};
class AInputTestMode : public AGameMode
{
    DECLARE_CLASS(AInputTestMode, AGameMode)
public:
    bool AllowInput = true;
    bool AllowsPlayerControllerInput() const override { return AllowInput; }
};
class ACameraProbe : public AActor
{
    DECLARE_CLASS(ACameraProbe, AActor)
public:
    mutable int Calls = 0;
    bool DestroyOnCalc = false;
    void CalcCamera(float, FMinimalViewInfo& POV) const override
    {
        ++Calls;
        POV.Location = GetActorLocation();
        if (DestroyOnCalc)
        {
            auto* Self = const_cast<ACameraProbe*>(this);
            Self->Destroy();
            GetWorld()->FlushPendingDestruction();
            Check(GUObjectArray.Find(Self) != INDEX_NONE, "camera callback reclaimed its active target");
        }
    }
};
class URemovingModifier : public UCameraModifier
{
    DECLARE_CLASS(URemovingModifier, UCameraModifier)
public:
    APlayerCameraManager* Manager = nullptr;
    bool ModifyCamera(float, FMinimalViewInfo& POV) override
    {
        POV.Location.Z += 3.f;
        Manager->RemoveCameraModifier(this);
        Manager->GetWorld()->FlushPendingDestruction();
        Check(GUObjectArray.Find(this) != INDEX_NONE, "modifier callback reclaimed itself");
        return false;
    }
};
struct FWorldFixture
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FWorldFixture(UClass* Mode = AGameMode::StaticClass())
    {
        World->SetGameModeClass(Mode);
        World->SetDefaultPawnClass(ATickProbe::StaticClass());
        World->BeginPlay();
        World->Tick(0.f);
    }
    ~FWorldFixture() { World->ClearWorld(); delete World; }
    APlayerController* Controller() const { return World->GetPlayerController(); }
    APlayerCameraManager* Camera() const { return Controller()->GetPlayerCameraManager(); }
};

void TimeAndInputTests()
{
    FWorldFixture F;
    auto* Pawn = CastChecked<ATickProbe>(F.World->GetPlayerPawn());
    auto* Component = CastChecked<UTickProbe>(Pawn->AddComponent(UTickProbe::StaticClass(), FName("TickProbe")));
    F.World->Tick(0.f);
    F.World->SetWorldTimeDilation(0.3f);
    Check(Near(F.World->GetWorldSettings()->TimeDilation, 0.3f), "Lua world API bypasses WorldSettings");
    Pawn->SetCustomTimeDilation(2.f);
    F.World->Tick(0.1f);
    Check(Near(F.World->GetDeltaSeconds(), 0.03f), "global time dilation applied more than once");
    Check(Near(Pawn->LastDelta, 0.06f) && Near(Component->LastDelta, 0.06f), "actor/component time dilation disagrees");
    Pawn->CustomTimeDilation = 0.f;
    F.World->Tick(0.1f);
    Check(Near(Pawn->LastDelta, 0.f) && Near(Component->LastDelta, 0.f), "hitstop uses a different custom dilation value");
    F.World->GetWorldSettings()->SetTimeDilation(0.5f);
    Check(Near(F.World->GetWorldTimeDilation(), 0.5f), "world getter has a second dilation state");
    F.World->SetWorldTimeDilation(0.f);
    Check(F.World->GetWorldTimeDilation() > 0.f, "world API skipped Unreal time dilation clamping");

    F.Controller()->SetIgnoreMoveInput(true);
    F.Controller()->InputKey(static_cast<int>(EKeyCode::LButton), true);
    Check(Pawn->Actions == 0, "sequence input lock still permits primary action");
    F.Controller()->InputKey(static_cast<int>(EKeyCode::LButton), false);
    F.Controller()->SetIgnoreMoveInput(false);
    F.Controller()->InputKey(static_cast<int>(EKeyCode::LButton), true);
    Check(Pawn->Actions == 1, "primary action did not resume after sequence");
}

void CameraAndSequenceTests()
{
    FWorldFixture F;
    auto* Outgoing = F.World->SpawnActor<ACameraProbe>(FName("MergeCamera"));
    Outgoing->SetActorLocation(FVector(100.f, 0.f, 0.f));
    auto* Incoming = F.World->SpawnActor<ACameraProbe>();
    Incoming->SetActorLocation(FVector::ZeroVector);
    F.Camera()->SetViewTarget(Outgoing);
    F.Camera()->UpdateCamera(0.f);
    Outgoing->Calls = 0;
    F.Camera()->UpdateCamera(0.1f);
    Check(Outgoing->Calls == 1, "view target evaluated twice in one update");
    F.Camera()->SetViewTargetWithBlend(Incoming, 1.f, VTBlend_Linear, 2.f, true);
    Outgoing->Destroy(); F.World->FlushPendingDestruction();
    F.Camera()->UpdateCamera(0.5f);
    Check(Near(F.Camera()->GetCameraCachePOV()->Location.X, 50.f), "destroyed outgoing target broke locked blend");
    F.Camera()->UpdateCamera(0.5f);
    Check(F.Camera()->GetViewTarget() == Incoming && !F.Camera()->HasActiveViewTargetBlend(), "blend did not finish");

    auto* Modifier = CastChecked<URemovingModifier>(F.Camera()->AddNewCameraModifier(URemovingModifier::StaticClass()));
    Modifier->Manager = F.Camera();
    F.Camera()->UpdateCamera(0.1f);
    Check(Near(F.Camera()->GetCameraCachePOV()->Location.Z, 3.f), "modifier output did not reach camera cache");
    F.World->FlushPendingDestruction();
    Incoming->DestroyOnCalc = true;
    F.Camera()->UpdateCamera(0.1f);
    Check(!F.Camera()->GetCameraCachePOV(), "destroyed camera target left a valid cache");
    F.World->FlushPendingDestruction();
    F.Camera()->UpdateCamera(0.f);
    Check(F.Camera()->GetViewTarget() == F.World->GetPlayerPawn(), "camera did not recover its possessed pawn");

    // A real sequence restores the pawn and deletes itself in its finish delegate.
    FLevelSequenceData Data;
    Data.FrameRate = 30; Data.EndFrame = 30;
    auto* CineCamera = F.World->SpawnActor<ACameraProbe>(FName("SequenceCamera"));
    CineCamera->SetActorLocation(FVector(80.f, 0.f, 0.f));
    FSequenceTrack Track;
    Track.TrackType = ESequenceTrackType::CameraCut;
    Track.CameraCuts.Add({0, 31, CineCamera->GetName(), 0.f});
    Data.Tracks.Add(Track);
    const FString AssetPath = "Build/MainMergeTests/return.seq.json";
    Check(FLevelSequenceAsset::SaveToFile(Data, AssetPath), "test sequence could not be saved");
    auto* Sequence = F.World->SpawnActor<ALevelSequenceActor>();
    Sequence->SetSequenceAssetPath(AssetPath);
    Sequence->SetRestoreBlendTime(1.f);
    Sequence->PlaySequence();
    F.Camera()->UpdateCamera(0.f);
    Check(F.Controller()->IsMoveInputIgnored() && F.Controller()->IsLookInputIgnored(), "sequence did not lock input");
    Sequence->OnSequenceFinished.Add([Sequence] { Sequence->Destroy(); }, &F);
    Sequence->Tick(1.f);
    F.World->FlushPendingDestruction();
    Check(!F.Controller()->IsMoveInputIgnored() && !F.Controller()->IsLookInputIgnored(), "sequence leaked input locks");
    Check(F.Camera()->HasActiveViewTargetBlend(), "sequence destruction canceled its return blend");
    F.Camera()->UpdateCamera(0.5f);
    const float Halfway = F.Camera()->GetCameraCachePOV()->Location.X;
    Check(Halfway > 1.f && Halfway < 79.f, "sequence return snapped instead of blending");
    F.Camera()->FadeOut(1.f);
    F.Camera()->EnterLetterbox(1.f, 0.12f);
    F.Camera()->UpdateCamera(0.5f);
    Check(Near(F.Camera()->GetFadeAmount(), 0.5f) && Near(F.Camera()->GetLetterboxRatio(), 0.06f), "camera effects did not advance once");
}

void BranchCameraIntegrationTests()
{
    FWorldFixture F(AInputTestMode::StaticClass());
    int Managers = 0;
    for (AActor* Actor : F.World->GetPersistentLevel()->GetActors())
        if (Cast<APlayerCameraManager>(Actor)) ++Managers;
    Check(Managers == 1, "merged controller spawned duplicate camera managers");

    auto* Player = F.World->SpawnActor<APlayerPawn>();
    F.World->Tick(0.f);
    F.Controller()->Possess(Player);
    F.World->Tick(0.f);
    auto* Light = Player->GetFlashlightActor();
    Check(Light && Light->GetActorEnableCollision(), "legacy flashlight collision policy disabled attack overlaps");
    Check(Light->GetRootComponent()->GetAttachParent() == Player->GetFlashlightPivot(), "flashlight followed the spring arm camera endpoint");
    Check(Player->GetCameraComponent()->GetAttachParent() == Player->GetSpringArmComponent()->GetArmEnd(), "player camera lost its spring arm endpoint");
    auto* Bob = F.Camera()->FindCameraModifier(UCameraModifier_HeadBob::StaticClass());
    Check(Bob != nullptr, "merged player did not install its Head Bob modifier");
    F.World->Tick(0.f);
    Check(F.Camera()->FindCameraModifier(UCameraModifier_HeadBob::StaticClass()) == Bob, "player replaced the Head Bob modifier every tick");

    const auto Press = [&](EKeyCode Key)
    {
        F.Controller()->InputKey(static_cast<int>(Key), true);
        F.Controller()->InputKey(static_cast<int>(Key), false);
    };
    F.Controller()->SetIgnoreMoveInput(true);
    F.Controller()->SetIgnoreLookInput(true);
    Press(EKeyCode::Q); Press(EKeyCode::F6); Press(EKeyCode::LButton);
    Check(!Player->IsThirdPersonMode() && F.Camera()->GetActiveCameraShakeCount() == 0 &&
        !Player->GetFlashlightAttackComponent()->IsAttacking(), "camera or attack shortcuts bypassed cinematic input locks");
    F.Controller()->SetIgnoreMoveInput(false);
    F.Controller()->SetIgnoreLookInput(false);
    Press(EKeyCode::Q); Press(EKeyCode::F6);
    Check(Player->IsThirdPersonMode() && Player->GetSpringArmComponent()->IsActive(), "Q did not restore the third-person camera after input unlock");
    Check(F.Camera()->GetActiveCameraShakeCount() == 1, "F6 shake was lost while merging sequencer support");
    F.Camera()->StopAllCameraShakes(true);
    Press(EKeyCode::F7);
    Check(F.Camera()->GetActiveCameraShakeCount() == 1, "F7 Perlin shake was lost while merging sequencer support");
    F.Camera()->StopAllCameraShakes(true);

    auto* Outgoing = F.World->SpawnActor<ACameraProbe>();
    auto* Incoming = F.World->SpawnActor<ACameraProbe>();
    Incoming->SetActorLocation(FVector(100.f, 0.f, 0.f));
    F.Camera()->SetViewTarget(Outgoing); F.Camera()->UpdateCamera(0.f);
    Outgoing->Calls = 0;
    F.World->Tick(0.1f);
    Check(Outgoing->Calls == 1, "world updated the camera more than once per frame");
    F.Camera()->SetViewTargetWithBlend(Incoming, 1.f, VTBlend_Cubic, 2.f, true);
    F.Camera()->UpdateCamera(0.25f);
    Check(Near(F.Camera()->GetBaseCameraPOV().Location.X, 15.625f), "sequencer cubic return blend became linear");
    F.Camera()->SetViewTargetWithBlend(Player, 1.f, VTBlend_Linear, 2.f, true);
    F.Camera()->UpdateCamera(0.f);
    Check(Near(F.Camera()->GetBaseCameraPOV().Location.X, 15.625f), "interrupted blend restarted at the outgoing actor");
    F.Camera()->UpdateCamera(1.f);

    F.Camera()->FadeOut(1.f, FLinearColor(0.f, 0.f, 0.f, 1.f), true, false);
    F.Camera()->EnterLetterbox(1.f, 0.12f);
    F.Camera()->UpdateCamera(0.5f);
    FGameViewportClient Viewport;
    Viewport.SetWorld(F.World);
    const FSceneView View = Viewport.CalcSceneView({0.f, 0.f, 1280.f, 720.f});
    Check(View.bCameraFade && !View.bFadeHUD && Near(View.FadeAmount, 0.5f) && Near(View.LetterboxRatio, 0.06f), "merged viewport dropped world-only fade or letterbox");
    Check((View.ViewLocation - F.Camera()->GetCameraPOV().Location).SizeSquared() < 0.00001f,
        "viewport bypassed the camera modifier output");
    Check(Viewport.BeginSIEMode(), "SIE could not copy the merged camera view");
    const FSceneView SIEView = Viewport.CalcSceneView({0.f, 0.f, 1280.f, 720.f});
    Check((SIEView.ViewLocation - View.ViewLocation).SizeSquared() < 0.00001f,
        "SIE started from a different camera view");
    Viewport.EndSIEMode();
    const FVector Saved = F.Camera()->GetBaseCameraPOV().Location;
    F.Controller()->UnPossess(); F.Camera()->UpdateCamera(0.f);
    Check(F.Camera()->GetViewTarget() == F.Controller() && (F.Camera()->GetBaseCameraPOV().Location - Saved).SizeSquared() < 0.00001f,
        "UnPossess lost the saved camera view");
    auto* RemovedPawn = F.World->SpawnActor<ATickProbe>();
    RemovedPawn->SetActorLocation(FVector(42.f, 8.f, 5.f));
    F.Controller()->Possess(RemovedPawn); F.Camera()->UpdateCamera(0.f);
    RemovedPawn->Destroy(); F.World->FlushPendingDestruction();
    F.Camera()->UpdateCamera(0.f);
    Check(F.Controller()->GetPawn() == nullptr && Near(F.Camera()->GetBaseCameraPOV().Location.X, 42.f),
        "validated view-target getter broke destroyed pawn camera preservation");
}

void LuaAndAttackTests()
{
    FWorldFixture F(AInputTestMode::StaticClass());
    auto* Player = F.World->SpawnActor<APlayerPawn>();
    F.World->Tick(0.f);
    F.Controller()->Possess(Player);
    auto Scripts = Player->GetComponentsByClass<ULuaScriptComponent>();
    Check(Scripts.Num() == 1 && Scripts[0]->GetScriptPath() == "Assets/Scripts/PlayerPawn.lua", "player contains duplicate or replaced Lua scripts");
    FWorldContext Context;
    Context.SetCurrentWorld(F.World);
    UGameInstance Instance;
    Instance.SetWorldContext(&Context);
    FLuaManager Manager;
    Manager.Init(Instance);
    auto& Lua = Manager.GetState();
    Lua["Owner"] = static_cast<AActor*>(Player);
    auto Result = Lua.safe_script(R"(
        local Class = dofile('Assets/Scripts/PlayerPawn.lua')
        Self = setmetatable({Owner = Owner}, {__index = Class})
        Self:BeginPlay()
        Input.IsKeyDown = function(key) return key == KeyCode.T end
        Input.IsKeyPressed = function() return false end
    )", sol::script_pass_on_error);
    CheckLua(Result);
    Check(Near(Player->GetFlashlightAttackComponent()->BatteryCost, 5.f), "camera controls replaced flashlight Lua configuration");
    Player->CustomTimeDilation = 0.f;
    Result = Lua.safe_script("Self:Tick(0.0)", sol::script_pass_on_error);
    Check(Result.valid() && Near(Player->CustomTimeDilation, 0.f) && Near(F.World->GetWorldTimeDilation(), 1.f), "Lua slomo overwrote flashlight hitstop");
    Player->CustomTimeDilation = 1.f;
    Result = Lua.safe_script("Self:Tick(0.1)", sol::script_pass_on_error);
    Check(Result.valid(), "merged player Lua tick failed");
    Check(Near(F.World->GetWorldTimeDilation(), 0.3f), "Lua slomo did not change WorldSettings");
    Check(Near(Player->GetCustomTimeDilation(), 1.f / 0.3f), "Lua slomo player compensation failed");
    F.Controller()->SetIgnoreMoveInput(true);
    Result = Lua.safe_script("Self:Tick(0.1)", sol::script_pass_on_error);
    Check(Result.valid() && Near(F.World->GetWorldTimeDilation(), 1.f) && Near(Player->CustomTimeDilation, 1.f), "cinematic did not release slomo state");
    Check(!Player->GetFlashlightAttackComponent()->TryStartAttack(), "direct attack bypassed sequence lock");
    F.Controller()->SetIgnoreMoveInput(false);
    Check(Player->GetFlashlightAttackComponent()->TryStartAttack(), "attack did not resume after sequence lock");
    Player->GetFlashlightAttackComponent()->CancelAttack();
    Player->SetSlomoGauge(0.1f);
    Result = Lua.safe_script("Self:Tick(0.1)", sol::script_pass_on_error);
    CheckLua(Result);
    Check(Player->IsSlomoActive(), "Lua gauge request did not reach native slomo state");
    Player->Tick(0.1f);
    Check(!Player->IsSlomoActive() && Near(Player->GetSlomoGauge(), 0.f) && Near(F.World->GetWorldTimeDilation(), 1.f), "depleted gauge did not restore time immediately");
    Player->Tick(0.1f);
    Result = Lua.safe_script("Self:Tick(0.1)", sol::script_pass_on_error);
    CheckLua(Result);
    Check(Player->GetSlomoGauge() > 0.f && !Player->IsSlomoActive(), "holding T restarted slomo as soon as an empty gauge recovered");
    Result = Lua.safe_script(R"(
        Input.IsKeyDown = function() return false end
        Self:Tick(0.1)
        Input.IsKeyDown = function(key) return key == KeyCode.T end
        Self:Tick(0.1)
    )", sol::script_pass_on_error);
    CheckLua(Result);
    Check(Player->IsSlomoActive(), "slomo did not rearm after T was released");
    CastChecked<AInputTestMode>(F.World->GetGameMode())->AllowInput = false;
    Result = Lua.safe_script(R"(
        local GhostClass = dofile('Assets/Scripts/GhostAI.lua')
        -- Missing AI state methods make any update past the cinematic gate fail.
        local Ghost = {Owner = Owner, AI = {}, State = 'Chase', ChaseTimer = 0}
        GhostClass.Tick(Ghost, 0.1)
        assert(Ghost.ChaseTimer == 0)
        local GameClass = dofile('Assets/Scripts/PacmanGameMode.lua')
        local Count = 2
        local Cleared = false
        local Game = setmetatable({Mode = {
            IsResolvingGhostHit = function() return false end,
            GetPlayerLife = function() return 3 end,
            GetMaxCoinCount = function() return 3 end,
            GetCollectedCoinCount = function() return Count end,
            ShowGameResult = function() Cleared = true end
        }}, {__index = GameClass})
        Game:Tick()
        assert(not Cleared)
        Count = 3
        Game:Tick()
        assert(Cleared)
    )", sol::script_pass_on_error);
    CheckLua(Result);
    Check(Result.valid(), "Lua cinematic pause or dynamic coin goal failed");
    Result = Lua.safe_script("Self:EndPlay(); Self = nil; Owner = nil", sol::script_pass_on_error);
    Check(Result.valid(), "merged Lua player failed to end play");
    Manager.Shutdown();
    Instance.SetWorldContext(nullptr);
}

void SlomoResourceTests()
{
    FWorldFixture F;
    auto* Player = F.World->SpawnActor<APlayerPawn>();
    F.World->Tick(0.f); F.Controller()->Possess(Player);
    F.World->SetWorldTimeDilation(0.5f); Player->CustomTimeDilation = 2.f;
    Check(Player->StartSlomo(0.3f), "native slomo activation failed");
    F.World->Tick(0.1f);
    Check(std::abs(Player->GetSlomoGauge() - 97.5f) < 0.001f, "slomo gauge did not drain using compensated player time");
    Player->SetSlomoActive(false);
    Check(Near(F.World->GetWorldTimeDilation(), 0.5f) && Near(Player->CustomTimeDilation, 2.f), "slomo discarded an existing time scale");
    const float Gauge = Player->GetSlomoGauge();
    F.World->SetPaused(true); F.World->Tick(0.1f);
    Check(Near(Player->GetSlomoGauge(), Gauge) && !Player->StartSlomo(0.3f), "pause changed or activated slomo gauge");
    F.World->SetPaused(false);
    Player->SetSlomoGauge(100.f); Player->StartSlomo(0.3f);
    for (int I = 0; I < 41; ++I) F.World->Tick(0.1f);
    Check(!Player->IsSlomoActive() && Near(F.World->GetWorldTimeDilation(), 0.5f), "four seconds of slomo did not deplete and restore");
    Check(Player->GetSlomoGauge() < 3.f, "empty slomo gauge was reset to full");
    Player->SetSlomoGauge(50.f); Player->StartSlomo(0.3f);
    F.Controller()->SetIgnoreMoveInput(true); F.World->Tick(0.1f);
    Check(!Player->IsSlomoActive() && Near(F.World->GetWorldTimeDilation(), 0.5f), "cinematic without a Lua tick leaked slomo");
    F.Controller()->SetIgnoreMoveInput(false);
    F.World->SetWorldTimeDilation(1.f); Player->CustomTimeDilation = 1.f;
    Player->SetSlomoGauge(50.f); Player->StartSlomo(0.3f);
    auto* Attack = Player->GetFlashlightAttackComponent();
    Check(Attack->TryStartAttack(), "slomo blocked ordinary flashlight attack");
    for (int I = 0; I < 20 && Attack->GetPhase() != EFlashlightAttackPhase::Swing; ++I) F.World->Tick(0.01f);
    Check(Attack->GetPhase() == EFlashlightAttackPhase::Swing, "slomo attack never reached its swing");
    FTransform FarAway(FRotator(), FVector(100, 100, 100));
    auto* Ghost = F.World->SpawnActor<AGhostActor>(NAME_None, &FarAway);
    auto* Capsule = Player->GetFlashlightActor()->GetAttackCapsule();
    Capsule->OnComponentBeginOverlap.Broadcast(Capsule, Ghost->GetHurtCapsule());
    Check(Attack->IsHitStopped() && Near(Player->CustomTimeDilation, 0.f), "slomo attack did not enter hitstop");
    const float FrozenGauge = Player->GetSlomoGauge();
    F.World->Tick(0.01f);
    Check(Near(Player->GetSlomoGauge(), FrozenGauge), "slomo gauge drained while the player was hitstopped");
    Player->SetSlomoActive(false);
    Check(Attack->IsHitStopped() && Near(Player->CustomTimeDilation, 0.f) && Near(F.World->GetWorldTimeDilation(), 1.f), "stopping slomo prematurely released hitstop");
    for (int I = 0; I < 20 && Attack->IsHitStopped(); ++I) F.World->Tick(0.01f);
    Check(!Attack->IsHitStopped() && Near(Player->CustomTimeDilation, 1.f), "hitstop restored the old slomo compensation");
    Attack->CancelAttack();
    F.World->SetWorldTimeDilation(0.5f); Player->CustomTimeDilation = 2.f;
    Player->SetSlomoGauge(50.f); Player->StartSlomo(0.3f); Player->RespawnAtPlayerStart();
    Check(!Player->IsSlomoActive() && Near(F.World->GetWorldTimeDilation(), 0.5f), "respawn leaked slomo time");
    Player->StartSlomo(0.3f); Player->Destroy();
    Check(Near(F.World->GetWorldTimeDilation(), 0.5f), "destroying the slomo player reset unrelated world time");
}

void PacmanSceneTests()
{
    (void)AMapActor::StaticClass(); (void)APlayerStart::StaticClass();
    (void)AStaticMeshActor::StaticClass(); (void)AGhostActor::StaticClass();
    (void)AExponentialHeightFogActor::StaticClass(); (void)ASkyLightActor::StaticClass();
    (void)APacmanGameMode::StaticClass();
    FRenderResourceManager::Init();
    UAssetManager::Init();
    UWorld* EditorWorld = UWorld::CreateWorld(EWorldType::Editor, false);
    Check(FJsonArchive::LoadWorld(EditorWorld, "Scenes/Pacman.scene"), "merged Pacman scene did not load");
    Check(FJsonArchive::SaveWorld(EditorWorld, "Build/MainMergeTests/Pacman.roundtrip.scene"), "merged Pacman scene did not save");
    nlohmann::json Original, Saved;
    std::ifstream("Scenes/Pacman.scene") >> Original;
    std::ifstream("Build/MainMergeTests/Pacman.roundtrip.scene") >> Saved;
    const auto& SavedActors = Saved["World"]["PersistentLevel"]["Actors"];
    for (const auto& Actor : Original["World"]["PersistentLevel"]["Actors"])
    {
        auto It = std::find_if(SavedActors.begin(), SavedActors.end(), [&](const auto& Candidate) { return Candidate["Name"] == Actor["Name"]; });
        Check(It != SavedActors.end() && (*It)["Class"] == Actor["Class"], "merged scene actor disappeared or changed class");
        Check((*It)["RootComponent"] == Actor["RootComponent"], "merged scene root changed");
        for (const auto& Component : Actor["Components"])
        {
            const auto& Components = (*It)["Components"];
            auto Comp = std::find_if(Components.begin(), Components.end(), [&](const auto& Candidate) { return Candidate["Name"] == Component["Name"]; });
            // CameraActor intentionally migrates its legacy editor icons to CineCameraMesh.
            if (Actor["Class"] == "ACameraActor" && (Component["Name"] == "CameraIcon" || Component["Name"] == "CameraArrow")) continue;
            Check(Comp != Components.end() && (*Comp)["Class"] == Component["Class"], "merged scene component disappeared");
            if (Component.contains("Collision")) Check((*Comp)["Collision"] == Component["Collision"], "merged scene collision changed on roundtrip");
            Check((*Comp)["Parent"] == Component["Parent"], "merged scene attachment changed");
        }
    }
    UWorld* PlayWorld = UWorld::GetDuplicatedWorldForPIE(EditorWorld);
    Check(PlayWorld != nullptr, "merged scene PIE duplication failed");
    PlayWorld->BeginPlay();
    auto* Player = Cast<APlayerPawn>(PlayWorld->GetPlayerPawn());
    auto* Controller = PlayWorld->GetPlayerController();
    Check(Player && Controller, "merged scene did not spawn its game player");
    Check(Controller->IsMoveInputIgnored() && Controller->IsLookInputIgnored(), "real intro did not lock input");
    FLevelSequenceData IntroData, RestoredData;
    Check(FLevelSequenceAsset::LoadFromFile("Assets/Sequences/Game_Start.seq.json", IntroData), "intro asset did not load");
    Check(FLevelSequenceAsset::DeserializeFromString(FLevelSequenceAsset::SerializeToString(IntroData), RestoredData), "intro spawnable mesh roundtrip failed");
    for (const auto& Entry : RestoredData.SpawnableActors)
    {
        if (Entry.StaticMeshPath.empty()) continue;
        auto* Actor = FLevelSequenceAsset::FindActorByNameInWorld(PlayWorld, Entry.ActorName);
        auto* ExpectedMesh = UAssetManager::GetAssetByPath<UStaticMesh>(Entry.StaticMeshPath);
        Check(Actor && ExpectedMesh, "intro mesh actor or its new asset is missing");
        Check(Actor->GetComponentByClass<UStaticMeshComponent>()->GetStaticMesh() == ExpectedMesh, "intro spawnable has no authored mesh");
        Check(!Actor->GetActorEnableCollision(), "intro visual mesh entered gameplay collision");
    }
    for (int I = 0; I < 270; ++I) PlayWorld->Tick(1.f / 60.f);
    Check(!Controller->IsMoveInputIgnored() && !Controller->IsLookInputIgnored(), "placeholder sequence kept input locked after intro");
    Check(Controller->GetViewTarget() == Player && !Controller->GetPlayerCameraManager()->HasActiveViewTargetBlend(), "real intro did not finish its return blend");
    Check(!FLevelSequenceAsset::FindActorByNameInWorld(PlayWorld, "IntroPacman") && !FLevelSequenceAsset::FindActorByNameInWorld(PlayWorld, "IntroFlashlight"), "intro visual spawnables were not cleaned up");
    Check(Player->GetCharacterMovement()->IsMovingOnGround(), "merged scene player did not land");
    AActor* Floor = nullptr;
    for (auto* Actor : PlayWorld->GetPersistentLevel()->GetActors()) if (Actor->GetFName() == FName("MazeFloor")) Floor = Actor;
    Check(Floor != nullptr, "local collision floor disappeared");
    FCollisionGeometry Geometry;
    Check(Floor->GetComponentByClass<UBoxComponent>()->GetCollisionGeometry(Geometry), "local collision floor lost its geometry");
    const float ExpectedZ = Geometry.Center.Z + Geometry.HalfExtent.Z + Player->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    Check(std::abs(Player->GetActorLocation().Z - ExpectedZ) < 0.02f, "merged scene player fell through its floor");
    Check(Player->GetFlashlightAttackComponent()->TryStartAttack(), "merged scene attack remained blocked after intro");
    Player->GetFlashlightAttackComponent()->CancelAttack();
    {
        FGameViewportClient Viewport;
        Viewport.SetWorld(PlayWorld); Viewport.Resize(1280, 720);
        auto* HUD = CastChecked<APacmanHUD>(PlayWorld->GetHUD());
        Check(HUD->Initialize(Controller, &Viewport), "combined battery/slomo HUD did not initialize");
        Player->SetSlomoGauge(50.f); Player->GetFlashlightActor()->SetGauge(25.f);
        HUD->Tick(0.f);
        auto* SlomoBar = HUD->FindWidget("SlomoBar");
        auto* BatteryFill = HUD->FindWidget("BatteryFill");
        Check(SlomoBar && BatteryFill, "one resource HUD replaced the other");
        Check(Near(SlomoBar->GetSize().X, 100.f) && Near(BatteryFill->GetSize().X, 34.f), "resource HUD displays the wrong gauge");
        Viewport.Resize(1920, 1080); HUD->Tick(0.f);
        Check(Near(SlomoBar->GetSize().X, 150.f) && Near(BatteryFill->GetSize().X, 51.f), "resource HUD scaling disagrees");
        auto* BatteryPanel = HUD->FindWidget("BatteryPanel");
        Check(Viewport.GetWidgetRoot()->RemoveChild(BatteryPanel), "battery panel was not a root child");
        delete BatteryPanel;
        Player->SetSlomoGauge(25.f); HUD->Tick(0.f);
        Check(Near(SlomoBar->GetSize().X, 75.f), "missing battery panel suppressed slomo HUD update");
        HUD->EndPlay();
    }
    PlayWorld->ClearWorld(); delete PlayWorld;
    EditorWorld->ClearWorld(); delete EditorWorld;
}

void PacmanSkyLightTests()
{
    UWorld* EditorWorld = UWorld::CreateWorld(EWorldType::Editor, false);
    Check(FJsonArchive::LoadWorld(EditorWorld, "Scenes/Pacman.scene"), "skylight test scene did not load");
    for (bool Cleared : {false, true})
    {
        UWorld* World = UWorld::GetDuplicatedWorldForPIE(EditorWorld);
        auto* Sky = Cast<ASkyLightActor>(FLevelSequenceAsset::FindActorByNameInWorld(World, "ASkyLightActor_0"));
        Check(Sky != nullptr, "Pacman skylight disappeared during PIE duplication");
        auto* Light = Sky->GetSkyLightComponent();
        auto Scripts = Sky->GetComponentsByClass<ULuaScriptComponent>();
        Check(Scripts.Num() == 1 && Scripts[0]->GetScriptPath() == "Assets/Scripts/PacmanSkyLight.lua", "skylight script was not connected to the existing component");
        // Keep ghosts stationary while exercising the real game mode and Lua lifecycle.
        for (AActor* Actor : World->GetPersistentLevel()->GetActors())
            if (Cast<AGhostActor>(Actor))
                for (auto* Script : Actor->GetComponentsByClass<ULuaScriptComponent>()) Script->SetScriptPath("");
        Light->SetIntensity(2.5f);
        ULuaTestEngine Engine;
        UEngine* PreviousEngine = GEngine;
        GEngine = &Engine;
        FWorldContext& Context = Engine.CreateNewWorldContext(EWorldType::PIE);
        Context.SetCurrentWorld(World);
        UGameInstance Instance;
        Instance.SetWorldContext(&Context);
        Instance.Init();
        Context.GameViewport = MakeShared<FGameViewportClient>();
        Context.GameViewport->SetWorld(World);
        Context.GameViewport->Resize(1280, 720);
        World->BeginPlay();
        auto* Mode = CastChecked<APacmanGameMode>(World->GetGameMode());
        Check(Scripts[0]->GetScriptInstance().valid(), "skylight Lua component did not load in BeginPlay");
        Check(Mode->OnGameplayStarted.IsBound() && Mode->OnResultCinematicStarted.IsBound(), "skylight did not subscribe to cinematic transitions");
        Check(!Mode->HasGameplayStarted() && Near(Light->GetIntensity(), 2.5f), "intro brightness changed before gameplay");
        for (int I = 0; I < 600 && !Mode->HasGameplayStarted(); ++I) World->Tick(0.01f);
        Check(Mode->HasGameplayStarted(), "intro did not signal gameplay start");
        const float StartIntensity = Light->GetIntensity();
        Check(StartIntensity > 2.45f, "skylight snapped to darkness at intro end");
        // Advance only the actual component to measure its one-second fade exactly.
        Scripts[0]->TickComponent(0.5f);
        Check(std::abs(Light->GetIntensity() - (StartIntensity - 1.25f)) < 0.001f, "skylight fade is not linear over one second");
        Check(Scripts[0]->LoadOrReloadScript(true), "skylight hot reload failed");
        Scripts[0]->TickComponent(0.5f);
        Check(Near(Light->GetIntensity(), 0.f), "skylight did not reach zero after one second");
        Scripts[0]->TickComponent(0.25f);
        Check(Near(Light->GetIntensity(), 0.f), "skylight did not remain dark during gameplay");
        Mode->ShowGameResult(Cleared);
        Check(Near(Light->GetIntensity(), 0.f), "result fade restored brightness before the cinematic started");
        for (int I = 0; I < 80; ++I) World->Tick(0.01f);
        Check(!Mode->HasResultCinematicStarted() && Near(Light->GetIntensity(), 0.f), "result brightness changed before the black frame was rendered");
        Context.GameViewport->OnUIRendered.Broadcast();
        Check(Mode->HasResultCinematicStarted(), "result cinematic did not start");
        Check(Mode->HasClearCinematicStarted() == Cleared, "game over and clear cinematic signals were confused");
        Check(Near(Light->GetIntensity(), 2.5f), "result cinematic did not synchronously restore the authored skylight intensity");
        Scripts[0]->TickComponent(1.f);
        Check(Near(Light->GetIntensity(), 2.5f), "old fade overwrote cinematic brightness");
        // Component removal must release callbacks while the game mode is still alive.
        Scripts[0]->EndPlay();
        Check(!Mode->OnGameplayStarted.IsBound() && !Mode->OnResultCinematicStarted.IsBound(), "skylight script leaked cinematic callbacks");
        Check(Near(Light->GetIntensity(), 2.5f), "skylight EndPlay did not restore authored brightness");
        World->EndPlay();
        Instance.Shutdown();
        Context.SetCurrentWorld(nullptr);
        GEngine = PreviousEngine;
        World->ClearWorld(); delete World;
    }
    EditorWorld->ClearWorld(); delete EditorWorld;
}
}

int main()
{
    try
    {
        FRenderDevice Device;
        Check(Device.GetDevice() != nullptr, "D3D11 initialization failed");
        RenderCommand::Init(&Device);
        TimeAndInputTests(); CameraAndSequenceTests(); BranchCameraIntegrationTests(); LuaAndAttackTests(); SlomoResourceTests(); PacmanSceneTests(); PacmanSkyLightTests();
        UAssetManager::Shutdown();
        FRenderResourceManager::Shutdown();
        std::cout << "PASS " << Checks << " main merge integration checks\n";
        return 0;
    }
    catch (const std::exception& Error) { std::cerr << "FAIL: " << Error.what() << '\n'; return 1; }
}
