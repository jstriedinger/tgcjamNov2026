// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockSpatialHash.h"

DEFINE_LOG_CATEGORY(LogFlock);

void FFlockSpatialHash::Build(TConstArrayView<FVector3f> Positions, float InCellSize, int32 TableSizeOverride)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FFlockSpatialHash::Build);

	CellSize = FMath::Max(InCellSize, 1.f);
	InvCellSize = 1.f / CellSize;

	const int32 NumAgents = Positions.Num();

	// ~2 slots per agent keeps collisions rare
	const uint32 WantedSlots = TableSizeOverride > 0 ? uint32(TableSizeOverride) : uint32(FMath::Max(NumAgents * 2, 64));
	const int32 TableSize = int32(FMath::RoundUpToPowerOfTwo(WantedSlots));
	SlotMask = uint32(TableSize - 1);

	SlotStart.SetNumUninitialized(TableSize);
	SlotCount.SetNumUninitialized(TableSize);
	FMemory::Memzero(SlotCount.GetData(), TableSize * sizeof(int32));

	SortedIndex.SetNumUninitialized(NumAgents);
	SortedPos.SetNumUninitialized(NumAgents);
	SortedCell.SetNumUninitialized(NumAgents);
	ScratchSlot.SetNumUninitialized(NumAgents);
	ScratchCell.SetNumUninitialized(NumAgents);

	// count agents per slot
	for (int32 i = 0; i < NumAgents; ++i)
	{
		const FIntVector3 Cell = GetCell(Positions[i]);
		const uint32 Slot = GetSlot(Cell);
		ScratchCell[i] = Cell;
		ScratchSlot[i] = Slot;
		++SlotCount[Slot];
	}

	// prefix sum gives each slot's start
	int32 Running = 0;
	for (int32 Slot = 0; Slot < TableSize; ++Slot)
	{
		SlotStart[Slot] = Running;
		Running += SlotCount[Slot];
	}

	// place agents into their slots
	FMemory::Memzero(SlotCount.GetData(), TableSize * sizeof(int32));
	for (int32 i = 0; i < NumAgents; ++i)
	{
		const uint32 Slot = ScratchSlot[i];
		const int32 Dst = SlotStart[Slot] + SlotCount[Slot]++;
		SortedIndex[Dst] = i;
		SortedPos[Dst] = Positions[i];
		SortedCell[Dst] = ScratchCell[i];
	}
}

void FFlockSpatialHash::Reset()
{
	SlotStart.Reset();
	SlotCount.Reset();
	SortedIndex.Reset();
	SortedPos.Reset();
	SortedCell.Reset();
	SlotMask = 0;
}

bool FFlockSpatialHash::ValidateQuery(const FVector3f& Center, float Radius, int32 IgnoreIndex, FString* OutError) const
{
	TArray<int32> FromHash;
	QueryRadius(Center, Radius, [&FromHash](int32 AgentIndex, const FVector3f&, float)
	{
		FromHash.Add(AgentIndex);
		return true;
	}, IgnoreIndex);

	// brute force with the same distance test as the query
	const float RadiusSq = Radius * Radius;
	TArray<int32> FromBrute;
	for (int32 Entry = 0; Entry < SortedIndex.Num(); ++Entry)
	{
		if (SortedIndex[Entry] != IgnoreIndex && (SortedPos[Entry] - Center).SizeSquared() <= RadiusSq)
		{
			FromBrute.Add(SortedIndex[Entry]);
		}
	}

	FromHash.Sort();
	FromBrute.Sort();

	for (int32 i = 1; i < FromHash.Num(); ++i)
	{
		if (FromHash[i] == FromHash[i - 1])
		{
			if (OutError)
			{
				*OutError = FString::Printf(TEXT("Agent %d visited twice (center %s, radius %.3f)"), FromHash[i], *Center.ToString(), Radius);
			}
			return false;
		}
	}

	if (FromHash != FromBrute)
	{
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("Hash found %d agents, brute force %d (center %s, radius %.3f)"),
				FromHash.Num(), FromBrute.Num(), *Center.ToString(), Radius);
		}
		return false;
	}

	return true;
}

void FFlockSpatialHash::GetOccupiedCells(TArray<TPair<FIntVector3, int32>>& OutCells) const
{
	TMap<FIntVector3, int32> Counts;
	for (const FIntVector3& Cell : SortedCell)
	{
		++Counts.FindOrAdd(Cell);
	}

	OutCells.Reset(Counts.Num());
	for (const TPair<FIntVector3, int32>& Pair : Counts)
	{
		OutCells.Add(Pair);
	}
}
