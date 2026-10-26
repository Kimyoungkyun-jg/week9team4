// Standalone integration test; intentionally outside application source globs.
#include "EnginePCH.h"
#include "GameFramework/Actor/MapActor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"
#include "Render/Texture2D.h"
#include "Render/RenderResourceManager.h"
#include "Serialization/JsonArchive.h"
#include "Core/Windows/WindowsPlatformTime.h"
#include "Editor/Details/MapTileEditor.h"
#include "Editor/Rendering/MapTileRenderer.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cmath>
#include <limits>

namespace
{
	int Failures = 0, Checks = 0;
	void Check(bool Pass, const char* Name)
	{
		++Checks;
		if (!Pass) { ++Failures; std::cerr << "FAIL: " << Name << '\n'; }
	}
	AMapActor* FindMap(UWorld* World)
	{
		for (AActor* Actor : World->GetPersistentLevel()->GetActors())
			if (auto* Map = Cast<AMapActor>(Actor)) return Map;
		return nullptr;
	}
	bool HasTerrain(const AMapActor& Map, int C, int R, EMapTileTerrain Expected)
	{
		EMapTileTerrain Value;
		return Map.TryGetTile(C, R, Value) && Value == Expected;
	}
	TArray<uint8> JsonBytes(const json& J)
	{
		const auto Text = J.dump();
		TArray<uint8> Bytes;
		Bytes.SetNum(static_cast<int32>(Text.size()));
		std::memcpy(Bytes.GetData(), Text.data(), Text.size());
		return Bytes;
	}
}

int main()
{
	FPlatformTime::InitTiming();
	FRenderDevice Device;
	RenderCommand::Init(&Device);
	FRenderResourceManager::Init();
	// Use the actual OBJ/MTL importer and register the actual asset path, without scanning unrelated assets.
	UAssetManager::Get().CreateDefaultTextures();
	UAssetManager::Get().CreateDefaultMaterial();
	UWorld* EarlyWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	AMapActor* EarlyMap = EarlyWorld->SpawnActor<AMapActor>();
	Check(EarlyMap && !EarlyMap->GetStaticMeshComponent()->GetStaticMesh() &&
		HasTerrain(*EarlyMap,0,0,EMapTileTerrain::Wall) && HasTerrain(*EarlyMap,28,24,EMapTileTerrain::Floor),
		"authored terrain initializes even before map asset registration");
	EarlyWorld->ClearWorld();
	UStaticMesh* Mesh = UAssetManager::LoadObjStaticMesh("Assets/Models/Map/Map.obj");
	Check(Mesh != nullptr, "OBJ/MTL asset loads");
	if (!Mesh) return 1;
	UAssetManager::Get().RegisterAsset("Models/Map/Map.obj", Mesh);
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	AMapActor* Map = World->SpawnActor<AMapActor>();
	Check(Map && Map->GetStaticMeshComponent()->GetStaticMesh() == Mesh, "spawn with single map mesh");
	Check(Map->GetComponents().Num() == 2 && !Map->PrimaryActorTick.bCanEverTick, "one root + one mesh, no Actor tick");
	Check(Map->GetColumns() == 56 && Map->GetRows() == 48 && Map->GetTileSize() == .5f, "default half-unit grid covers full 28x24 map");
	Check(!Map->GetGridTransformWarning(), "default transform supported");
	Check(std::abs(Map->GetGridOrigin().Z - (-0.978149f)) < .00001f, "Map.obj grid follows the lower floor surface");
	int32 WallCount = 0, FloorCount = 0;
	uint64 SeedHash = 14695981039346656037ull;
	json SeedPreview = json::array();
	for (int32 Row = 0; Row < Map->GetRows(); ++Row)
	{
		json PreviewRow = json::array();
		for (int32 Column = 0; Column < Map->GetColumns(); ++Column)
		{
			EMapTileTerrain Terrain;
			Map->TryGetTile(Column, Row, Terrain);
			WallCount += Terrain == EMapTileTerrain::Wall;
			FloorCount += Terrain == EMapTileTerrain::Floor;
			SeedHash = (SeedHash ^ static_cast<uint8>(Terrain)) * 1099511628211ull;
			PreviewRow.push_back(static_cast<int32>(Terrain));
		}
		SeedPreview.push_back(PreviewRow);
	}
	std::ofstream("Build/MapActorTests/default-terrain.json") << SeedPreview.dump();
	std::cout << "Default map seed: Wall " << WallCount << ", Floor " << FloorCount << '\n';
	// Golden counts/hash sampled independently from the user-approved 56x48 image.
	Check(WallCount == 2037 && FloorCount == 651, "approved initial terrain counts");
	Check(SeedHash == 3552546181708364762ull, "all 2688 creation tiles match the approved image in logical coordinate order");
	for (int32 Row = 0; Row < 48; ++Row)
		for (int32 Column = 0; Column < 56; ++Column)
			if (Row == 0 || Row == 47 || Column == 0 || Column == 55 || Column == 1)
				Check(HasTerrain(*Map,Column,Row,EMapTileTerrain::Wall), "outer border and marked right interior strip are Wall");
	Check(HasTerrain(*Map,54,46,EMapTileTerrain::Wall) && HasTerrain(*Map,48,40,EMapTileTerrain::Wall) &&
		HasTerrain(*Map,35,43,EMapTileTerrain::Wall) && HasTerrain(*Map,35,5,EMapTileTerrain::Wall) &&
		HasTerrain(*Map,21,23,EMapTileTerrain::Wall), "marked islands, large rooms and thin corridors become Wall");
	Check(HasTerrain(*Map,2,24,EMapTileTerrain::Wall) && HasTerrain(*Map,28,24,EMapTileTerrain::Floor) &&
		HasTerrain(*Map,29,1,EMapTileTerrain::Floor) && HasTerrain(*Map,40,36,EMapTileTerrain::Floor), "unmarked central room and corridors remain Floor");

	const AMapActor& ReadOnlyMap = *Map;
	auto TerrainCopy = ReadOnlyMap.GetTilesCopy();
	Check(TerrainCopy.Num() == 56 * 48 && sizeof(EMapTileTerrain) == 1, "terrain copy has 2688 one-byte elements");
	bool CopyMatches = TerrainCopy.Num() == 56 * 48;
	if (CopyMatches)
		for (int32 Row = 0; Row < 48; ++Row)
			for (int32 Column = 0; Column < 56; ++Column)
				CopyMatches &= HasTerrain(*Map,Column,Row,TerrainCopy[Row * 56 + Column]);
	Check(CopyMatches, "terrain copy includes the entire final terrain in row-major order");
	if (TerrainCopy.Num() == 56 * 48)
	{
		TerrainCopy[0] = EMapTileTerrain::Floor;
		Check(HasTerrain(*Map,0,0,EMapTileTerrain::Wall), "editing a returned copy leaves MapActor terrain unchanged");
		Map->SetTile(0,0,EMapTileTerrain::Unused);
		Check(TerrainCopy[0] == EMapTileTerrain::Floor, "later MapActor edits leave an earlier copy unchanged");
		const auto EditedCopy = ReadOnlyMap.GetTilesCopy();
		Check(EditedCopy.Num() == 56 * 48 && EditedCopy[0] == EMapTileTerrain::Unused,
			"a new copy reflects current edits including Unused rather than the initial template");
		Map->SetTile(0,0,EMapTileTerrain::Wall);
	}
	TArray<uint8> SeedBytes;
	UWorld* SeedWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	Check(FJsonArchive::SaveWorldToBytes(World,SeedBytes) && FJsonArchive::LoadWorldFromBytes(SeedWorld,SeedBytes), "save/load approved initial terrain");
	AMapActor* SeedCopy = FindMap(SeedWorld);
	for (int32 Row = 0; Row < 48; ++Row)
		for (int32 Column = 0; Column < 56; ++Column)
		{
			EMapTileTerrain Expected;
			Map->TryGetTile(Column,Row,Expected);
			Check(SeedCopy && HasTerrain(*SeedCopy,Column,Row,Expected), "entire initial seed preserved on load");
		}
	SeedWorld->ClearWorld();
	UWorld* InitialPIE = UWorld::GetDuplicatedWorldForPIE(World);
	AMapActor* InitialPlayMap = FindMap(InitialPIE);
	bool InitialPIEMatches = InitialPlayMap && InitialPlayMap != Map &&
		InitialPlayMap->GetColumns() == 56 && InitialPlayMap->GetRows() == 48;
	for (int32 Row = 0; Row < 48; ++Row)
		for (int32 Column = 0; Column < 56; ++Column)
			InitialPIEMatches &= InitialPlayMap && HasTerrain(*InitialPlayMap,Column,Row,
				static_cast<EMapTileTerrain>(SeedPreview[Row][Column].get<int32>()));
	Check(InitialPIEMatches, "PIE preserves all tiles of the approved creation layout");
	if (InitialPlayMap) InitialPlayMap->SetTile(0,0,EMapTileTerrain::Floor);
	Check(InitialPlayMap && HasTerrain(*InitialPlayMap,0,0,EMapTileTerrain::Floor) &&
		HasTerrain(*Map,0,0,EMapTileTerrain::Wall), "PIE edits leave the editor creation layout independent");
	InitialPIE->ClearWorld();
	Map->FillTiles(EMapTileTerrain::Unused);
	Map->ConfigureGrid(2,3,2.f,FVector(7,8,9));
	Map->GetRootComponent()->SetTransform(FTransform(FRotator(0,25,0),FVector(2,3,4),FVector(2,2,2)));
	Check(Map->ResetGridToInitial(), "reset edited/resized grid");
	Check(Map->GetColumns() == 56 && Map->GetRows() == 48 && Map->GetTileSize() == .5f &&
		(Map->GetGridOrigin() - FVector(-14,-12,-.978149f)).Size() < .00001f, "reset restores dimensions, size and Map.obj origin");
	Check(Map->GetStaticMeshComponent()->GetStaticMesh() == Mesh && Map->GetActorLocation().X == 2 && Map->GetActorScale3D().X == 2 &&
		std::abs(Map->GetActorRotation().Yaw-25.f) < .0001f, "reset preserves mesh and Actor transform");
	for (int32 Row = 0; Row < 48; ++Row)
		for (int32 Column = 0; Column < 56; ++Column)
			Check(HasTerrain(*Map,Column,Row,static_cast<EMapTileTerrain>(SeedPreview[Row][Column].get<int32>())), "reset reproduces every creation tile");
	Map->GetStaticMeshComponent()->SetStaticMesh(nullptr);
	Map->ConfigureGrid(2,3,2.f,FVector(7,8,9));
	Map->SetTile(0,0,EMapTileTerrain::Unused);
	Check(!Map->ResetGridToInitial() && Map->GetColumns() == 2 && Map->GetTileSize() == 2.f && HasTerrain(*Map,0,0,EMapTileTerrain::Unused), "reset without mesh fails without changing grid");
	Map->GetStaticMeshComponent()->SetStaticMesh(Mesh);
	Check(Map->ResetGridToInitial(), "reset recovers when mesh is restored");
	// A closed vertical box inside approved Floor (28,24) must still be found by mesh analysis.
	// No renderer buffers are needed: this fixture exercises CPU initialization only.
	UStaticMesh* ProbeMesh = NewObject<UStaticMesh>();
	ProbeMesh->MeshData.AABB = Mesh->GetMeshData().AABB;
	for (float Z : { Mesh->GetMeshData().AABB.Min.Z, 0.f })
		for (const FVector2& XY : { FVector2(.05f,.05f), FVector2(.45f,.05f), FVector2(.45f,.45f), FVector2(.05f,.45f) })
		{
			FVertex Vertex;
			Vertex.Position = FVector(XY.X,XY.Y,Z);
			ProbeMesh->MeshData.Vertices.Add(Vertex);
		}
	for (uint32 Side = 0; Side < 4; ++Side)
	{
		const uint32 Next = (Side + 1) % 4;
		for (uint32 Index : { Side,Next,Next+4, Side,Next+4,Side+4 })
			ProbeMesh->MeshData.Indices.Add(Index);
	}
	ProbeMesh->RebuildRenderData();
	Map->GetStaticMeshComponent()->SetStaticMesh(ProbeMesh);
	Check(Map->ResetGridToInitial() && HasTerrain(*Map,28,24,EMapTileTerrain::Wall),
		"mesh analysis detects a wall even where the numeric target is Floor");
	Check(HasTerrain(*Map,29,24,EMapTileTerrain::Floor),
		"numeric Floor leaves wall-free mesh cells as Floor");
	Check(HasTerrain(*Map,0,0,EMapTileTerrain::Wall) && HasTerrain(*Map,35,43,EMapTileTerrain::Wall),
		"numeric Wall overrides apply where the probe mesh has no walls");
	Map->GetStaticMeshComponent()->SetStaticMesh(Mesh);
	Check(Map->ResetGridToInitial() && HasTerrain(*Map,28,24,EMapTileTerrain::Floor),
		"restoring Map.obj reproduces the approved central Floor");
	Map->GetRootComponent()->SetTransform(FTransform::Identity);
	Map->SetTile(0, 0, EMapTileTerrain::Wall);
	Map->SetTile(3, 2, EMapTileTerrain::Unused);
	Map->SetTile(55, 47, EMapTileTerrain::Wall);
	for (int R = 0; R < 48; ++R)
		for (int C = 0; C < 56; ++C)
		{
			FVector Center;
			int OutC = -1, OutR = -1;
			Check(Map->TryTileToWorldCenter(C, R, Center) && Map->TryWorldToTileXYProjected(Center, OutC, OutR) && C == OutC && R == OutR, "identity round trip");
		}
	int C = -7, R = -8;
	Check(Map->TryWorldToTileXYProjected(FVector(-14, -12, 900), C, R) && C == 0 && R == 0, "minimum inclusive, height projected");
	Check(Map->TryWorldToTileXYProjected(FVector(-13.5f, -11.5f, -900), C, R) && C == 1 && R == 1, "internal half-unit boundary chooses positive-side tile");
	Check(!Map->TryWorldToTileXYProjected(FVector(-14.001f, -12, 0), C, R), "negative local relative X rejected");
	Check(!Map->TryWorldToTileXYProjected(FVector(-14, -12.001f, 0), C, R), "negative local relative Y rejected");
	Check(!Map->TryWorldToTileXYProjected(FVector(14, 0, 0), C, R) && !Map->TryWorldToTileXYProjected(FVector(0, 12, 0), C, R), "max edges exclusive");
	Check(!Map->IsValidTile(-1, 0) && !Map->IsValidTile(56, 0) && !Map->SetTile(0, 48, EMapTileTerrain::Wall), "invalid tile access rejected");
	Check(!Map->SetTile(0, 0, static_cast<EMapTileTerrain>(99)), "invalid terrain rejected");
	const FVector Origin = Map->GetGridOrigin();
	const auto BeforeResizeCopy = Map->GetTilesCopy();
	Check(Map->ConfigureGrid(58, 50, .5f, Origin), "grow grid");
	const auto GrownCopy = Map->GetTilesCopy();
	Check(GrownCopy.Num() == 58 * 50 && GrownCopy[2 * 58 + 3] == EMapTileTerrain::Unused &&
		GrownCopy[49 * 58 + 57] == EMapTileTerrain::Floor, "terrain copy uses current dimensions and resized row stride");
	Check(BeforeResizeCopy.Num() == 56 * 48 && BeforeResizeCopy[2 * 56 + 3] == EMapTileTerrain::Unused,
		"resizing MapActor preserves the earlier copy and its original row stride");
	Check(HasTerrain(*Map, 0, 0, EMapTileTerrain::Wall) && HasTerrain(*Map, 3, 2, EMapTileTerrain::Unused) && HasTerrain(*Map, 55, 47, EMapTileTerrain::Wall), "width change preserves coordinates");
	Check(HasTerrain(*Map, 57, 49, EMapTileTerrain::Floor), "new tiles initialized Floor");
	Check(Map->ConfigureGrid(4, 3, .75f, FVector(-2, -1, .3f)) && HasTerrain(*Map, 3, 2, EMapTileTerrain::Unused), "shrink preserves overlap");
	Check(!Map->ConfigureGrid(0, 3, 1, Origin) && !Map->ConfigureGrid(257, 3, 1, Origin) &&
		!Map->ConfigureGrid(INT32_MAX, INT32_MAX, 1, Origin) && !Map->ConfigureGrid(4, 3, 0, Origin) &&
		!Map->ConfigureGrid(4, 3, -1, Origin) && !Map->ConfigureGrid(4, 3, std::numeric_limits<float>::infinity(), Origin), "bad allocations and sizes rejected");
	Check(Map->GetColumns() == 4 && Map->GetTileSize() == .75f, "invalid config leaves state unchanged");
	for (float Yaw : { 0.f, 37.f, 90.f, 180.f, -135.f })
		for (float Scale : { .001f, .25f, 1.f, 2.f, 7.f })
		{
			Map->GetRootComponent()->SetTransform(FTransform(FRotator(0, Yaw, 0), FVector(-17, 23, 4), FVector(Scale, Scale, Scale)));
			for (int Row = 0; Row < 3; ++Row)
				for (int Column = 0; Column < 4; ++Column)
				{
					FVector Center;
					Check(Map->TryTileToWorldCenter(Column, Row, Center) && Map->TryWorldToTileXYProjected(Center, C, R) && C == Column && R == Row, "translated rotated scaled round trip");
				}
			FVector A, B, D;
			Map->TryTileToWorldCenter(0, 0, A); Map->TryTileToWorldCenter(1, 0, B); Map->TryTileToWorldCenter(0, 1, D);
			Check(std::abs((B - A).Size() - .75f * Scale) < .0001f && std::abs(FVector::Cross(B-A,D-A).Size() - .75f*.75f*Scale*Scale) < .001f, "side scales linearly, area quadratically");
		}
	for (const FVector& Scale : { FVector(0,0,0), FVector(-1,-1,-1), FVector(1,2,1) })
	{
		Map->GetRootComponent()->SetTransform(FTransform(FRotator(), FVector(), Scale));
		FVector Center(8, 9, 10); C = -7; R = -8;
		Check(Map->GetGridTransformWarning() && !Map->TryTileToWorldCenter(0,0,Center) && !Map->TryWorldToTileXYProjected(FVector(),C,R) && C == -7 && R == -8 && Center.X == 8, "unsupported scale fails without changing outputs");
	}
	for (const FRotator& Rotation : { FRotator(5,0,0), FRotator(0,0,5) })
	{
		Map->GetRootComponent()->SetTransform(FTransform(Rotation, FVector()));
		Check(Map->GetGridTransformWarning() != nullptr, "pitch roll rejected");
	}
	Map->GetRootComponent()->SetTransform(FTransform(FRotator(0,37,0), FVector(3,-8,2), FVector(2,2,2)));
	Map->GetStaticMeshComponent()->SetRelativeLocation(FVector(1,0,0));
	Check(Map->GetGridTransformWarning() != nullptr, "separate mesh transform rejected");
	Map->GetStaticMeshComponent()->SetTransform(FTransform::Identity);
	// Give the renderer explicit terrain; its fixture must not depend on the creation layout.
	Map->SetTile(1,0,EMapTileTerrain::Floor);
	Map->SetTile(2,0,EMapTileTerrain::Floor);
	// Exercise the actual ImGui tile button, then read back the real D3D overlay.
	Map->GetRootComponent()->SetTransform(FTransform::Identity);
	ImGui::CreateContext();
	ImGuiIO& IO = ImGui::GetIO();
	IO.IniFilename = nullptr;
	IO.DisplaySize = ImVec2(1000, 1100); IO.DeltaTime = 1.f/60.f;
	unsigned char* FontPixels; int FontWidth, FontHeight;
	IO.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontWidth, &FontHeight);
	FMapTileEditor TileEditor;
	auto DrawEditor = [&](bool ActivateReset = false)
	{
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(850, 1000));
		ImGui::Begin("Map integration test");
		if (ActivateReset)
		{
			// Activate the real button through ImGui's navigation path in this test context.
			ImGui::PushID(Map);
			const ImGuiID ResetID = ImGui::GetID("Reset to Initial Grid");
			ImGui::PopID();
			ImGuiContext& GUI = *ImGui::GetCurrentContext();
			GUI.NavActivateId = GUI.NavActivateDownId = ResetID;
			GUI.NavInputSource = ImGuiInputSource_Keyboard;
		}
		TileEditor.DrawDetails(*Map); ImGui::End();
		ImGui::Render();
	};
	DrawEditor(); DrawEditor();
	ImVec2 TileClick(-1,-1);
	for (ImGuiWindow* Window : ImGui::GetCurrentContext()->Windows)
		if (std::strstr(Window->Name, "Tile Array"))
			TileClick = ImVec2(Window->DC.CursorStartPos.x + 36.f + 3.f*22.f + 11.f, Window->DC.CursorStartPos.y + 2.f*22.f + 11.f);
	IO.AddMousePosEvent(TileClick.x, TileClick.y); IO.AddMouseButtonEvent(0, true);
	DrawEditor(); IO.AddMouseButtonEvent(0, false); DrawEditor();
	Check(TileEditor.IsSelected(*Map, 0, 0) && !TileEditor.IsSelected(*Map, 0, 2), "fixed-layout bottom-right click selects world (0,0)");
	FMapTileRenderer Overlay;
	Overlay.Init();
	D3D11_TEXTURE2D_DESC TargetDesc{};
	TargetDesc.Width = TargetDesc.Height = 256; TargetDesc.ArraySize = TargetDesc.MipLevels = 1;
	TargetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; TargetDesc.SampleDesc.Count = 1;
	TargetDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
	auto Target = RenderCommand::CreateTexture2D(TargetDesc);
	D3D11_TEXTURE2D_DESC DepthDesc = TargetDesc;
	DepthDesc.Format = DXGI_FORMAT_D32_FLOAT; DepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	auto Depth = RenderCommand::CreateTexture2D(DepthDesc);
	ID3D11RenderTargetView* RTV = Target->GetRTV();
	auto* Context = Device.GetContext();
	Context->OMSetRenderTargets(1, &RTV, Depth->GetDSV());
	D3D11_VIEWPORT Viewport{0,0,256,256,0,1}; Context->RSSetViewports(1,&Viewport);
	const float Black[4] = {0,0,0,1};
	Context->ClearRenderTargetView(RTV,Black); Context->ClearDepthStencilView(Depth->GetDSV(),D3D11_CLEAR_DEPTH,1,0);
	const FMatrix VP(.4f,0,0,0, 0,.5f,0,0, 0,0,-.01f,0, .2f,0,.5f,1);
	Overlay.OnRender(*World,VP,TileEditor);
	D3D11_TEXTURE2D_DESC ReadDesc = TargetDesc;
	ReadDesc.BindFlags = 0; ReadDesc.Usage = D3D11_USAGE_STAGING; ReadDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> Readback;
	Check(SUCCEEDED(Device.GetDevice()->CreateTexture2D(&ReadDesc,nullptr,Readback.GetAddressOf())), "create overlay readback");
	Context->CopyResource(Readback.Get(),Target->GetRawPtr());
	D3D11_MAPPED_SUBRESOURCE Pixels{};
	Check(SUCCEEDED(Context->Map(Readback.Get(),0,D3D11_MAP_READ,0,&Pixels)), "read back rendered overlay");
	auto TilePixel = [&](int Column, int Row)
	{
		FVector Center; Map->TryTileToWorldCenter(Column,Row,Center);
		const FVector NDC = VP.TransformPosition(Center);
		const int X = static_cast<int>((NDC.X*.5f+.5f)*256), Y = static_cast<int>((.5f-NDC.Y*.5f)*256);
		return static_cast<const uint8*>(Pixels.pData) + Y*Pixels.RowPitch + X*4;
	};
	const auto* Selected = TilePixel(0,0); const auto* Floor = TilePixel(1,0); const auto* Unused = TilePixel(3,2);
	Check(Selected[0] > 120 && Selected[1] > 100 && Selected[2] < 40, "ImGui selection renders yellow in matching world tile");
	Check(Floor[1] > Floor[0] && Unused[2] > Unused[0], "terrain colors rendered");
	Check(TilePixel(1,0)[1] < TilePixel(2,0)[1], "checker parity rendered independently of mesh UV");
	Check(static_cast<const uint8*>(Pixels.pData)[0] == 0, "overlay is finite");
	Context->Unmap(Readback.Get(),0);
	TileEditor.bShowOverlay = false; Context->ClearRenderTargetView(RTV,Black); Overlay.OnRender(*World,VP,TileEditor);
	Context->CopyResource(Readback.Get(),Target->GetRawPtr()); Context->Map(Readback.Get(),0,D3D11_MAP_READ,0,&Pixels);
	Check(TilePixel(0,0)[0] == 0, "map overlay toggle disables rendering"); Context->Unmap(Readback.Get(),0);
	TileEditor.bShowOverlay = true; World->SetWorldType(EWorldType::PIE); Overlay.OnRender(*World,VP,TileEditor);
	Context->CopyResource(Readback.Get(),Target->GetRawPtr()); Context->Map(Readback.Get(),0,D3D11_MAP_READ,0,&Pixels);
	Check(TilePixel(0,0)[0] == 0, "no overlay in PIE/game world"); Context->Unmap(Readback.Get(),0);
	World->SetWorldType(EWorldType::Editor);
	// Use the production EDITOR camera matrix. A custom +X-right projection cannot
	// expose the mirrored-view defect reported in the real viewport.
	FEditorViewportClient CameraView;
	CameraView.SetAspectRatio(1.f);
	const FQuat ReportedRotation = FQuat(-.270307f,.279916f,.639903f,.66265f).Normalized();
	auto SetCamera = [&](const FRotator& Rotation)
	{
		CameraView.SetViewRotation(Rotation);
		const FQuat Q = Rotation.Quaternion();
		FVector Center; Map->TryTileToWorldCenter(1,1,Center);
		CameraView.SetViewLocation(Center - Q.GetForwardVector()*15.f);
	};
	auto ClickDisplayTile = [&](int32 DisplayColumn, int32 DisplayRow)
	{
		DrawEditor(); DrawEditor();
		ImVec2 Click(-1,-1);
		for (ImGuiWindow* Window : ImGui::GetCurrentContext()->Windows)
			if (std::strstr(Window->Name,"Tile Array"))
				Click = ImVec2(Window->DC.CursorStartPos.x+36.f+DisplayColumn*22.f+11.f,Window->DC.CursorStartPos.y+DisplayRow*22.f+11.f);
		IO.AddMousePosEvent(Click.x,Click.y); IO.AddMouseButtonEvent(0,true);
		DrawEditor(); IO.AddMouseButtonEvent(0,false); DrawEditor();
	};
	auto ProjectTile = [&](int32 Column, int32 Row)
	{
		FVector Center; Map->TryTileToWorldCenter(Column,Row,Center);
		const FVector4 Clip = FVector4(Center,1.f)*CameraView.GetViewProjectionMatrix();
		return FVector2(Clip.X/Clip.W,Clip.Y/Clip.W);
	};
	auto CheckCameraHighlight = [&](int32 Column, int32 Row)
	{
		Context->ClearRenderTargetView(RTV,Black); Context->ClearDepthStencilView(Depth->GetDSV(),D3D11_CLEAR_DEPTH,1,0);
		Overlay.OnRender(*World,CameraView.GetViewProjectionMatrix(),TileEditor);
		Context->CopyResource(Readback.Get(),Target->GetRawPtr());
		Context->Map(Readback.Get(),0,D3D11_MAP_READ,0,&Pixels);
		const FVector2 NDC = ProjectTile(Column,Row);
		const int X = static_cast<int>((NDC.X*.5f+.5f)*256), Y = static_cast<int>((.5f-NDC.Y*.5f)*256);
		if (X >= 0 && X < 256 && Y >= 0 && Y < 256)
		{
			const auto* Pixel = static_cast<const uint8*>(Pixels.pData)+Y*Pixels.RowPitch+X*4;
			Check(Pixel[0]>120 && Pixel[1]>100 && Pixel[2]<40,"fixed-layout ImGui selection highlights the same logical tile in production editor projection");
		}
		else Check(false,"selected highlight is in view");
		Context->Unmap(Readback.Get(),0);
	};
	SetCamera(ReportedRotation.ToFRotator());
	Check(ProjectTile(3,0).X < ProjectTile(2,0).X,"reported camera projects increasing columns toward screen left");
	ClickDisplayTile(0,2);
	Check(TileEditor.IsSelected(*Map,3,0) && !TileEditor.IsSelected(*Map,0,0),"fixed leftmost ImGui tile uses the high column and agrees with the reported viewport");
	CheckCameraHighlight(3,0);
	SetCamera(FRotator(45,0,0));
	Check(TileEditor.GetDisplayColumns(*Map)==4 && TileEditor.GetDisplayRows(*Map)==3,"quarter-turn camera does not swap display columns and rows");
	ClickDisplayTile(2,1);
	Check(TileEditor.IsSelected(*Map,1,1),"same fixed cell mapping applies after rotating the camera");
	CheckCameraHighlight(1,1);
	const bool SelectedBeforeViews = TileEditor.IsSelected(*Map,1,1);
	for (float MapYaw : {0.f,37.f,90.f,180.f})
		for (const FRotator& CameraRotation : {ReportedRotation.ToFRotator(),FRotator(45,0,0),FRotator(45,90,0),FRotator(45,180,0),
			FRotator(45,270,0),FRotator(90,0,0),FRotator(-45,90,0),FRotator(0,90,0),FRotator(45,90,90)})
				{
					Map->GetRootComponent()->SetRelativeRotation(FRotator(0,MapYaw,0));
					SetCamera(CameraRotation);
					const int Width = TileEditor.GetDisplayColumns(*Map), Height = TileEditor.GetDisplayRows(*Map);
					Check(Width==4 && Height==3,"camera/map rotations do not change rectangular display dimensions");
					std::vector<bool> Seen(Map->GetColumns()*Map->GetRows(),false);
					for (int DR = 0; DR < Height; ++DR)
						for (int DC = 0; DC < Width; ++DC)
						{
							int32 TC=-1, TR=-1;
							const bool Valid = TileEditor.TryDisplayToTile(*Map,DC,DR,TC,TR) && Map->IsValidTile(TC,TR);
							Check(Valid && TC==3-DC && TR==2-DR,"display cell keeps exactly the same logical tile for every camera/map rotation");
							if (!Valid) continue;
							const int Index = TR*Map->GetColumns()+TC;
							Check(!Seen[Index],"fixed display has no duplicate tiles"); Seen[Index]=true;
						}
				}
	Check(SelectedBeforeViews && TileEditor.IsSelected(*Map,1,1) && HasTerrain(*Map,3,2,EMapTileTerrain::Unused),"camera/map rotations preserve logical selection and terrain");
	Map->GetRootComponent()->SetTransform(FTransform::Identity);
	int32 NewC=-7,NewR=-8;
	Check(!TileEditor.TryDisplayToTile(*Map,-1,0,NewC,NewR) && NewC==-7 && NewR==-8,"invalid display slot does not modify outputs");
	Map->FillTiles(EMapTileTerrain::Unused);
	DrawEditor(true);
	Check(Map->GetColumns() == 56 && Map->GetRows() == 48 && HasTerrain(*Map,2,24,EMapTileTerrain::Wall) && HasTerrain(*Map,28,24,EMapTileTerrain::Floor), "ImGui reset button restores initial grid");
	Check(!TileEditor.IsSelected(*Map,0,0), "ImGui reset clears tile selection");
	ImGui::DestroyContext();
	Map->ConfigureGrid(4,3,.75f,FVector(-2,-1,.3f));
	Map->FillTiles(EMapTileTerrain::Floor);
	Map->SetTile(0,0,EMapTileTerrain::Wall); Map->SetTile(3,2,EMapTileTerrain::Unused);
	Map->GetRootComponent()->SetTransform(FTransform(FRotator(0,37,0), FVector(3,-8,2), FVector(2,2,2)));
	TArray<uint8> Saved;
	Check(FJsonArchive::SaveWorldToBytes(World, Saved), "save using actual scene serializer");
	Check(FJsonArchive::SaveWorld(World, "Build/MapActorTests/roundtrip.scene"), "save scene to disk");
	UWorld* Loaded = UWorld::CreateWorld(EWorldType::Editor, false);
	Check(FJsonArchive::LoadWorld(Loaded, "Build/MapActorTests/roundtrip.scene"), "load actual scene file");
	AMapActor* Copy = FindMap(Loaded);
	Check(Copy && Copy->GetColumns() == 4 && Copy->GetRows() == 3 && Copy->GetTileSize() == .75f && Copy->GetGridOrigin().X == -2 && HasTerrain(*Copy,3,2,EMapTileTerrain::Unused), "scene preserves config and terrain");
	Check(Copy && Copy->GetStaticMeshComponent()->GetStaticMesh() == Mesh && !Copy->GetGridTransformWarning(), "scene preserves mesh and transform");
	if (Copy)
		for (int Row = 0; Row < 3; ++Row)
			for (int Column = 0; Column < 4; ++Column)
			{
				FVector A, B;
				Map->TryTileToWorldCenter(Column,Row,A); Copy->TryTileToWorldCenter(Column,Row,B);
				Check((A-B).Size() < .0001f, "saved root transform and full origin preserved");
			}
	Check(Copy && Copy->ResetGridToInitial() && Copy->GetColumns() == 56 && HasTerrain(*Copy,2,24,EMapTileTerrain::Wall) &&
		Map->GetColumns() == 4 && HasTerrain(*Map,3,2,EMapTileTerrain::Unused), "reset after scene load restores initial terrain independently");
	UWorld* PIE = UWorld::GetDuplicatedWorldForPIE(World);
	AMapActor* PlayMap = FindMap(PIE);
	Check(PlayMap && PlayMap != Map && PlayMap->GetWorld() == PIE && PlayMap->GetColumns() == 4 && HasTerrain(*PlayMap,3,2,EMapTileTerrain::Unused), "actual PIE world duplication preserves terrain");
	Check(PlayMap && PlayMap->GetStaticMeshComponent() != Map->GetStaticMeshComponent() && PlayMap->GetStaticMeshComponent()->GetStaticMesh() == Mesh && !PlayMap->GetGridTransformWarning(), "PIE component independent, asset shared");
	if (PlayMap) { PlayMap->FillTiles(EMapTileTerrain::Wall); PlayMap->ConfigureGrid(5,4,1.f,Origin); }
	Check(Map->GetColumns() == 4 && HasTerrain(*Map,3,2,EMapTileTerrain::Unused), "PIE writes and resize do not affect editor");
	Check(PlayMap && PlayMap->ResetGridToInitial() && PlayMap->GetColumns() == 56 && HasTerrain(*PlayMap,2,24,EMapTileTerrain::Wall) &&
		Map->GetColumns() == 4 && HasTerrain(*Map,3,2,EMapTileTerrain::Unused), "PIE reset does not change editor terrain");
	// Corrupt scene payloads are rejected by MapActor before its own array allocation.
	const json Original = json::parse(Saved.begin(), Saved.end());
	json Legacy = Original;
	std::function<bool(json&)> SetLegacyGrid = [&](json& Node)
	{
		if (Node.is_object() && Node.contains("MapGrid"))
		{
			auto& Grid = Node["MapGrid"];
			Grid["Columns"] = 28; Grid["Rows"] = 24; Grid["TileSize"] = 1.f;
			Grid["Terrain"] = std::vector<int32>(28 * 24, 0);
			Grid["Terrain"][2 * 28 + 3] = static_cast<int32>(EMapTileTerrain::Unused);
			return true;
		}
		if (Node.is_structured()) for (auto& Child : Node) if (SetLegacyGrid(Child)) return true;
		return false;
	};
	Check(SetLegacyGrid(Legacy) && FJsonArchive::LoadWorldFromBytes(Loaded,JsonBytes(Legacy)), "load earlier 28x24 size-1 scene format");
	AMapActor* LegacyMap = FindMap(Loaded);
	Check(LegacyMap && LegacyMap->GetColumns() == 28 && LegacyMap->GetRows() == 24 && LegacyMap->GetTileSize() == 1.f &&
		HasTerrain(*LegacyMap,3,2,EMapTileTerrain::Unused) && HasTerrain(*LegacyMap,3,1,EMapTileTerrain::Floor), "saved grid replaces new constructor seed without regeneration");
	for (int Kind = 0; Kind < 5; ++Kind)
	{
		json Bad = Original;
		std::function<bool(json&)> Corrupt = [&](json& Node)
		{
			if (Node.is_object() && Node.contains("MapGrid"))
			{
				auto& Grid = Node["MapGrid"];
				if (Kind == 0) Grid["Columns"] = INT32_MAX;
				if (Kind == 1) Grid["Terrain"].erase(Grid["Terrain"].begin());
				if (Kind == 2) Grid["Terrain"][0] = 99;
				if (Kind == 3) Grid["Terrain"][0] = 256;
				if (Kind == 4) Grid["Terrain"][0] = -256;
				return true;
			}
			if (Node.is_structured()) for (auto& Child : Node) if (Corrupt(Child)) return true;
			return false;
		};
		Check(Corrupt(Bad) && !FJsonArchive::LoadWorldFromBytes(Loaded, JsonBytes(Bad)), "malformed grid rejected");
	}
	PIE->ClearWorld(); Loaded->ClearWorld(); World->ClearWorld();
	std::cout << "MapActor integration checks: " << Checks - Failures << '/' << Checks << " passed\n";
	return Failures ? 1 : 0;
}
