// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlockSpatialHash.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FlockSpatialHashTests
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Hash query vs brute force, logs an error on the test when they differ */
	bool CheckQuery(FAutomationTestBase& Test, const FFlockSpatialHash& Hash, const FVector3f& Center, float Radius,
		int32 IgnoreIndex, const TCHAR* Context)
	{
		FString Error;
		if (!Hash.ValidateQuery(Center, Radius, IgnoreIndex, &Error))
		{
			Test.AddError(FString::Printf(TEXT("[%s] %s"), Context, *Error));
			return false;
		}
		return true;
	}

	TArray<FVector3f> RandomPoints(FRandomStream& Stream, int32 Num, float Extent)
	{
		TArray<FVector3f> Points;
		Points.SetNumUninitialized(Num);
		for (FVector3f& P : Points)
		{
			P = FVector3f(Stream.FRandRange(-Extent, Extent), Stream.FRandRange(-Extent, Extent), Stream.FRandRange(-Extent, Extent));
		}
		return Points;
	}
}

using namespace FlockSpatialHashTests;

// random clouds at several scales and cell sizes, random radii
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlockHashRandomTest, "Project.Flocking.SpatialHash.RandomVsBruteForce", TestFlags)
bool FFlockHashRandomTest::RunTest(const FString& Parameters)
{
	const int32 Counts[] = { 0, 1, 10, 1000 };
	const float Extents[] = { 1000.f, 10000.f, 1000000.f };
	const float CellSizes[] = { 50.f, 300.f, 1000.f };

	FRandomStream Stream(1337);
	FFlockSpatialHash Hash;
	int32 Failures = 0;

	for (int32 Count : Counts)
	{
		for (float Extent : Extents)
		{
			for (float CellSize : CellSizes)
			{
				const TArray<FVector3f> Points = RandomPoints(Stream, Count, Extent);
				Hash.Build(Points, CellSize);

				for (int32 Q = 0; Q < 200 && Failures < 10; ++Q)
				{
					const float Radius = Stream.FRandRange(0.f, CellSize * 3.f);
					// half the queries from an agent (ignoring itself), half from random points
					if (Count > 0 && (Q & 1))
					{
						const int32 Self = Stream.RandRange(0, Count - 1);
						Failures += !CheckQuery(*this, Hash, Points[Self], Radius, Self, TEXT("Random agent"));
					}
					else
					{
						const FVector3f Center(Stream.FRandRange(-Extent, Extent), Stream.FRandRange(-Extent, Extent), Stream.FRandRange(-Extent, Extent));
						Failures += !CheckQuery(*this, Hash, Center, Radius, INDEX_NONE, TEXT("Random point"));
					}
				}
			}
		}
	}

	return Failures == 0;
}

// points on cell borders, negative coordinates, radius equal to the distance
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlockHashBordersTest, "Project.Flocking.SpatialHash.BordersAndNegatives", TestFlags)
bool FFlockHashBordersTest::RunTest(const FString& Parameters)
{
	const float CellSize = 100.f;
	TArray<FVector3f> Points;
	for (int32 Z = -6; Z <= 6; ++Z)
	{
		for (int32 Y = -6; Y <= 6; ++Y)
		{
			for (int32 X = -6; X <= 6; ++X)
			{
				// half-cell steps: every other point sits exactly on a cell border
				Points.Add(FVector3f(X, Y, Z) * (CellSize * 0.5f));
			}
		}
	}

	FFlockSpatialHash Hash;
	Hash.Build(Points, CellSize);

	const float Radii[] = { 0.f, 50.f, 99.999f, 100.f, 100.001f, 150.f, 250.f };
	int32 Failures = 0;
	for (int32 i = 0; i < Points.Num() && Failures < 10; i += 7)
	{
		for (float Radius : Radii)
		{
			Failures += !CheckQuery(*this, Hash, Points[i], Radius, i, TEXT("Grid agent"));
			Failures += !CheckQuery(*this, Hash, Points[i], Radius, INDEX_NONE, TEXT("Grid agent, self included"));
		}
	}

	// an agent exactly at distance R must be found
	int32 Found = 0;
	Hash.QueryRadius(FVector3f::ZeroVector, CellSize, [&Found](int32, const FVector3f& Pos, float)
	{
		Found += FMath::IsNearlyEqual(Pos.Size(), 100.f) ? 1 : 0;
		return true;
	});
	TestEqual(TEXT("Agents exactly at the radius are included (6 axis neighbors)"), Found, 6);

	// radius 0 only finds coincident agents
	int32 FoundAtZero = 0;
	Hash.QueryRadius(FVector3f(50.f, 0.f, 0.f), 0.f, [&FoundAtZero](int32, const FVector3f&, float)
	{
		++FoundAtZero;
		return true;
	});
	TestEqual(TEXT("Radius 0 finds only the agent on the exact spot"), FoundAtZero, 1);

	return Failures == 0;
}

// tiny tables: every cell collides with others in the same slot
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlockHashCollisionsTest, "Project.Flocking.SpatialHash.ForcedCollisions", TestFlags)
bool FFlockHashCollisionsTest::RunTest(const FString& Parameters)
{
	FRandomStream Stream(42);
	const TArray<FVector3f> Points = RandomPoints(Stream, 500, 2000.f);

	FFlockSpatialHash Hash;
	int32 Failures = 0;
	for (int32 TableSize : { 1, 2, 8 })
	{
		Hash.Build(Points, 200.f, TableSize);
		TestEqual(TEXT("Table size override applied"), Hash.GetTableSize(), int32(FMath::RoundUpToPowerOfTwo(uint32(TableSize))));

		for (int32 Q = 0; Q < 200 && Failures < 10; ++Q)
		{
			const int32 Self = Stream.RandRange(0, Points.Num() - 1);
			Failures += !CheckQuery(*this, Hash, Points[Self], Stream.FRandRange(0.f, 600.f), Self, TEXT("Forced collisions"));
		}
	}
	return Failures == 0;
}

// everyone in one cell, and a radius so large the query scans linearly
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlockHashDegenerateTest, "Project.Flocking.SpatialHash.DegenerateCases", TestFlags)
bool FFlockHashDegenerateTest::RunTest(const FString& Parameters)
{
	FRandomStream Stream(7);
	FFlockSpatialHash Hash;
	int32 Failures = 0;

	// packed cluster
	TArray<FVector3f> Cluster = RandomPoints(Stream, 500, 5.f);
	Hash.Build(Cluster, 300.f);
	for (float Radius : { 0.5f, 3.f, 1000.f })
	{
		Failures += !CheckQuery(*this, Hash, Cluster[0], Radius, 0, TEXT("Cluster"));
	}

	// huge radius against small cells
	const TArray<FVector3f> Spread = RandomPoints(Stream, 300, 5000.f);
	Hash.Build(Spread, 10.f);
	Failures += !CheckQuery(*this, Hash, FVector3f::ZeroVector, 100000.f, INDEX_NONE, TEXT("Linear scan, everything"));
	Failures += !CheckQuery(*this, Hash, Spread[3], 2500.f, 3, TEXT("Linear scan, partial"));

	// empty hash and reset
	Hash.Build(TArray<FVector3f>(), 100.f);
	Failures += !CheckQuery(*this, Hash, FVector3f::ZeroVector, 1000.f, INDEX_NONE, TEXT("Empty"));
	Hash.Build(Spread, 100.f);
	Hash.Reset();
	TestEqual(TEXT("Reset empties the hash"), Hash.Num(), 0);
	Failures += !CheckQuery(*this, Hash, FVector3f::ZeroVector, 1000.f, INDEX_NONE, TEXT("After reset"));

	// negative radius finds nothing
	Hash.Build(Spread, 100.f);
	bool bVisited = false;
	Hash.QueryRadius(FVector3f::ZeroVector, -5.f, [&bVisited](int32, const FVector3f&, float) { bVisited = true; return true; });
	TestFalse(TEXT("Negative radius visits nothing"), bVisited);

	return Failures == 0;
}

// rebuilding with a different agent count must not leave stale entries behind
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlockHashRebuildTest, "Project.Flocking.SpatialHash.Rebuild", TestFlags)
bool FFlockHashRebuildTest::RunTest(const FString& Parameters)
{
	FRandomStream Stream(99);
	FFlockSpatialHash Hash;
	int32 Failures = 0;

	for (int32 Count : { 1000, 10, 2000, 0, 37 })
	{
		const TArray<FVector3f> Points = RandomPoints(Stream, Count, 3000.f);
		Hash.Build(Points, 300.f);
		TestEqual(TEXT("Agent count after rebuild"), Hash.Num(), Count);

		for (int32 Q = 0; Q < 50 && Failures < 10; ++Q)
		{
			const FVector3f Center(Stream.FRandRange(-3000.f, 3000.f), Stream.FRandRange(-3000.f, 3000.f), Stream.FRandRange(-3000.f, 3000.f));
			Failures += !CheckQuery(*this, Hash, Center, 900.f, INDEX_NONE, TEXT("Rebuild"));
		}

		// no index from a previous, larger build may show up
		Hash.QueryRadius(FVector3f::ZeroVector, 100000.f, [&](int32 AgentIndex, const FVector3f&, float)
		{
			if (AgentIndex >= Count)
			{
				AddError(FString::Printf(TEXT("Stale agent index %d with only %d agents"), AgentIndex, Count));
				++Failures;
			}
			return true;
		});
	}
	return Failures == 0;
}

// the center cell is visited first, returning false stops the query
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlockHashOrderTest, "Project.Flocking.SpatialHash.OrderAndEarlyStop", TestFlags)
bool FFlockHashOrderTest::RunTest(const FString& Parameters)
{
	// index 0 lives in the cell left of the center, index 1 in the center cell
	TArray<FVector3f> Points = { FVector3f(-50.f, 50.f, 50.f), FVector3f(60.f, 50.f, 50.f), FVector3f(150.f, 50.f, 50.f) };
	FFlockSpatialHash Hash;
	Hash.Build(Points, 100.f);

	TArray<int32> Order;
	Hash.QueryRadius(FVector3f(50.f, 50.f, 50.f), 150.f, [&Order](int32 AgentIndex, const FVector3f&, float)
	{
		Order.Add(AgentIndex);
		return true;
	});
	TestEqual(TEXT("All three found"), Order.Num(), 3);
	TestTrue(TEXT("Center cell agent comes first"), Order.Num() > 0 && Order[0] == 1);

	int32 Visits = 0;
	Hash.QueryRadius(FVector3f(50.f, 50.f, 50.f), 150.f, [&Visits](int32, const FVector3f&, float)
	{
		++Visits;
		return false;
	});
	TestEqual(TEXT("Returning false stops after one visit"), Visits, 1);

	// reported distance matches the real one
	Hash.QueryRadius(FVector3f::ZeroVector, 1000.f, [&](int32 AgentIndex, const FVector3f& Pos, float DistSq)
	{
		TestTrue(TEXT("Reported position"), Pos == Points[AgentIndex]);
		TestTrue(TEXT("Reported DistSq"), FMath::IsNearlyEqual(DistSq, Points[AgentIndex].SizeSquared(), 0.01f));
		return true;
	});

	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
