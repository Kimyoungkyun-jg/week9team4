#include "EnginePCH.h"
#include "PacmanHUD.h"

#include "CoinActor.h"
#include "PacmanGameMode.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Actor/MapActor.h"
#include "GameFramework/DefaultPawn.h"
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
	if (bNameEntryOpen)
		if (FGameViewportClient* Viewport = GetGameViewport())
			if (UImageWidget* Background = Cast<UImageWidget>(FindWidget("NameBackground")))
				Background->SetSize({static_cast<float>(Viewport->GetWidth()), static_cast<float>(Viewport->GetHeight())});
	if (bScoreboardOpen) LayoutScoreboard();
	UWorld* World = GetWorld();
	UMinimapWidget* Minimap = Cast<UMinimapWidget>(FindWidget("Minimap"));
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
	if (ADefaultPawn* DefaultPawn = Cast<ADefaultPawn>(Player))
		if (UCameraComponent* Camera = DefaultPawn->GetCameraComponent())
			Yaw = Camera->GetWorldRotation().Yaw;
	const float Radians = (Yaw - Map->GetActorRotation().Yaw) * 0.01745329252f;
	Minimap->SetPlayer(PlayerUV, {std::cos(Radians), std::sin(Radians)});
}

void APacmanHUD::OnWidgetShown(const FString& Path)
{
	if (Path == "Assets/UI/GameClear.ui.json")
	{
		BindButton("Button2", [this] { ShowNameEntry(); });
		return;
	}
	if (Path == "Assets/UI/NameEntry.ui.json")
	{
		bNameEntryOpen = true;
		SetText("ClearTime", "Clear time: " + FormatTime(ClearSeconds));
		if (UTextInputWidget* Input = Cast<UTextInputWidget>(FindWidget("NameEntry")))
			Input->SetOnSubmitted([this] { SaveScore(); });
		BindButton("SaveScore", [this] { SaveScore(); });
		return;
	}
	if (Path != "Assets/UI/Scoreboard.ui.json") return;
	bNameEntryOpen = false;
	bScoreboardOpen = true;
	LayoutScoreboard();
	UpdateScoreboard();
	BindButton("BackToMenu", [this]
	{
		if (FGameViewportClient* Viewport = GetGameViewport())
			Viewport->RequestSceneChange("Scenes/MainMenu.scene", "Scenes/MainMenu.scene");
	});
}

void APacmanHUD::ShowNameEntry()
{
	if (UWorld* World = GetWorld())
		if (APacmanGameMode* Mode = Cast<APacmanGameMode>(World->GetGameMode()))
			ClearSeconds = Mode->GetClearSeconds();
	ShowWidget("Assets/UI/NameEntry.ui.json");
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
	if (First == FString::npos) { SetText("NamePrompt", "Enter a name first"); return; }
	const size_t Last = Name.find_last_not_of(' ');
	Name = Name.substr(First, Last - First + 1);
	auto Scores = LoadScores();
	Scores.push_back({Name, ClearSeconds});
	std::stable_sort(Scores.begin(), Scores.end(), [](const FScoreEntry& A, const FScoreEntry& B) { return A.Seconds < B.Seconds; });
	if (!SaveScores(Scores)) { SetText("NamePrompt", "Could not save score"); return; }
	bScoreSaved = true;
	ShowScoreboard();
}

void APacmanHUD::UpdateScoreboard()
{
	const auto Scores = LoadScores();
	for (size_t Index = 0; Index < 5; ++Index)
	{
		const FString Number = std::to_string(Index + 1);
		SetText("ScoreName" + Number, Index < Scores.size() ? FitScoreName(Scores[Index].Name) : "");
		SetText("ScoreTime" + Number, Index < Scores.size() ? FormatTime(Scores[Index].Seconds) : "");
	}
	SetText("CurrentTime", FormatTime(ClearSeconds));
}

void APacmanHUD::LayoutScoreboard()
{
	FGameViewportClient* Viewport = GetGameViewport();
	if (!Viewport) return;
	const float Width = static_cast<float>(Viewport->GetWidth());
	const float Height = static_cast<float>(Viewport->GetHeight());
	const float Scale = std::min(Width / 1672.0f, Height / 941.0f);
	const float Left = (Width - 1672.0f * Scale) * 0.5f;
	const float Top = (Height - 941.0f * Scale) * 0.5f;
	if (UImageWidget* Shade = Cast<UImageWidget>(FindWidget("ScoreboardShade")))
		Shade->SetSize({Width, Height});
	if (UImageWidget* Background = Cast<UImageWidget>(FindWidget("ScoreboardBackground")))
	{
		Background->SetPosition({Left, Top});
		Background->SetSize({1672.0f * Scale, 941.0f * Scale});
	}
	constexpr float RowY[5] = {0.368f, 0.454f, 0.541f, 0.628f, 0.715f};
	for (size_t Index = 0; Index < 5; ++Index)
	{
		const FString Number = std::to_string(Index + 1);
		if (UTextWidget* Name = Cast<UTextWidget>(FindWidget("ScoreName" + Number)))
		{
			Name->SetAnchor({0, 0});
			Name->SetPosition({Left + 0.383f * 1672.0f * Scale, Top + RowY[Index] * 941.0f * Scale});
			Name->SetFontSize(32.0f * Scale);
		}
		if (UTextWidget* Time = Cast<UTextWidget>(FindWidget("ScoreTime" + Number)))
		{
			Time->SetAnchor({0, 0});
			Time->SetPosition({Left + 0.623f * 1672.0f * Scale, Top + RowY[Index] * 941.0f * Scale});
			Time->SetFontSize(32.0f * Scale);
		}
	}
	if (UTextWidget* Time = Cast<UTextWidget>(FindWidget("CurrentTime")))
	{
		Time->SetAnchor({0, 0});
		Time->SetPosition({Left + 0.498f * 1672.0f * Scale, Top + 0.838f * 941.0f * Scale});
		Time->SetFontSize(36.0f * Scale);
	}
	if (UButtonWidget* Back = Cast<UButtonWidget>(FindWidget("BackToMenu")))
	{
		Back->SetAnchor({0, 0});
		Back->SetPosition({Left + 1552.0f * Scale, Top + 30.0f * Scale});
		Back->SetSize({90.0f * Scale, 55.0f * Scale});
		Back->SetFontSize(32.0f * Scale);
	}
}
