#include "core.h"
#include "undoc.h"
#include "socket.h"
#include "intercom.h"
#include "except.h"
#include "scanner.h"

///////////////////////////////////////////////////////////////
// Private Variables
///////////////////////////////////////////////////////////////

static BOOL sDriverIsShuttingDown = FALSE;
static KEVENT sTcpServerStoppedEvent = { 0 };
static HANDLE sTcpServerThreadHandle = INVALID_HANDLE_VALUE;

///////////////////////////////////////////////////////////////
// Private API
///////////////////////////////////////////////////////////////

NTSTATUS
KmScanRequest(
  PKSOCKET Socket);

VOID
KmTcpServerThread(
  PVOID Context);

///////////////////////////////////////////////////////////////
// Implementation
///////////////////////////////////////////////////////////////

NTSTATUS
KmScanRequest(
  PKSOCKET Socket)
{
  NTSTATUS status = STATUS_UNSUCCESSFUL;

  // Raise IRQ to dispatch level
  KIRQL prevInterruptRequestLevel = PASSIVE_LEVEL;
  KeRaiseIrql(DISPATCH_LEVEL, &prevInterruptRequestLevel);

  __try
  {
    // Receive scan settings
    REQUEST_SCAN requestScan = { 0 };
    UINT32 requestScanSize = sizeof(requestScan);
    status = KmRecv(Socket, &requestScan, &requestScanSize, 0);
    if (NT_SUCCESS(status))
    {
      // Open process
      PEPROCESS process = NULL;
      status = PsLookupProcessByProcessId((HANDLE)requestScan.Pid, &process);
      if (NT_SUCCESS(status))
      {
        // Allocate buffer to hold bytes to scan for
        PBYTE requestBytes = ExAllocatePoolWithTag(NonPagedPool, requestScan.NumberOfBytes, MEMORY_TAG);
        if (requestBytes)
        {
          // Receive bytes to scan for
          UINT32 requestBytesSize = requestScan.NumberOfBytes;
          status = KmRecv(Socket, requestBytes, &requestBytesSize, 0);
          if (NT_SUCCESS(status))
          {
            // Receive scan type and start scanning
            SCAN_TYPE scanType = { 0 };
            UINT32 scanTypeSize = sizeof(scanType);
            status = KmRecv(Socket, &scanType, &scanTypeSize, 0);
            if (NT_SUCCESS(status))
            {
              switch (scanType)
              {
                case SCAN_TYPE_RESET: KmResetScanner(); break;
                case SCAN_TYPE_FIRST_ARRAY_OF_BYTES: KmFirstScanArrayOfBytes((PVOID)process->DirectoryTableBase, requestScan.NumberOfBytes, requestBytes); break;
                case SCAN_TYPE_NEXT_CHANGED: KmNextScanChanged(); break;
                case SCAN_TYPE_NEXT_UNCHANGED: KmNextScanUnchanged(); break;
                case SCAN_TYPE_UNDO: KmUndoScanOperation(); break;
              }
            }

            // Print current findings
            KmPrintScanResults();

            // Send results to client
            //UINT32 sendBufferSize = sizeof(sendBuffer);
            //status = KmSend(Socket, sendBuffer, &sendBufferSize, 0);
          }

          // Free requested bytes
          ExFreePoolWithTag(requestBytes, MEMORY_TAG);
        }

        // Close process
        ObDereferenceObject(process);
      }
      else
      {
        LOG("Invalid process\n");
      }
    }
  }
  __except (DEFAULT_EXCEPTION_HANDLER)
  {
    status = STATUS_UNHANDLED_EXCEPTION;
  }

  // Lower IRQ to previous level
  KeLowerIrql(prevInterruptRequestLevel);

  return status;
}

VOID
KmTcpServerThread(
  PVOID Context)
{
  UNREFERENCED_PARAMETER(Context);

  NTSTATUS status = STATUS_UNSUCCESSFUL;

  // Initialize winsock
  status = KmInitializeWsk();
  if (NT_SUCCESS(status))
  {
    LOG("TCP server started\n");

    __try
    {
      // Create sockets
      PKSOCKET serverSocket = NULL;
      status = KmCreateListenSocket(&serverSocket, (ADDRESS_FAMILY)AF_INET, (UINT16)SOCK_STREAM, IPPROTO_TCP);
      if (NT_SUCCESS(status))
      {
        // Bind local address and port
        SOCKADDR_IN address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = HTONS(9095);
        status = KmBind(serverSocket, (PSOCKADDR)&address);
        if (NT_SUCCESS(status))
        {
          LOG("Listening...\n");

          while (sDriverIsShuttingDown == FALSE)
          {
            // Accept client connection
            PKSOCKET clientSocket = NULL;
            status = KmAccept(serverSocket, &clientSocket, NULL, (PSOCKADDR)&address);
            if (NT_SUCCESS(status))
            {
              LOG("Connected to client\n");

              // Receive request type
              REQUEST_TYPE requestType = { 0 };
              UINT32 requestTypeSize = sizeof(requestType);
              status = KmRecv(clientSocket, &requestType, &requestTypeSize, 0);
              if (NT_SUCCESS(status))
              {
                switch (requestType)
                {
                  case REQUEST_TYPE_SCAN: status = KmScanRequest(clientSocket); break;
                }
              }

              // Close client socket
              KmCloseSocket(clientSocket);

              LOG("Disconnected from client\n");
            }
          }
        }

        // Close server socket
        KmCloseSocket(serverSocket);
      }
    }
    __except (DEFAULT_EXCEPTION_HANDLER)
    {
      status = STATUS_UNHANDLED_EXCEPTION;
    }

    // Deinitialize winsock
    KmDeinitializeWsk();

    LOG("TCP server stopped\n");
  }

  KeSetEvent(&sTcpServerStoppedEvent, IO_NO_INCREMENT, FALSE);
}

///////////////////////////////////////////////////////////////
// Driver Entry
///////////////////////////////////////////////////////////////

VOID
DriverUnload(
  PDRIVER_OBJECT Driver)
{
  UNREFERENCED_PARAMETER(Driver);
  
  // Stop listen for incoming connections
  sDriverIsShuttingDown = TRUE;

  // Wait till TCP server has beed stopped
  KeWaitForSingleObject(&sTcpServerStoppedEvent, Executive, KernelMode, FALSE, NULL);

  LOG("Driver stopped\n");
}

NTSTATUS
DriverEntry(
  PDRIVER_OBJECT Driver,
  PUNICODE_STRING RegPath)
{
  UNREFERENCED_PARAMETER(RegPath);

  LOG("Driver starting\n");

  // Setup driver unload procedure
  Driver->DriverUnload = DriverUnload;

  // Initialize events
  KeInitializeEvent(&sTcpServerStoppedEvent, SynchronizationEvent, FALSE);

  // Initialize memory scanner
  KmInitializeScanner();

  // Create TCP listen thread
  return PsCreateSystemThread(&sTcpServerThreadHandle, THREAD_ALL_ACCESS, 0, 0, 0, KmTcpServerThread, 0);
}