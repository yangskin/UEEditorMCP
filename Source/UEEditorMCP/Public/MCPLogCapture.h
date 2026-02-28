// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"

/**
 * FMCPLogCapture
 *
 * Custom FOutputDevice that hooks into GLog to capture editor log output.
 * Maintains a ring buffer of recent log entries, filterable by category
 * and verbosity.  Used by FGetEditorLogsAction to expose structured logs
 * to the MCP Python layer.
 *
 * Thread-safe: Serialize() can be called from any thread; queries lock.
 */
class UEEDITORMCP_API FMCPLogCapture : public FOutputDevice
{
public:
	/** Singleton accessor */
	static FMCPLogCapture& Get();

	/** Start capturing — adds this device to GLog */
	void Start();

	/** Stop capturing — removes this device from GLog */
	void Stop();

	/** Is capture currently active? */
	bool IsCapturing() const { return bCapturing; }

	// =====================================================================
	// Data
	// =====================================================================

	struct FLogEntry
	{
		uint64 Seq = 0;
		FDateTime TimestampUtc;
		double Timestamp = 0.0;
		FName Category;
		ELogVerbosity::Type Verbosity;
		FString Message;
		int32 MessageBytes = 0;
	};

	/**
	 * Get the most recent log entries.
	 *
	 * @param Count        Max entries to return (clamped to buffer size)
	 * @param CategoryFilter  If non-empty, only entries matching this category
	 * @param MinVerbosity    Minimum verbosity level (default: All)
	 * @return Array of matching entries (newest last)
	 */
	TArray<FLogEntry> GetRecent(
		int32 Count = 100,
		const FString& CategoryFilter = TEXT(""),
		ELogVerbosity::Type MinVerbosity = ELogVerbosity::All) const;

	/**
	 * Get entries newer than the given sequence id with optional filtering.
	 * @param AfterSeq         Return entries where Seq > AfterSeq.
	 * @param MaxLines         Max matched entries to return.
	 * @param MaxBytes         Approx max UTF-8 payload bytes for matched entries.
	 * @param CategoryFilters  Optional category filters (contains match, case-insensitive).
	 * @param MinVerbosity     Minimum severity level.
	 * @param ContainsFilter   Optional substring filter for message.
	 * @param OutTruncated     True when results were trimmed by MaxLines/MaxBytes.
	 * @param OutLastSeq       Seq of the newest entry in the returned batch (or latest buffer seq if none matched).
	 */
	TArray<FLogEntry> GetSince(
		uint64 AfterSeq,
		int32 MaxLines,
		int32 MaxBytes,
		const TArray<FString>& CategoryFilters,
		ELogVerbosity::Type MinVerbosity,
		const FString& ContainsFilter,
		bool& OutTruncated,
		uint64& OutLastSeq) const;

	/** Last captured entry sequence id (0 if empty). */
	uint64 GetLatestSeq() const;

	/** Timestamp of latest captured entry (UTC min if empty). */
	FDateTime GetLastReceivedUtc() const;

	/** Whether any log line was captured in the last N seconds. */
	bool HasRecentData(double RecentWindowSeconds) const;

	/** Clear all captured entries */
	void Clear();

	/** Total entries captured since start (including those rolled off) */
	int64 GetTotalCaptured() const { return TotalCaptured; }

protected:
	// FOutputDevice interface
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;

private:
	FMCPLogCapture();
	~FMCPLogCapture();

	mutable FCriticalSection Lock;
	TArray<FLogEntry> RingBuffer;
	int32 HeadIndex = 0;
	int32 Count = 0;
	int64 TotalBytes = 0;
	bool bCapturing = false;
	int64 TotalCaptured = 0;
	uint64 NextSeq = 1;
	FDateTime LastReceivedUtc;

	static constexpr int32 BufferCapacity = 10000;
	static constexpr int64 MaxBufferBytes = 5 * 1024 * 1024;
};
