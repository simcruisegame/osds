// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Environment/OsdsWeather.h"

/**
 * Live real-world data: the parsers and the rules that turn public real-time feeds
 * into the game's state. No network code here (see UOsdsLiveWorldSubsystem), so
 * every rule has a unit test with a recorded feed.
 *
 * Feeds (all free, no key, no sign-up):
 *   weather   Open-Meteo forecast API, "current" block (15-minute model analysis)
 *   metar     aviationweather.gov METAR JSON (observed at the nearest airport)
 *   air       Open-Meteo air-quality API (CAMS): aerosol optical depth, PM2.5, AQI
 *   sea       Open-Meteo marine API: waves, sea temperature, sea level with tides
 *   tide      NOAA CO-OPS water level (observed, US stations), datum MSL
 *   aircraft  adsb.lol ADS-B exchange (community receivers), aircraft within 25 nm
 *   state     US Census geocoder (the state of a point, to pick its road inventory)
 *   roads     FHWA HPMS road inventory (US public domain): measured daily traffic (AADT),
 *             trucks, lanes, speed limit and roughness (IRI) of every federal-aid road
 *   photos    Wikimedia Commons geosearch: real photographs near the car, with their licences
 */

/** Open-Meteo "current" weather. */
struct OSDS_API FOsdsLiveWeather
{
	bool bValid = false;
	FDateTime TimeUtc;
	float TemperatureC = 0.0f;
	float HumidityPct = 0.0f;
	float PrecipitationMm = 0.0f;
	int32 WmoCode = 0;
	/** Cloud cover fractions 0-1: total and the low, mid and high layers. */
	float CloudCover = 0.0f;
	float CloudLow = 0.0f;
	float CloudMid = 0.0f;
	float CloudHigh = 0.0f;
	float VisibilityM = 0.0f;
	float WindSpeedMs = 0.0f;
	float WindGustMs = 0.0f;
	/** Direction the wind comes FROM, degrees clockwise from north. */
	float WindFromDeg = 0.0f;
	float PressureHPa = 0.0f;
	bool bIsDay = true;
	/** The place's offset from UTC now (its time zone and daylight saving), seconds. */
	int32 UtcOffsetSeconds = 0;
};

/** One METAR observation (aviationweather.gov JSON row). */
struct OSDS_API FOsdsMetar
{
	FString Station;
	double Lat = 0.0;
	double Lon = 0.0;
	FDateTime TimeUtc;
	/** Highest-coverage layer as a fraction (CLR 0, FEW 0.19, SCT 0.44, BKN 0.78, OVC 1). */
	float CloudCover = 0.0f;
	/** Statute-mile visibility in metres; "10+" reads as 16 093 m. */
	float VisibilityM = 16093.0f;
	FString WxString;
	FString Raw;
	float Km = 0.0f;
};

/** CAMS air quality at the place. */
struct OSDS_API FOsdsLiveAir
{
	bool bValid = false;
	float Pm25 = 0.0f;
	float Pm10 = 0.0f;
	/** Column aerosol optical depth at 550 nm. */
	float AerosolOpticalDepth = -1.0f;
	float DustUgM3 = 0.0f;
	float UsAqi = 0.0f;
	float UvIndex = 0.0f;
};

/** Sea state: modelled waves and sea level, and an observed tide gauge when one is near. */
struct OSDS_API FOsdsLiveSea
{
	bool bValid = false;
	float WaveHeightM = 0.0f;
	float WavePeriodS = 0.0f;
	float WaveFromDeg = 0.0f;
	float SeaSurfaceTempC = 0.0f;
	/** Modelled sea level above mean sea level, tides included (m). */
	float SeaLevelM = 0.0f;
	/** Observed water level above MSL at a tide gauge, when there is one (m). */
	bool bTideObserved = false;
	float TideM = 0.0f;
	FString TideStation;
};

/** One aircraft from an ADS-B feed. */
struct OSDS_API FOsdsLiveAircraft
{
	FString Hex;
	FString Flight;
	FString Type;
	FString Registration;
	double Lat = 0.0;
	double Lon = 0.0;
	/** Height above mean sea level, m (GNSS height when reported, else barometric). */
	float AltitudeM = 0.0f;
	bool bOnGround = false;
	float GroundSpeedMs = 0.0f;
	/** Track over the ground, degrees clockwise from north. */
	float TrackDeg = 0.0f;
	float VerticalRateMs = 0.0f;
	/** Age of the position when the feed was read, s. */
	float PositionAgeS = 0.0f;
	/** ADS-B emitter category (A1 light .. A5 heavy, A7 rotorcraft). */
	FString Category;
};

/** One road section of the FHWA HPMS inventory. */
struct OSDS_API FOsdsRoadSection
{
	FString RouteId;
	FString Name;
	/** Annual average daily traffic, both directions (vehicles a day). */
	int32 Aadt = 0;
	/** Daily single-unit and combination trucks. */
	int32 Trucks = 0;
	int32 Lanes = 0;
	int32 SpeedLimitMph = 0;
	/** Functional class 1 (interstate) .. 7 (local). */
	int32 FunctionalClass = 0;
	/** International Roughness Index, inches per mile (0 = not measured). */
	float IriInPerMile = 0.0f;
	/** Polyline, longitude/latitude degrees. */
	TArray<FVector2d> LonLat;
};

/** A photograph of the place (Wikimedia Commons). */
struct OSDS_API FOsdsPlacePhoto
{
	FString Title;
	FString PageUrl;
	FString ThumbUrl;
	double Lat = 0.0;
	double Lon = 0.0;
	FString License;
	FString Artist;
	FString Taken;
	int32 Width = 0;
	int32 Height = 0;
	/** The 1024 px copy on this computer once read (Saved/Live/photos), else empty. */
	FString LocalFile;
};

/** A NOAA CO-OPS station that reports water levels. */
struct OSDS_API FOsdsTideStation
{
	FString Id;
	FString Name;
	double Lat = 0.0;
	double Lon = 0.0;
};

struct OSDS_API FOsdsLiveData
{
	// URLs

	static FString WeatherUrl(double Lat, double Lon);
	static FString AirUrl(double Lat, double Lon);
	static FString MarineUrl(double Lat, double Lon);
	/** METARs of the last two hours in a box of +-0.65 degrees latitude, +-0.75 longitude. */
	static FString MetarUrl(double Lat, double Lon);
	static FString TideStationsUrl();
	static FString TideUrl(const FString& StationId);
	static FString AircraftUrl(double Lat, double Lon, int32 RadiusNm);
	static FString StateUrl(double Lat, double Lon);
	/** HPMS road sections with traffic counts in a box of +-HalfKm around the place. */
	static FString RoadsUrl(const FString& StateCode, double Lat, double Lon, double HalfKm = 1.5);
	static FString PhotosUrl(double Lat, double Lon, int32 RadiusM = 600);

	// Parsers. Each returns false with a reason when the text is not the feed it expects.

	static bool ParseWeather(const FString& Json, FOsdsLiveWeather& Out, FString& OutError);
	static bool ParseAir(const FString& Json, FOsdsLiveAir& Out, FString& OutError);
	static bool ParseMarine(const FString& Json, FOsdsLiveSea& Out, FString& OutError);
	static bool ParseMetars(const FString& Json, TArray<FOsdsMetar>& Out, FString& OutError);
	static bool ParseTideStations(const FString& Json, TArray<FOsdsTideStation>& Out, FString& OutError);
	/** Latest NOAA water level (datum MSL), m, and its time. */
	static bool ParseTide(const FString& Json, float& OutLevelM, FDateTime& OutUtc, FString& OutError);
	static bool ParseAircraft(const FString& Json, TArray<FOsdsLiveAircraft>& Out, FString& OutError);
	static bool ParseState(const FString& Json, FString& OutCode, FString& OutError);
	static bool ParseRoads(const FString& Json, TArray<FOsdsRoadSection>& Out, FString& OutError);
	/** Only photographs under a free licence (CC BY, CC BY-SA, CC0, public domain) are kept. */
	static bool ParsePhotos(const FString& Json, TArray<FOsdsPlacePhoto>& Out, FString& OutError);

	// Traffic patterns

	/**
	 * Share of a day's traffic in one local hour (0-23), for weekdays and for weekends:
	 * typical urban hourly distributions after the FHWA Traffic Monitoring Guide (weekday
	 * peaks near 7-8 % of the day at 7-8 h and 16-18 h; the weekend's one broad midday hump).
	 * Each 24-hour curve sums to 1.
	 */
	static float HourShare(int32 LocalHour, bool bWeekend);

	/** Day-of-week factor on the average day (0 = Sunday): Friday busiest, Sunday quietest. */
	static float DayFactor(int32 DayOfWeek);

	/** Vehicles an hour on a road section at a local time, both directions; rain takes off about 5 %, snow 15 %. */
	static float FlowPerHour(const FOsdsRoadSection& Road, const FDateTime& Local, bool bRain, bool bSnow);

	/** Vehicles per km on each lane at that flow: flow / (lanes x speed), at the speed limit (else 30 mph). */
	static float DensityPerKmLane(const FOsdsRoadSection& Road, float FlowPerHour);

	/** The section nearest a place within MaxM metres, or nullptr; its distance in OutM. */
	static const FOsdsRoadSection* NearestRoad(const TArray<FOsdsRoadSection>& Roads, double Lat, double Lon, double MaxM, double& OutM);

	// Rules

	/** WMO weather interpretation codes (Open-Meteo weather_code). */
	static bool IsRainCode(int32 Wmo);
	static bool IsSnowCode(int32 Wmo);
	static bool IsFogCode(int32 Wmo);
	static FString DescribeWmo(int32 Wmo);

	/** The nearest METAR within MaxKm and MaxAgeMinutes of Now, or nullptr. */
	static const FOsdsMetar* NearestMetar(const TArray<FOsdsMetar>& Metars, double Lat, double Lon, const FDateTime& NowUtc,
		double MaxKm = 40.0, double MaxAgeMinutes = 90.0);

	/** The nearest tide station within MaxKm, or nullptr. */
	static const FOsdsTideStation* NearestTideStation(const TArray<FOsdsTideStation>& Stations, double Lat, double Lon, double MaxKm = 30.0);

	/**
	 * Fuses the feeds into the conditions the sky renders. The model analysis gives the
	 * full set; an airport observation close in space and time overrides what it measures
	 * (sky cover, visibility, precipitation), because it is what a person there sees now.
	 */
	static FOsdsWeatherConditions Fuse(const FOsdsLiveWeather* Weather, const FOsdsMetar* Metar, const FOsdsLiveAir* Air,
		const FOsdsWeatherConditions& Base);

	/**
	 * Where an aircraft is DeltaSeconds after its report, by dead reckoning along its
	 * track at its ground speed and vertical rate (great-circle step, metres to degrees).
	 */
	static void Extrapolate(const FOsdsLiveAircraft& Aircraft, double DeltaSeconds, double& OutLat, double& OutLon, float& OutAltitudeM);

	/** Great-circle distance, km. */
	static double DistanceKm(double LatA, double LonA, double LatB, double LonB);

	/**
	 * How far below the local horizontal plane the Earth's surface lies at a ground distance
	 * (m), including standard refraction (k = 0.13): far aircraft sit lower in a flat world.
	 */
	static double CurvatureDropM(double DistanceM);

	/** 16-point compass name of a direction in degrees ("SSE"). */
	static FString Compass(float Degrees);

	/** One line for the HUD and logs. */
	static FString Summary(const FOsdsLiveWeather* Weather, const FOsdsMetar* Metar, const FOsdsLiveAir* Air,
		const FOsdsLiveSea* Sea, int32 AircraftCount);
};
