// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

// Forward declarations
class UMCPBridge;

/**
 * FMCPClientHandler
 *
 * Handles a single persistent client connection on its own thread.
 * Created by FMCPServer when a new client connects.
 */
class FMCPClientHandler : public FRunnable
{
public:
	FMCPClientHandler(FSocket* InClientSocket, UMCPBridge* InBridge, TAtomic<bool>& InServerStopping);
	virtual ~FMCPClientHandler();

	// FRunnable Interface
	virtual bool Init() override { return true; }
	virtual uint32 Run() override;
	virtual void Exit() override;

	/** Signal this handler to stop */
	void RequestStop() { bShouldStop = true; }

	/** Check if this handler has finished */
	bool IsFinished() const { return bIsFinished; }

private:
	/** Receive a message from client (length-prefixed JSON) */
	bool ReceiveMessage(FString& OutMessage);

	/** Send a response to client (length-prefixed JSON) */
	bool SendResponse(const FString& Response);

	/** Handle ping command (no game thread needed) */
	FString HandlePing();

	/** Handle close command */
	void HandleClose();

	/** Handle get_context command (no game thread needed) */
	FString HandleGetContext();

	/** Execute command on game thread and get response */
	FString ExecuteOnGameThread(const FString& CommandType, TSharedPtr<FJsonObject> Params);

	FSocket* ClientSocket;
	UMCPBridge* Bridge;
	FRunnableThread* Thread;
	TAtomic<bool>& bServerStopping;
	TAtomic<bool> bShouldStop;
	TAtomic<bool> bIsFinished;

	/** Connection timeout in seconds (increased from 120s to accommodate
	 *  long game-thread operations like blueprint compilation) */
	static constexpr float ConnectionTimeout = 300.0f;

	/** Receive buffer size */
	static constexpr int32 RecvBufferSize = 1024 * 1024;  // 1MB
};


/**
 * FMCPServer
 *
 * TCP server that accepts connections from MCP clients and routes
 * commands to the Bridge for execution.
 *
 * Supports multiple concurrent client connections, each handled on
 * its own thread via FMCPClientHandler.
 *
 * Key features:
 * - Persistent connections (socket stays open between commands)
 * - Multi-client support (one thread per connection)
 * - ping/close commands handled without game thread
 * - Timeout handling for stale connections
 */
class UEEDITORMCP_API FMCPServer : public FRunnable
{
public:
	FMCPServer(UMCPBridge* InBridge, int32 InPort = 55558);
	virtual ~FMCPServer();

	/** Start the server thread */
	bool Start();

	/** Stop the server thread */
	void Stop();

	/** Check if server is running */
	bool IsRunning() const { return bIsRunning; }

	// =========================================================================
	// FRunnable Interface
	// =========================================================================

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Exit() override;

private:
	/** Clean up finished client handlers */
	void CleanupFinishedHandlers();

	/** The bridge that owns this server */
	UMCPBridge* Bridge;

	/** Listener socket */
	FSocket* ListenerSocket;

	/** Port to listen on */
	int32 Port;

	/** Server accept-loop thread */
	FRunnableThread* Thread;

	/** Active client handlers */
	TArray<FMCPClientHandler*> ClientHandlers;

	/** Mutex for ClientHandlers array */
	FCriticalSection HandlersLock;

	/** Flag to signal thread to stop */
	TAtomic<bool> bShouldStop;

	/** Flag indicating if server is running */
	TAtomic<bool> bIsRunning;

	/** Guard against re-entrant Stop calls */
	TAtomic<bool> bIsStopping;

	/** Maximum concurrent clients */
	static constexpr int32 MaxClients = 8;
};
