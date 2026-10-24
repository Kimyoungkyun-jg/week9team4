#include "EnginePCH.h"
#include "CollisionShape.h"
#include <vector>
#include <limits>

namespace
{
constexpr float ContactTolerance = 1.e-4f;
bool Finite(const FVector& V) { return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z); }
struct FSupport { FVector P, A, B; };
struct FSimplex
{
    FSupport Points[4];
    int Count = 0;
    int size() const { return Count; }
    FSupport& operator[](int I) { return Points[I]; }
    const FSupport& operator[](int I) const { return Points[I]; }
    void push_back(const FSupport& Point) { assert(Count < 4); Points[Count++] = Point; }
    const FSupport* begin() const { return Points; }
    const FSupport* end() const { return Points + Count; }
};
FSupport Support(const FCollisionGeometry& A, const FCollisionGeometry& B, const FVector& D)
{
    const FVector PA = A.Support(D), PB = B.Support(-D);
    return { PA - PB, PA, PB };
}

// Closest point on each simplex face. At most four vertices: small exhaustive
// active-set solve avoids separate degenerate line/triangle/tetrahedron paths.
template <typename TSimplex>
bool Weights(const TSimplex& S, int Mask, double* W, FVector& Point)
{
    int Id[4], N = 0;
    for (int I = 0; I < static_cast<int>(S.size()); ++I) if (Mask & (1 << I)) Id[N++] = I;
    double M[3][4] = {};
    FVector E[3];
    const FVector Base = S[Id[0]].P;
    for (int I = 0; I < N - 1; ++I) E[I] = S[Id[I + 1]].P - Base;
    for (int I = 0; I < N - 1; ++I)
    {
        for (int J = 0; J < N - 1; ++J) M[I][J] = E[I].Dot(E[J]);
        M[I][N - 1] = -E[I].Dot(Base);
    }
    for (int I = 0; I < N - 1; ++I)
    {
        int Pivot = I;
        for (int J = I + 1; J < N - 1; ++J) if (std::abs(M[J][I]) > std::abs(M[Pivot][I])) Pivot = J;
        if (std::abs(M[Pivot][I]) < 1.e-12) return false;
        for (int K = I; K < N; ++K) std::swap(M[I][K], M[Pivot][K]);
        const double Divisor = M[I][I];
        for (int K = I; K < N; ++K) M[I][K] /= Divisor;
        for (int J = 0; J < N - 1; ++J) if (J != I)
        {
            const double Factor = M[J][I];
            for (int K = I; K < N; ++K) M[J][K] -= Factor * M[I][K];
        }
    }
    std::fill(W, W + 4, 0.0);
    W[Id[0]] = 1.0;
    for (int I = 1; I < N; ++I) { W[Id[I]] = M[I - 1][N - 1]; W[Id[0]] -= W[Id[I]]; }
    Point = FVector::ZeroVector;
    for (int I = 0; I < static_cast<int>(S.size()); ++I)
    {
        if (W[I] < -1.e-6) return false;
        Point += S[I].P * static_cast<float>(W[I]);
    }
    return true;
}

struct FDistance { float Distance; FVector Normal, PointA, PointB; FSimplex Simplex; };

// Exact signed distances for the common projectile/query pairs. These avoid
// iterative GJK and EPA entirely, including already-overlapping projectiles.
bool AnalyticDistance(const FCollisionGeometry& A, const FCollisionGeometry& B, FDistance& Out)
{
    if (A.Type != ECollisionShape::Sphere)
    {
        if (B.Type != ECollisionShape::Sphere || !AnalyticDistance(B, A, Out)) return false;
        Out.Normal = -Out.Normal; std::swap(Out.PointA, Out.PointB); return true;
    }
    FVector Closest, Normal;
    float SignedCenterDistance;
    if (B.Type == ECollisionShape::Sphere || B.Type == ECollisionShape::Capsule)
    {
        Closest = B.Center;
        FVector Fallback(1, 0, 0);
        if (B.Type == ECollisionShape::Capsule)
        {
            const FVector Axis = B.Rotation.RotateVector(FVector(0, 0, 1));
            const float Segment = std::max(0.f, B.HalfHeight - B.Radius);
            Closest += Axis * std::clamp((A.Center - B.Center).Dot(Axis), -Segment, Segment);
            Fallback = Axis.Cross(FVector(1, 0, 0));
            if (Fallback.SizeSquared() < SMALL_NUMBER) Fallback = Axis.Cross(FVector(0, 1, 0));
            Fallback = Fallback.GetSafeNormal();
        }
        const FVector Offset = A.Center - Closest;
        const float Length = Offset.Size();
        Normal = Length > 1.e-7f ? Offset / Length : Fallback;
        SignedCenterDistance = Length - B.Radius;
        Closest += Normal * B.Radius;
    }
    else if (B.Type == ECollisionShape::Box)
    {
        const FVector Local = B.Rotation.Inverse().RotateVector(A.Center - B.Center);
        FVector Point;
        for (int Axis = 0; Axis < 3; ++Axis) Point[Axis] = std::clamp(Local[Axis], -B.HalfExtent[Axis], B.HalfExtent[Axis]);
        const FVector Offset = Local - Point;
        const float Length = Offset.Size();
        FVector LocalNormal;
        if (Length > 1.e-7f) { LocalNormal = Offset / Length; SignedCenterDistance = Length; }
        else
        {
            const FVector Remaining = B.HalfExtent - Local.GetAbs();
            const int Axis = Remaining.X <= Remaining.Y && Remaining.X <= Remaining.Z ? 0 : Remaining.Y <= Remaining.Z ? 1 : 2;
            LocalNormal = FVector::ZeroVector;
            LocalNormal[Axis] = Local[Axis] >= 0.f ? 1.f : -1.f;
            Point[Axis] = LocalNormal[Axis] * B.HalfExtent[Axis];
            SignedCenterDistance = -Remaining[Axis];
        }
        Normal = B.Rotation.RotateVector(LocalNormal);
        Closest = B.Center + B.Rotation.RotateVector(Point);
    }
    else return false;
    Out = { SignedCenterDistance - A.Radius, Normal, A.Center - Normal * A.Radius, Closest, {} };
    return true;
}

FDistance Distance(const FCollisionGeometry& A, const FCollisionGeometry& B)
{
    FDistance Analytic;
    if (AnalyticDistance(A, B, Analytic)) return Analytic;
    FSimplex S; S.push_back(Support(A, B, FVector(1, 0, 0)));
    FVector Closest = S[0].P, PA = S[0].A, PB = S[0].B;
    for (int Iter = 0; Iter < 64; ++Iter)
    {
        float Best = FLT_MAX;
        double BestW[4] = {};
        for (int Mask = 1; Mask < (1 << S.size()); ++Mask)
        {
            double W[4]; FVector P;
            if (Weights(S, Mask, W, P) && P.SizeSquared() < Best)
            {
                Best = P.SizeSquared(); Closest = P;
                std::copy(W, W + 4, BestW);
            }
        }
        PA = PB = FVector::ZeroVector;
        FSimplex Reduced;
        for (int I = 0; I < static_cast<int>(S.size()); ++I) if (BestW[I] > 1.e-7)
        {
            PA += S[I].A * static_cast<float>(BestW[I]);
            PB += S[I].B * static_cast<float>(BestW[I]);
            Reduced.push_back(S[I]);
        }
        S = Reduced;
        if (Best <= 1.e-10f || S.size() >= 4) break;
        const FSupport Next = Support(A, B, -Closest);
        if (Best - Closest.Dot(Next.P) <= 1.e-7f * std::max(1.0f, Best)) break;
        bool Duplicate = false;
        for (const FSupport& V : S) if ((V.P - Next.P).SizeSquared() < 1.e-12f) Duplicate = true;
        if (Duplicate) break;
        S.push_back(Next);
    }
    const float D = Closest.Size();
    return { D, D > 1.e-6f ? Closest / D : (A.Center - B.Center).GetSafeNormal(SMALL_NUMBER, FVector(1, 0, 0)), PA, PB, S };
}

// EPA is needed only for an initially penetrating pair, not ordinary shape casts.
void Penetration(const FCollisionGeometry& A, const FCollisionGeometry& B, const FDistance& DistanceResult, FHitResult& Hit)
{
    FDistance Analytic;
    if (AnalyticDistance(A, B, Analytic))
    {
        Hit.PenetrationDepth = std::max(0.f, -Analytic.Distance);
        Hit.bStartPenetrating = Hit.PenetrationDepth > ContactTolerance;
        Hit.Normal = Hit.ImpactNormal = Analytic.Normal;
        Hit.ImpactPoint = Analytic.PointB;
        return;
    }
    std::vector<FSupport> Candidates;
    auto AddPoint = [&](const FSupport& P)
    {
        for (const auto& Existing : Candidates) if ((Existing.P - P.P).SizeSquared() < 1.e-12f) return;
        Candidates.push_back(P);
    };
    for (const auto& Point : DistanceResult.Simplex) AddPoint(Point);
    auto AddDirection = [&](const FVector& D) { if (D.SizeSquared() > 1.e-12f) { AddPoint(Support(A, B, D)); AddPoint(Support(A, B, -D)); } };
    // Extend the terminal GJK simplex in its missing dimensions. At most 14
    // unique points (1,001 tetrahedra), instead of 26 points / 14,950 combinations.
    const auto& Simplex = DistanceResult.Simplex;
    if (Simplex.size() == 2)
    {
        const FVector Line = Simplex[1].P - Simplex[0].P;
        FVector Perp = Line.Cross(FVector(1, 0, 0));
        if (Perp.SizeSquared() < SMALL_NUMBER) Perp = Line.Cross(FVector(0, 1, 0));
        AddDirection(Perp); AddDirection(Line.Cross(Perp));
    }
    else if (Simplex.size() == 3) AddDirection((Simplex[1].P - Simplex[0].P).Cross(Simplex[2].P - Simplex[0].P));
    AddDirection(FVector(1, 0, 0)); AddDirection(FVector(0, 1, 0)); AddDirection(FVector(0, 0, 1));
    // Pure axial supports can form an octahedron with the origin on every
    // tetrahedral boundary. A diagonal pair breaks that degeneracy.
    AddDirection(FVector(1, 1, 1));
    std::vector<FSupport> V;
    for (int I = 0; I < static_cast<int>(Candidates.size()) && V.empty(); ++I)
    for (int J = I + 1; J < static_cast<int>(Candidates.size()) && V.empty(); ++J)
    for (int K = J + 1; K < static_cast<int>(Candidates.size()) && V.empty(); ++K)
    for (int L = K + 1; L < static_cast<int>(Candidates.size()); ++L)
    {
        FSimplex T; T.push_back(Candidates[I]); T.push_back(Candidates[J]); T.push_back(Candidates[K]); T.push_back(Candidates[L]);
        double W[4]; FVector P;
        if (Weights(T, 15, W, P) && P.SizeSquared() < 1.e-8f && *std::min_element(W, W + 4) > 1.e-5)
        { V.assign(T.begin(), T.end()); break; }
    }
    if (V.empty()) return; // touching/degenerate geometry has no positive penetration.
    struct FFace { int A, B, C; FVector N; float D; };
    std::vector<FFace> Faces;
    auto AddFace = [&](int I, int J, int K)
    {
        FVector N = (V[J].P - V[I].P).Cross(V[K].P - V[I].P).GetSafeNormal();
        if (N.SizeSquared() < 0.5f) return;
        float D = N.Dot(V[I].P);
        if (D < 0) { std::swap(J, K); N = -N; D = -D; }
        Faces.push_back({ I, J, K, N, D });
    };
    AddFace(0, 1, 2); AddFace(0, 3, 1); AddFace(0, 2, 3); AddFace(1, 3, 2);
    for (int Iter = 0; Iter < 80 && !Faces.empty(); ++Iter)
    {
        const FFace Face = *std::min_element(Faces.begin(), Faces.end(), [](const FFace& X, const FFace& Y) { return X.D < Y.D; });
        const FSupport Next = Support(A, B, Face.N);
        if (Next.P.Dot(Face.N) - Face.D < ContactTolerance || Iter == 79)
        {
            Hit.PenetrationDepth = Face.D;
            Hit.bStartPenetrating = Face.D > ContactTolerance;
            Hit.Normal = Hit.ImpactNormal = -Face.N;
            std::vector<FSupport> Triangle{ V[Face.A], V[Face.B], V[Face.C] };
            double W[4]; FVector P;
            if (Weights(Triangle, 7, W, P))
                Hit.ImpactPoint = Triangle[0].B * static_cast<float>(W[0]) + Triangle[1].B * static_cast<float>(W[1]) + Triangle[2].B * static_cast<float>(W[2]);
            return;
        }
        std::vector<std::pair<int, int>> Edges;
        auto Edge = [&](int I, int J)
        {
            auto It = std::find(Edges.begin(), Edges.end(), std::make_pair(J, I));
            if (It != Edges.end()) Edges.erase(It); else Edges.emplace_back(I, J);
        };
        for (auto It = Faces.begin(); It != Faces.end();)
        {
            if (It->N.Dot(Next.P - V[It->A].P) > 1.e-6f)
            { Edge(It->A, It->B); Edge(It->B, It->C); Edge(It->C, It->A); It = Faces.erase(It); }
            else ++It;
        }
        if (Edges.empty()) return;
        const int Index = static_cast<int>(V.size()); V.push_back(Next);
        for (auto [I, J] : Edges) AddFace(I, J, Index);
    }
}
}

FCollisionShape FCollisionShape::MakeBox(const FVector& Extent, const FVector& Center)
{ FCollisionShape S; S.Type = ECollisionShape::Box; S.HalfExtent = Extent.GetAbs(); S.Center = Center; return S; }
FCollisionShape FCollisionShape::MakeSphere(float Radius)
{ FCollisionShape S; S.Type = ECollisionShape::Sphere; S.Radius = std::max(0.0f, Radius); return S; }
FCollisionShape FCollisionShape::MakeCapsule(float Radius, float HalfHeight)
{ FCollisionShape S = MakeSphere(Radius); S.Type = ECollisionShape::Capsule; S.HalfHeight = std::max(S.Radius, HalfHeight); return S; }

FCollisionGeometry MakeCollisionGeometry(const FCollisionShape& Shape, const FTransform& Transform)
{
    FCollisionGeometry G;
    G.Type = Shape.Type; G.Center = Transform.GetLocalMatrix().TransformPosition(Shape.Center); G.Rotation = Transform.GetOrientation();
    const FVector Scale = Transform.Scale.GetAbs();
    const float UniformScale = std::min({ Scale.X, Scale.Y, Scale.Z });
    G.HalfExtent = Shape.HalfExtent * Scale;
    G.Radius = Shape.Radius * UniformScale;
    G.HalfHeight = Shape.HalfHeight * UniformScale;
    return G;
}

bool FCollisionGeometry::IsValid() const
{
    return Type != ECollisionShape::None && Finite(Center) && Finite(HalfExtent) &&
        std::isfinite(Rotation.X) && std::isfinite(Rotation.Y) && std::isfinite(Rotation.Z) && std::isfinite(Rotation.W) &&
        std::isfinite(Radius) && std::isfinite(HalfHeight) && Radius >= 0 && HalfHeight >= 0 &&
        HalfExtent.X >= 0 && HalfExtent.Y >= 0 && HalfExtent.Z >= 0;
}

FVector FCollisionGeometry::Support(const FVector& Direction) const
{
    if (Type == ECollisionShape::Box)
    {
        const FVector D = Rotation.Inverse().RotateVector(Direction);
        return Center + Rotation.RotateVector(FVector(D.X >= 0 ? HalfExtent.X : -HalfExtent.X,
            D.Y >= 0 ? HalfExtent.Y : -HalfExtent.Y, D.Z >= 0 ? HalfExtent.Z : -HalfExtent.Z));
    }
    FVector P = Center + Direction.GetSafeNormal(SMALL_NUMBER, FVector(1, 0, 0)) * Radius;
    if (Type == ECollisionShape::Capsule)
    {
        const FVector Axis = Rotation.RotateVector(FVector(0, 0, 1));
        P += Axis * ((Direction.Dot(Axis) >= 0 ? 1.f : -1.f) * std::max(0.f, HalfHeight - Radius));
    }
    return P;
}

FBox FCollisionGeometry::GetBounds() const
{
    FVector E;
    if (Type == ECollisionShape::Box)
        E = Rotation.RotateVector(FVector(1, 0, 0)).GetAbs() * HalfExtent.X +
            Rotation.RotateVector(FVector(0, 1, 0)).GetAbs() * HalfExtent.Y +
            Rotation.RotateVector(FVector(0, 0, 1)).GetAbs() * HalfExtent.Z;
    else
    {
        E = FVector(Radius);
        if (Type == ECollisionShape::Capsule) E += Rotation.RotateVector(FVector(0, 0, 1)).GetAbs() * std::max(0.f, HalfHeight - Radius);
    }
    return { Center - E, Center + E };
}

bool CollisionBoundsIntersect(const FBox& A, const FBox& B)
{
    return A.Min.X <= B.Max.X && A.Max.X >= B.Min.X && A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y && A.Min.Z <= B.Max.Z && A.Max.Z >= B.Min.Z;
}

bool OverlapCollisionGeometry(const FCollisionGeometry& A, const FCollisionGeometry& B, FHitResult* Hit)
{
    if (!A.IsValid() || !B.IsValid() || !CollisionBoundsIntersect(A.GetBounds(), B.GetBounds())) return false;
    const FDistance D = Distance(A, B);
    if (D.Distance > ContactTolerance) return false;
    if (Hit)
    {
        *Hit = FHitResult(0.f); Hit->Location = A.Center; Hit->ImpactPoint = D.PointB;
        Hit->Normal = Hit->ImpactNormal = D.Normal;
        Penetration(A, B, D, *Hit);
    }
    return true;
}

bool SweepCollisionGeometry(const FCollisionGeometry& Moving, const FVector& Delta, const FCollisionGeometry& Target, FHitResult& Hit)
{
    Hit = FHitResult(1.f);
    if (!Moving.IsValid() || !Target.IsValid() || !Finite(Delta)) return false;
    FCollisionGeometry Current = Moving;
    float Time = 0.f;
    FVector SeparatingNormal = (Moving.Center - Target.Center).GetSafeNormal(SMALL_NUMBER, FVector(1, 0, 0));
    for (int Iter = 0; Iter < 64; ++Iter)
    {
        const FDistance D = Distance(Current, Target);
        if (D.Distance <= ContactTolerance)
        {
            Hit.Time = Time; Hit.Distance = Delta.Size() * Time; Hit.Location = Current.Center;
            Hit.ImpactPoint = D.PointB; Hit.Normal = Hit.ImpactNormal = Time > 0.f ? SeparatingNormal : D.Normal;
            if (Time == 0.f) Penetration(Moving, Target, D, Hit);
            // A touching pair may move apart or tangentially. An initial penetration
            // also permits movement out of the surface, leaving resolution to movement.
            if (Time == 0.f && Delta.Dot(Hit.Normal) >= -1.e-7f) return false;
            return true;
        }
        const float ClosingSpeed = -Delta.Dot(D.Normal);
        if (ClosingSpeed <= 1.e-8f) return false;
        SeparatingNormal = D.Normal;
        const float Step = std::max((D.Distance - ContactTolerance * 0.5f) / ClosingSpeed, 1.e-7f);
        Time += Step;
        if (Time > 1.f) return false;
        Current.Center = Moving.Center + Delta * Time;
    }
    return false;
}
