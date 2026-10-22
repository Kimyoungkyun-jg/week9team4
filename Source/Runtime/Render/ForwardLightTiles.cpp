#include "EnginePCH.h"
#include "ForwardLightTiles.h"
#include "Render/RenderCommand.h"
#include "Render/Renderer.h"
#include "Render/SceneView.h"
#include "Render/Texture2D.h"
#include <algorithm>
#include <cmath>
#include <cstring>

bool FForwardLightTileGrid::FTileBuffer::EnsureCapacity(uint32 Count, uint32 Stride)
{
	if (Buffer && Count <= Capacity) return true;
	if (!Count || Count > UINT32_MAX / Stride) return false;
	D3D11_BUFFER_DESC Desc{};
	Desc.ByteWidth = Count * Stride;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	Desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	Desc.StructureByteStride = Stride;
	ID3D11Device* Device = RenderCommand::GetDevice();
	ComPtr<ID3D11Buffer> NewBuffer;
	ComPtr<ID3D11ShaderResourceView> NewSRV;
	ComPtr<ID3D11UnorderedAccessView> NewUAV;
	if (FAILED(Device->CreateBuffer(&Desc, nullptr, NewBuffer.GetAddressOf()))) return false;
	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.NumElements = Count;
	if (FAILED(Device->CreateShaderResourceView(NewBuffer.Get(), &SRVDesc, NewSRV.GetAddressOf()))) return false;
	D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc{};
	UAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	UAVDesc.Buffer.NumElements = Count;
	if (FAILED(Device->CreateUnorderedAccessView(NewBuffer.Get(), &UAVDesc, NewUAV.GetAddressOf()))) return false;
	Buffer = std::move(NewBuffer);
	SRV = std::move(NewSRV);
	UAV = std::move(NewUAV);
	Capacity = Count;
	return true;
}

bool FForwardLightTileGrid::Init()
{
	CullingCB = RenderCommand::CreateConstantBuffer(sizeof(FGridConstants));
	LightingInfoCB = RenderCommand::CreateConstantBuffer(sizeof(FGridConstants));
	if (!CullingCB || !LightingInfoCB || !Allocator.EnsureCapacity(2, sizeof(uint32))) return false;
	const std::array<const char*, 2> EntryPoints = { "mainCS", "mainClusterCS" };
	const std::array<ComPtr<ID3D11ComputeShader>*, 2> Shaders = { &ComputeShader, &ClusterComputeShader };
	for (uint32 Index = 0; Index < EntryPoints.size(); ++Index)
	{
		ComPtr<ID3DBlob> Code, Errors;
		if (FAILED(D3DCompileFromFile(L"Resources/Shader/TileLightCullingCS.hlsli", nullptr,
			D3D_COMPILE_STANDARD_FILE_INCLUDE, EntryPoints[Index], "cs_5_0",
			D3DCOMPILE_ENABLE_STRICTNESS, 0, Code.GetAddressOf(), Errors.GetAddressOf())))
		{
			if (Errors) OutputDebugStringA(static_cast<const char*>(Errors->GetBufferPointer()));
			return false;
		}

		if (FAILED(RenderCommand::GetDevice()->CreateComputeShader(Code->GetBufferPointer(),
			Code->GetBufferSize(), nullptr, Shaders[Index]->GetAddressOf()))) return false;
	}
	return true;
}

bool FForwardLightTileGrid::PrepareBuild(const FSceneView& View, const FForwardLightingConstants& Lighting, 
	const FStructuredBuffer* SceneLights, const FTexture2D* Depth, ELightCullingMode InMode)
{
	bPixelTilesValid = false;
	Constants = {};
	Constants.GridView = View.ViewMatrix;
	Constants.GridSliceCount = 1;
	Constants.GridIsPerspective = View.bIsPerspective ? 1u : 0u;
	Constants.GridPointCount = Lighting.LightCounts[0];
	Constants.GridSpotCount = Lighting.LightCounts[1];
	std::memcpy(Constants.GridLightOffsets, Lighting.LightOffsets, sizeof(Constants.GridLightOffsets));

	if (!CullingCB || !LightingInfoCB || !Depth || !Depth->GetWidth() || !Depth->GetHeight()) return false;

	Constants.GridViewWidth = Depth->GetWidth();
	Constants.GridViewHeight = Depth->GetHeight();
	Constants.GridTileCountX = (Constants.GridViewWidth - 1) / LightTileSize + 1;
	Constants.GridTileCountY = (Constants.GridViewHeight - 1) / LightTileSize + 1;

	if (InMode == ELightCullingMode::Off || (!Constants.GridPointCount && !Constants.GridSpotCount))
		return true;

	const bool bClustered = InMode == ELightCullingMode::Clustered;
	ID3D11ComputeShader* Shader = bClustered ? ClusterComputeShader.Get() : ComputeShader.Get();
	if (!Shader || !SceneLights || !SceneLights->GetSRV() || (!bClustered && !Depth->GetSRV()))
		return false;


	const FMatrix& Projection = View.ProjectionMatrix;
	if (!std::isfinite(Projection.M[1][0]) || std::abs(Projection.M[1][0]) < 1.0e-8f ||
		!std::isfinite(Projection.M[2][1]) || std::abs(Projection.M[2][1]) < 1.0e-8f ||
		!std::isfinite(Projection.M[0][2]) || !std::isfinite(Projection.M[3][2]) ||
		(View.bIsPerspective ? std::abs(Projection.M[3][2]) : std::abs(Projection.M[0][2])) < 1.0e-8f ||
		!std::isfinite(View.NearClip) || !std::isfinite(View.FarClip) ||
		View.NearClip <= 0.0f || View.FarClip <= View.NearClip)
		return false;
	Constants.GridProjectionParams = FVector4(1.0f / Projection.M[1][0], 1.0f / Projection.M[2][1],
		Projection.M[0][2], Projection.M[3][2]);
	Constants.GridSliceCount = bClustered ? ClusterDepthSlices : 1;

	// 원근은 log2(depth), 직교는 선형 depth에 scale과 bias를 적용한다.
	const float SliceCount = static_cast<float>(Constants.GridSliceCount);
	float DepthScale, DepthBias;
	if (View.bIsPerspective)
	{
		const float NearLog = std::log2(View.NearClip);
		DepthScale = SliceCount / (std::log2(View.FarClip) - NearLog);
		DepthBias = -NearLog * DepthScale;
	}
	else
	{
		DepthScale = SliceCount / (View.FarClip - View.NearClip);
		DepthBias = -View.NearClip * DepthScale;
	}
	Constants.GridDepthParams = FVector4(View.NearClip, View.FarClip, DepthScale, DepthBias);

	// 셀마다 메타데이터 하나를 만들고, 인덱스 풀은 평균 예산으로 확보한다.
	const uint64 CellCount = static_cast<uint64>(Constants.GridTileCountX) *
		Constants.GridTileCountY * Constants.GridSliceCount;
	const uint32 AverageLightsPerCell = bClustered ? ClusterAverageLightsPerCell : TiledAverageLightsPerCell;
	if (CellCount > UINT32_MAX / (AverageLightsPerCell * sizeof(uint32))) return false;

	const uint32 IndexBudget = static_cast<uint32>(CellCount) * AverageLightsPerCell;
	if (!Ranges.EnsureCapacity(static_cast<uint32>(CellCount), sizeof(uint32) * 4) ||
		!PointIndices.EnsureCapacity(std::max({ IndexBudget, Constants.GridPointCount, 1u }), sizeof(uint32)) ||
		!SpotIndices.EnsureCapacity(std::max({ IndexBudget, Constants.GridSpotCount, 1u }), sizeof(uint32)))
		return false;

	// 모든 준비가 끝났을 때만 요청한 모드를 활성화한다.
	Constants.GridPointIndexCapacity = PointIndices.Capacity;
	Constants.GridSpotIndexCapacity = SpotIndices.Capacity;
	Constants.GridMode = static_cast<uint32>(InMode);
	return true;
}

bool FForwardLightTileGrid::ExecuteBuild(FRenderer* Renderer, FTexture2D* Depth, const FStructuredBuffer* SceneLights)
{
	// Clustered는 깊이 텍스처 없이 고정 깊이 구간의 경계를 만든다.
	const bool bClustered = Constants.GridMode == static_cast<uint32>(ELightCullingMode::Clustered);
	ID3D11ComputeShader* Shader = bClustered ? ClusterComputeShader.Get() : ComputeShader.Get();
	if (Constants.GridMode == static_cast<uint32>(ELightCullingMode::Off) ||
		!Renderer || !SceneLights || !SceneLights->GetSRV() || !Shader ||
		!Ranges.UAV || !PointIndices.UAV || !SpotIndices.UAV || !Allocator.UAV ||
		(!bClustered && (!Depth || !Depth->GetSRV())))
		return false;

	Renderer->UpdateConstant(CullingCB.get(), &Constants, sizeof(Constants));
	Renderer->ClearUnorderedAccess(Allocator.UAV.Get());

	// 기존 입력·출력 슬롯과 목록 버퍼를 유지한다.
	const std::array<ID3D11ShaderResourceView*, 2> Inputs =
	{ bClustered ? nullptr : Depth->GetSRV(), SceneLights->GetSRV() };
	const std::array<ID3D11UnorderedAccessView*, 4> Outputs =
	{ Ranges.UAV.Get(), PointIndices.UAV.Get(), SpotIndices.UAV.Get(), Allocator.UAV.Get() };

	const uint32 DispatchDepth = bClustered ? Constants.GridSliceCount : 1u;
	Renderer->DispatchCompute(Shader, CullingCB->GetBuffer(), 0, Inputs, Outputs,
		Constants.GridTileCountX, Constants.GridTileCountY, DispatchDepth);
	return true;

}

void FForwardLightTileGrid::PreparePixelShader(FRenderer* Renderer, bool bBuildSucceeded)
{
	bPixelTilesValid = Constants.GridMode != static_cast<uint32>(ELightCullingMode::Off) &&
		bBuildSucceeded && Ranges.SRV && PointIndices.SRV && SpotIndices.SRV;
	if (!Renderer || !LightingInfoCB) return;

	FGridConstants PixelConstants = Constants;
	if (!bPixelTilesValid)
		PixelConstants.GridMode = static_cast<uint32>(ELightCullingMode::Off);
	Renderer->UpdateConstant(LightingInfoCB.get(), &PixelConstants, sizeof(PixelConstants));

}

std::array<ID3D11Resource*, 4> FForwardLightTileGrid::GetWriteResources() const
{
	return {Ranges.Buffer.Get(), PointIndices.Buffer.Get(), SpotIndices.Buffer.Get(), Allocator.Buffer.Get()};
}

std::array<ID3D11ShaderResourceView*, 3> FForwardLightTileGrid::GetPixelResources() const
{
	return bPixelTilesValid ? std::array<ID3D11ShaderResourceView*, 3>{Ranges.SRV.Get(), PointIndices.SRV.Get(), SpotIndices.SRV.Get()}
		: std::array<ID3D11ShaderResourceView*, 3>{};
}
