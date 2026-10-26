#include "EnginePCH.h"
#include "MapActor.h"
#include "Asset/AssetManager.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Serialization/StructuredArchiveSlots.h"
#include <cmath>
#include <string>
#include <unordered_set>
#include <climits>
#include <cctype>

namespace
{
	constexpr uintmax_t MaxColliderJsonBytes = 64 * 1024 * 1024;

	bool ValidateBoxData(const TArray<FMapBoxColliderData>& Boxes, FString& Error)
	{
		if (Boxes.Num() > AMapActor::MaxBoxColliderCount) { Error = "Too many BoxColliders (maximum 65536)."; return false; }
		std::unordered_set<FName, FNameHash> Names;
		for (int32 Index = 0; Index < Boxes.Num(); ++Index)
		{
			const auto& Box = Boxes[Index];
			const FString Prefix = "Box[" + std::to_string(Index) + "]: ";
			if (Box.Name.empty() || Box.Name.size() > 255 ||
				std::any_of(Box.Name.begin(), Box.Name.end(), [](unsigned char C) { return C < 32 || C == 127; }))
			{ Error = Prefix + "Name must contain 1..255 bytes and no control characters."; return false; }
			// FName parses a numeric suffix into a signed int plus one. Reject overflow first.
			const auto Suffix = Box.Name.find_last_of('_');
			if (Suffix != FString::npos)
			{
				const auto Number = Box.Name.substr(Suffix + 1);
				if (!Number.empty() && Number.find_first_not_of("0123456789") == FString::npos &&
					(Number.size() > 10 || std::stoull(Number) >= INT_MAX))
				{ Error = Prefix + "Name numeric suffix is out of range."; return false; }
			}
			const FName Name(Box.Name);
			if (Name.IsNone() || !Names.insert(Name).second)
			{ Error = Prefix + "Name is None or duplicates another Box name (case insensitive)."; return false; }
			for (int Axis = 0; Axis < 3; ++Axis)
			{
				if (!std::isfinite(Box.Location[Axis]) || std::abs(Box.Location[Axis]) > 1.e6f ||
					!std::isfinite(Box.Rotation[Axis]) || std::abs(Box.Rotation[Axis]) > 1.e6f ||
					!std::isfinite(Box.Scale[Axis]) || Box.Scale[Axis] < 1.e-4f || Box.Scale[Axis] > 1.e4f ||
					!std::isfinite(Box.Extent[Axis]) || Box.Extent[Axis] < 0.f || Box.Extent[Axis] > 1.e6f)
				{ Error = Prefix + "Invalid transform/extent: finite location/rotation +/-1e6, scale 1e-4..1e4, half extent 0..1e6 required."; return false; }
			}
			if (!IsCollisionChannel(Box.Collision.ObjectChannel))
			{ Error = Prefix + "Invalid ObjectChannel."; return false; }
			for (auto Response : Box.Collision.Responses)
				if (Response < ECollisionResponse::Ignore || Response > ECollisionResponse::Block)
				{ Error = Prefix + "Invalid collision response."; return false; }
		}
		return true;
	}

	std::filesystem::path ColliderJsonPath(const FString& Path)
	{
		if (Path.empty()) throw std::runtime_error("Select a JSON file path first.");
		auto FullPath = std::filesystem::absolute(std::filesystem::path(std::u8string(Path.begin(), Path.end()))).lexically_normal();
		auto Extension = FullPath.extension().string();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
		if (Extension != ".json") throw std::runtime_error("BoxCollider files must use the .json extension.");
		return FullPath;
	}

	// CREATE_NEW prevents sharing a temporary file; failed writes never truncate the destination.
	bool WriteColliderJson(const std::filesystem::path& Path, const std::string& Text, FString& Error)
	{
		std::filesystem::path Temporary;
		HANDLE File = INVALID_HANDLE_VALUE;
		for (int Attempt = 0; Attempt < 100 && File == INVALID_HANDLE_VALUE; ++Attempt)
		{
			Temporary = Path;
			Temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(Attempt);
			File = CreateFileW(Temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (File == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) break;
		}
		if (File == INVALID_HANDLE_VALUE) { Error = "Cannot create temporary JSON file. Windows error " + std::to_string(GetLastError()); return false; }
		DWORD Written = 0;
		bool Success = WriteFile(File, Text.data(), static_cast<DWORD>(Text.size()), &Written, nullptr) && Written == Text.size();
		DWORD Failure = Success ? ERROR_SUCCESS : GetLastError();
		if (Success && !FlushFileBuffers(File)) { Success = false; Failure = GetLastError(); }
		if (!CloseHandle(File) && Success) { Success = false; Failure = GetLastError(); }
		if (Success && !MoveFileExW(Temporary.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{ Success = false; Failure = GetLastError(); }
		if (!Success)
		{
			Error = "Cannot commit JSON file; existing destination preserved. Windows error " + std::to_string(Failure);
			DeleteFileW(Temporary.c_str());
		}
		return Success;
	}

	bool IsFinite(const FVector& V)
	{
		return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
	}

	const UStaticMeshComponent* FindMapMeshComponent(const AMapActor& Map)
	{
		const UStaticMeshComponent* Mesh = nullptr;
		for (UActorComponent* Component : Map.GetComponents())
			if (const auto* Candidate = Cast<UStaticMeshComponent>(Component))
			{
				if (Mesh) return nullptr;
				Mesh = Candidate;
			}
		return Mesh == Map.GetStaticMeshComponent() ? Mesh : nullptr;
	}

	bool HasValidSeedMeshData(const FStaticMeshData* Mesh)
	{
		if (!Mesh || Mesh->Indices.IsEmpty() || Mesh->Indices.Num() % 3 != 0 || Mesh->Vertices.IsEmpty() ||
			!std::isfinite(Mesh->AABB.Min.Z + 0.231775f)) return false;
		for (uint32 Index : Mesh->Indices)
			if (Index >= static_cast<uint32>(Mesh->Vertices.Num()) || !IsFinite(Mesh->Vertices[Index].Position)) return false;
		return true;
	}

	FMatrix InverseGridMatrix(const FMatrix& Matrix, const FVector& Scale)
	{
		// Condition the full affine transform before the engine inverse (which returns
		// identity for a small determinant). (S*R*T)^-1 = (R*T)^-1 * S^-1.
		FMatrix Rigid = Matrix;
		for (int32 Row = 0; Row < 3; ++Row)
			for (int32 Column = 0; Column < 3; ++Column) Rigid.M[Row][Column] /= Scale[Row];
		FMatrix Inverse = Rigid.Inverse();
		for (int32 Row = 0; Row < 4; ++Row)
			for (int32 Column = 0; Column < 3; ++Column) Inverse.M[Row][Column] /= Scale[Column];
		return Inverse;
	}

	struct FWallSectionSegment { FVector2 A, B; };

	bool SegmentEntersTile(const FWallSectionSegment& Segment, float MinX, float MinY, float Size)
	{
		// Clip the segment against an inset tile rectangle. Touching only an edge/corner
		// does not mark its neighbor, but a thin wall crossing a tile does.
		double Enter = 0.0, Exit = 1.0;
		const double Epsilon = Size * 1.e-5;
		const double Mins[] = { MinX + Epsilon, MinY + Epsilon };
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			const double Start = Segment.A[Axis], Delta = static_cast<double>(Segment.B[Axis]) - Start;
			const double Min = Mins[Axis], Max = Min + Size - 2.0 * Epsilon;
			if (std::abs(Delta) < 1.e-12)
			{
				if (Start < Min || Start > Max) return false;
				continue;
			}
			double Near = (Min - Start) / Delta, Far = (Max - Start) / Delta;
			if (Near > Far) std::swap(Near, Far);
			Enter = std::max(Enter, Near); Exit = std::min(Exit, Far);
			if (Enter >= Exit) return false;
		}
		return true;
	}

	// Approved target terrain: [Row][Column], Row 0 first, columns 0..55.
	// 0 = Floor, 1 = Wall. Only 1 may change mesh-derived Floor to Wall;
	// 0 preserves the mesh result, so newly detected mesh walls are never cleared.
	constexpr uint8 InitialTerrain[][AMapActor::InitialColumns] =
	{
		{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, // Row 0
		{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, // Row 1
		{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, // Row 2
		{ 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1 }, // Row 3
		{ 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1 }, // Row 4
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 5
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 6
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 7
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 8
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 9
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 10
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1 }, // Row 11
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 12
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 13
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 14
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 15
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 16
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 17
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 18
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 19
		{ 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 20
		{ 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1 }, // Row 21
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, // Row 22
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, // Row 23
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, // Row 24
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, // Row 25
		{ 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1 }, // Row 26
		{ 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 27
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 28
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 29
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 30
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 31
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 32
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 33
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 34
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 35
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1 }, // Row 36
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 37
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 38
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 39
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 40
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 41
		{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 }, // Row 42
		{ 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1 }, // Row 43
		{ 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1 }, // Row 44
		{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, // Row 45
		{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, // Row 46
		{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, // Row 47
	};
	static_assert(sizeof(InitialTerrain) / sizeof(InitialTerrain[0]) == AMapActor::InitialRows);
	static_assert([]
	{
		for (const auto& Row : InitialTerrain)
			for (uint8 Terrain : Row)
				if (Terrain > 1) return false;
		return true;
	}(), "Initial terrain supports 0 = Floor and 1 = Wall only.");
}

AMapActor::AMapActor()
{
	GetStaticMeshComponent()->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Models/Map/Map.obj"));
	// A mesh-wide AABB would block the entire floor. Future wall boxes own collision.
	GetStaticMeshComponent()->SetCollisionEnabled(false);
	// The wall overrides are available even before the mesh is loaded.
	if (!GetStaticMeshComponent()->GetMeshData() || !ResetGridToInitial()) InitializeDefaultTerrain();
}

void AMapActor::BeginPlay()
{
	Super::BeginPlay();
	if (GetWorld() && (GetWorld()->GetWorldType() == EWorldType::PIE || GetWorld()->GetWorldType() == EWorldType::Game))
	{
		if (!HasExistingWallCollision())
			RebuildWallCollision();
	}
}

bool AMapActor::HasExistingWallCollision() const
{
	for (UActorComponent* Component : GetComponents())
	{
		if (Component && Component->IsA(UBoxComponent::StaticClass()))
			return true;
	}
	return false;
}

TArray<UBoxComponent*> AMapActor::GetBoxCollisionComponents() const
{
	TArray<UBoxComponent*> Boxes;
	for (UActorComponent* Component : GetComponents())
		if (auto* Box = Cast<UBoxComponent>(Component)) Boxes.Add(Box);
	return Boxes;
}

void AMapActor::ClearWallCollisionComponents()
{
	TArray<UActorComponent*> ToRemove;
	for (UActorComponent* Component : GetComponents())
	{
		if (Component && Component->IsA(UBoxComponent::StaticClass()))
			ToRemove.Add(Component);
	}
	for (UActorComponent* Component : ToRemove)
		DestroyComponent(Component);
}

bool AMapActor::GenerateWallCollisionComponents(FString* OutError, const std::function<void()>& BeforeReplace)
{
	FString Error;
	if (const char* Warning = GetGridTransformWarning())
	{
		if (OutError) *OutError = Warning;
		return false;
	}
	TArray<FMapBoxColliderData> Boxes;
	for (const FBox& Bounds : GetWallCollisionLocalBoxes())
	{
		FMapBoxColliderData Box;
		Box.Name = "WallCollision_" + std::to_string(Boxes.Num());
		Box.Location = (Bounds.Min + Bounds.Max) * 0.5f;
		Box.Extent = (Bounds.Max - Bounds.Min) * 0.5f;
		Box.Collision.ObjectChannel = ECollisionChannel::WorldStatic;
		Box.Collision.Responses.fill(ECollisionResponse::Ignore);
		Box.Collision.Responses[static_cast<int32>(ECollisionChannel::Pawn)] = ECollisionResponse::Block;
		Box.Collision.bEnabled = true;
		Boxes.Add(Box);
	}
	const bool Success = ReplaceBoxColliders(Boxes, Error, BeforeReplace);
	if (OutError) *OutError = Error;
	return Success;
}

bool AMapActor::CaptureBoxColliders(TArray<FMapBoxColliderData>& OutBoxes, FString& Error) const
{
	Error.clear();
	TArray<FMapBoxColliderData> Boxes;
	for (auto* Box : GetBoxCollisionComponents())
	{
		if (Box == GetRootComponent() || Box->GetAttachParent() != GetRootComponent())
		{ Error = "Box '" + Box->GetName() + "' must attach directly to the MapActor root. Export did not change its attachment."; return false; }
		FMapBoxColliderData Data;
		Data.Name = Box->GetName();
		Data.Location = Box->GetRelativeLocation();
		Data.Rotation = Box->GetRelativeRotation();
		Data.Scale = Box->GetRelativeScale3D();
		Data.Extent = Box->GetUnscaledBoxExtent();
		Data.Collision = Box->GetCollisionSettings();
		Boxes.Add(Data);
	}
	if (!ValidateBoxData(Boxes, Error)) return false;
	OutBoxes = std::move(Boxes);
	return true;
}

bool AMapActor::SaveBoxCollidersToJson(const FString& Path, FString& Error, bool bAllowEmpty) const
{
	try
	{
		TArray<FMapBoxColliderData> Boxes;
		if (!CaptureBoxColliders(Boxes, Error)) return false;
		if (Boxes.IsEmpty() && !bAllowEmpty) { Error = "No BoxColliders. Confirm empty export first."; return false; }
		json Root = { {"Version", 1}, {"Boxes", json::array()} };
		for (const auto& Box : Boxes)
		{
			json Responses = json::array();
			for (auto Response : Box.Collision.Responses) Responses.push_back(static_cast<int32>(Response));
			Root["Boxes"].push_back({ {"Name", Box.Name},
				{"Location", {Box.Location.X, Box.Location.Y, Box.Location.Z}},
				{"Rotation", {Box.Rotation.Pitch, Box.Rotation.Yaw, Box.Rotation.Roll}},
				{"Scale", {Box.Scale.X, Box.Scale.Y, Box.Scale.Z}},
				{"BoxExtent", {Box.Extent.X, Box.Extent.Y, Box.Extent.Z}},
				{"CollisionEnabled", Box.Collision.bEnabled},
				{"ObjectChannel", static_cast<int32>(Box.Collision.ObjectChannel)},
				{"Responses", Responses}, {"GenerateOverlapEvents", Box.Collision.bGenerateOverlapEvents} });
		}
		const std::string Text = Root.dump(2) + "\n";
		if (Text.size() > MaxColliderJsonBytes) { Error = "JSON exceeds 64 MiB."; return false; }
		return WriteColliderJson(ColliderJsonPath(Path), Text, Error);
	}
	catch (const std::exception& Exception) { Error = Exception.what(); return false; }
}

bool AMapActor::ReadBoxCollidersFromJson(const FString& Path, TArray<FMapBoxColliderData>& OutBoxes, FString& Error)
{
	Error.clear();
	try
	{
		const auto FullPath = ColliderJsonPath(Path);
		const auto Size = std::filesystem::file_size(FullPath);
		if (Size > MaxColliderJsonBytes) throw std::runtime_error("JSON exceeds 64 MiB.");
		std::ifstream File(FullPath, std::ios::binary);
		if (!File) throw std::runtime_error("Cannot open JSON file.");
		std::string Text(static_cast<size_t>(Size), '\0');
		File.read(Text.data(), static_cast<std::streamsize>(Size));
		if (!File || File.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Cannot read JSON file or file changed while reading.");
		// Bound nesting before parsing so adversarial JSON cannot exhaust the parser stack.
		const json Root = json::parse(Text, [](int Depth, json::parse_event_t, json&) {
			if (Depth > 16) throw std::runtime_error("JSON nesting is too deep.");
			return true;
		});
		if (!Root.is_object() || !Root.at("Version").is_number_integer() || Root.at("Version") != 1)
			throw std::runtime_error("Unsupported BoxCollider JSON version (expected 1).");
		const auto& Array = Root.at("Boxes");
		if (!Array.is_array() || Array.empty() || Array.size() > MaxBoxColliderCount)
			throw std::runtime_error("Boxes must be a nonempty array of at most 65536 entries. Use Clear to remove all boxes.");
		TArray<FMapBoxColliderData> Boxes;
		for (const auto& Entry : Array)
		{
			if (!Entry.is_object()) throw std::runtime_error("Each Box must be an object.");
			FMapBoxColliderData Box;
			Box.Name = Entry.at("Name").get<FString>();
			auto ReadVector = [&](const char* Key, auto& Value)
			{
				const auto& Vector = Entry.at(Key);
				if (!Vector.is_array() || Vector.size() != 3) throw std::runtime_error(FString(Key) + " must be an array of 3 numbers.");
				for (int Axis = 0; Axis < 3; ++Axis)
				{
					if (!Vector[Axis].is_number()) throw std::runtime_error(FString(Key) + " must contain numbers.");
					const double Number = Vector[Axis].get<double>();
					if (!std::isfinite(Number) || std::abs(Number) > 1.e6) throw std::runtime_error(FString(Key) + " is out of range.");
					Value[Axis] = static_cast<float>(Number);
				}
			};
			ReadVector("Location", Box.Location); ReadVector("Rotation", Box.Rotation);
			ReadVector("Scale", Box.Scale); ReadVector("BoxExtent", Box.Extent);
			Box.Collision.bEnabled = Entry.at("CollisionEnabled").get<bool>();
			Box.Collision.bGenerateOverlapEvents = Entry.at("GenerateOverlapEvents").get<bool>();
			auto ReadEnum = [](const json& Value, int Maximum)
			{
				if (!Value.is_number_integer() || Value < 0 || Value >= Maximum) throw std::runtime_error("Collision channel/response is out of range.");
				return Value.get<int32>();
			};
			Box.Collision.ObjectChannel = static_cast<ECollisionChannel>(ReadEnum(Entry.at("ObjectChannel"), CollisionChannelCount));
			const auto& Responses = Entry.at("Responses");
			if (!Responses.is_array() || Responses.size() != CollisionChannelCount) throw std::runtime_error("Responses must contain all 6 channels.");
			for (int Index = 0; Index < CollisionChannelCount; ++Index)
				Box.Collision.Responses[Index] = static_cast<ECollisionResponse>(ReadEnum(Responses[Index], 3));
			Boxes.Add(Box);
		}
		if (!ValidateBoxData(Boxes, Error)) return false;
		OutBoxes = std::move(Boxes);
		return true;
	}
	catch (const std::exception& Exception) { Error = Exception.what(); return false; }
}

bool AMapActor::ReplaceBoxColliders(const TArray<FMapBoxColliderData>& Boxes, FString& Error,
	const std::function<void()>& BeforeReplace)
{
	Error.clear();
	TArray<UBoxComponent*> Prepared;
	const auto Previous = GetBoxCollisionComponents();
	try
	{
		if (!GetRootComponent()) throw std::runtime_error("MapActor has no root.");
		if (HasBegunPlay() && !Previous.IsEmpty()) throw std::runtime_error("Stop Play before replacing existing BoxColliders.");
		if (!ValidateBoxData(Boxes, Error)) return false;
		for (const auto* Box : Previous)
			if (Box == GetRootComponent()) throw std::runtime_error("Cannot replace a Box used as the Actor root.");
		TArray<FName> Names;
		for (const auto& Data : Boxes)
		{
			const FName Name(Data.Name);
			// Never pass a colliding name to NewObject: this engine can overwrite existing objects.
			if (auto* Existing = FindComponentByName(Name); Existing && !Cast<UBoxComponent>(Existing))
				throw std::runtime_error("Box name conflicts with a non-Box component: " + Data.Name);
			Names.Add(Name);
		}
		Prepared.Reserve(Boxes.Num());
		for (const auto& Data : Boxes)
		{
			auto* Box = Cast<UBoxComponent>(AddComponent(UBoxComponent::StaticClass(), MakeUniqueObjectName(this, UBoxComponent::StaticClass())));
			if (!Box) throw std::runtime_error("Failed to create BoxCollider. Previous boxes preserved.");
			Prepared.Add(Box);
			Box->SetCollisionEnabled(false);
			Box->SetRelativeLocation(Data.Location);
			Box->SetRelativeRotation(Data.Rotation);
			Box->SetRelativeScale3D(Data.Scale);
			Box->SetBoxExtent(Data.Extent);
			Box->SetCollisionObjectType(Data.Collision.ObjectChannel);
			for (int Channel = 0; Channel < CollisionChannelCount; ++Channel)
				Box->SetCollisionResponseToChannel(static_cast<ECollisionChannel>(Channel), Data.Collision.Responses[Channel]);
			Box->SetGenerateOverlapEvents(Data.Collision.bGenerateOverlapEvents);
			Box->SetCollisionEnabled(Data.Collision.bEnabled);
			if (GetWorld() && !Box->IsRegistered()) throw std::runtime_error("BoxCollider registration failed. Previous boxes preserved.");
		}
		// No previous component or editor reference is touched until all new boxes exist.
		if (BeforeReplace) BeforeReplace();
		for (auto* Box : Previous) DestroyComponent(Box);
		for (int Index = 0; Index < Prepared.Num(); ++Index)
		{
			Prepared[Index]->SetName(Names[Index]);
		}
		return true;
	}
	catch (const std::exception& Exception)
	{
		// Also catches a component added to the owner if AddComponent throws during registration.
		for (auto* Box : GetBoxCollisionComponents())
			if (Previous.Find(Box) == INDEX_NONE) DestroyComponent(Box);
		Error = Exception.what();
		return false;
	}
}

TArray<FBox> AMapActor::GetWallCollisionLocalBoxes() const
{
	TArray<FBox> Boxes;
	if (GetGridTransformWarning()) return Boxes;
	// Keep touching wall rectangles joined; inset only their exposed faces.
	// Checkerboard wall corners need a tighter inset so the pawn cannot slip diagonally through.
	bool bDiagonalWallCorner = false;
	for (int32 Y = 0; Y + 1 < Rows && !bDiagonalWallCorner; ++Y)
		for (int32 X = 0; X + 1 < Columns; ++X)
		{
			const bool A = Tiles[Y * Columns + X] == EMapTileTerrain::Wall;
			const bool B = Tiles[Y * Columns + X + 1] == EMapTileTerrain::Wall;
			const bool C = Tiles[(Y + 1) * Columns + X] == EMapTileTerrain::Wall;
			const bool D = Tiles[(Y + 1) * Columns + X + 1] == EMapTileTerrain::Wall;
			if (A == D && B == C && A != B) { bDiagonalWallCorner = true; break; }
		}
	const float Inset = bDiagonalWallCorner ? 0.05f : 0.08f;
	TArray<uint8> Covered;
	Covered.Init(0, Columns * Rows);
	for (int32 Row = 0; Row < Rows; ++Row)
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			const int32 Index = Row * Columns + Column;
			if (Covered[Index] || Tiles[Index] != EMapTileTerrain::Wall) continue;
			int32 EndColumn = Column + 1;
			while (EndColumn < Columns && !Covered[Row * Columns + EndColumn] &&
				Tiles[Row * Columns + EndColumn] == EMapTileTerrain::Wall) ++EndColumn;
			int32 EndRow = Row + 1;
			while (EndRow < Rows)
			{
				if ((Column > 0 && Tiles[EndRow * Columns + Column - 1] == EMapTileTerrain::Wall) ||
					(EndColumn < Columns && Tiles[EndRow * Columns + EndColumn] == EMapTileTerrain::Wall)) break;
				bool bFullRow = true;
				for (int32 X = Column; X < EndColumn; ++X)
					if (Covered[EndRow * Columns + X] || Tiles[EndRow * Columns + X] != EMapTileTerrain::Wall)
						{ bFullRow = false; break; }
				if (!bFullRow) break;
				++EndRow;
			}
			for (int32 Y = Row; Y < EndRow; ++Y)
				for (int32 X = Column; X < EndColumn; ++X) Covered[Y * Columns + X] = 1;

			bool bWallLeft = false, bWallRight = false, bWallBelow = false, bWallAbove = false;
			for (int32 Y = Row; Y < EndRow; ++Y)
			{
				bWallLeft |= Column > 0 && Tiles[Y * Columns + Column - 1] == EMapTileTerrain::Wall;
				bWallRight |= EndColumn < Columns && Tiles[Y * Columns + EndColumn] == EMapTileTerrain::Wall;
			}
			for (int32 X = Column; X < EndColumn; ++X)
			{
				bWallBelow |= Row > 0 && Tiles[(Row - 1) * Columns + X] == EMapTileTerrain::Wall;
				bWallAbove |= EndRow < Rows && Tiles[EndRow * Columns + X] == EMapTileTerrain::Wall;
			}
			constexpr float WallHeight = 2.5f;
			Boxes.Add({ GridOrigin + FVector(Column * TileSize + (bWallLeft ? 0.0f : Inset), Row * TileSize + (bWallBelow ? 0.0f : Inset), 0.0f),
				GridOrigin + FVector(EndColumn * TileSize - (bWallRight ? 0.0f : Inset), EndRow * TileSize - (bWallAbove ? 0.0f : Inset), WallHeight) });
		}
	return Boxes;
}

void AMapActor::RebuildWallCollision()
{
	GenerateWallCollisionComponents();
}

bool AMapActor::ResetGridToInitial()
{
	const UStaticMeshComponent* MeshComponent = FindMapMeshComponent(*this);
	const FStaticMeshData* Mesh = MeshComponent ? MeshComponent->GetMeshData() : nullptr;
	if (!HasValidSeedMeshData(Mesh))
	{
		HTR_LOG(Warning, "MapActor: cannot reset initial grid without valid map mesh data");
		return false;
	}
	Columns = InitialColumns;
	Rows = InitialRows;
	TileSize = InitialTileSize;
	// Both Map.obj and the earlier grounded variant have the same 0.231775-thick floor slab.
	// Derive its top from the asset so an explicitly loaded older mesh stays aligned too.
	GridOrigin = FVector(-14.f, -12.f, Mesh->AABB.Min.Z + 0.231775f);
	InitializeDefaultTerrain();
	++GridRevision;
	if (HasBegunPlay() && !HasExistingWallCollision()) RebuildWallCollision();
	return true;
}

void AMapActor::InitializeDefaultTerrain()
{
	Tiles.Init(DefaultTerrain, InitialColumns * InitialRows);
	const UStaticMeshComponent* MeshComponent = FindMapMeshComponent(*this);
	if (MeshComponent && HasValidSeedMeshData(MeshComponent->GetMeshData())) InitializeDefaultTerrainFromMesh();
	ApplyInitialWallOverrides();
}

void AMapActor::InitializeDefaultTerrainFromMesh()
{
	const UStaticMeshComponent* MeshComponent = FindMapMeshComponent(*this);
	const FStaticMeshData* Mesh = MeshComponent ? MeshComponent->GetMeshData() : nullptr;
	if (!Mesh) return; // Asset unavailable during early class construction: retain editable Floor defaults.
	// Slice the closed map geometry just above the raised floor base. Projecting all
	// geometry from above would mistake overhead surfaces for walls across rooms.
	// The slice is an authored seed, not an agent height or a clearance expansion.
	const float SectionZ = GridOrigin.Z + 0.1f;
	const float EdgeEpsilon = TileSize * 1.e-5f;
	TArray<FWallSectionSegment> Segments;
	for (int32 Index = 0; Index + 2 < Mesh->Indices.Num(); Index += 3)
	{
		const FVector Triangle[] = {
			Mesh->Vertices[Mesh->Indices[Index]].Position,
			Mesh->Vertices[Mesh->Indices[Index + 1]].Position,
			Mesh->Vertices[Mesh->Indices[Index + 2]].Position
		};
		FVector2 Points[2];
		int32 Count = 0;
		for (int32 I = 0; I < 3; ++I)
		{
			const FVector& A = Triangle[I];
			const FVector& B = Triangle[(I + 1) % 3];
			if ((A.Z > SectionZ) != (B.Z > SectionZ))
			{
				const FVector Hit = A + (B - A) * ((SectionZ - A.Z) / (B.Z - A.Z));
				Points[Count++] = FVector2(Hit.X, Hit.Y);
			}
		}
		if (Count == 2 && (std::abs(Points[0].X - Points[1].X) > EdgeEpsilon || std::abs(Points[0].Y - Points[1].Y) > EdgeEpsilon))
			Segments.Add({ Points[0], Points[1] });
	}
	// Even/odd scanline fill marks the interiors of solid walls, including walls wider
	// than one cell. Half-open segment endpoints avoid counting triangle joins twice.
	TArray<float> Intersections;
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		Intersections.Reset();
		const float Y = GridOrigin.Y + (Row + .5f) * TileSize;
		for (const auto& Segment : Segments)
			if ((Segment.A.Y > Y) != (Segment.B.Y > Y))
				Intersections.Add(Segment.A.X + (Y - Segment.A.Y) * (Segment.B.X - Segment.A.X) / (Segment.B.Y - Segment.A.Y));
		std::sort(Intersections.begin(), Intersections.end());
		int32 UniqueCount = 0;
		for (float X : Intersections)
			if (!UniqueCount || std::abs(X - Intersections[UniqueCount - 1]) > EdgeEpsilon)
				Intersections[UniqueCount++] = X;
		Intersections.SetNum(UniqueCount, false);
		int32 Crossings = 0;
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			const float X = GridOrigin.X + (Column + .5f) * TileSize;
			while (Crossings < UniqueCount && Intersections[Crossings] < X) ++Crossings;
			if (Crossings % 2) Tiles[Row * Columns + Column] = EMapTileTerrain::Wall;
		}
	}
	// Also mark boundary cells so a wall thinner than 0.5 cannot disappear between centers.
	for (const auto& Segment : Segments)
	{
		const float MinX = std::min(Segment.A.X, Segment.B.X), MaxX = std::max(Segment.A.X, Segment.B.X);
		const float MinY = std::min(Segment.A.Y, Segment.B.Y), MaxY = std::max(Segment.A.Y, Segment.B.Y);
		if (MaxX <= GridOrigin.X || MaxY <= GridOrigin.Y || MinX >= GridOrigin.X + Columns * TileSize || MinY >= GridOrigin.Y + Rows * TileSize) continue;
		auto CellIndex = [&](float Position, float Origin, int32 Count)
		{
			return static_cast<int32>(std::floor(std::clamp((Position - Origin) / TileSize, 0.f, static_cast<float>(Count - 1))));
		};
		const int32 FirstColumn = CellIndex(MinX, GridOrigin.X, Columns), LastColumn = CellIndex(MaxX, GridOrigin.X, Columns);
		const int32 FirstRow = CellIndex(MinY, GridOrigin.Y, Rows), LastRow = CellIndex(MaxY, GridOrigin.Y, Rows);
		for (int32 Row = FirstRow; Row <= LastRow; ++Row)
			for (int32 Column = FirstColumn; Column <= LastColumn; ++Column)
			{
				if (Tiles[Row * Columns + Column] == EMapTileTerrain::Wall) continue;
				const float X = GridOrigin.X + Column * TileSize, Y = GridOrigin.Y + Row * TileSize;
				if (SegmentEntersTile(Segment, X, Y, TileSize)) Tiles[Row * Columns + Column] = EMapTileTerrain::Wall;
			}
	}
}

void AMapActor::ApplyInitialWallOverrides()
{
	for (int32 Row = 0; Row < InitialRows; ++Row)
		for (int32 Column = 0; Column < InitialColumns; ++Column)
		{
			EMapTileTerrain& Terrain = Tiles[Row * InitialColumns + Column];
			if (InitialTerrain[Row][Column] == 1 && Terrain == EMapTileTerrain::Floor)
				Terrain = EMapTileTerrain::Wall;
		}
}

bool AMapActor::IsValidTerrain(EMapTileTerrain Terrain)
{
	return Terrain == EMapTileTerrain::Floor || Terrain == EMapTileTerrain::Wall || Terrain == EMapTileTerrain::Unused;
}

bool AMapActor::IsValidConfiguration(int32 C, int32 R, float Size, const FVector& Origin)
{
	return C > 0 && R > 0 && C <= MaxGridDimension && R <= MaxGridDimension &&
		static_cast<int64>(C) * R <= MaxTileCount && std::isfinite(Size) && Size > 0.0f &&
		IsFinite(Origin) && std::isfinite(Origin.X + C * Size) && std::isfinite(Origin.Y + R * Size);
}

bool AMapActor::ConfigureGrid(int32 C, int32 R, float Size, const FVector& Origin)
{
	if (!IsValidConfiguration(C, R, Size, Origin))
	{
		HTR_LOG(Warning, "MapActor: invalid grid configuration (max dimension {}, max tiles {})", MaxGridDimension, MaxTileCount);
		return false;
	}
	if (C != Columns || R != Rows)
	{
		TArray<EMapTileTerrain> NewTiles;
		NewTiles.Init(DefaultTerrain, C * R);
		for (int32 Row = 0; Row < std::min(R, Rows); ++Row)
			for (int32 Column = 0; Column < std::min(C, Columns); ++Column)
				NewTiles[Row * C + Column] = Tiles[Row * Columns + Column];
		Tiles = std::move(NewTiles);
	}
	Columns = C;
	Rows = R;
	TileSize = Size;
	GridOrigin = Origin;
	++GridRevision;
	if (HasBegunPlay() && !HasExistingWallCollision()) RebuildWallCollision();
	return true;
}

bool AMapActor::IsValidTile(int32 Column, int32 Row) const
{
	return Column >= 0 && Row >= 0 && Column < Columns && Row < Rows;
}

bool AMapActor::TryGetTile(int32 Column, int32 Row, EMapTileTerrain& OutTerrain) const
{
	if (!IsValidTile(Column, Row)) return false;
	OutTerrain = Tiles[Row * Columns + Column];
	return true;
}

TArray<EMapTileTerrain> AMapActor::GetTilesCopy() const
{
	return Tiles;
}

bool AMapActor::SetTile(int32 Column, int32 Row, EMapTileTerrain Terrain)
{
	if (!IsValidTile(Column, Row) || !IsValidTerrain(Terrain)) return false;
	Tiles[Row * Columns + Column] = Terrain;
	++GridRevision;
	if (HasBegunPlay() && !HasExistingWallCollision()) RebuildWallCollision();
	return true;
}

bool AMapActor::FillTiles(EMapTileTerrain Terrain)
{
	if (!IsValidTerrain(Terrain)) return false;
	for (auto& Tile : Tiles) Tile = Terrain;
	++GridRevision;
	if (HasBegunPlay() && !HasExistingWallCollision()) RebuildWallCollision();
	return true;
}

const char* AMapActor::GetGridTransformWarning() const
{
	const USceneComponent* Root = GetRootComponent();
	if (!Root) return "Map has no root component.";
	// Find by type in owned components: never dereference a deleted inherited mesh pointer.
	const UStaticMeshComponent* Mesh = nullptr;
	for (UActorComponent* Component : GetComponents())
		if (auto* Candidate = Cast<UStaticMeshComponent>(Component))
		{
			if (Mesh) return "Map requires exactly one StaticMeshComponent.";
			Mesh = Candidate;
		}
	if (!Mesh) return "Map mesh component is missing.";
	if (Mesh != GetStaticMeshComponent()) return "Map inherited mesh component is missing; restore the original component.";
	if (Mesh->GetAttachParent() != Root) return "Attach the map mesh directly to the Actor root.";
	const FMatrix RelativeMesh = Mesh->GetTransform().GetLocalMatrix();
	for (int32 I = 0; I < 4; ++I)
		for (int32 J = 0; J < 4; ++J)
			if (!std::isfinite(RelativeMesh.M[I][J]) || std::abs(RelativeMesh.M[I][J] - (I == J ? 1.0f : 0.0f)) > 1.e-5f)
				return "Map mesh relative transform must be identity. Move/rotate/scale the Actor root.";
	const FVector Scale = Root->GetWorldScale3D();
	if (!IsFinite(Scale) || Scale.X <= 0.f || Scale.Y <= 0.f || Scale.Z <= 0.f ||
		std::abs(Scale.X - Scale.Y) > Scale.X * 1.e-5f || std::abs(Scale.X - Scale.Z) > Scale.X * 1.e-5f)
		return "Map requires a finite, positive uniform scale; zero/negative/nonuniform scale is unsupported.";
	const FMatrix Matrix = Root->GetWorldMatrix();
	for (const auto& Row : Matrix.M)
		for (float V : Row)
			if (!std::isfinite(V)) return "Map transform must be finite.";
	// The world local-Z axis must point up. Test the actual matrix, including parent transforms.
	if (std::abs(Matrix.M[2][0]) > Scale.Z * 1.e-5f || std::abs(Matrix.M[2][1]) > Scale.Z * 1.e-5f || Matrix.M[2][2] <= 0.f)
		return "Map supports yaw (Z rotation) only; pitch/roll is unsupported.";
	const FMatrix Inverse = InverseGridMatrix(Matrix, Scale);
	for (const auto& Row : Inverse.M)
		for (float V : Row)
			if (!std::isfinite(V)) return "Map transform is not invertible at this scale.";
	return nullptr;
}

bool AMapActor::TryGetGridToWorld(FMatrix& OutMatrix) const
{
	if (GetGridTransformWarning()) return false;
	OutMatrix = GetRootComponent()->GetWorldMatrix();
	return true;
}

bool AMapActor::TryWorldToTileXYProjected(const FVector& World, int32& OutColumn, int32& OutRow) const
{
	FMatrix Matrix;
	if (!IsFinite(World) || !TryGetGridToWorld(Matrix)) return false;
	const FVector Local = InverseGridMatrix(Matrix, GetRootComponent()->GetWorldScale3D()).TransformPosition(World);
	const double X = (static_cast<double>(Local.X) - GridOrigin.X) / TileSize;
	const double Y = (static_cast<double>(Local.Y) - GridOrigin.Y) / TileSize;
	if (!std::isfinite(X) || !std::isfinite(Y) || X < 0.0 || Y < 0.0 || X >= Columns || Y >= Rows) return false;
	OutColumn = static_cast<int32>(std::floor(X));
	OutRow = static_cast<int32>(std::floor(Y));
	return true;
}

bool AMapActor::TryWorldToGridUVProjected(const FVector& World, FVector2& OutUV) const
{
	FMatrix Matrix;
	if (!IsFinite(World) || !TryGetGridToWorld(Matrix)) return false;
	const FVector Local = InverseGridMatrix(Matrix, GetRootComponent()->GetWorldScale3D()).TransformPosition(World);
	const double U = (static_cast<double>(Local.X) - GridOrigin.X) / (TileSize * Columns);
	const double V = (static_cast<double>(Local.Y) - GridOrigin.Y) / (TileSize * Rows);
	if (!std::isfinite(U) || !std::isfinite(V) || U < 0.0 || V < 0.0 || U >= 1.0 || V >= 1.0) return false;
	OutUV = FVector2(static_cast<float>(U), static_cast<float>(V));
	return true;
}

bool AMapActor::TryTileToWorldCenter(int32 Column, int32 Row, FVector& OutWorld) const
{
	FMatrix Matrix;
	if (!IsValidTile(Column, Row) || !TryGetGridToWorld(Matrix)) return false;
	const FVector World = Matrix.TransformPosition(GridOrigin + FVector((Column + 0.5f) * TileSize, (Row + 0.5f) * TileSize, 0.f));
	if (!IsFinite(World)) return false;
	OutWorld = World;
	return true;
}

void AMapActor::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record); // Components include the mesh asset and shared root transform.
	FArchive& Ar = Record.GetUnderlyingArchive();
	auto Data = Record.EnterRecord("MapGrid");
	int32 Version = 1, C = Columns, R = Rows;
	float Size = TileSize;
	FVector Origin = GridOrigin;
	Data << SA_VALUE("Version", Version) << SA_VALUE("Columns", C) << SA_VALUE("Rows", R)
		<< SA_VALUE("TileSize", Size) << SA_VALUE("OriginX", Origin.X)
		<< SA_VALUE("OriginY", Origin.Y) << SA_VALUE("OriginZ", Origin.Z);
	// Validate before allocating even when the archive is malformed.
	if (Version != 1 || !IsValidConfiguration(C, R, Size, Origin))
	{
		HTR_LOG(Error, "MapActor: invalid serialized grid configuration");
		Ar.SetError();
		return;
	}
	int32 Count = C * R;
	auto Array = Data.EnterArray("Terrain", Count);
	if (Count != C * R)
	{
		HTR_LOG(Error, "MapActor: serialized terrain count does not match dimensions");
		Ar.SetError();
		return;
	}
	TArray<EMapTileTerrain> Loaded;
	if (Ar.IsLoading()) Loaded.Init(DefaultTerrain, Count);
	for (int32 I = 0; I < Count; ++I)
	{
		int32 Value = Ar.IsSaving() ? static_cast<int32>(Tiles[I]) : 0;
		Array.EnterElement() << Value;
		if (Value < 0 || Value > static_cast<int32>(EMapTileTerrain::Unused)) Ar.SetError();
		if (Ar.IsLoading()) Loaded[I] = static_cast<EMapTileTerrain>(Value);
	}
	if (Ar.IsLoading() && !Ar.IsError())
	{
		Columns = C; Rows = R; TileSize = Size; GridOrigin = Origin;
		Tiles = std::move(Loaded); // Owning value array, independent in PIE.
		++GridRevision;
	}
}
