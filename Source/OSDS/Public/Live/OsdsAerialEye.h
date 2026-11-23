// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Scan/OsdsScanEye.h"

/**
 * The aerial eye: a scan-engine eye (IOsdsScanEye) that reads the aerial photograph of each
 * zoom-16 world tile on the CPU and writes surface probability channels with the contract's names
 * (road_prob, building_prob, water_prob, canopy_prob, surface_logits_<class>).
 *
 * Two models, one ONNX contract (input "image" 1x3x512x512 RGB 0..1 covering the tile and its guard
 * border; output "probs" 1x12x512x512, classes in lab/scan/scan_data.py order):
 * - aerial_student: our own small network (MIT), data/ai/scan-eye-student.onnx, shipped.
 *   lab/scan/student_eye.py trains it on open labels and on the Satlas eye's answers.
 * - satlas_landcover: the Satlas Aerial_SwinB_SI backbone (Allen Institute for AI, ODC-BY 1.0) with
 *   our head, Saved/AI/scan-eye-satlas.onnx (development teacher, 352 MB, not in git;
 *   lab/scan/satlas_eye.py export).
 *
 * On by default with aerial_student (lower 48 states; rules elsewhere); -OsdsScanEye=none or
 * -OsdsNoEye turns it off, =satlas_landcover picks the development teacher. The world
 * streamer queues every tile with its photograph, and one thread at below-normal priority runs the
 * model with NNERuntimeORTCpu (the project's global ORT pool), never the game thread, never the GPU.
 * Results go to a disk cache, Saved/Scan/eyes/<eye>-v<version>/16/<x>/<y>.eye, so a tile costs its
 * inference once and a second drive runs none. The scan engine reads them with Lookup.
 *
 * The instance lives on the heap and is never destroyed (ONNX Runtime at process exit).
 * Console: OSDS.Eye.Stats; osds.Eye.Dump 1 writes photo | classes PNGs of each tile it
 * reads to Saved/Scan/eyes/dump.
 */
/**
 * The place's own colour of one surface class on one tile (scan contract TINT): the mean photo colour
 * of the tile's cells where the eye is sure of that class (p >= 0.6), so asphalt, dirt, grass, roofs
 * and bare soil take this place's colour. The world thread turns it into a material multiplier.
 */
struct FOsdsClassTint
{
	FName Channel;
	/** Mean sRGB colour of the photo over the class's sure cells. */
	FColor Mean = FColor::Black;
	/** Share of the tile's cells that are sure of this class. */
	float Share = 0.f;
	/** 0..1: grows with the number of sure cells (full at 2 % of the tile) and with the eye's certainty. */
	float Confidence = 0.f;
};

class OSDS_API FOsdsAerialEye : public IOsdsScanEye
{
public:
	static FOsdsAerialEye& Get();

	/** Loads the chosen model (game thread: it creates a UObject). False when off, absent or refused. */
	bool Initialize();
	bool IsReady() const { return bReady; }

	// IOsdsScanEye
	virtual FName Name() const override { return EyeName; }
	virtual int32 Version() const override { return EyeVersion; }
	virtual FString Licence() const override;
	virtual FString TrainingDataLicence() const override;
	virtual bool CanRun(const FOsdsScanInputs& Inputs) const override;
	virtual double CostSecondsEstimate() const override { return bStudent ? 0.35 : 1.5; }
	virtual bool Run(const FOsdsScanInputs& Inputs, TArray<FOsdsScanChannel>& OutChannels, TArray<FOsdsScanPoint>& OutPoints) override;

	/** Any thread: queue the zoom-16 tile; AerialPath is its zoom-14 photograph (4 x 4 tiles). */
	void Request(const FIntPoint& Tile16, const FString& AerialPath);

	/** Any thread: the channels (and class tints) of a scanned tile (memory, then disk). False when not scanned (yet). */
	bool Lookup(const FIntPoint& Tile16, TArray<FOsdsScanChannel>& Out, TArray<FOsdsClassTint>* OutTints = nullptr);

	/** The class tints of a tile from its inputs and the eye's channels (deterministic; tested). */
	static void ComputeTints(const FOsdsScanInputs& Inputs, const TArray<FOsdsScanChannel>& Channels, TArray<FOsdsClassTint>& Out);

	/**
	 * The eye's inputs for a zoom-16 tile cut from its zoom-14 photograph (PhotoSize x PhotoSize for
	 * 4 x 4 tiles): the tile and its guard border resampled to InputSize, clamped at the photo's edge.
	 */
	static void InputsFromPhoto(const TArray<FColor>& Photo, int32 PhotoSize, const FIntPoint& Tile16, FOsdsScanInputs& Out);

	FString Describe() const;

	// ---- Photo-true ground albedo (PHTX, scan contract I/L; agreed with the world lane 2026-11-14) ----
	//
	// The helper's `aerial16` command gives each zoom-16 tile its own 1024 px photograph (about 0.5 m a
	// pixel, USGS public domain). With it the eye (1) reads land cover from the same kind of photo it
	// learned on, and (2) writes a CLEAN photo albedo: inside natural classes (grass, crop, shrub, bare,
	// tree canopy) the pixels that do not belong to the class (shadows, cars, bright or dark objects) are
	// replaced by nearby pixels of the same class, chosen by a hash of the GLOBAL pixel position, so two
	// neighbouring tiles give the same answer along their shared edge. Roads, roofs, water and unsure
	// cells keep the photo. Channels albedo_photo_r/g/b (sRGB) and albedo_photo_mask (255 where cleaned
	// or natural), PhotoStored x PhotoStored = 1024 + 2 x 16 guard (2 cells per scan cell), north-up; the
	// guard comes from the neighbouring tiles' photographs (mirrored where a neighbour has none).

	/** The helper used to fetch the zoom-16 photographs (any thread; set by the world streamer). */
	static void SetAtlas(const struct FOsdsAtlasConfig& Config);

	static constexpr int32 PhotoCells = 1024;
	static constexpr int32 PhotoGuard = 16;
	static constexpr int32 PhotoStored = PhotoCells + 2 * PhotoGuard;

	/**
	 * The tile's photo mosaic, PhotoStored square: its own zoom-16 photograph in the middle and the
	 * neighbours' edges in the guard. PhotoOf returns a tile's 1024 px photograph or an empty array.
	 * False when the tile's own photograph is missing.
	 */
	static bool BuildMosaic(const FIntPoint& Tile16, TFunctionRef<bool(const FIntPoint&, TArray<FColor>&)> PhotoOf, TArray<FColor>& OutMosaic);

	/**
	 * The clean photo albedo of a mosaic (PhotoStored square) from the land-cover channels (528 square,
	 * the same area). Deterministic in global pixel positions. OutCleaned counts replaced pixels.
	 */
	static void CleanPhoto(const TArray<FColor>& Mosaic, const FIntPoint& Tile16, const TArray<FOsdsScanChannel>& LandCover,
		TArray<FOsdsScanChannel>& OutAlbedo, int32* OutCleaned = nullptr);

	/** The tile (z, x, y in X, Y, Z) is in the lower 48 states, where the student learned. */
	static bool IsLower48(const FIntVector& TileZXY);

	/** Game thread, once a frame: the last frame's time, for the worker's back-off. */
	static void NoteFrameSeconds(float Seconds);
	/** Any thread: the last frame took more than 1.1 x the 60 fps budget. */
	static bool IsGameSlow();

	/** The output classes in order: the channel each one writes. */
	static const TArray<FName>& ChannelNames();

	static constexpr int32 InputSize = 512;
	static constexpr int32 Cells = 512;
	static constexpr int32 GuardCells = 8;
	static constexpr int32 EyeVersion = 2;
	/** Cache file format (bump on any layout change). */
	static constexpr int32 FileVersion = 3;

	/** Encode / decode the cache file (tested). */
	static void Encode(const TArray<FOsdsScanChannel>& Channels, const TArray<FOsdsClassTint>& Tints, TArray<uint8>& OutBytes);
	static bool Decode(const TArray<uint8>& Bytes, TArray<FOsdsScanChannel>& OutChannels, TArray<FOsdsClassTint>& OutTints);

private:
	FOsdsAerialEye() = default;
	bool LoadCached(const FIntPoint& Tile16, TArray<uint8>& OutBytes) const;
	/** False when the World cache could not keep the layer (it logged why). */
	bool StoreCached(const FIntPoint& Tile16, const TArray<uint8>& Bytes) const;
	void RunWorker();
	/** A tile's zoom-16 photograph through the helper (worker only; cached by the helper). */
	bool Photo16(const FIntPoint& Tile16, bool bFetch, TArray<FColor>& Out);
	void DumpPicture(const FOsdsScanInputs& Inputs, const FIntPoint& Tile16, const TArray<FOsdsScanChannel>& Channels) const;

	bool bTried = false;
	bool bReady = false;
	bool bStudent = true;
	FName EyeName = TEXT("aerial_student");
	FString ModelPath;

	FCriticalSection Lock;
	TArray<TPair<FIntPoint, FString>> Queue;
	TSet<FIntPoint> Queued;
	struct FScanned { TArray<FOsdsScanChannel> Channels; TArray<FOsdsClassTint> Tints; };
	TMap<FIntPoint, FScanned> Recent;
	TArray<FIntPoint> RecentOrder;
	bool bWorkerRunning = false;
	TAtomic<bool> bStop{false};

	TSharedPtr<class FOsdsAerialEyeModel> Model;

	// Statistics (under Lock).
	int32 Runs = 0, CacheHits = 0, Failures = 0, BackOffs = 0;
	double TotalSeconds = 0.0, MaxSeconds = 0.0;
	TArray<double> RunTimes;
};
