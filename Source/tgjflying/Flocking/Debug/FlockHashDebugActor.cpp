// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockHashDebugActor.h"
#include "Async/ParallelFor.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FlockSubsystem.h"

namespace
{
	void Smooth(double& Value, double Sample)
	{
		Value = Value <= 0.0 ? Sample : FMath::Lerp(Value, Sample, 0.05);
	}

	constexpr int32 MaxDrawnCells = 3000;
}

AFlockHashDebugActor::AFlockHashDebugActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Stream.Initialize(1234);
}

void AFlockHashDebugActor::InitPoints()
{
	const FVector3f Origin(GetActorLocation());
	const FVector3f Box(Extent);

	Points.SetNumUninitialized(NumPoints);
	Velocities.SetNumUninitialized(NumPoints);
	for (int32 i = 0; i < NumPoints; ++i)
	{
		Points[i] = Origin + FVector3f(
			Stream.FRandRange(-Box.X, Box.X),
			Stream.FRandRange(-Box.Y, Box.Y),
			Stream.FRandRange(-Box.Z, Box.Z));
		Velocities[i] = FVector3f(Stream.GetUnitVector());
	}
	TotalMismatches = 0;
}

void AFlockHashDebugActor::MovePoints(float DeltaSeconds)
{
	const FVector3f Origin(GetActorLocation());
	const FVector3f Box(Extent);

	for (int32 i = 0; i < Points.Num(); ++i)
	{
		FVector3f& P = Points[i];
		FVector3f& V = Velocities[i];
		P += V * (MoveSpeed * DeltaSeconds);

		// bounce on the box walls
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float Local = P[Axis] - Origin[Axis];
			if (FMath::Abs(Local) > Box[Axis])
			{
				P[Axis] = Origin[Axis] + FMath::Clamp(Local, -Box[Axis], Box[Axis]);
				V[Axis] = -V[Axis];
			}
		}
	}
}

void AFlockHashDebugActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Points.Num() != NumPoints)
	{
		InitPoints();
	}

	MovePoints(DeltaSeconds);

	double Start = FPlatformTime::Seconds();
	Hash.Build(Points, CellSize);
	Smooth(BuildMicros, (FPlatformTime::Seconds() - Start) * 1e6);

	if (bTimeFullPass)
	{
		NeighborCounts.SetNumZeroed(Points.Num());

		auto QueryFromPoint = [this](int32 Index)
		{
			int32 Count = 0;
			Hash.QueryRadius(Points[Index], QueryRadius, [&Count](int32, const FVector3f&, float)
			{
				++Count;
				return true;
			}, Index);
			NeighborCounts[Index] = Count;
		};

		Start = FPlatformTime::Seconds();
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			QueryFromPoint(i);
		}
		Smooth(SerialMicros, (FPlatformTime::Seconds() - Start) * 1e6);

		Start = FPlatformTime::Seconds();
		ParallelFor(TEXT("FlockHashDebugQuery"), Points.Num(), 64, QueryFromPoint);
		Smooth(ParallelMicros, (FPlatformTime::Seconds() - Start) * 1e6);

		int64 Sum = 0;
		for (int32 Count : NeighborCounts)
		{
			Sum += Count;
		}
		Smooth(AvgNeighbors, Points.Num() > 0 ? double(Sum) / Points.Num() : 0.0);
	}

	if (bValidate && Points.Num() > 0)
	{
		for (int32 Check = 0; Check < 8; ++Check)
		{
			const int32 Index = Check == 0 ? FMath::Clamp(QueryPointIndex, 0, Points.Num() - 1) : Stream.RandRange(0, Points.Num() - 1);
			const float Radius = Check == 0 ? QueryRadius : Stream.FRandRange(0.f, CellSize * 3.f);
			FString Error;
			if (!Hash.ValidateQuery(Points[Index], Radius, Index, &Error))
			{
				++TotalMismatches;
				UE_LOG(LogFlock, Error, TEXT("FlockHashDebug mismatch: %s"), *Error);
			}
		}
	}

	// the master debug switch also hides the drawing
	const UFlockSubsystem* Flock = GetWorld() ? GetWorld()->GetSubsystem<UFlockSubsystem>() : nullptr;
	if (Flock && !Flock->IsDebugDrawEnabled())
	{
		return;
	}

	Draw();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(uint64(GetUniqueID()), 0.f, TotalMismatches > 0 ? FColor::Red : FColor::Green,
			FString::Printf(TEXT("FlockHash  %d pts | cell %.0f | build %.1f us | all-query serial %.1f us, parallel %.1f us | avg neighbors %.1f | mismatches %d"),
				Points.Num(), CellSize, BuildMicros, SerialMicros, ParallelMicros, AvgNeighbors, TotalMismatches));
	}
}

void AFlockHashDebugActor::Draw() const
{
	UWorld* World = GetWorld();
	if (!World || Points.Num() == 0)
	{
		return;
	}

	if (bDrawCells)
	{
		TArray<TPair<FIntVector3, int32>> Cells;
		Hash.GetOccupiedCells(Cells);
		const int32 NumToDraw = FMath::Min(Cells.Num(), MaxDrawnCells);
		for (int32 i = 0; i < NumToDraw; ++i)
		{
			const FBox3f Bounds = Hash.GetCellBounds(Cells[i].Key);
			const uint8 Shade = uint8(FMath::Clamp(60 + Cells[i].Value * 25, 60, 255));
			DrawDebugBox(World, FVector(Bounds.GetCenter()), FVector(Bounds.GetExtent()), FColor(Shade, Shade, Shade), false, -1.f, 0, 1.f);
		}
	}

	const int32 QueryIndex = FMath::Clamp(QueryPointIndex, 0, Points.Num() - 1);
	const FVector QueryCenter(Points[QueryIndex]);

	if (bDrawPoints)
	{
		for (const FVector3f& P : Points)
		{
			DrawDebugPoint(World, FVector(P), 4.f, FColor::White, false, -1.f);
		}
	}

	DrawDebugSphere(World, QueryCenter, QueryRadius, 24, FColor::Yellow, false, -1.f, 0, 2.f);
	DrawDebugPoint(World, QueryCenter, 14.f, FColor::Red, false, -1.f);
	Hash.QueryRadius(Points[QueryIndex], QueryRadius, [World, &QueryCenter](int32, const FVector3f& Pos, float)
	{
		DrawDebugPoint(World, FVector(Pos), 10.f, FColor::Green, false, -1.f);
		DrawDebugLine(World, QueryCenter, FVector(Pos), FColor::Green, false, -1.f, 0, 1.f);
		return true;
	}, QueryIndex);
}
