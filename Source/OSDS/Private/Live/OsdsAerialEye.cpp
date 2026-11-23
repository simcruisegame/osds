// Copyright OSDS contributors. All Rights Reserved.

#include "Live/OsdsAerialEye.h"
#include "World/OsdsWorldCache.h"

#include "Async/Async.h"
#include <atomic>
#include "Misc/App.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Compression.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Atlas/OsdsAtlasClient.h"
#include "World/OsdsWorldResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsAerialEye, Log, All);

static TAutoConsoleVariable<int32> CVarOsdsEyePhoto16(TEXT("osds.Eye.Photo16"), 0,
	TEXT("1: the eye fetches each tile's zoom-16 photograph through the helper and writes the clean photo albedo (PHTX). Off until the renderer reads it (no fetches nobody sees)."));
static TAutoConsoleVariable<int32> CVarOsdsEyeDump(TEXT("osds.Eye.Dump"), 0,
	TEXT("1: the aerial eye also writes photo | classes PNGs of each tile it reads to Saved/Scan/eyes/dump."));

class FOsdsAerialEyeModel
{
public:
	TStrongObjectPtr<UNNEModelData> Data;
	TSharedPtr<UE::NNE::IModelCPU> CpuModel;
	TSharedPtr<UE::NNE::IModelInstanceCPU> Cpu;
	TArray<float> Input;
	TArray<float> Output;
	int32 NumClasses = 0;
	/** The last decoded zoom-14 photograph: 16 tiles share one. Worker only. */
	FString PhotoPath;
	TArray<FColor> Photo;
	int32 PhotoSize = 0;
};

namespace OsdsAerialEyeLocal
{
	const uint32 Magic = 0x32455953; // "SYE2"
	/** A cell is "sure" of a class at this probability. */
	const uint8 SureLevel = 153; // 0.6 x 255
	/** Stops a helper fetch at process exit (defined with the photo-true albedo below). */
	std::atomic<bool>& Cancel();

	/** Class colours of the dump pictures (lab/scan/scan_data.py PALETTE). */
	const FColor Palette[12] = {
		FColor(60, 60, 60), FColor(150, 120, 80), FColor(200, 60, 60), FColor(40, 90, 200), FColor(20, 110, 30), FColor(120, 200, 90),
		FColor(220, 210, 90), FColor(140, 150, 60), FColor(210, 180, 140), FColor(170, 170, 190), FColor(80, 160, 160), FColor(250, 250, 250)};

	/** Bilinear sample of an image at (U, V) in pixels (pixel centres at .5), clamped at the edges. */
	FLinearColor Sample(const TArray<FColor>& Img, int32 W, int32 H, double U, double V)
	{
		const double X = FMath::Clamp(U - 0.5, 0.0, W - 1.0), Y = FMath::Clamp(V - 0.5, 0.0, H - 1.0);
		const int32 X0 = FMath::FloorToInt32(X), Y0 = FMath::FloorToInt32(Y);
		const int32 X1 = FMath::Min(X0 + 1, W - 1), Y1 = FMath::Min(Y0 + 1, H - 1);
		const float Fx = static_cast<float>(X - X0), Fy = static_cast<float>(Y - Y0);
		auto C = [&](int32 Px, int32 Py) { const FColor& P = Img[Py * W + Px]; return FLinearColor(P.R, P.G, P.B, 0.f); };
		return FMath::Lerp(FMath::Lerp(C(X0, Y0), C(X1, Y0), Fx), FMath::Lerp(C(X0, Y1), C(X1, Y1), Fx), Fy);
	}

	/** Bilinear sample of one float plane (N x N) at (U, V) in its pixels. */
	float SamplePlane(const float* P, int32 N, double U, double V)
	{
		const double X = FMath::Clamp(U - 0.5, 0.0, N - 1.0), Y = FMath::Clamp(V - 0.5, 0.0, N - 1.0);
		const int32 X0 = FMath::FloorToInt32(X), Y0 = FMath::FloorToInt32(Y);
		const int32 X1 = FMath::Min(X0 + 1, N - 1), Y1 = FMath::Min(Y0 + 1, N - 1);
		const float Fx = static_cast<float>(X - X0), Fy = static_cast<float>(Y - Y0);
		return FMath::Lerp(FMath::Lerp(P[Y0 * N + X0], P[Y0 * N + X1], Fx), FMath::Lerp(P[Y1 * N + X0], P[Y1 * N + X1], Fx), Fy);
	}
}

static std::atomic<float> GOsdsEyeFrameSeconds{0.f};

void FOsdsAerialEye::NoteFrameSeconds(float Seconds)
{
	GOsdsEyeFrameSeconds.store(Seconds, std::memory_order_relaxed);
}

bool FOsdsAerialEye::IsGameSlow()
{
	return GOsdsEyeFrameSeconds.load(std::memory_order_relaxed) > 1.1f / 60.f;
}

bool FOsdsAerialEye::IsLower48(const FIntVector& TileZXY)
{
	const double N = FMath::Pow(2.0, TileZXY.X);
	const double Lon = (TileZXY.Y + 0.5) / N * 360.0 - 180.0;
	const double Lat = FMath::RadiansToDegrees(FMath::Atan(FMath::Sinh(PI * (1.0 - 2.0 * (TileZXY.Z + 0.5) / N))));
	return Lon > -125.5 && Lon < -66.5 && Lat > 24.0 && Lat < 49.5;
}

FOsdsAerialEye& FOsdsAerialEye::Get()
{
	static FOsdsAerialEye* Instance = new FOsdsAerialEye();
	return *Instance;
}

const TArray<FName>& FOsdsAerialEye::ChannelNames()
{
	// Order of the model's classes (lab/scan/scan_data.py CLASSES); names from the scan contract.
	static const TArray<FName> Names = {
		TEXT("road_prob"),                    // paved road
		TEXT("surface_logits_dirt_track"),    // unpaved road or track
		TEXT("building_prob"),                // roof
		TEXT("water_prob"),
		TEXT("canopy_prob"),                  // tree canopy
		TEXT("surface_logits_grass_short"),
		TEXT("surface_logits_crop"),
		TEXT("surface_logits_shrub"),
		TEXT("surface_logits_bare_soil"),
		TEXT("surface_logits_concrete"),      // other built ground: parking, yards, plazas
		TEXT("surface_logits_wetland"),
		TEXT("surface_logits_snow_and_ice"),
	};
	return Names;
}

FString FOsdsAerialEye::Licence() const
{
	return bStudent
		? TEXT("OSDS aerial student (MIT, OSDS contributors), which contains the first two stages of the Satlas Aerial_SwinB_SI backbone (Allen Institute for AI, ODC-BY 1.0). Contains information from OpenStreetMap, (c) OpenStreetMap contributors, ODbL.")
		: TEXT("Satlas Aerial_SwinB_SI backbone (Allen Institute for AI, ODC-BY 1.0) + OSDS head (MIT). Contains information from OpenStreetMap, (c) OpenStreetMap contributors, ODbL.");
}

FString FOsdsAerialEye::TrainingDataLicence() const
{
	FString Ours = TEXT("USGS The National Map aerial imagery (public domain); OpenStreetMap via OpenFreeMap (ODbL); Meta and WRI canopy height map v2 (CC BY 4.0); ESA WorldCover 2021 v200 (CC BY 4.0)");
	return bStudent
		? Ours + TEXT("; soft labels of the satlas_landcover eye on 2,100 more USGS aerial tiles; its first two stages by the Satlas makers on SatlasPretrain (NAIP public domain, OpenStreetMap and Microsoft Buildings ODbL, ESA WorldCover CC BY 4.0, own annotations ODC-BY) from an ImageNet start")
		: Ours + TEXT(" for the head; the Satlas backbone by its makers on SatlasPretrain (NAIP public domain, OpenStreetMap and Microsoft Buildings ODbL, ESA WorldCover CC BY 4.0, own annotations ODC-BY) from an ImageNet start");
}

bool FOsdsAerialEye::Initialize()
{
	check(IsInGameThread());
	if (bTried)
	{
		return bReady;
	}
	bTried = true;
	FString Choice;
	// On by default (owner's criterion 2026-11-14: the whole game looks real): the shipped student eye.
	// -OsdsScanEye=none or -OsdsNoEye turns it off; =satlas_landcover picks the development teacher.
	Choice = TEXT("aerial_student");
	FParse::Value(FCommandLine::Get(), TEXT("OsdsScanEye="), Choice);
	if (Choice.IsEmpty() || Choice.Equals(TEXT("none"), ESearchCase::IgnoreCase) || FParse::Param(FCommandLine::Get(), TEXT("OsdsNoEye")))
	{
		UE_LOG(LogOsdsAerialEye, Display, TEXT("Scan eye off"));
		return false;
	}
	if (Choice.Equals(TEXT("satlas_landcover"), ESearchCase::IgnoreCase))
	{
		bStudent = false;
		EyeName = TEXT("satlas_landcover");
		ModelPath = FPaths::ProjectSavedDir() / TEXT("AI/scan-eye-satlas.onnx");
	}
	else
	{
		bStudent = true;
		EyeName = TEXT("aerial_student");
		ModelPath = FPaths::ProjectDir() / TEXT("data/ai/scan-eye-student.onnx");
	}
	TArray64<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *ModelPath, FILEREAD_Silent))
	{
		UE_LOG(LogOsdsAerialEye, Display, TEXT("Scan eye %s off: no %s"), *EyeName.ToString(), *ModelPath);
		return false;
	}
	TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeORTCpu"));
	if (!Runtime.IsValid())
	{
		UE_LOG(LogOsdsAerialEye, Display, TEXT("Scan eye off: no NNERuntimeORTCpu"));
		return false;
	}
	TSharedPtr<FOsdsAerialEyeModel> M = MakeShared<FOsdsAerialEyeModel>();
	M->Data.Reset(NewObject<UNNEModelData>(GetTransientPackage(), NAME_None, RF_Transient));
	M->Data->Init(TEXT("onnx"), Bytes);
	if (Runtime->CanCreateModelCPU(M->Data.Get()) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogOsdsAerialEye, Warning, TEXT("Scan eye: the CPU runtime cannot load %s"), *ModelPath);
		return false;
	}
	M->CpuModel = Runtime->CreateModelCPU(M->Data.Get());
	M->Cpu = M->CpuModel.IsValid() ? M->CpuModel->CreateModelInstanceCPU() : nullptr;
	const uint32 Shape[] = {1, 3, InputSize, InputSize};
	if (!M->Cpu.IsValid() || M->Cpu->SetInputTensorShapes({UE::NNE::FTensorShape::Make(Shape)}) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogOsdsAerialEye, Warning, TEXT("Scan eye: the model refused a 1x3x%dx%d input"), InputSize, InputSize);
		return false;
	}
	const TConstArrayView<UE::NNE::FTensorShape> Shapes = M->Cpu->GetOutputTensorShapes();
	if (Shapes.Num() != 1 || Shapes[0].Volume() % (uint64(InputSize) * InputSize) != 0)
	{
		UE_LOG(LogOsdsAerialEye, Warning, TEXT("Scan eye: unexpected outputs in %s"), *ModelPath);
		return false;
	}
	M->NumClasses = static_cast<int32>(Shapes[0].Volume() / (uint64(InputSize) * InputSize));
	if (M->NumClasses != ChannelNames().Num())
	{
		UE_LOG(LogOsdsAerialEye, Warning, TEXT("Scan eye: %d classes, expected %d"), M->NumClasses, ChannelNames().Num());
		return false;
	}
	M->Input.SetNumZeroed(3 * InputSize * InputSize);
	M->Output.SetNumZeroed(static_cast<int32>(Shapes[0].Volume()));
	// Workers decode and encode pictures; the module must be loaded on the game thread.
	FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	Model = M;
	bReady = true;
	FCoreDelegates::OnPreExit.AddLambda([]() { Get().bStop = true; OsdsAerialEyeLocal::Cancel() = true; });
	// The game thread hands its frame time to the worker (FApp's time may only be read on the game thread).
	FCoreDelegates::OnEndFrame.AddLambda([]() { NoteFrameSeconds(static_cast<float>(FApp::GetDeltaTime())); });
	UE_LOG(LogOsdsAerialEye, Display, TEXT("Scan eye ready: %s v%d, %s (%d classes) on NNERuntimeORTCpu"), *EyeName.ToString(), EyeVersion, *ModelPath, M->NumClasses);
	return true;
}

// The eye's classes of a Tile are a World cache layer (ADR 0005), one per eye model; the generator
// version is the eye's own version and the file format's. Older Saved/Scan/eyes files are not read.
namespace
{
	FName EyeLayer(FName EyeName) { return FName(*FString::Printf(TEXT("eye-%s"), *EyeName.ToString())); }
}

bool FOsdsAerialEye::LoadCached(const FIntPoint& Tile16, TArray<uint8>& OutBytes) const
{
	return FOsdsWorldCache::Get().Load(Tile16, EyeLayer(EyeName), EyeVersion * 100 + FileVersion, OutBytes);
}

bool FOsdsAerialEye::StoreCached(const FIntPoint& Tile16, const TArray<uint8>& Bytes) const
{
	return FOsdsWorldCache::Get().Store(Tile16, EyeLayer(EyeName), EyeVersion * 100 + FileVersion, Bytes);
}

void FOsdsAerialEye::InputsFromPhoto(const TArray<FColor>& Photo, int32 PhotoSize, const FIntPoint& Tile16, FOsdsScanInputs& Out)
{
	using namespace OsdsAerialEyeLocal;
	Out.TileZXY = FIntVector(16, Tile16.X, Tile16.Y);
	Out.Cells = Cells;
	Out.GuardCells = GuardCells;
	const double Tile = PhotoSize / 4.0;                  // photo pixels per tile
	const double Guard = Tile * GuardCells / Cells;       // photo pixels per guard border
	const double X0 = (((Tile16.X % 4) + 4) % 4) * Tile - Guard, Y0 = (((Tile16.Y % 4) + 4) % 4) * Tile - Guard;
	const double Step = (Tile + 2.0 * Guard) / InputSize;
	// Past the photo's edge the guard mirrors the photo: a clamped edge makes streaks the model reads as
	// roofs or roads, a mirror keeps the ground's own texture.
	auto Mirror = [PhotoSize](double U) { return U < 0.0 ? -U : (U >= PhotoSize ? 2.0 * PhotoSize - U : U); };
	Out.AerialSize = InputSize;
	Out.AerialRGB.SetNumUninitialized(InputSize * InputSize);
	for (int32 Y = 0; Y < InputSize; ++Y)
	{
		for (int32 X = 0; X < InputSize; ++X)
		{
			const FLinearColor C = Sample(Photo, PhotoSize, PhotoSize, Mirror(X0 + (X + 0.5) * Step), Mirror(Y0 + (Y + 0.5) * Step));
			Out.AerialRGB[Y * InputSize + X] = FColor(static_cast<uint8>(C.R + 0.5f), static_cast<uint8>(C.G + 0.5f), static_cast<uint8>(C.B + 0.5f), 255);
		}
	}
}

bool FOsdsAerialEye::CanRun(const FOsdsScanInputs& Inputs) const
{
	if (!bReady || Inputs.AerialSize < 64 || Inputs.AerialRGB.Num() < Inputs.AerialSize * Inputs.AerialSize || Inputs.Cells <= 0)
	{
		return false;
	}
	// The student learned on the lower 48 states' photographs; on Hawaii's and Alaska's it is wrong
	// (checked: Hilo reads as one roof). There the scan rules decide until it learns them.
	return !bStudent || IsLower48(Inputs.TileZXY);
}

bool FOsdsAerialEye::Run(const FOsdsScanInputs& Inputs, TArray<FOsdsScanChannel>& OutChannels, TArray<FOsdsScanPoint>& OutPoints)
{
	using namespace OsdsAerialEyeLocal;
	OutPoints.Reset();
	if (!CanRun(Inputs) || IsInGameThread())
	{
		return false;
	}
	FOsdsAerialEyeModel& M = *Model;
	// The photo (tile and guard) resampled to the model's input.
	const int32 Plane = InputSize * InputSize;
	const double In = static_cast<double>(Inputs.AerialSize) / InputSize;
	for (int32 Y = 0; Y < InputSize; ++Y)
	{
		for (int32 X = 0; X < InputSize; ++X)
		{
			const FLinearColor C = Inputs.AerialSize == InputSize
				? FLinearColor(Inputs.AerialRGB[Y * InputSize + X].R, Inputs.AerialRGB[Y * InputSize + X].G, Inputs.AerialRGB[Y * InputSize + X].B, 0.f)
				: Sample(Inputs.AerialRGB, Inputs.AerialSize, Inputs.AerialSize, (X + 0.5) * In, (Y + 0.5) * In);
			const int32 I = Y * InputSize + X;
			M.Input[I] = C.R / 255.f;
			M.Input[Plane + I] = C.G / 255.f;
			M.Input[2 * Plane + I] = C.B / 255.f;
		}
	}
	const UE::NNE::FTensorBindingCPU InBinding{M.Input.GetData(), static_cast<uint64>(M.Input.Num() * sizeof(float))};
	const UE::NNE::FTensorBindingCPU OutBinding{M.Output.GetData(), static_cast<uint64>(M.Output.Num() * sizeof(float))};
	if (M.Cpu->RunSync({InBinding}, {OutBinding}) != UE::NNE::EResultStatus::Ok)
	{
		return false;
	}
	// Probabilities to 0-255 channels on the cell grid (tile and guard), bilinear from the model's grid.
	const int32 Size = Inputs.Cells + 2 * Inputs.GuardCells;
	const double Scale = static_cast<double>(InputSize) / Size;
	OutChannels.Reset();
	for (int32 K = 0; K < M.NumClasses; ++K)
	{
		FOsdsScanChannel& Ch = OutChannels.AddDefaulted_GetRef();
		Ch.Name = ChannelNames()[K];
		Ch.Size = Size;
		Ch.Data.SetNumUninitialized(Size * Size);
		const float* P = M.Output.GetData() + int64(K) * Plane;
		double Decided = 0.0;
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const float V = SamplePlane(P, InputSize, (X + 0.5) * Scale, (Y + 0.5) * Scale);
				Ch.Data[Y * Size + X] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(V * 255.f), 0, 255));
				Decided += FMath::Max(V, 1.f - V);
			}
		}
		// Trust: how decided the eye is on this tile (mean of max(p, 1 - p)), 0.5..1 mapped to 0..1.
		Ch.Confidence = static_cast<float>(FMath::Clamp((Decided / (double(Size) * Size) - 0.5) * 2.0, 0.0, 1.0));
	}
	return true;
}

void FOsdsAerialEye::ComputeTints(const FOsdsScanInputs& Inputs, const TArray<FOsdsScanChannel>& Channels, TArray<FOsdsClassTint>& Out)
{
	using namespace OsdsAerialEyeLocal;
	Out.Reset();
	if (Channels.Num() == 0 || Inputs.AerialSize <= 0)
	{
		return;
	}
	const int32 Size = Channels[0].Size;
	const int32 G = FMath::Clamp(Inputs.GuardCells, 0, Size / 4);
	const double In = static_cast<double>(Inputs.AerialSize) / Size;
	const double Interior = double(Size - 2 * G) * (Size - 2 * G);
	for (const FOsdsScanChannel& Ch : Channels)
	{
		if (Ch.Size != Size)
		{
			continue;
		}
		// Linear-light mean over the sure cells of the tile's interior (sRGB averaged as light, not as numbers).
		double R = 0.0, Gr = 0.0, B = 0.0, P = 0.0;
		int64 Count = 0;
		for (int32 Y = G; Y < Size - G; ++Y)
		{
			for (int32 X = G; X < Size - G; ++X)
			{
				const uint8 V = Ch.Data[Y * Size + X];
				if (V < SureLevel)
				{
					continue;
				}
				const FColor C = Inputs.AerialRGB[FMath::Min(Inputs.AerialSize - 1, int32((Y + 0.5) * In)) * Inputs.AerialSize + FMath::Min(Inputs.AerialSize - 1, int32((X + 0.5) * In))];
				const FLinearColor L = FLinearColor::FromSRGBColor(C);
				R += L.R; Gr += L.G; B += L.B; P += V / 255.0;
				++Count;
			}
		}
		if (Count == 0)
		{
			continue;
		}
		FOsdsClassTint& T = Out.AddDefaulted_GetRef();
		T.Channel = Ch.Name;
		T.Mean = FLinearColor(float(R / Count), float(Gr / Count), float(B / Count)).ToFColorSRGB();
		T.Share = static_cast<float>(Count / Interior);
		T.Confidence = static_cast<float>(FMath::Min(1.0, T.Share / 0.02) * FMath::Clamp((P / Count - 0.5) * 2.0, 0.0, 1.0));
	}
}

// ---- Photo-true ground albedo ----------------------------------------------------------------------

namespace OsdsAerialEyeLocal
{
	FCriticalSection& AtlasLock() { static FCriticalSection* L = new FCriticalSection(); return *L; }
	FOsdsAtlasConfig*& AtlasConfig() { static FOsdsAtlasConfig* C = nullptr; return C; }
	std::atomic<bool>& Cancel() { static std::atomic<bool>* B = new std::atomic<bool>(false); return *B; }

	/** A well-mixed 64-bit hash of a global pixel and a counter (splitmix64). */
	uint64 Mix(uint64 X)
	{
		X += 0x9E3779B97F4A7C15ull;
		X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
		X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
		return X ^ (X >> 31);
	}

	int32 Luma(const FColor& C) { return (C.R * 77 + C.G * 150 + C.B * 29) >> 8; }

	/** The natural classes the clean albedo works on (model output order). */
	bool IsNatural(int32 K) { return K == 5 || K == 6 || K == 7 || K == 8; } // grass, crop, shrub, bare (tree canopy keeps its own shading)
}

void FOsdsAerialEye::SetAtlas(const FOsdsAtlasConfig& Config)
{
	using namespace OsdsAerialEyeLocal;
	FScopeLock Guard(&AtlasLock());
	if (!AtlasConfig())
	{
		AtlasConfig() = new FOsdsAtlasConfig(Config); // never destroyed, like the eye
	}
}

bool FOsdsAerialEye::Photo16(const FIntPoint& Tile16, bool bFetch, TArray<FColor>& Out)
{
	using namespace OsdsAerialEyeLocal;
	FOsdsAtlasConfig Config;
	{
		FScopeLock Guard(&AtlasLock());
		if (!AtlasConfig() || !AtlasConfig()->IsConfigured())
		{
			return false;
		}
		Config = *AtlasConfig();
	}
	const FString Path = FPaths::Combine(Config.CacheDir, FString::Printf(TEXT("a16-%d-%d.jpg"), Tile16.X, Tile16.Y));
	if (!IFileManager::Get().FileExists(*Path))
	{
		if (!bFetch || bStop)
		{
			return false;
		}
		FOsdsAtlasRequest Request;
		Request.Command = TEXT("aerial16");
		Request.Args = {FString::FromInt(Tile16.X), FString::FromInt(Tile16.Y)};
		Request.TimeoutSeconds = 60.0;
		FOsdsAtlasClient::RunBlocking(Config, Request, &Cancel());
		if (!IFileManager::Get().FileExists(*Path))
		{
			return false;
		}
	}
	int32 W = 0, H = 0;
	Out.Reset();
	return UOsdsWorldResources::DecodeImageFile(Path, Out, W, H) && W == PhotoCells && H == PhotoCells;
}

bool FOsdsAerialEye::BuildMosaic(const FIntPoint& Tile16, TFunctionRef<bool(const FIntPoint&, TArray<FColor>&)> PhotoOf, TArray<FColor>& OutMosaic)
{
	const int32 N = PhotoCells, S = PhotoStored, G = PhotoGuard;
	TArray<FColor> Own;
	if (!PhotoOf(Tile16, Own) || Own.Num() != N * N)
	{
		return false;
	}
	// The eight neighbours (only their edges are used).
	TArray<FColor> Near[3][3];
	bool bHave[3][3] = {};
	for (int32 DY = -1; DY <= 1; ++DY)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			if (DX || DY)
			{
				bHave[DY + 1][DX + 1] = PhotoOf(Tile16 + FIntPoint(DX, DY), Near[DY + 1][DX + 1]) && Near[DY + 1][DX + 1].Num() == N * N;
			}
		}
	}
	OutMosaic.SetNumUninitialized(S * S);
	for (int32 J = 0; J < S; ++J)
	{
		for (int32 I = 0; I < S; ++I)
		{
			const int32 GX = I - G, GY = J - G;
			const int32 DX = GX < 0 ? -1 : (GX >= N ? 1 : 0), DY = GY < 0 ? -1 : (GY >= N ? 1 : 0);
			if ((DX || DY) && bHave[DY + 1][DX + 1])
			{
				OutMosaic[J * S + I] = Near[DY + 1][DX + 1][(GY - DY * N) * N + (GX - DX * N)];
			}
			else
			{
				// No neighbour: mirror the tile's own photo across its edge.
				const int32 MX = GX < 0 ? -1 - GX : (GX >= N ? 2 * N - 1 - GX : GX);
				const int32 MY = GY < 0 ? -1 - GY : (GY >= N ? 2 * N - 1 - GY : GY);
				OutMosaic[J * S + I] = Own[MY * N + MX];
			}
		}
	}
	return true;
}

void FOsdsAerialEye::CleanPhoto(const TArray<FColor>& Mosaic, const FIntPoint& Tile16, const TArray<FOsdsScanChannel>& LandCover,
	TArray<FOsdsScanChannel>& OutAlbedo, int32* OutCleaned)
{
	using namespace OsdsAerialEyeLocal;
	const int32 S = PhotoStored, G = PhotoGuard;
	OutAlbedo.Reset();
	if (Mosaic.Num() != S * S || LandCover.Num() < 12 || LandCover[0].Size <= 0)
	{
		return;
	}
	const int32 L = LandCover[0].Size;
	// Class of every mosaic pixel (nearest land-cover cell) and whether the eye is sure (p >= 0.6).
	TArray<uint8> Class;
	Class.SetNumUninitialized(S * S);
	for (int32 J = 0; J < S; ++J)
	{
		const int32 CY = FMath::Min(L - 1, J * L / S);
		for (int32 I = 0; I < S; ++I)
		{
			const int32 C = CY * L + FMath::Min(L - 1, I * L / S);
			int32 Best = 0;
			for (int32 K = 1; K < 12; ++K) if (LandCover[K].Data[C] > LandCover[Best].Data[C]) Best = K;
			Class[J * S + I] = (LandCover[Best].Data[C] >= 153 && IsNatural(Best)) ? static_cast<uint8>(Best) : 255;
		}
	}
	// Per natural class: luminance median and spread over the tile's interior.
	int32 Median[12] = {}, Spread[12] = {};
	FColor Mean[12];
	for (int32 K = 0; K < 12; ++K)
	{
		if (!IsNatural(K)) continue;
		TArray<int32> Hist;
		Hist.Init(0, 256);
		uint64 R = 0, Gr = 0, B = 0, Count = 0;
		for (int32 J = G; J < S - G; ++J) for (int32 I = G; I < S - G; ++I)
		{
			if (Class[J * S + I] != K) continue;
			const FColor& P = Mosaic[J * S + I];
			++Hist[Luma(P)]; R += P.R; Gr += P.G; B += P.B; ++Count;
		}
		if (Count < 64) { Median[K] = -1; continue; }
		auto Quantile = [&](double Q) { uint64 Sum = 0; for (int32 V = 0; V < 256; ++V) { Sum += Hist[V]; if (Sum >= Q * Count) return V; } return 255; };
		Median[K] = Quantile(0.5);
		Spread[K] = FMath::Max(4, (Quantile(0.84) - Quantile(0.16)) / 2);
		Mean[K] = FColor(uint8(R / Count), uint8(Gr / Count), uint8(B / Count), 255);
	}
	auto IsOutlier = [&](int32 Index, int32 K)
	{
		const int32 Y = Luma(Mosaic[Index]);
		return Y < Median[K] - 2 * Spread[K] - 6 || Y > Median[K] + 3 * Spread[K] + 10;
	};
	// The pixels to replace: outliers of their class, grown by one pixel so a shadow's soft edge goes too.
	TArray<uint8> Bad;
	Bad.Init(0, S * S);
	for (int32 Index = 0; Index < S * S; ++Index)
	{
		const int32 K = Class[Index];
		if (K != 255 && Median[K] >= 0 && IsOutlier(Index, K)) Bad[Index] = 2;
	}
	for (int32 J = 1; J < S - 1; ++J)
	{
		for (int32 I = 1; I < S - 1; ++I)
		{
			const int32 Index = J * S + I;
			if (Bad[Index] || Class[Index] == 255) continue;
			if (Bad[Index - 1] == 2 || Bad[Index + 1] == 2 || Bad[Index - S] == 2 || Bad[Index + S] == 2) Bad[Index] = 1;
		}
	}
	TArray<FColor> Out = Mosaic;
	int32 Cleaned = 0;
	const int64 OX = int64(Tile16.X) * PhotoCells - G, OY = int64(Tile16.Y) * PhotoCells - G;
	for (int32 J = 0; J < S; ++J)
	{
		for (int32 I = 0; I < S; ++I)
		{
			const int32 Index = J * S + I;
			const int32 K = Class[Index];
			if (!Bad[Index] || K == 255 || Median[K] < 0) continue;
			// Nearby clean pixels of the same class, in an order fixed by the GLOBAL position (two tiles that
			// see the same pixels near their shared edge pick the same one); of the first four found, the one
			// closest to the class's typical brightness, so no bright or dark specks are copied in.
			const uint64 Seed = Mix((uint64(OX + I) << 32) ^ uint64(OY + J));
			int32 Best = -1, BestError = MAX_int32, Found = 0;
			for (int32 T = 0; T < 32 && Found < 4; ++T)
			{
				const uint64 H = Mix(Seed + T);
				const int32 DX = int32(H % 33) - 16, DY = int32((H >> 16) % 33) - 16;
				const int32 X = I + DX, Y = J + DY;
				if (X < 0 || Y < 0 || X >= S || Y >= S) continue;
				const int32 C = Y * S + X;
				if (Class[C] != K || Bad[C]) continue;
				++Found;
				const int32 Error = FMath::Abs(Luma(Mosaic[C]) - Median[K]);
				if (Error < BestError) { BestError = Error; Best = C; }
			}
			Out[Index] = Best >= 0 ? Mosaic[Best] : Mean[K];
			++Cleaned;
		}
	}
	const TCHAR* Names[3] = {TEXT("albedo_photo_r"), TEXT("albedo_photo_g"), TEXT("albedo_photo_b")};
	for (int32 Ch = 0; Ch < 4; ++Ch)
	{
		FOsdsScanChannel& C = OutAlbedo.AddDefaulted_GetRef();
		C.Name = Ch < 3 ? FName(Names[Ch]) : FName(TEXT("albedo_photo_mask"));
		C.Size = S;
		C.Confidence = 1.f;
		C.Data.SetNumUninitialized(S * S);
		for (int32 Index = 0; Index < S * S; ++Index)
		{
			const FColor& P = Out[Index];
			C.Data[Index] = Ch == 0 ? P.R : Ch == 1 ? P.G : Ch == 2 ? P.B : (Class[Index] != 255 ? 255 : 0);
		}
	}
	if (OutCleaned) *OutCleaned = Cleaned;
}

void FOsdsAerialEye::Request(const FIntPoint& Tile16, const FString& AerialPath)
{
	if (!bReady || bStop || AerialPath.IsEmpty())
	{
		return;
	}
	{
		FScopeLock Guard(&Lock);
		if (Queued.Contains(Tile16) || Recent.Contains(Tile16))
		{
			return;
		}
		Queued.Add(Tile16);
		// Newest first: the streamer asks for the tiles on the car's way last.
		Queue.Insert(TPair<FIntPoint, FString>(Tile16, AerialPath), 0);
		// The car outran a long queue: drop the oldest asks (they are scanned when the car comes back).
		while (Queue.Num() > 48)
		{
			Queued.Remove(Queue.Last().Key);
			Queue.Pop();
		}
		if (bWorkerRunning)
		{
			return;
		}
		bWorkerRunning = true;
	}
	Async(EAsyncExecution::Thread, [this]() { RunWorker(); });
}

void FOsdsAerialEye::RunWorker()
{
	if (FRunnableThread* Thread = FRunnableThread::GetRunnableThread())
	{
		Thread->SetThreadPriority(TPri_BelowNormal);
	}
	for (;;)
	{
		TPair<FIntPoint, FString> Job;
		{
			FScopeLock Guard(&Lock);
			if (Queue.Num() == 0 || bStop)
			{
				bWorkerRunning = false;
				return;
			}
			Job = Queue[0];
			Queue.RemoveAt(0);
		}
		const FIntPoint Tile = Job.Key;
		TArray<FOsdsScanChannel> Channels;
		TArray<FOsdsClassTint> Tints;
		bool bHit = false;
		{
			TArray<uint8> Bytes;
			bHit = LoadCached(Tile, Bytes) && Decode(Bytes, Channels, Tints);
		}
		double Seconds = 0.0;
		bool bOk = bHit;
		if (!bHit)
		{
			FOsdsAerialEyeModel& M = *Model;
			if (M.PhotoPath != Job.Value)
			{
				int32 W = 0, H = 0;
				M.Photo.Reset();
				M.PhotoPath.Reset();
				if (UOsdsWorldResources::DecodeImageFile(Job.Value, M.Photo, W, H) && W == H && W >= 256)
				{
					M.PhotoPath = Job.Value;
					M.PhotoSize = W;
				}
			}
			// Back off while the game is slow (frames over 1.1 x the 60 fps budget): wait up to 2 s for it
			// to recover. The tile still has the scan rules meanwhile.
			for (int32 Wait = 0; Wait < 8 && IsGameSlow() && !bStop; ++Wait)
			{
				FPlatformProcess::Sleep(0.25f);
				FScopeLock Guard(&Lock);
				++BackOffs;
			}
			// The tile's own zoom-16 photograph (4 x the detail) when the helper has it: the eye reads it
			// (the kind of photo it learned on) and the clean photo albedo is made from it.
			TArray<FColor> Mosaic;
			const bool bMosaic = CVarOsdsEyePhoto16.GetValueOnAnyThread() > 0 && (!bStudent || IsLower48(FIntVector(16, Tile.X, Tile.Y)))
				&& BuildMosaic(Tile, [this](const FIntPoint& T, TArray<FColor>& P) { return Photo16(T, true, P); }, Mosaic);
			if (bMosaic || !M.PhotoPath.IsEmpty())
			{
				FOsdsScanInputs Inputs;
				if (bMosaic)
				{
					Inputs.TileZXY = FIntVector(16, Tile.X, Tile.Y);
					Inputs.Cells = Cells;
					Inputs.GuardCells = GuardCells;
					Inputs.AerialRGB = Mosaic;
					Inputs.AerialSize = PhotoStored;
				}
				else
				{
					InputsFromPhoto(M.Photo, M.PhotoSize, Tile, Inputs);
				}
				TArray<FOsdsScanPoint> Points;
				const double T0 = FPlatformTime::Seconds();
				bOk = Run(Inputs, Channels, Points);
				int32 Cleaned = 0;
				if (bOk && bMosaic)
				{
					TArray<FOsdsScanChannel> Albedo;
					CleanPhoto(Mosaic, Tile, Channels, Albedo, &Cleaned);
					ComputeTints(Inputs, Channels, Tints);
					Channels.Append(MoveTemp(Albedo));
				}
				Seconds = FPlatformTime::Seconds() - T0;
				if (bOk)
				{
					if (!bMosaic) ComputeTints(Inputs, Channels, Tints);
					TArray<uint8> Bytes;
					Encode(Channels, Tints, Bytes);
					if (!StoreCached(Tile, Bytes))
					{
						UE_LOG(LogOsdsAerialEye, Warning, TEXT("Scan eye %s: tile %d,%d not saved to the World cache; it is scanned again next visit"), *EyeName.ToString(), Tile.X, Tile.Y);
					}
					UE_LOG(LogOsdsAerialEye, Log, TEXT("SCANEYE eye=%s tile=%d,%d photo=%s ms=%.0f cleaned=%d"), *EyeName.ToString(), Tile.X, Tile.Y, bMosaic ? TEXT("z16") : TEXT("z14"), Seconds * 1000.0, Cleaned);
					if (CVarOsdsEyeDump.GetValueOnAnyThread() > 0)
					{
						DumpPicture(Inputs, Tile, Channels);
					}
				}
			}
		}
		FScopeLock Guard(&Lock);
		Queued.Remove(Tile);
		if (bOk)
		{
			Recent.Add(Tile, FScanned{MoveTemp(Channels), MoveTemp(Tints)});
			RecentOrder.Add(Tile);
			while (RecentOrder.Num() > 16)
			{
				Recent.Remove(RecentOrder[0]);
				RecentOrder.RemoveAt(0);
			}
			if (bHit) { ++CacheHits; }
			else { ++Runs; TotalSeconds += Seconds; MaxSeconds = FMath::Max(MaxSeconds, Seconds); RunTimes.Add(Seconds); }
		}
		else
		{
			++Failures;
		}
	}
}

void FOsdsAerialEye::DumpPicture(const FOsdsScanInputs& Inputs, const FIntPoint& Tile16, const TArray<FOsdsScanChannel>& Channels) const
{
	using namespace OsdsAerialEyeLocal;
	if (Channels.Num() < 12 || Inputs.AerialSize <= 0)
	{
		return;
	}
	const int32 S = Channels[0].Size;
	// Photo | classes, and a third panel with the clean photo albedo when the tile has one.
	const FOsdsScanChannel* AR = Channels.FindByPredicate([](const FOsdsScanChannel& C) { return C.Name == FName(TEXT("albedo_photo_r")); });
	const FOsdsScanChannel* AG = Channels.FindByPredicate([](const FOsdsScanChannel& C) { return C.Name == FName(TEXT("albedo_photo_g")); });
	const FOsdsScanChannel* AB = Channels.FindByPredicate([](const FOsdsScanChannel& C) { return C.Name == FName(TEXT("albedo_photo_b")); });
	const int32 Panels = (AR && AG && AB) ? 3 : 2;
	TArray<FColor> Pic;
	Pic.SetNumUninitialized(Panels * S * S);
	if (Panels == 3)
	{
		const int32 A = AR->Size;
		for (int32 Y = 0; Y < S; ++Y)
		{
			for (int32 X = 0; X < S; ++X)
			{
				const int32 I = FMath::Min(A - 1, Y * A / S) * A + FMath::Min(A - 1, X * A / S);
				Pic[Y * 3 * S + 2 * S + X] = FColor(AR->Data[I], AG->Data[I], AB->Data[I], 255);
			}
		}
	}
	const double In = static_cast<double>(Inputs.AerialSize) / S;
	for (int32 Y = 0; Y < S; ++Y)
	{
		for (int32 X = 0; X < S; ++X)
		{
			const FLinearColor C = Sample(Inputs.AerialRGB, Inputs.AerialSize, Inputs.AerialSize, (X + 0.5) * In, (Y + 0.5) * In);
			Pic[Y * Panels * S + X] = FColor(static_cast<uint8>(C.R), static_cast<uint8>(C.G), static_cast<uint8>(C.B), 255);
			int32 Best = 0;
			for (int32 K = 1; K < 12; ++K) if (Channels[K].Data[Y * S + X] > Channels[Best].Data[Y * S + X]) Best = K;
			Pic[Y * Panels * S + S + X] = Palette[Best];
		}
	}
	IImageWrapperModule* Wrappers = FModuleManager::GetModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Png = Wrappers ? Wrappers->CreateImageWrapper(EImageFormat::PNG) : nullptr;
	if (Png.IsValid() && Png->SetRaw(Pic.GetData(), Pic.Num() * sizeof(FColor), Panels * S, S, ERGBFormat::BGRA, 8))
	{
		const TArray64<uint8>& Bytes = Png->GetCompressed();
		FFileHelper::SaveArrayToFile(Bytes, *(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Scan/eyes/dump/16-%d-%d.png"), Tile16.X, Tile16.Y)));
	}
}

bool FOsdsAerialEye::Lookup(const FIntPoint& Tile16, TArray<FOsdsScanChannel>& Out, TArray<FOsdsClassTint>* OutTints)
{
	if (!bReady)
	{
		return false;
	}
	{
		FScopeLock Guard(&Lock);
		if (const FScanned* Found = Recent.Find(Tile16))
		{
			Out = Found->Channels;
			if (OutTints) *OutTints = Found->Tints;
			return true;
		}
	}
	TArray<uint8> Bytes;
	TArray<FOsdsClassTint> Tints;
	if (!LoadCached(Tile16, Bytes) || !Decode(Bytes, Out, Tints))
	{
		return false;
	}
	if (OutTints) *OutTints = MoveTemp(Tints);
	return true;
}

void FOsdsAerialEye::Encode(const TArray<FOsdsScanChannel>& Channels, const TArray<FOsdsClassTint>& Tints, TArray<uint8>& OutBytes)
{
	TArray<uint8> Raw;
	FMemoryWriter W(Raw);
	uint32 Count = Channels.Num();
	W << Count;
	for (const FOsdsScanChannel& C : Channels)
	{
		FString Name = C.Name.ToString();
		int32 Size = C.Size;
		float Conf = C.Confidence;
		W << Name << Size << Conf;
		W.Serialize(const_cast<uint8*>(C.Data.GetData()), C.Data.Num());
	}
	uint32 TintCount = Tints.Num();
	W << TintCount;
	for (const FOsdsClassTint& T : Tints)
	{
		FString Name = T.Channel.ToString();
		FColor Mean = T.Mean;
		float Share = T.Share, Conf = T.Confidence;
		W << Name << Mean << Share << Conf;
	}
	const int32 Bound = FCompression::CompressMemoryBound(NAME_Zlib, Raw.Num());
	OutBytes.SetNumUninitialized(12 + Bound);
	int32 Compressed = Bound;
	FCompression::CompressMemory(NAME_Zlib, OutBytes.GetData() + 12, Compressed, Raw.GetData(), Raw.Num());
	const uint32 Header[3] = {OsdsAerialEyeLocal::Magic, static_cast<uint32>(FileVersion), static_cast<uint32>(Raw.Num())};
	FMemory::Memcpy(OutBytes.GetData(), Header, 12);
	OutBytes.SetNum(12 + Compressed);
}

bool FOsdsAerialEye::Decode(const TArray<uint8>& Bytes, TArray<FOsdsScanChannel>& OutChannels, TArray<FOsdsClassTint>& OutTints)
{
	OutTints.Reset();
	if (Bytes.Num() < 12)
	{
		return false;
	}
	uint32 Header[3];
	FMemory::Memcpy(Header, Bytes.GetData(), 12);
	if (Header[0] != OsdsAerialEyeLocal::Magic || Header[1] != uint32(FileVersion) || Header[2] > 64u * 1024 * 1024)
	{
		return false;
	}
	TArray<uint8> Raw;
	Raw.SetNumUninitialized(Header[2]);
	if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), Raw.Num(), Bytes.GetData() + 12, Bytes.Num() - 12))
	{
		return false;
	}
	FMemoryReader R(Raw);
	uint32 Count = 0;
	R << Count;
	OutChannels.Reset();
	for (uint32 I = 0; I < Count && !R.IsError(); ++I)
	{
		FString Name;
		int32 Size = 0;
		float Conf = 0.f;
		R << Name << Size << Conf;
		if (Size <= 0 || Size > 4096 || R.Tell() + int64(Size) * Size > R.TotalSize())
		{
			return false;
		}
		FOsdsScanChannel& C = OutChannels.AddDefaulted_GetRef();
		C.Name = FName(*Name);
		C.Size = Size;
		C.Confidence = Conf;
		C.Data.SetNumUninitialized(Size * Size);
		R.Serialize(C.Data.GetData(), C.Data.Num());
	}
	uint32 TintCount = 0;
	R << TintCount;
	for (uint32 I = 0; I < TintCount && I < 64 && !R.IsError(); ++I)
	{
		FString Name;
		FColor Mean;
		float Share = 0.f, Conf = 0.f;
		R << Name << Mean << Share << Conf;
		OutTints.Add(FOsdsClassTint{FName(*Name), Mean, Share, Conf});
	}
	return !R.IsError() && OutChannels.Num() == int32(Count) && OutTints.Num() == int32(TintCount);
}

FString FOsdsAerialEye::Describe() const
{
	FScopeLock Guard(&const_cast<FOsdsAerialEye*>(this)->Lock);
	TArray<double> Sorted = RunTimes;
	Sorted.Sort();
	const double P95 = Sorted.Num() ? Sorted[FMath::Min(Sorted.Num() - 1, FMath::FloorToInt32(0.95 * Sorted.Num()))] : 0.0;
	return FString::Printf(TEXT("SCANEYE STATS eye=%s ready=%d runs=%d mean_ms=%.0f p95_ms=%.0f max_ms=%.0f cache_hits=%d failures=%d backoffs_250ms=%d queued=%d"),
		*EyeName.ToString(), bReady ? 1 : 0, Runs, Runs ? 1000.0 * TotalSeconds / Runs : 0.0, 1000.0 * P95, 1000.0 * MaxSeconds, CacheHits, Failures, BackOffs, Queue.Num());
}

static FAutoConsoleCommand GOsdsEyeStats(TEXT("OSDS.Eye.Stats"), TEXT("Logs the scan eye's run statistics."),
	FConsoleCommandDelegate::CreateLambda([]() { UE_LOG(LogOsdsAerialEye, Display, TEXT("%s"), *FOsdsAerialEye::Get().Describe()); }));
