// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlockSpatialHash.h"
#include "FlockHashDebugActor.generated.h"

/** Test bench for FFlockSpatialHash */
UCLASS()
class TGJFLYING_API AFlockHashDebugActor : public AActor
{
	GENERATED_BODY()

protected:

	/** Number of points in the cloud */
	UPROPERTY(EditAnywhere, Category="Flock Hash Debug", meta = (ClampMin = 1, ClampMax = 20000))
	int32 NumPoints = 1000;

	/** Half size of the box the points move in */
	UPROPERTY(EditAnywhere, Category="Flock Hash Debug")
	FVector Extent = FVector(3000.0, 3000.0, 1500.0);

	/** Hash cell size */
	UPROPERTY(EditAnywhere, Category="Flock Hash Debug", meta = (ClampMin = 1))
	float CellSize = 300.f;

	/** Radius of the drawn query and the timed pass */
	UPROPERTY(EditAnywhere, Category="Flock Hash Debug", meta = (ClampMin = 0))
	float QueryRadius = 300.f;

	UPROPERTY(EditAnywhere, Category="Flock Hash Debug", meta = (ClampMin = 0))
	int32 QueryPointIndex = 0;

	/** Point speed in cm/s */
	UPROPERTY(EditAnywhere, Category="Flock Hash Debug", meta = (ClampMin = 0))
	float MoveSpeed = 300.f;

	UPROPERTY(EditAnywhere, Category="Flock Hash Debug")
	bool bDrawCells = true;

	UPROPERTY(EditAnywhere, Category="Flock Hash Debug")
	bool bDrawPoints = true;

	/** Checks a few queries against brute force every frame */
	UPROPERTY(EditAnywhere, Category="Flock Hash Debug")
	bool bValidate = true;

	/** Times a query from every point, serial and parallel */
	UPROPERTY(EditAnywhere, Category="Flock Hash Debug")
	bool bTimeFullPass = true;

	UPROPERTY(EditAnywhere, Category="Flock Hash Debug")
	bool bTickInEditor = false;

public:

	AFlockHashDebugActor();

	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return bTickInEditor; }

private:

	void InitPoints();
	void MovePoints(float DeltaSeconds);
	void Draw() const;

	TArray<FVector3f> Points;
	TArray<FVector3f> Velocities;
	TArray<int32> NeighborCounts;
	FFlockSpatialHash Hash;
	FRandomStream Stream;

	// smoothed timings in microseconds
	double BuildMicros = 0.0;
	double SerialMicros = 0.0;
	double ParallelMicros = 0.0;
	double AvgNeighbors = 0.0;
	int32 TotalMismatches = 0;
};
