// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogFlock, Log, All);

/** Uniform 3D spatial hash for flock agents, rebuilt from scratch every frame with a counting sort */
class TGJFLYING_API FFlockSpatialHash
{
public:

	void Build(TConstArrayView<FVector3f> Positions, float InCellSize, int32 TableSizeOverride = 0);

	/** Empties the hash, keeps the allocations */
	void Reset();

	/** Visits every agent within Radius of Center (inclusive), each exactly once */
	template<typename VisitorType>
	void QueryRadius(const FVector3f& Center, float Radius, VisitorType&& Visit, int32 IgnoreIndex = INDEX_NONE) const;

	/** Runs the same query through the hash and through brute force and compares them */
	bool ValidateQuery(const FVector3f& Center, float Radius, int32 IgnoreIndex = INDEX_NONE, FString* OutError = nullptr) const;

	/** Every occupied cell and how many agents it holds, for debug drawing */
	void GetOccupiedCells(TArray<TPair<FIntVector3, int32>>& OutCells) const;

	FORCEINLINE FIntVector3 GetCell(const FVector3f& Position) const
	{
		return FIntVector3(
			FMath::FloorToInt32(Position.X * InvCellSize),
			FMath::FloorToInt32(Position.Y * InvCellSize),
			FMath::FloorToInt32(Position.Z * InvCellSize));
	}

	FBox3f GetCellBounds(const FIntVector3& Cell) const
	{
		const FVector3f Min(Cell.X * CellSize, Cell.Y * CellSize, Cell.Z * CellSize);
		return FBox3f(Min, Min + FVector3f(CellSize));
	}

	int32 Num() const { return SortedIndex.Num(); }
	float GetCellSize() const { return CellSize; }
	int32 GetTableSize() const { return SlotStart.Num(); }

private:

	FORCEINLINE uint32 GetSlot(const FIntVector3& Cell) const
	{
		uint32 Hash = (uint32(Cell.X) * 73856093u) ^ (uint32(Cell.Y) * 19349663u) ^ (uint32(Cell.Z) * 83492791u);
		// murmur3 finalizer, so the power-of-two mask sees all the bits
		Hash ^= Hash >> 16;
		Hash *= 0x85ebca6bu;
		Hash ^= Hash >> 13;
		Hash *= 0xc2b2ae35u;
		Hash ^= Hash >> 16;
		return Hash & SlotMask;
	}

	float CellSize = 100.f;
	float InvCellSize = 0.01f;
	uint32 SlotMask = 0;

	/** Per table slot: first entry and number of entries */
	TArray<int32> SlotStart;
	TArray<int32> SlotCount;

	/** Per entry, sorted by slot */
	TArray<int32> SortedIndex;
	TArray<FVector3f> SortedPos;
	TArray<FIntVector3> SortedCell;

	/** Per agent, reused between builds */
	TArray<uint32> ScratchSlot;
	TArray<FIntVector3> ScratchCell;
};

template<typename VisitorType>
void FFlockSpatialHash::QueryRadius(const FVector3f& Center, float Radius, VisitorType&& Visit, int32 IgnoreIndex) const
{
	const int32 NumEntries = SortedIndex.Num();
	if (NumEntries == 0 || !(Radius >= 0.f))
	{
		return;
	}

	const float RadiusSq = Radius * Radius;

	// returns false when the visitor asked to stop
	auto VisitEntry = [&](int32 Entry) -> bool
	{
		const int32 AgentIndex = SortedIndex[Entry];
		if (AgentIndex == IgnoreIndex)
		{
			return true;
		}
		const FVector3f& Pos = SortedPos[Entry];
		const float DistSq = (Pos - Center).SizeSquared();
		if (DistSq <= RadiusSq)
		{
			return Visit(AgentIndex, Pos, DistSq);
		}
		return true;
	};

	// pad the box by a few ulps so floor() rounding cannot drop a cell
	const float Pad = (Center.GetAbsMax() + Radius) * 1e-6f + 0.01f;
	const FIntVector3 Min = GetCell(Center - FVector3f(Radius + Pad));
	const FIntVector3 Max = GetCell(Center + FVector3f(Radius + Pad));

	const int64 NumCells = int64(Max.X - Min.X + 1) * int64(Max.Y - Min.Y + 1) * int64(Max.Z - Min.Z + 1);
	if (NumCells > NumEntries)
	{
		// more cells than agents: a linear scan is cheaper
		for (int32 Entry = 0; Entry < NumEntries; ++Entry)
		{
			if (!VisitEntry(Entry))
			{
				return;
			}
		}
		return;
	}

	// returns false when the visitor asked to stop
	auto VisitCell = [&](const FIntVector3& Cell) -> bool
	{
		const uint32 Slot = GetSlot(Cell);
		const int32 Start = SlotStart[Slot];
		const int32 End = Start + SlotCount[Slot];
		for (int32 Entry = Start; Entry < End; ++Entry)
		{
			// slot collisions: only accept agents that really live in this cell
			if (SortedCell[Entry] == Cell && !VisitEntry(Entry))
			{
				return false;
			}
		}
		return true;
	};

	// center cell first, it holds the closest agents
	const FIntVector3 CenterCell = GetCell(Center);
	if (!VisitCell(CenterCell))
	{
		return;
	}

	for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
	{
		for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
		{
			for (int32 X = Min.X; X <= Max.X; ++X)
			{
				const FIntVector3 Cell(X, Y, Z);
				if (Cell == CenterCell)
				{
					continue;
				}
				if (!VisitCell(Cell))
				{
					return;
				}
			}
		}
	}
}
