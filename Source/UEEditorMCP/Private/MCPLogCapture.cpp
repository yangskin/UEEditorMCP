// Copyright (c) 2025 zolnoor. All rights reserved.

#include "MCPLogCapture.h"
#include "Actions/EditorAction.h"

namespace
{
bool CategoryMatches(const FName& Category, const TArray<FString>& Filters)
{
	if (Filters.Num() == 0)
	{
		return true;
	}

	const FString CategoryString = Category.ToString();
	for (const FString& Filter : Filters)
	{
		if (!Filter.IsEmpty() && CategoryString.Contains(Filter, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

bool ContainsMatches(const FString& Message, const FString& ContainsFilter)
{
	if (ContainsFilter.IsEmpty())
	{
		return true;
	}
	return Message.Contains(ContainsFilter, ESearchCase::IgnoreCase);
}
}

// ============================================================================
// Singleton
// ============================================================================

FMCPLogCapture& FMCPLogCapture::Get()
{
	static FMCPLogCapture Instance;
	return Instance;
}

FMCPLogCapture::FMCPLogCapture()
{
	RingBuffer.SetNum(BufferCapacity);
	LastReceivedUtc = FDateTime::MinValue();
}

FMCPLogCapture::~FMCPLogCapture()
{
	Stop();
}

// ============================================================================
// Start / Stop
// ============================================================================

void FMCPLogCapture::Start()
{
	if (bCapturing)
	{
		return;
	}
	if (!GLog)
	{
		return;
	}
	GLog->AddOutputDevice(this);
	bCapturing = true;
	UE_LOG(LogMCP, Log, TEXT("MCPLogCapture: Started capturing editor logs (buffer=%d)"), BufferCapacity);
}

void FMCPLogCapture::Stop()
{
	if (!bCapturing)
	{
		return;
	}
	if (GLog)
	{
		GLog->RemoveOutputDevice(this);
	}
	bCapturing = false;
}

// ============================================================================
// FOutputDevice::Serialize
// ============================================================================

void FMCPLogCapture::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
	FScopeLock ScopeLock(&Lock);

	FLogEntry Entry;
	Entry.Seq = NextSeq++;
	Entry.TimestampUtc = FDateTime::UtcNow();
	Entry.Timestamp = FPlatformTime::Seconds();
	Entry.Category = Category;
	Entry.Verbosity = Verbosity;

	// Truncate extremely long messages to prevent memory bloat in the ring buffer.
	static constexpr int32 MaxMessageLen = 8192;
	const int32 Len = FCString::Strlen(V);
	if (Len > MaxMessageLen)
	{
		Entry.Message = FString(MaxMessageLen, V);
		Entry.Message.Append(TEXT("... [truncated]"));
	}
	else
	{
		Entry.Message = V;
	}
	Entry.MessageBytes = FTCHARToUTF8(*Entry.Message).Length();
	LastReceivedUtc = Entry.TimestampUtc;

	if (Count >= BufferCapacity)
	{
		const FLogEntry& Oldest = RingBuffer[HeadIndex];
		TotalBytes = FMath::Max<int64>(0, TotalBytes - Oldest.MessageBytes);
		HeadIndex = (HeadIndex + 1) % BufferCapacity;
		Count = BufferCapacity - 1;
	}

	const int32 InsertIndex = (HeadIndex + Count) % BufferCapacity;
	RingBuffer[InsertIndex] = Entry;
	++Count;
	TotalBytes += Entry.MessageBytes;

	while (Count > 1 && TotalBytes > MaxBufferBytes)
	{
		const FLogEntry& Oldest = RingBuffer[HeadIndex];
		TotalBytes = FMath::Max<int64>(0, TotalBytes - Oldest.MessageBytes);
		HeadIndex = (HeadIndex + 1) % BufferCapacity;
		--Count;
	}

	++TotalCaptured;
}

// ============================================================================
// Query
// ============================================================================

TArray<FMCPLogCapture::FLogEntry> FMCPLogCapture::GetRecent(
	int32 InCount,
	const FString& CategoryFilter,
	ELogVerbosity::Type MinVerbosity) const
{
	FScopeLock ScopeLock(&Lock);

	const int32 TotalInBuffer = Count;
	TArray<FLogEntry> Result;
	Result.Reserve(FMath::Min(InCount, TotalInBuffer));

	// Walk backwards from newest to oldest
	for (int32 i = 0; i < TotalInBuffer && Result.Num() < InCount; ++i)
	{
		const int32 Idx = (HeadIndex + TotalInBuffer - 1 - i + BufferCapacity) % BufferCapacity;
		const FLogEntry& Entry = RingBuffer[Idx];

		// Filter by category
		if (!CategoryFilter.IsEmpty() && !Entry.Category.ToString().Contains(CategoryFilter))
		{
			continue;
		}

		// Filter by verbosity (lower numeric value = higher severity)
		if (MinVerbosity != ELogVerbosity::All && Entry.Verbosity > MinVerbosity)
		{
			continue;
		}

		Result.Add(Entry);
	}

	// Reverse so oldest is first, newest is last
	Algo::Reverse(Result);
	return Result;
}

TArray<FMCPLogCapture::FLogEntry> FMCPLogCapture::GetSince(
	uint64 AfterSeq,
	int32 MaxLines,
	int32 MaxBytes,
	const TArray<FString>& CategoryFilters,
	ELogVerbosity::Type MinVerbosity,
	const FString& ContainsFilter,
	bool& OutTruncated,
	uint64& OutLastSeq) const
{
	FScopeLock ScopeLock(&Lock);

	OutTruncated = false;
	if (Count > 0)
	{
		const int32 LatestIndex = (HeadIndex + Count - 1) % BufferCapacity;
		OutLastSeq = RingBuffer[LatestIndex].Seq;
	}
	else
	{
		OutLastSeq = 0;
	}

	const int32 SafeMaxLines = FMath::Clamp(MaxLines, 20, 2000);
	const int32 SafeMaxBytes = FMath::Clamp(MaxBytes, 8192, 1024 * 1024);

	TArray<FLogEntry> Matched;
	Matched.Reserve(FMath::Min(SafeMaxLines, Count));

	int32 AccumulatedBytes = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		const int32 Idx = (HeadIndex + i) % BufferCapacity;
		const FLogEntry& Entry = RingBuffer[Idx];

		if (Entry.Seq <= AfterSeq)
		{
			continue;
		}

		if (MinVerbosity != ELogVerbosity::All && Entry.Verbosity > MinVerbosity)
		{
			continue;
		}

		if (!CategoryMatches(Entry.Category, CategoryFilters))
		{
			continue;
		}

		if (!ContainsMatches(Entry.Message, ContainsFilter))
		{
			continue;
		}

		const int32 EntryBytes = FMath::Max(Entry.MessageBytes, 1);
		if ((Matched.Num() >= SafeMaxLines) || (AccumulatedBytes + EntryBytes > SafeMaxBytes))
		{
			OutTruncated = true;
			break;
		}

		Matched.Add(Entry);
		AccumulatedBytes += EntryBytes;
		OutLastSeq = Entry.Seq;
	}

	return Matched;
}

uint64 FMCPLogCapture::GetLatestSeq() const
{
	FScopeLock ScopeLock(&Lock);
	if (Count <= 0)
	{
		return 0;
	}
	const int32 LatestIndex = (HeadIndex + Count - 1) % BufferCapacity;
	return RingBuffer[LatestIndex].Seq;
}

FDateTime FMCPLogCapture::GetLastReceivedUtc() const
{
	FScopeLock ScopeLock(&Lock);
	return LastReceivedUtc;
}

bool FMCPLogCapture::HasRecentData(double RecentWindowSeconds) const
{
	FScopeLock ScopeLock(&Lock);
	if (LastReceivedUtc == FDateTime::MinValue())
	{
		return false;
	}

	const FTimespan Elapsed = FDateTime::UtcNow() - LastReceivedUtc;
	return Elapsed.GetTotalSeconds() <= RecentWindowSeconds;
}

void FMCPLogCapture::Clear()
{
	FScopeLock ScopeLock(&Lock);
	HeadIndex = 0;
	Count = 0;
	TotalBytes = 0;
}
