#include "EnginePCH.h"
#include "PacmanHUD.h"

#include "CoinActor.h"
#include "PlayerPawn.h"
#include "PacmanGameMode.h"
#include "ScoreboardUI.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Actor/MapActor.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "UI/Widget.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>

namespace
{
	constexpr const char* ScorePath = "Saved/Scoreboard.json";
	struct FScoreEntry { FString Name; double Seconds; };

	FString FormatTime(double Seconds)
	{
		const int64 Centiseconds = static_cast<int64>(std::llround(Seconds * 100.0));
		return std::format("{}:{:02}.{:02}", Centiseconds / 6000, (Centiseconds / 100) % 60, Centiseconds % 100);
	}

	FString FitScoreName(const FString& Name)
	{
		size_t End = 0;
		for (int32 Count = 0; Count < 9 && End < Name.size(); ++Count)
		{
			++End;
			while (End < Name.size() && (static_cast<unsigned char>(Name[End]) & 0xC0) == 0x80) ++End;
		}
		if (End == Name.size()) return Name;
		while (End && (static_cast<unsigned char>(Name[--End]) & 0xC0) == 0x80) {}
		return Name.substr(0, End) + "...";
	}

	std::vector<FScoreEntry> LoadScores()
	{
		std::ifstream File(ScorePath);
		if (!File) return {};
		const json Data = json::parse(File, nullptr, false);
		if (!Data.is_array()) return {};
		std::vector<FScoreEntry> Scores;
		for (const json& Row : Data)
		{
			if (!Row.is_object() || !Row.contains("name") || !Row["name"].is_string() ||
				!Row.contains("seconds") || !Row["seconds"].is_number()) continue;
			FScoreEntry Entry{Row["name"].get<FString>(), Row["seconds"].get<double>()};
			if (Entry.Name.empty() || Entry.Name.size() > 64 || !std::isfinite(Entry.Seconds) || Entry.Seconds < 0.0) continue;
			Scores.push_back(std::move(Entry));
		}
		std::stable_sort(Scores.begin(), Scores.end(), [](const FScoreEntry& A, const FScoreEntry& B) { return A.Seconds < B.Seconds; });
		return Scores;
	}

	bool SaveScores(const std::vector<FScoreEntry>& Scores)
	{
		std::error_code Error;
		std::filesystem::create_directories("Saved", Error);
		if (Error) return false;
		json Data = json::array();
		for (const FScoreEntry& Score : Scores)
			Data.push_back({{"name", Score.Name}, {"seconds", Score.Seconds}});
		std::ofstream File(ScorePath, std::ios::trunc);
		if (!File) return false;
		File << Data.dump(2);
		return File.good();
	}
}

APacmanHUD::APacmanHUD()
{
	PrimaryActorTick.bCanEverTick = true;
}

void APacmanHUD::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bResultScreenOpen) LayoutResultScreen();
	if (bNameEntryOpen) LayoutNameEntry();
	if (bScoreboardOpen) ScoreboardUI::Layout(*this);
	UWorld* World = GetWorld();
	UMinimapWidget* Minimap = Cast<UMinimapWidget>(FindWidget("Minimap"));
	if (Minimap) LayoutGameUI();
	if (!World || !Minimap || !World->GetPersistentLevel()) return;
	const AMapActor* Map = nullptr;
	for (AActor* Actor : World->GetPersistentLevel()->GetActors())
		if ((Map = Cast<AMapActor>(Actor))) break;
	Minimap->SetMap(Map);
	if (!Map || Map->GetGridTransformWarning())
	{
		Minimap->ClearPlayer();
		Minimap->SetCoins({});
		return;
	}

	TArray<FVector2> Coins;
	for (AActor* Actor : World->GetPersistentLevel()->GetActors())
		if (ACoinActor* Coin = Cast<ACoinActor>(Actor))
		{
			FVector2 UV;
			if (Map->TryWorldToGridUVProjected(Coin->GetActorLocation(), UV)) Coins.Add(UV);
		}
	Minimap->SetCoins(std::move(Coins));

	APawn* Player = World->GetPlayerPawn();
	FVector2 PlayerUV;
	if (!Player || !Map->TryWorldToGridUVProjected(Player->GetActorLocation(), PlayerUV))
	{
		Minimap->ClearPlayer();
		return;
	}
	float Yaw = Player->GetActorRotation().Yaw;
	if (UCameraComponent* Camera = Player->GetCameraComponent()) Yaw = Camera->GetWorldRotation().Yaw;
	const float Radians = (Yaw - Map->GetActorRotation().Yaw) * 0.01745329252f;
	Minimap->SetPlayer(PlayerUV, {std::cos(Radians), std::sin(Radians)});
}

void APacmanHUD::OnWidgetShown(const FString& Path)
{
	if (Path == "Assets/UI/GameUI.ui.json") { LayoutGameUI(); return; }
	bResultScreenOpen = Path == "Assets/UI/GameOver.ui.json" || Path == "Assets/UI/GameClear.ui.json";
	bShowingGameOver = Path == "Assets/UI/GameOver.ui.json";
	if (bResultScreenOpen) LayoutResultScreen();
	if (Path == "Assets/UI/GameOver.ui.json")
	{
		// 게임오버 화면 버튼 클릭 시 메인메뉴로 이동
		BindButton("Button2", [this]
		{
			if (FGameViewportClient* Viewport = GetGameViewport())
				Viewport->RequestSceneChange("Scenes/MainMenu.scene", "Scenes/MainMenu.scene");
		});
		return;
	}
	if (Path == "Assets/UI/GameClear.ui.json")
	{
		BindButton("Button2", [this] { ShowNameEntry(); });
		return;
	}
	if (Path == "Assets/UI/NameEntry.ui.json")
	{
		bNameEntryOpen = true;
		LayoutNameEntry();
		SetText("ClearTime", FormatTime(ClearSeconds));
		if (UTextInputWidget* Input = Cast<UTextInputWidget>(FindWidget("NameEntry")))
			Input->SetOnSubmitted([this] { SaveScore(); });
		BindButton("SaveScore", [this] { SaveScore(); });
		return;
	}
	if (Path != "Assets/UI/Scoreboard.ui.json") return;
	bNameEntryOpen = false;
	bScoreboardOpen = true;
	ScoreboardUI::Layout(*this);
	ScoreboardUI::Populate(*this, ClearSeconds);
	BindButton("BackToMenu", [this]
	{
		if (FGameViewportClient* Viewport = GetGameViewport())
			Viewport->RequestSceneChange("Scenes/MainMenu.scene", "Scenes/MainMenu.scene");
	});
}

void APacmanHUD::LayoutGameUI()
{
	FGameViewportClient* Viewport = GetGameViewport();
	if (!Viewport) return;
	const float Scale = std::min(Viewport->GetWidth() / 1280.0f, Viewport->GetHeight() / 720.0f);
	if (Scale <= 0.0f) return;
	if (UMinimapWidget* Map = Cast<UMinimapWidget>(FindWidget("Minimap")))
	{
		Map->SetPosition({-32.0f * Scale, 8.0f * Scale});
		Map->SetSize({318.0f * Scale, 302.0f * Scale});
	}
	const auto LayoutIcon = [this, Scale](const char* Name, float X)
	{
		if (UImageWidget* Icon = Cast<UImageWidget>(FindWidget(Name)))
		{
			Icon->SetPosition({X * Scale, 24.0f * Scale});
			Icon->SetSize({48.0f * Scale, 48.0f * Scale});
		}
	};
	const auto LayoutText = [this, Scale](const char* Name, float X, float Width)
	{
		if (UTextWidget* Label = Cast<UTextWidget>(FindWidget(Name)))
		{
			Label->SetPosition({X * Scale, 26.0f * Scale});
			Label->SetSize({Width * Scale, 48.0f * Scale});
			Label->SetFontSize(40.0f * Scale);
		}
	};
	LayoutIcon("Heart", 32.0f);
	LayoutText("Life", 88.0f, 50.0f);
	LayoutIcon("Coin", 170.0f);
	LayoutText("Coins", 226.0f, 70.0f);

	if (UTextWidget* SlomoLabel = Cast<UTextWidget>(FindWidget("SlomoLabel")))
	{
		SlomoLabel->SetAnchor({1.0f, 1.0f});
		SlomoLabel->SetPivot({1.0f, 1.0f});
		SlomoLabel->SetPosition({-32.0f * Scale, -50.0f * Scale});
		SlomoLabel->SetSize({220.0f * Scale, 26.0f * Scale});
		SlomoLabel->SetFontSize(20.0f * Scale);
	}
	if (UImageWidget* SlomoBarBG = Cast<UImageWidget>(FindWidget("SlomoBarBG")))
	{
		SlomoBarBG->SetAnchor({1.0f, 1.0f});
		SlomoBarBG->SetPivot({1.0f, 1.0f});
		SlomoBarBG->SetPosition({-32.0f * Scale, -24.0f * Scale});
		SlomoBarBG->SetSize({204.0f * Scale, 18.0f * Scale});
	}
	if (UImageWidget* SlomoBar = Cast<UImageWidget>(FindWidget("SlomoBar")))
	{
		SlomoBar->SetAnchor({1.0f, 1.0f});
		SlomoBar->SetPivot({1.0f, 1.0f});
		float GaugeRatio = 1.0f;
		if (UWorld* World = GetWorld())
		{
			if (APlayerPawn* PlayerPawn = Cast<APlayerPawn>(World->GetPlayerPawn()))
			{
				GaugeRatio = PlayerPawn->GetMaxSlomoGauge() > 0.0f ? 
					(PlayerPawn->GetSlomoGauge() / PlayerPawn->GetMaxSlomoGauge()) : 0.0f;
			}
		}
		GaugeRatio = std::clamp(GaugeRatio, 0.0f, 1.0f);
		const float FullWidth = 200.0f * Scale;
		SlomoBar->SetPosition({(-34.0f - (1.0f - GaugeRatio) * 200.0f) * Scale, -26.0f * Scale});
		SlomoBar->SetSize({FullWidth * GaugeRatio, 14.0f * Scale});
	}
}

void APacmanHUD::LayoutResultScreen()
{
	FGameViewportClient* Viewport = GetGameViewport();
	if (!Viewport) return;
	const float Width = static_cast<float>(Viewport->GetWidth());
	const float Height = static_cast<float>(Viewport->GetHeight());
	if (Width <= 0.0f || Height <= 0.0f) return;
	const float ArtWidth = bShowingGameOver ? 938.0f : 1066.0f;
	constexpr float ArtHeight = 768.0f;
	const float Scale = std::min(Width / ArtWidth, Height / ArtHeight);
	if (UButtonWidget* Button = Cast<UButtonWidget>(FindWidget("Button2")))
	{
		Button->SetAnchor({0, 0});
		Button->SetPivot({0, 0});
		Button->SetPosition({0, 0});
		Button->SetSize({Width, Height});
		Button->SetBackgroundColor({0, 0, 0, 1});
		Button->SetHoverBackgroundColor({0, 0, 0, 1});
	}
	if (UImageWidget* Image = Cast<UImageWidget>(FindWidget("Image3")))
	{
		Image->SetAnchor({0, 0});
		Image->SetPivot({0, 0});
		Image->SetPosition({(Width - ArtWidth * Scale) * 0.5f, (Height - ArtHeight * Scale) * 0.5f});
		Image->SetSize({ArtWidth * Scale, ArtHeight * Scale});
	}
}

void APacmanHUD::ShowNameEntry()
{
	if (UWorld* World = GetWorld())
		if (APacmanGameMode* Mode = Cast<APacmanGameMode>(World->GetGameMode()))
			ClearSeconds = Mode->GetClearSeconds();
	ShowWidget("Assets/UI/NameEntry.ui.json");
}

void APacmanHUD::LayoutNameEntry()
{
	FGameViewportClient* Viewport = GetGameViewport();
	if (!Viewport) return;
	const float Width = static_cast<float>(Viewport->GetWidth());
	const float Height = static_cast<float>(Viewport->GetHeight());
	const float Scale = std::min(Width / 1672.0f, Height / 941.0f);
	const float Left = (Width - 1672.0f * Scale) * 0.5f;
	const float Top = (Height - 941.0f * Scale) * 0.5f;
	if (UImageWidget* Shade = Cast<UImageWidget>(FindWidget("NameShade"))) Shade->SetSize({Width, Height});
	if (UImageWidget* Background = Cast<UImageWidget>(FindWidget("NameBackground")))
	{
		Background->SetPosition({Left, Top});
		Background->SetSize({1672.0f * Scale, 941.0f * Scale});
	}
	if (UTextWidget* Time = Cast<UTextWidget>(FindWidget("ClearTime")))
	{
		Time->SetAnchor({0, 0});
		Time->SetPosition({Left + 875.0f * Scale, Top + 425.0f * Scale});
		Time->SetSize({210.0f * Scale, 50.0f * Scale});
		Time->SetFontSize(38.0f * Scale);
	}
	if (UTextInputWidget* Input = Cast<UTextInputWidget>(FindWidget("NameEntry")))
	{
		Input->SetAnchor({0, 0});
		Input->SetPosition({Left + 570.0f * Scale, Top + 585.0f * Scale});
		Input->SetSize({530.0f * Scale, 65.0f * Scale});
		Input->SetFontSize(35.0f * Scale);
	}
	if (UTextWidget* Error = Cast<UTextWidget>(FindWidget("NameError")))
	{
		Error->SetAnchor({0, 0});
		Error->SetPosition({Left + 670.0f * Scale, Top + 681.0f * Scale});
		Error->SetSize({400.0f * Scale, 40.0f * Scale});
		Error->SetFontSize(26.0f * Scale);
	}
	if (UButtonWidget* Confirm = Cast<UButtonWidget>(FindWidget("SaveScore")))
	{
		Confirm->SetAnchor({0, 0});
		Confirm->SetPosition({Left + 683.0f * Scale, Top + 732.0f * Scale});
		Confirm->SetSize({306.0f * Scale, 62.0f * Scale});
	}
}

void APacmanHUD::ShowScoreboard()
{
	if (bScoreboardOpen) return;
	ShowWidget("Assets/UI/Scoreboard.ui.json");
}

void APacmanHUD::SaveScore()
{
	if (bScoreSaved) return;
	UTextInputWidget* Input = Cast<UTextInputWidget>(FindWidget("NameEntry"));
	if (!Input) return;
	FString Name = Input->GetValue();
	const size_t First = Name.find_first_not_of(' ');
	if (First == FString::npos) { SetText("NameError", "Enter a name first"); return; }
	const size_t Last = Name.find_last_not_of(' ');
	Name = Name.substr(First, Last - First + 1);
	auto Scores = LoadScores();
	Scores.push_back({Name, ClearSeconds});
	std::stable_sort(Scores.begin(), Scores.end(), [](const FScoreEntry& A, const FScoreEntry& B) { return A.Seconds < B.Seconds; });
	if (!SaveScores(Scores)) { SetText("NameError", "Could not save score"); return; }
	bScoreSaved = true;
	ShowScoreboard();
}

void ScoreboardUI::Populate(AGameHUD& HUD, std::optional<double> CurrentSeconds)
{
	const auto Scores = LoadScores();
	for (size_t Index = 0; Index < 5; ++Index)
	{
		const FString Number = std::to_string(Index + 1);
		HUD.SetText("ScoreName" + Number, Index < Scores.size() ? FitScoreName(Scores[Index].Name) : "");
		HUD.SetText("ScoreTime" + Number, Index < Scores.size() ? FormatTime(Scores[Index].Seconds) : "");
	}
	HUD.SetText("CurrentTime", CurrentSeconds ? FormatTime(*CurrentSeconds) : "");
}

void ScoreboardUI::Layout(AGameHUD& HUD)
{
	FGameViewportClient* Viewport = HUD.GetGameViewport();
	if (!Viewport) return;
	const float Width = static_cast<float>(Viewport->GetWidth());
	const float Height = static_cast<float>(Viewport->GetHeight());
	const float Scale = std::min(Width / 1672.0f, Height / 941.0f);
	const float Left = (Width - 1672.0f * Scale) * 0.5f;
	const float Top = (Height - 941.0f * Scale) * 0.5f;
	if (UImageWidget* Shade = Cast<UImageWidget>(HUD.FindWidget("ScoreboardShade")))
		Shade->SetSize({Width, Height});
	if (UImageWidget* Background = Cast<UImageWidget>(HUD.FindWidget("ScoreboardBackground")))
	{
		Background->SetPosition({Left, Top});
		Background->SetSize({1672.0f * Scale, 941.0f * Scale});
	}
	constexpr float RowY[5] = {0.368f, 0.454f, 0.541f, 0.628f, 0.715f};
	for (size_t Index = 0; Index < 5; ++Index)
	{
		const FString Number = std::to_string(Index + 1);
		if (UTextWidget* Name = Cast<UTextWidget>(HUD.FindWidget("ScoreName" + Number)))
		{
			Name->SetAnchor({0, 0});
			Name->SetPosition({Left + 0.383f * 1672.0f * Scale, Top + RowY[Index] * 941.0f * Scale});
			Name->SetSize({310.0f * Scale, 48.0f * Scale});
			Name->SetFontSize(32.0f * Scale);
		}
		if (UTextWidget* Time = Cast<UTextWidget>(HUD.FindWidget("ScoreTime" + Number)))
		{
			Time->SetAnchor({0, 0});
			Time->SetPosition({Left + 0.623f * 1672.0f * Scale, Top + RowY[Index] * 941.0f * Scale});
			Time->SetSize({210.0f * Scale, 48.0f * Scale});
			Time->SetFontSize(32.0f * Scale);
		}
	}
	if (UTextWidget* Time = Cast<UTextWidget>(HUD.FindWidget("CurrentTime")))
	{
		Time->SetAnchor({0, 0});
		Time->SetPosition({Left + 0.498f * 1672.0f * Scale, Top + 0.838f * 941.0f * Scale});
		Time->SetSize({340.0f * Scale, 55.0f * Scale});
		Time->SetFontSize(36.0f * Scale);
	}
	if (UButtonWidget* Back = Cast<UButtonWidget>(HUD.FindWidget("BackToMenu")))
	{
		Back->SetAnchor({0, 0});
		Back->SetPosition({Left + 1552.0f * Scale, Top + 30.0f * Scale});
		Back->SetSize({90.0f * Scale, 55.0f * Scale});
		Back->SetFontSize(32.0f * Scale);
	}
}
