// Copyright OSDS contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Live/OsdsAerialEye.h"

/** The scan eye's cache file reads back exactly, and its channel names follow the scan contract. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsAerialEyeCacheTest, "OSDS.AI.AerialEye.Cache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOsdsAerialEyeCacheTest::RunTest(const FString& Parameters)
{
	const TArray<FName>& Names = FOsdsAerialEye::ChannelNames();
	TestEqual(TEXT("12 classes"), Names.Num(), 12);
	TestTrue(TEXT("contract names"), Names.Contains(FName(TEXT("road_prob"))) && Names.Contains(FName(TEXT("building_prob")))
		&& Names.Contains(FName(TEXT("water_prob"))) && Names.Contains(FName(TEXT("canopy_prob"))));
	const FOsdsAerialEye& Eye = FOsdsAerialEye::Get();
	TestTrue(TEXT("weights licence and OSM credit"), Eye.Licence().Contains(TEXT("MIT")) && Eye.Licence().Contains(TEXT("OpenStreetMap")));
	TestTrue(TEXT("training data recorded"), Eye.TrainingDataLicence().Contains(TEXT("WorldCover")));

	const int32 Size = FOsdsAerialEye::Cells + 2 * FOsdsAerialEye::GuardCells;
	TArray<FOsdsScanChannel> In;
	for (int32 K = 0; K < Names.Num(); ++K)
	{
		FOsdsScanChannel& C = In.AddDefaulted_GetRef();
		C.Name = Names[K];
		C.Size = Size;
		C.Confidence = 0.05f * K;
		C.Data.SetNumUninitialized(Size * Size);
		for (int32 I = 0; I < C.Data.Num(); ++I) C.Data[I] = static_cast<uint8>((I * 7 + K * 31) & 255);
	}
	TArray<uint8> Bytes;
	TArray<FOsdsClassTint> TintsIn = {FOsdsClassTint{TEXT("road_prob"), FColor(90, 92, 95, 255), 0.12f, 0.8f}};
	FOsdsAerialEye::Encode(In, TintsIn, Bytes);
	TArray<FOsdsClassTint> TintsOut;
	TArray<FOsdsScanChannel> Out;
	TestTrue(TEXT("decodes"), FOsdsAerialEye::Decode(Bytes, Out, TintsOut));
	TestEqual(TEXT("channel count"), Out.Num(), In.Num());
	bool bSame = Out.Num() == In.Num();
	for (int32 K = 0; bSame && K < In.Num(); ++K)
	{
		bSame = Out[K].Name == In[K].Name && Out[K].Size == In[K].Size && Out[K].Confidence == In[K].Confidence && Out[K].Data == In[K].Data;
	}
	TestTrue(TEXT("byte-identical channels"), bSame);
	TestTrue(TEXT("tints read back"), TintsOut.Num() == 1 && TintsOut[0].Channel == TintsIn[0].Channel && TintsOut[0].Mean == TintsIn[0].Mean && TintsOut[0].Confidence == TintsIn[0].Confidence);

	TArray<uint8> Broken = Bytes;
	Broken[0] ^= 0xFF;
	TestFalse(TEXT("a wrong magic is refused"), FOsdsAerialEye::Decode(Broken, Out, TintsOut));
	Broken = Bytes;
	Broken.SetNum(Bytes.Num() / 2);
	AddExpectedMessage(TEXT("Failed to uncompress memory"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("a cut file is refused"), FOsdsAerialEye::Decode(Broken, Out, TintsOut));
	return true;
}

/** The eye's input covers the tile and its guard border, cut from the right quarter of the zoom-14 photo. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsAerialEyeInputsTest, "OSDS.AI.AerialEye.Inputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOsdsAerialEyeInputsTest::RunTest(const FString& Parameters)
{
	// A 1024 px photo whose 16 quarters each have their own colour (red = column, green = row).
	const int32 N = 1024;
	TArray<FColor> Photo;
	Photo.SetNumUninitialized(N * N);
	for (int32 Y = 0; Y < N; ++Y) for (int32 X = 0; X < N; ++X) Photo[Y * N + X] = FColor(40 + 60 * (X / 256), 40 + 60 * (Y / 256), 0, 255);
	FOsdsScanInputs In;
	FOsdsAerialEye::InputsFromPhoto(Photo, N, FIntPoint(4 * 100 + 2, 4 * 200 + 1), In);
	TestEqual(TEXT("input size"), In.AerialSize, FOsdsAerialEye::InputSize);
	TestEqual(TEXT("tile"), In.TileZXY, FIntVector(16, 402, 801));
	const FColor Centre = In.AerialRGB[(In.AerialSize / 2) * In.AerialSize + In.AerialSize / 2];
	TestEqual(TEXT("centre from column 2"), int32(Centre.R), 40 + 60 * 2);
	TestEqual(TEXT("centre from row 1"), int32(Centre.G), 40 + 60 * 1);
	// The guard border reaches into the neighbouring quarter on the left (column 1).
	const FColor Left = In.AerialRGB[(In.AerialSize / 2) * In.AerialSize + 1];
	TestEqual(TEXT("guard reaches the west neighbour"), int32(Left.R), 40 + 60 * 1);
	// At the photo's own edge the guard mirrors the photo (tile in column 0: the guard shows column 0 again).
	FOsdsAerialEye::InputsFromPhoto(Photo, N, FIntPoint(4 * 100, 4 * 200), In);
	TestEqual(TEXT("mirrored at the photo edge"), int32(In.AerialRGB[(In.AerialSize / 2) * In.AerialSize].R), 40);

	// The student runs only on the lower 48 states' photographs.
	TestTrue(TEXT("Ankeny, Iowa is in the lower 48"), FOsdsAerialEye::IsLower48(FIntVector(16, 15728, 24393)));
	TestFalse(TEXT("Hilo, Hawaii is not"), FOsdsAerialEye::IsLower48(FIntVector(16, 4535, 29104)));
	TestFalse(TEXT("Anchorage, Alaska is not"), FOsdsAerialEye::IsLower48(FIntVector(16, 5476, 19225)));

	// Tints: a class sure everywhere takes the photo's colour; a class sure nowhere gives no tint.
	const int32 Size = FOsdsAerialEye::Cells + 2 * FOsdsAerialEye::GuardCells;
	TArray<FOsdsScanChannel> Channels;
	Channels.Add(FOsdsScanChannel{TEXT("road_prob"), Size, TArray<uint8>(), 1.f});
	Channels[0].Data.Init(255, Size * Size);
	Channels.Add(FOsdsScanChannel{TEXT("water_prob"), Size, TArray<uint8>(), 1.f});
	Channels[1].Data.Init(10, Size * Size);
	TArray<FOsdsClassTint> Tints;
	FOsdsAerialEye::ComputeTints(In, Channels, Tints);
	TestEqual(TEXT("one tint"), Tints.Num(), 1);
	if (Tints.Num() == 1)
	{
		TestEqual(TEXT("road tint is the photo's colour"), int32(Tints[0].Mean.R), 40);
		TestTrue(TEXT("full share and confidence"), Tints[0].Share > 0.99f && Tints[0].Confidence > 0.99f);
	}
	return true;
}

/**
 * The clean photo albedo: shadows inside a natural class are replaced by the class's own nearby pixels,
 * the guard band comes from the neighbour's photograph, and two neighbouring tiles agree along their
 * shared edge (no seam at z16 borders: the world lane's condition).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsAerialEyeCleanPhotoTest, "OSDS.AI.AerialEye.CleanPhotoSeams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOsdsAerialEyeCleanPhotoTest::RunTest(const FString& Parameters)
{
	const int32 N = FOsdsAerialEye::PhotoCells, S = FOsdsAerialEye::PhotoStored, G = FOsdsAerialEye::PhotoGuard;
	// One continuous "world" photograph: grass with fine noise, and dark tree shadows, one across the A|B edge.
	auto World = [](int64 X, int64 Y) -> FColor
	{
		const uint32 H = GetTypeHash(FIntPoint(int32(X), int32(Y)));
		const int32 Noise = int32(H % 21) - 10;
		const FVector2D Shadows[3] = {FVector2D(100.0 * 1024 + 476.0, 200.0 * 1024 + 300.0), FVector2D(101.0 * 1024 - 2.0, 200.0 * 1024 + 600.0), FVector2D(102.0 * 1024 + 400.0, 200.0 * 1024 + 500.0)};
		for (const FVector2D& C : Shadows)
		{
			if (FVector2D::DistSquared(FVector2D(double(X), double(Y)), C) < 12.0 * 12.0) return FColor(20 + Noise / 4, 35 + Noise / 4, 18, 255);
		}
		return FColor(uint8(70 + Noise), uint8(130 + Noise), uint8(55 + Noise), 255);
	};
	auto PhotoOf = [&](const FIntPoint& T, TArray<FColor>& Out)
	{
		Out.SetNumUninitialized(N * N);
		for (int32 Y = 0; Y < N; ++Y) for (int32 X = 0; X < N; ++X) Out[Y * N + X] = World(int64(T.X) * N + X, int64(T.Y) * N + Y);
		return true;
	};
	// The eye says "grass, sure" everywhere.
	const int32 L = FOsdsAerialEye::Cells + 2 * FOsdsAerialEye::GuardCells;
	TArray<FOsdsScanChannel> Land;
	for (int32 K = 0; K < 12; ++K)
	{
		FOsdsScanChannel& C = Land.AddDefaulted_GetRef();
		C.Name = FOsdsAerialEye::ChannelNames()[K];
		C.Size = L;
		C.Data.Init(K == 5 ? 255 : 0, L * L);
	}
	const FIntPoint A(100, 200), B(101, 200);
	TArray<FColor> MA, MB;
	TestTrue(TEXT("mosaic A"), FOsdsAerialEye::BuildMosaic(A, PhotoOf, MA));
	TestTrue(TEXT("mosaic B"), FOsdsAerialEye::BuildMosaic(B, PhotoOf, MB));
	TestEqual(TEXT("mosaic size"), MA.Num(), S * S);
	TestTrue(TEXT("A's east guard is B's photo"), MA[(G + 10) * S + G + N + 3] == World(int64(B.X) * N + 3, int64(B.Y) * N + 10));
	TArray<FOsdsScanChannel> CA, CB;
	int32 CleanedA = 0, CleanedB = 0;
	FOsdsAerialEye::CleanPhoto(MA, A, Land, CA, &CleanedA);
	FOsdsAerialEye::CleanPhoto(MB, B, Land, CB, &CleanedB);
	TestEqual(TEXT("four albedo channels"), CA.Num(), 4);
	TestTrue(TEXT("shadows were cleaned"), CleanedA > 200 && CleanedB > 200);
	if (CA.Num() == 4 && CB.Num() == 4)
	{
		// A shadow pixel inside A became grass-bright.
		const int32 In = (G + 300) * S + (G + 476);
		TestTrue(TEXT("the shadow is now grass"), CA[1].Data[In] > 100);
		// Seam: A's last interior column against B's first, cleaned vs the photo itself.
		double Clean = 0.0, Raw = 0.0;
		int32 Same = 0, Count = 0;
		for (int32 Y = G; Y < G + N; ++Y)
		{
			const int32 IA = Y * S + G + N - 1, IB = Y * S + G;
			for (int32 Ch = 0; Ch < 3; ++Ch)
			{
				Clean += FMath::Abs(int32(CA[Ch].Data[IA]) - int32(CB[Ch].Data[IB]));
			}
			Raw += FMath::Abs(int32(MA[IA].R) - int32(MB[IB].R)) + FMath::Abs(int32(MA[IA].G) - int32(MB[IB].G)) + FMath::Abs(int32(MA[IA].B) - int32(MB[IB].B));
			// The same ground pixel seen by both tiles (A's guard, B's interior) gets the same colour.
			for (int32 X = 0; X < G; ++X)
			{
				const int32 GA = Y * S + G + N + X, GB = Y * S + G + X;
				Same += (CA[0].Data[GA] == CB[0].Data[GB] && CA[1].Data[GA] == CB[1].Data[GB] && CA[2].Data[GA] == CB[2].Data[GB]) ? 1 : 0;
				++Count;
			}
		}
		Clean /= 3.0 * N;
		Raw /= 3.0 * N;
		AddInfo(FString::Printf(TEXT("edge difference: clean %.2f, photo %.2f; same colour in the shared band %.1f %%"), Clean, Raw, 100.0 * Same / FMath::Max(1, Count)));
		TestTrue(TEXT("no seam: the cleaned edge differs no more than the photo's own edge (+2)"), Clean <= Raw + 2.0);
		TestTrue(TEXT("the shared band agrees (>= 98 %)"), Same >= 0.98 * Count);
	}
	return true;
}

/**
 * The eye's back-off reads the frame time on its worker thread without touching FApp's game-thread
 * time (that raised an ensure, "no inherited time context", in game runs).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsAerialEyeBackOffTest, "OSDS.AI.AerialEye.BackOffOffGameThread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOsdsAerialEyeBackOffTest::RunTest(const FString& Parameters)
{
	FOsdsAerialEye::NoteFrameSeconds(1.f / 30.f);
	TFuture<bool> Slow = Async(EAsyncExecution::Thread, []() { return FOsdsAerialEye::IsGameSlow(); });
	TestTrue(TEXT("a 30 fps frame is slow, read on a worker"), Slow.Get());
	FOsdsAerialEye::NoteFrameSeconds(1.f / 90.f);
	TFuture<bool> Fast = Async(EAsyncExecution::Thread, []() { return FOsdsAerialEye::IsGameSlow(); });
	TestFalse(TEXT("a 90 fps frame is not slow"), Fast.Get());
	FOsdsAerialEye::NoteFrameSeconds(0.f);
	return true;
}

#endif
