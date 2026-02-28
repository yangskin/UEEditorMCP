// Copyright (c) 2025 zolnoor. All rights reserved.

#include "MCPServer.h"
#include "MCPBridge.h"
#include "Actions/EditorAction.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"

// =============================================================================
// FMCPClientHandler - per-client thread
// =============================================================================

FMCPClientHandler::FMCPClientHandler(FSocket* InClientSocket, UMCPBridge* InBridge, TAtomic<bool>& InServerStopping)
	: ClientSocket(InClientSocket)
	, Bridge(InBridge)
	, Thread(nullptr)
	, bServerStopping(InServerStopping)
	, bShouldStop(false)
	, bIsFinished(false)
{
	// Start the handler thread
	Thread = FRunnableThread::Create(this, TEXT("UEEditorMCP Client Handler"));
}

FMCPClientHandler::~FMCPClientHandler()
{
	bShouldStop = true;

	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}

	if (ClientSocket)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(ClientSocket);
		}
		ClientSocket = nullptr;
	}
}

uint32 FMCPClientHandler::Run()
{
	// Set socket options
	ClientSocket->SetNonBlocking(false);
	ClientSocket->SetNoDelay(true);

	float LastActivityTime = FPlatformTime::Seconds();

	while (!bShouldStop && !bServerStopping)
	{
		// Check for timeout
		float CurrentTime = FPlatformTime::Seconds();
		if (CurrentTime - LastActivityTime > ConnectionTimeout)
		{
			UE_LOG(LogMCP, Warning, TEXT("UEEditorMCP: Client connection timed out"));
			break;
		}

		// Wait for socket to become readable (data available OR connection closed)
		// This uses select() internally �?reliable and avoids non-blocking toggle issues
		if (!ClientSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(0.5)))
		{
			// Timeout �?no data yet, loop to check stop flags and timeout
			continue;
		}

		// Socket is readable. Either data is available or the connection was closed (EOF).
		// Try to receive the length-prefixed message. If the connection was closed,
		// ReceiveMessage will fail when trying to read the 4-byte length header.
		FString Message;
		if (!ReceiveMessage(Message))
		{
			UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Client disconnected or receive failed"));
			break;
		}

		LastActivityTime = FPlatformTime::Seconds();

		// Parse JSON
		TSharedPtr<FJsonObject> JsonObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
		if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
		{
			FString ErrorResponse = TEXT("{\"status\":\"error\",\"error\":\"Invalid JSON\"}");
			SendResponse(ErrorResponse);
			continue;
		}

		// Get command type
		FString CommandType;
		if (!JsonObj->TryGetStringField(TEXT("type"), CommandType))
		{
			FString ErrorResponse = TEXT("{\"status\":\"error\",\"error\":\"Missing 'type' field\"}");
			SendResponse(ErrorResponse);
			continue;
		}

		// Handle special commands that don't need game thread
		if (CommandType == TEXT("ping"))
		{
			SendResponse(HandlePing());
			continue;
		}

		if (CommandType == TEXT("close"))
		{
			HandleClose();
			break;
		}

		if (CommandType == TEXT("get_context"))
		{
			SendResponse(HandleGetContext());
			continue;
		}

		// Get params (optional)
		TSharedPtr<FJsonObject> Params;
		const TSharedPtr<FJsonObject>* ParamsPtr;
		if (JsonObj->TryGetObjectField(TEXT("params"), ParamsPtr))
		{
			Params = *ParamsPtr;
		}
		else
		{
			Params = MakeShared<FJsonObject>();
		}

		// Execute on game thread and get response
		FString Response = ExecuteOnGameThread(CommandType, Params);
		SendResponse(Response);
	}

	bIsFinished = true;
	return 0;
}

void FMCPClientHandler::Exit()
{
	bIsFinished = true;
}

bool FMCPClientHandler::ReceiveMessage(FString& OutMessage)
{
	// Receive length prefix (4 bytes, big endian)
	uint8 LengthBytes[4];
	int32 BytesRead = 0;
	if (!ClientSocket->Recv(LengthBytes, 4, BytesRead) || BytesRead != 4)
	{
		return false;
	}

	int32 Length = (LengthBytes[0] << 24) | (LengthBytes[1] << 16) | (LengthBytes[2] << 8) | LengthBytes[3];

	// Sanity check
	if (Length <= 0 || Length > RecvBufferSize)
	{
		UE_LOG(LogMCP, Warning, TEXT("UEEditorMCP: Invalid message length: %d"), Length);
		return false;
	}

	// Receive message
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(Length);

	int32 TotalReceived = 0;
	while (TotalReceived < Length)
	{
		int32 Received = 0;
		if (!ClientSocket->Recv(Buffer.GetData() + TotalReceived, Length - TotalReceived, Received) || Received <= 0)
		{
			return false;
		}
		TotalReceived += Received;
	}

	// Convert to string
	OutMessage = FString(Length, UTF8_TO_TCHAR(reinterpret_cast<const char*>(Buffer.GetData())));
	return true;
}

bool FMCPClientHandler::SendResponse(const FString& Response)
{
	// Convert to UTF-8
	FTCHARToUTF8 Converter(*Response);
	int32 Length = Converter.Length();

	// Send length prefix (4 bytes, big endian)
	uint8 LengthBytes[4] = {
		static_cast<uint8>((Length >> 24) & 0xFF),
		static_cast<uint8>((Length >> 16) & 0xFF),
		static_cast<uint8>((Length >> 8) & 0xFF),
		static_cast<uint8>(Length & 0xFF)
	};

	int32 BytesSent = 0;
	if (!ClientSocket->Send(LengthBytes, 4, BytesSent) || BytesSent != 4)
	{
		return false;
	}

	// Send message
	int32 TotalSent = 0;
	while (TotalSent < Length)
	{
		int32 Sent = 0;
		if (!ClientSocket->Send(reinterpret_cast<const uint8*>(Converter.Get()) + TotalSent, Length - TotalSent, Sent) || Sent <= 0)
		{
			return false;
		}
		TotalSent += Sent;
	}

	return true;
}

FString FMCPClientHandler::HandlePing()
{
	return TEXT("{\"status\":\"success\",\"result\":{\"pong\":true}}");
}

void FMCPClientHandler::HandleClose()
{
	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Client requested disconnect"));
	SendResponse(TEXT("{\"status\":\"success\",\"result\":{\"closed\":true}}"));
}

FString FMCPClientHandler::HandleGetContext()
{
	FString Result;
	FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(false);

	AsyncTask(ENamedThreads::GameThread, [this, &Result, DoneEvent]()
	{
		// Scope guard: ensure DoneEvent->Trigger() is always called,
		// even if an unhandled exception occurs in the lambda body.
		struct FTriggerOnExit
		{
			FEvent* Event;
			~FTriggerOnExit() { Event->Trigger(); }
		} TriggerGuard{DoneEvent};

		if (!Bridge)
		{
			Result = TEXT("{\"status\":\"error\",\"error\":\"Bridge not available\"}");
			return;
		}

		try
		{
			TSharedPtr<FJsonObject> ContextJson = Bridge->GetContext().ToJson();

			TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("status"), TEXT("success"));
			Response->SetObjectField(TEXT("result"), ContextJson);

			FString ResponseStr;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
			FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
			Result = ResponseStr;
		}
		catch (...)
		{
			UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Exception in HandleGetContext"));
			Result = TEXT("{\"status\":\"error\",\"error\":\"Exception during get_context\"}");
		}
	});

	DoneEvent->Wait();
	FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

	return Result;
}

FString FMCPClientHandler::ExecuteOnGameThread(const FString& CommandType, TSharedPtr<FJsonObject> Params)
{
	// Heap-allocate Result so the lambda never writes to a dangling stack variable on timeout.
	auto Result = MakeShared<FString>();
	FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(false);

	// Shared flag: when the caller times out it sets this to false,
	// telling the lambda to return DoneEvent to the pool.
	auto bCallerWaiting = MakeShared<TAtomic<bool>>(true);

	// Capture CommandType by value to avoid dangling reference on timeout.
	AsyncTask(ENamedThreads::GameThread, [this, CmdType = CommandType, Params, Result, DoneEvent, bCallerWaiting]()
	{
		// Scope guard: always trigger DoneEvent even if an exception propagates.
		// If the caller already timed out, also return the event to the pool.
		struct FCleanupGuard
		{
			FEvent* Event;
			TSharedPtr<TAtomic<bool>> CallerWaiting;
			~FCleanupGuard()
			{
				Event->Trigger();
				if (!CallerWaiting->Load())
				{
					FPlatformProcess::ReturnSynchEventToPool(Event);
				}
			}
		} Guard{DoneEvent, bCallerWaiting};

		if (!Bridge)
		{
			*Result = TEXT("{\"status\":\"error\",\"error\":\"Bridge not available\"}");
			return;
		}

		TSharedPtr<FJsonObject> Response;
		try
		{
			Response = Bridge->ExecuteCommandSafe(CmdType, Params);
		}
		catch (...)
		{
			UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Unhandled exception in ExecuteCommandSafe for '%s'"), *CmdType);
		}

		if (Response.IsValid())
		{
			FString ResponseStr;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
			FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
			*Result = ResponseStr;
		}
		else
		{
			*Result = FString::Printf(
				TEXT("{\"status\":\"error\",\"error\":\"Command '%s' returned null response\"}"),
				*CmdType);
		}
	});

	// Wait with timeout protection (240s). If the game thread is blocked for
	// longer than this (e.g., during modal dialogs or heavy compilation),
	// return a timeout error rather than hanging forever.
	static constexpr uint32 GameThreadTimeoutMs = 240000;
	if (!DoneEvent->Wait(GameThreadTimeoutMs))
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Game thread execution timed out after %ds for command '%s'"),
			GameThreadTimeoutMs / 1000, *CommandType);
		// Signal lambda to return the event to pool when it eventually runs.
		// Do NOT return to pool here — the lambda still holds a pointer to it.
		bCallerWaiting->Store(false);
		return FString::Printf(
			TEXT("{\"status\":\"error\",\"error\":\"Game thread execution timed out after %ds for command: %s\"}"),
			GameThreadTimeoutMs / 1000, *CommandType);
	}

	FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

	return MoveTemp(*Result);
}

// =============================================================================
// FMCPServer - accept loop, spawns FMCPClientHandler per connection
// =============================================================================

FMCPServer::FMCPServer(UMCPBridge* InBridge, int32 InPort)
	: Bridge(InBridge)
	, ListenerSocket(nullptr)
	, Port(InPort)
	, Thread(nullptr)
	, bShouldStop(false)
	, bIsRunning(false)
	, bIsStopping(false)
{
}

FMCPServer::~FMCPServer()
{
	Stop();
}

bool FMCPServer::Start()
{
	if (bIsRunning)
	{
		return true;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Failed to get socket subsystem"));
		return false;
	}

	ListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UEEditorMCP Listener"), false);
	if (!ListenerSocket)
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Failed to create listener socket"));
		return false;
	}

	// Allow address reuse to avoid TIME_WAIT issues on restart
	ListenerSocket->SetReuseAddr(true);

	// Bind to localhost only (127.0.0.1) - NOT exposed to network
	TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	bool bIsValid = false;
	Addr->SetIp(TEXT("127.0.0.1"), bIsValid);
	Addr->SetPort(Port);

	if (!ListenerSocket->Bind(*Addr))
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Failed to bind to port %d"), Port);
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return false;
	}

	if (!ListenerSocket->Listen(MaxClients))
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Failed to listen on socket"));
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return false;
	}

	bShouldStop = false;
	Thread = FRunnableThread::Create(this, TEXT("UEEditorMCP Server Thread"));
	if (!Thread)
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Failed to create server thread"));
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return false;
	}

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Server started on port %d (max %d clients)"), Port, MaxClients);
	return true;
}

void FMCPServer::Stop()
{
	if (bIsStopping)
	{
		return;
	}

	bIsStopping = true;
	bShouldStop = true;

	// Close listener to unblock WaitForPendingConnection
	if (ListenerSocket)
	{
		ListenerSocket->Close();
	}

	// Wait for accept loop to exit
	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}

	// Stop and delete all client handlers
	{
		FScopeLock Lock(&HandlersLock);
		for (FMCPClientHandler* Handler : ClientHandlers)
		{
			Handler->RequestStop();
			delete Handler;  // destructor waits for thread + destroys socket
		}
		ClientHandlers.Empty();
	}

	// Destroy listener socket
	if (ListenerSocket)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(ListenerSocket);
		}
		ListenerSocket = nullptr;
	}

	bIsRunning = false;
	bIsStopping = false;
	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Server stopped"));
}

bool FMCPServer::Init()
{
	return true;
}

uint32 FMCPServer::Run()
{
	bIsRunning = true;

	while (!bShouldStop)
	{
		// Clean up finished handlers periodically
		CleanupFinishedHandlers();

		// Wait for connection (with timeout so we can check bShouldStop)
		bool bPendingConnection = false;
		if (ListenerSocket->WaitForPendingConnection(bPendingConnection, FTimespan::FromSeconds(0.5)))
		{
			if (bPendingConnection)
			{
				FSocket* ClientSocket = ListenerSocket->Accept(TEXT("UEEditorMCP Client"));
				if (ClientSocket)
				{
					FScopeLock Lock(&HandlersLock);

					// Check if at capacity
					if (ClientHandlers.Num() >= MaxClients)
					{
						UE_LOG(LogMCP, Warning, TEXT("UEEditorMCP: Max clients (%d) reached, rejecting connection"), MaxClients);
						ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
						if (SocketSubsystem)
						{
							SocketSubsystem->DestroySocket(ClientSocket);
						}
						continue;
					}

					UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Client connected (total: %d)"), ClientHandlers.Num() + 1);

					// Create a handler that runs on its own thread
					FMCPClientHandler* Handler = new FMCPClientHandler(ClientSocket, Bridge, bShouldStop);
					ClientHandlers.Add(Handler);
				}
			}
		}
	}

	bIsRunning = false;
	return 0;
}

void FMCPServer::Exit()
{
	bIsRunning = false;
}

void FMCPServer::CleanupFinishedHandlers()
{
	FScopeLock Lock(&HandlersLock);

	for (int32 i = ClientHandlers.Num() - 1; i >= 0; --i)
	{
		if (ClientHandlers[i]->IsFinished())
		{
			UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Cleaning up finished client handler (remaining: %d)"), ClientHandlers.Num() - 1);
			delete ClientHandlers[i];
			ClientHandlers.RemoveAt(i);
		}
	}
}
