// Copyright OSDS contributors. All Rights Reserved.

#include "Live/OsdsNeuralDetail.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "NNERuntimeGPU.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "World/OsdsWorldCache.h"
#include "World/OsdsWorldResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsNeural, Log, All);

class FOsdsNeuralModel
{
public:
	TStrongObjectPtr<UNNEModelData> Data;
	TSharedPtr<UE::NNE::IModelGPU> GpuModel;
	TSharedPtr<UE::NNE::IModelInstanceGPU> Gpu;
	TSharedPtr<UE::NNE::IModelCPU> CpuModel;
	TSharedPtr<UE::NNE::IModelInstanceCPU> Cpu;

	UE::NNE::IModelInstanceRunSync* Instance() const
	{
		return Gpu.IsValid() ? static_cast<UE::NNE::IModelInstanceRunSync*>(Gpu.Get()) : static_cast<UE::NNE::IModelInstanceRunSync*>(Cpu.Get());
	}
};

namespace OsdsNeuralPrivate
{
	FAutoConsoleCommand GBench(
		TEXT("OSDS.Neural.Bench"),
		TEXT("OSDS.Neural.Bench [image file]: run the AI detail engine on one aerial photograph (no cache) and log the time"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			FOsdsNeuralDetail& Neural = FOsdsNeuralDetail::Get();
			if (!Neural.Initialize())
			{
				UE_LOG(LogOsdsNeural, Warning, TEXT("AI detail engine not available: %s"), *Neural.Describe());
				return;
			}
			const FString Path = Args.Num() > 0 ? Args[0] : FPaths::ProjectDir() / TEXT("data/tiles/aerial/a-4545-6976.jpg");
			TArray<FColor> In, Out;
			int32 W = 0, H = 0, OW = 0, OH = 0;
			double Seconds = 0.0;
			if (!UOsdsWorldResources::DecodeImageFile(Path, In, W, H) || !Neural.Upscale(In, W, H, Out, OW, OH, Seconds))
			{
				UE_LOG(LogOsdsNeural, Warning, TEXT("Bench failed on %s"), *Path);
				return;
			}
			UE_LOG(LogOsdsNeural, Display, TEXT("Bench: %s %dx%d -> %dx%d in %.2f s (%d windows, %.1f ms each). %s"), *FPaths::GetCleanFilename(Path), W, H, OW, OH,
				Seconds, FOsdsNeuralDetail::WindowCount(W, H), Seconds * 1000.0 / FMath::Max(FOsdsNeuralDetail::WindowCount(W, H), 1), *Neural.Describe());
			IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			TSharedPtr<IImageWrapper> Jpeg = Wrappers.CreateImageWrapper(EImageFormat::JPEG);
			if (Jpeg && Jpeg->SetRaw(Out.GetData(), Out.Num() * sizeof(FColor), OW, OH, ERGBFormat::BGRA, 8))
			{
				const FString Saved = FPaths::ProjectSavedDir() / TEXT("NeuralDetail") / (FPaths::GetBaseFilename(Path) + TEXT("-bench.jpg"));
				FFileHelper::SaveArrayToFile(Jpeg->GetCompressed(95), *Saved);
				UE_LOG(LogOsdsNeural, Display, TEXT("Bench result: %s"), *Saved);
			}
		}));
}

FOsdsNeuralDetail& FOsdsNeuralDetail::Get()
{
	// Never destroyed (release bug R6): a function-static instance was destroyed by the C
	// runtime at process exit, after the engine had shut down. Its ONNX Runtime environment then
	// waited for its thread pool's workers, which were themselves waiting for the loader lock the
	// exiting thread held, so a Shipping game hung for ever after its window closed. The OS
	// frees the memory and the threads when the process ends.
	static FOsdsNeuralDetail* Instance = new FOsdsNeuralDetail();
	return *Instance;
}

int32 FOsdsNeuralDetail::WindowCount(int32 Width, int32 Height)
{
	const int32 Core = Window - 2 * Overlap;
	return FMath::DivideAndRoundUp(Width, Core) * FMath::DivideAndRoundUp(Height, Core);
}

bool FOsdsNeuralDetail::Initialize()
{
	check(IsInGameThread());
	if (bTried)
	{
		return bReady;
	}
	bTried = true;
	// Quitting while a photograph is being enhanced crashed the game on its way out (2.1 release
	// smoke test: the worker called into the image and inference modules as the engine unloaded
	// them). Stop the worker first and give it a moment to finish the window it is on.
	FCoreDelegates::OnPreExit.AddRaw(this, &FOsdsNeuralDetail::StopForExit);
	const TCHAR* Cmd = FCommandLine::Get();
	if (FParse::Param(Cmd, TEXT("OsdsNoNeural")))
	{
		RuntimeName = TEXT("off (-OsdsNoNeural)");
		return false;
	}
	int32 Scale = OutputScale;
	if (FParse::Value(Cmd, TEXT("OsdsNeuralScale="), Scale) && (Scale == 1 || Scale == 2 || Scale == 4))
	{
		OutputScale = Scale;
	}

	const FString ModelPath = FPaths::ProjectDir() / TEXT("data/ai/real-esrgan-general-x4v3.onnx");
	TArray64<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *ModelPath))
	{
		RuntimeName = TEXT("no model file: classic sharpening");
		bSharpenFallback = true;
		UE_LOG(LogOsdsNeural, Display, TEXT("AI detail engine off (no %s): aerial photographs get a classic unsharp mask"), *ModelPath);
		return false;
	}

	Model = MakeShared<FOsdsNeuralModel>();
	Model->Data.Reset(NewObject<UNNEModelData>(GetTransientPackage(), NAME_None, RF_Transient));
	Model->Data->Init(TEXT("onnx"), Bytes);

	// Where it runs: the CPU and system RAM by default, so the power-capped GPU keeps every
	// watt for the frame (the work is in the background either way); -OsdsNeuralDevice=gpu
	// uses DirectML instead. Either falls back to the other.
	FString Device = TEXT("cpu");
	FParse::Value(Cmd, TEXT("OsdsNeuralDevice="), Device);
	auto TryGpu = [this]()
	{
		if (TWeakInterfacePtr<INNERuntimeGPU> Gpu = UE::NNE::GetRuntime<INNERuntimeGPU>(TEXT("NNERuntimeORTDml")); Gpu.IsValid()
			&& Gpu->CanCreateModelGPU(Model->Data.Get()) == INNERuntimeGPU::ECanCreateModelGPUStatus::Ok)
		{
			Model->GpuModel = Gpu->CreateModelGPU(Model->Data.Get());
			Model->Gpu = Model->GpuModel.IsValid() ? Model->GpuModel->CreateModelInstanceGPU() : nullptr;
			RuntimeName = TEXT("NNERuntimeORTDml (GPU)");
		}
	};
	auto TryCpu = [this]()
	{
		if (TWeakInterfacePtr<INNERuntimeCPU> Cpu = UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeORTCpu")); Cpu.IsValid()
			&& Cpu->CanCreateModelCPU(Model->Data.Get()) == INNERuntimeCPU::ECanCreateModelCPUStatus::Ok)
		{
			Model->CpuModel = Cpu->CreateModelCPU(Model->Data.Get());
			Model->Cpu = Model->CpuModel.IsValid() ? Model->CpuModel->CreateModelInstanceCPU() : nullptr;
			RuntimeName = TEXT("NNERuntimeORTCpu (CPU and RAM)");
		}
	};
	if (Device.Equals(TEXT("gpu"), ESearchCase::IgnoreCase))
	{
		TryGpu();
		if (!Model->Gpu.IsValid()) { TryCpu(); }
	}
	else
	{
		TryCpu();
		if (!Model->Cpu.IsValid()) { TryGpu(); }
	}
	UE::NNE::IModelInstanceRunSync* Instance = Model->Instance();
	if (!Instance)
	{
		UE_LOG(LogOsdsNeural, Display, TEXT("AI detail engine off: no NNE GPU runtime (runtimes: %s)"),
			*FString::Join(UE::NNE::GetAllRuntimeNames(), TEXT(", ")));
		RuntimeName = TEXT("no NNE runtime: classic sharpening");
		bSharpenFallback = true;
		Model.Reset();
		return false;
	}
	const uint32 Shape[] = {1, 3, static_cast<uint32>(Window), static_cast<uint32>(Window)};
	if (Instance->SetInputTensorShapes({UE::NNE::FTensorShape::Make(Shape)}) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogOsdsNeural, Warning, TEXT("AI detail engine: the model refused a 1x3x%dx%d input"), Window, Window);
		Model.Reset();
		return false;
	}
	// The worker encodes JPEGs with ImageWrapper; a module may only be loaded on the game thread.
	ImageWrappers = &FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	bReady = true;
	UE_LOG(LogOsdsNeural, Display, TEXT("AI detail engine ready: Real-ESRGAN General x4v3 on %s, aerial photographs kept at %dx."), *RuntimeName, OutputScale);
	return true;
}

bool FOsdsNeuralDetail::Upscale(const TArray<FColor>& In, int32 Width, int32 Height, TArray<FColor>& Out, int32& OutWidth, int32& OutHeight, double& OutSeconds)
{
	if (!bReady || !Model.IsValid() || Width <= 0 || Height <= 0 || In.Num() != Width * Height)
	{
		return false;
	}
	FScopeLock Guard(&RunLock);
	UE::NNE::IModelInstanceRunSync* Instance = Model->Instance();
	const int32 Core = Window - 2 * Overlap;
	const int32 OW = Width * ModelScale, OH = Height * ModelScale, OutWindow = Window * ModelScale;
	TArray<FColor> Full;
	Full.SetNumUninitialized(OW * OH);
	TArray<float> Input, Output;
	Input.SetNumUninitialized(3 * Window * Window);
	Output.SetNumUninitialized(3 * OutWindow * OutWindow);
	const int32 Plane = Window * Window, OutPlane = OutWindow * OutWindow;

	const double Start = FPlatformTime::Seconds();
	for (int32 CY = 0; CY < Height; CY += Core)
	{
		for (int32 CX = 0; CX < Width; CX += Core)
		{
			if (bStopping) return false;
			const int32 SX = CX - Overlap, SY = CY - Overlap;
			for (int32 Y = 0; Y < Window; ++Y)
			{
				const int32 IY = FMath::Clamp(SY + Y, 0, Height - 1);
				for (int32 X = 0; X < Window; ++X)
				{
					const FColor& C = In[IY * Width + FMath::Clamp(SX + X, 0, Width - 1)];
					const int32 I = Y * Window + X;
					Input[I] = C.R / 255.0f;
					Input[Plane + I] = C.G / 255.0f;
					Input[2 * Plane + I] = C.B / 255.0f;
				}
			}
			const UE::NNE::FTensorBindingCPU InBinding{Input.GetData(), static_cast<uint64>(Input.Num() * sizeof(float))};
			const UE::NNE::FTensorBindingCPU OutBinding{Output.GetData(), static_cast<uint64>(Output.Num() * sizeof(float))};
			const double RunStart = FPlatformTime::Seconds();
			if (Instance->RunSync({InBinding}, {OutBinding}) != UE::NNE::EResultStatus::Ok)
			{
				UE_LOG(LogOsdsNeural, Warning, TEXT("AI detail engine: inference failed"));
				return false;
			}
			if (Duty < 0.999f && !IsInGameThread() && Model->Gpu.IsValid())
			{
				// Leave the GPU to the frame between windows.
				FPlatformProcess::Sleep(static_cast<float>((FPlatformTime::Seconds() - RunStart) * (1.0 / FMath::Max(Duty, 0.05f) - 1.0)));
			}
			// Keep the core of the window: the overlap hides the network's edge effects.
			const int32 CoreW = FMath::Min(Core, Width - CX), CoreH = FMath::Min(Core, Height - CY);
			for (int32 Y = 0; Y < CoreH * ModelScale; ++Y)
			{
				const int32 WY = Overlap * ModelScale + Y;
				FColor* Row = &Full[(CY * ModelScale + Y) * OW + CX * ModelScale];
				for (int32 X = 0; X < CoreW * ModelScale; ++X)
				{
					const int32 I = WY * OutWindow + Overlap * ModelScale + X;
					Row[X] = FColor(
						static_cast<uint8>(FMath::Clamp(Output[I] * 255.0f + 0.5f, 0.0f, 255.0f)),
						static_cast<uint8>(FMath::Clamp(Output[OutPlane + I] * 255.0f + 0.5f, 0.0f, 255.0f)),
						static_cast<uint8>(FMath::Clamp(Output[2 * OutPlane + I] * 255.0f + 0.5f, 0.0f, 255.0f)), 255);
				}
			}
		}
	}
	OutSeconds = FPlatformTime::Seconds() - Start;
	TotalSeconds += OutSeconds;
	Images++;

	const int32 Factor = ModelScale / FMath::Clamp(OutputScale, 1, ModelScale);
	if (Factor > 1)
	{
		Downsample(Full, OW, OH, Factor, Out);
		OutWidth = OW / Factor;
		OutHeight = OH / Factor;
	}
	else
	{
		Out = MoveTemp(Full);
		OutWidth = OW;
		OutHeight = OH;
	}
	return true;
}

void FOsdsNeuralDetail::Downsample(const TArray<FColor>& In, int32 Width, int32 Height, int32 Factor, TArray<FColor>& Out)
{
	const int32 W = Width / Factor, H = Height / Factor, N = Factor * Factor;
	Out.SetNumUninitialized(W * H);
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			uint32 R = 0, G = 0, B = 0, A = 0;
			for (int32 DY = 0; DY < Factor; ++DY)
			{
				const FColor* Row = &In[(Y * Factor + DY) * Width + X * Factor];
				for (int32 DX = 0; DX < Factor; ++DX)
				{
					R += Row[DX].R; G += Row[DX].G; B += Row[DX].B; A += Row[DX].A;
				}
			}
			Out[Y * W + X] = FColor((R + N / 2) / N, (G + N / 2) / N, (B + N / 2) / N, (A + N / 2) / N);
		}
	}
}

bool FOsdsNeuralDetail::CacheKey(const FString& SourcePath, FIntPoint& OutTile, FName& OutLayer, int32& OutVersion) const
{
	// The photographs are named a16-<x>-<y>.jpg (zoom-16 Tile) or a-<x>-<y>.jpg (regional mosaic, its own
	// grid): the two numbers key the World cache entry and the prefix names the layer, so the two grids
	// never mix. The generator version folds in the output scale and the source file's size and time
	// stamp: a new photograph, or a changed scale, is enhanced again (ADR 0005).
	const FString Base = FPaths::GetBaseFilename(SourcePath);
	TArray<FString> Parts;
	Base.ParseIntoArray(Parts, TEXT("-"));
	if (Parts.Num() != 3 || !Parts[1].IsNumeric() || !Parts[2].IsNumeric())
	{
		return false;
	}
	const FFileStatData Stat = IFileManager::Get().GetStatData(*SourcePath);
	OutTile = FIntPoint(FCString::Atoi(*Parts[1]), FCString::Atoi(*Parts[2]));
	OutLayer = FName(*FString::Printf(TEXT("neural-%s"), *Parts[0]));
	OutVersion = static_cast<int32>(HashCombineFast(HashCombineFast(GetTypeHash(int32(GeneratorVersion)), GetTypeHash(int32(OutputScale))),
		GetTypeHash(static_cast<uint64>(Stat.FileSize ^ Stat.ModificationTime.GetTicks()))));
	return true;
}

bool FOsdsNeuralDetail::EnhanceAerial(const FString& SourcePath, TArray<FColor>& Pixels, int32& Width, int32& Height)
{
	if (!bReady)
	{
		// Without the network the photograph still gets a classic unsharp mask, in place.
		if (bSharpenFallback && Pixels.Num() == Width * Height)
		{
			UnsharpMask(Pixels, Width, Height, 0.6f);
			return true;
		}
		return false;
	}
	if (Pixels.IsEmpty() || OutputScale <= 1)
	{
		return false;
	}
	TArray<FColor> Enhanced;
	int32 W = 0, H = 0;
	FIntPoint CacheTile;
	FName CacheLayer;
	int32 CacheVersion = 0;
	TArray<uint8> Jpg;
	if (CacheKey(SourcePath, CacheTile, CacheLayer, CacheVersion) && FOsdsWorldCache::Get().Load(CacheTile, CacheLayer, CacheVersion, Jpg))
	{
		TSharedPtr<IImageWrapper> Wrapper = ImageWrappers ? ImageWrappers->CreateImageWrapper(EImageFormat::JPEG) : nullptr;
		TArray64<uint8> Raw;
		if (Wrapper && Wrapper->SetCompressed(Jpg.GetData(), Jpg.Num()) && Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw)
			&& Wrapper->GetWidth() == Width * OutputScale && Wrapper->GetHeight() == Height * OutputScale)
		{
			W = Wrapper->GetWidth();
			H = Wrapper->GetHeight();
			Enhanced.SetNumUninitialized(W * H);
			FMemory::Memcpy(Enhanced.GetData(), Raw.GetData(), W * H * sizeof(FColor));
			Pixels = MoveTemp(Enhanced);
			Width = W;
			Height = H;
			return true;
		}
	}
	FScopeLock Guard(&QueueLock);
	if (!bStopping && !Queued.Contains(SourcePath))
	{
		Queued.Add(SourcePath);
		Queue.Add(SourcePath);
		if (!bWorkerRunning)
		{
			bWorkerRunning = true;
			Async(EAsyncExecution::Thread, [this]() { RunWorker(); });
		}
	}
	return false;
}

void FOsdsNeuralDetail::StopForExit()
{
	bStopping = true;
	const double Until = FPlatformTime::Seconds() + 20.0;
	for (;;)
	{
		{
			FScopeLock Guard(&QueueLock);
			if (!bWorkerRunning) return;
		}
		if (FPlatformTime::Seconds() > Until)
		{
			UE_LOG(LogOsdsNeural, Warning, TEXT("AI detail engine: the worker did not stop within 20 s of quitting"));
			return;
		}
		FPlatformProcess::Sleep(0.01f);
	}
}

void FOsdsNeuralDetail::RunWorker()
{
	for (;;)
	{
		FString Path;
		{
			FScopeLock Guard(&QueueLock);
			if (Queue.IsEmpty() || bStopping)
			{
				bWorkerRunning = false;
				return;
			}
			// Newest first: the photograph the car has just reached matters most.
			Path = Queue.Pop();
		}
		Process(Path);
		FScopeLock Guard(&QueueLock);
		Queued.Remove(Path);
	}
}

void FOsdsNeuralDetail::Process(const FString& SourcePath)
{
	TArray<FColor> In, Out;
	int32 W = 0, H = 0, OW = 0, OH = 0;
	double Seconds = 0.0;
	const double Start = FPlatformTime::Seconds();
	if (bStopping || !UOsdsWorldResources::DecodeImageFile(SourcePath, In, W, H) || !Upscale(In, W, H, Out, OW, OH, Seconds) || bStopping)
	{
		return;
	}
	UE_LOG(LogOsdsNeural, Display, TEXT("AI detail: %s %dx%d -> %dx%d, %.1f s of inference on %s, %.1f s in all"), *FPaths::GetCleanFilename(SourcePath),
		W, H, OW, OH, Seconds, *RuntimeName, FPlatformTime::Seconds() - Start);
	TSharedPtr<IImageWrapper> Jpeg = ImageWrappers ? ImageWrappers->CreateImageWrapper(EImageFormat::JPEG) : nullptr;
	if (Jpeg && Jpeg->SetRaw(Out.GetData(), Out.Num() * sizeof(FColor), OW, OH, ERGBFormat::BGRA, 8))
	{
		FIntPoint CacheTile;
		FName CacheLayer;
		int32 CacheVersion = 0;
		if (CacheKey(SourcePath, CacheTile, CacheLayer, CacheVersion))
		{
			const TArray64<uint8> Packed = Jpeg->GetCompressed(92);
			FOsdsWorldCache::Get().Store(CacheTile, CacheLayer, CacheVersion, TConstArrayView<uint8>(Packed.GetData(), static_cast<int32>(Packed.Num())));
		}
	}
	const FString Key = FPaths::GetCleanFilename(SourcePath);
	// The texture's mip chain here, on the worker: the game thread only copies it in.
	TSharedRef<TArray<TArray<FColor>>> Mips = MakeShared<TArray<TArray<FColor>>>();
	UOsdsWorldResources::BuildMipChain(Out, OW, OH, true, *Mips);
	AsyncTask(ENamedThreads::GameThread, [this, Key, Mips, OW, OH]()
	{
		if (OnEnhanced)
		{
			OnEnhanced(Key, *Mips, OW, OH);
		}
	});
}

void FOsdsNeuralDetail::UnsharpMask(TArray<FColor>& Pixels, int32 Width, int32 Height, float InAmount, float SigmaPx, int32 Threshold)
{
	if (Width < 3 || Height < 3 || Pixels.Num() != Width * Height || InAmount <= 0.0f)
	{
		return;
	}
	// Luminance (Rec. 709 weights on the 8-bit values), blurred by a separable Gaussian.
	const int32 N = Width * Height;
	TArray<float> Luma, Tmp, Blur;
	Luma.SetNumUninitialized(N);
	Tmp.SetNumUninitialized(N);
	Blur.SetNumUninitialized(N);
	for (int32 I = 0; I < N; ++I)
	{
		const FColor& C = Pixels[I];
		Luma[I] = 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
	}
	const int32 Radius = FMath::Max(1, FMath::CeilToInt(SigmaPx * 2.5f));
	TArray<float> Kernel;
	float Sum = 0.0f;
	for (int32 K = -Radius; K <= Radius; ++K)
	{
		const float V = FMath::Exp(-0.5f * FMath::Square(K / SigmaPx));
		Kernel.Add(V);
		Sum += V;
	}
	for (float& V : Kernel)
	{
		V /= Sum;
	}
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			float Acc = 0.0f;
			for (int32 K = -Radius; K <= Radius; ++K)
			{
				Acc += Kernel[K + Radius] * Luma[Y * Width + FMath::Clamp(X + K, 0, Width - 1)];
			}
			Tmp[Y * Width + X] = Acc;
		}
	}
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			float Acc = 0.0f;
			for (int32 K = -Radius; K <= Radius; ++K)
			{
				Acc += Kernel[K + Radius] * Tmp[FMath::Clamp(Y + K, 0, Height - 1) * Width + X];
			}
			Blur[Y * Width + X] = Acc;
		}
	}
	// The luminance detail goes back on every channel (no colour fringes); small differences are
	// JPEG noise and stay as they are.
	for (int32 I = 0; I < N; ++I)
	{
		const float Detail = Luma[I] - Blur[I];
		if (FMath::Abs(Detail) < Threshold)
		{
			continue;
		}
		const float Add = InAmount * Detail;
		FColor& C = Pixels[I];
		C.R = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(C.R + Add), 0, 255));
		C.G = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(C.G + Add), 0, 255));
		C.B = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(C.B + Add), 0, 255));
	}
}

FString FOsdsNeuralDetail::Describe() const
{
	return FString::Printf(TEXT("AI detail engine: %s, %d photographs, %.1f s of inference in total"), RuntimeName.IsEmpty() ? TEXT("not started") : *RuntimeName,
		Images, TotalSeconds);
}
