/* Copyright (c) Alan Ludwig
   Portions are based on the Azure Sphere IoT Sample which is (c) Microsoft Corp
   Licensed under the MIT License. */

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

// applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"
#include <applibs/log.h>
#include <applibs/networking.h>
#include <applibs/gpio.h>
#include <applibs/pwm.h>
#include <applibs/storage.h>
#include <applibs/eventloop.h>

// We are targeting the MT3620 Dev Kit

#include "eventloop_timer_utilities.h"

#include "motor.h"
#include "networking.h"

// define resource macro's so we don't need the hardware headers
#define SAMPLE_LED 8

/// <summary>
/// Exit codes for this application. These are used for the
/// application exit code. They must all be between zero and 255,
/// where zero is reserved for successful termination.
/// </summary>
typedef enum
{
    ExitCode_Success = 0,
    ExitCode_TermHandler_SigTerm = 1,
    ExitCode_Main_EventLoopFail = 2,
    ExitCode_AzureTimer_Consume = 4,
    ExitCode_Init_EventLoop = 5,
    ExitCode_Init_TwinStatusLed = 8,
    ExitCode_Init_AzureTimer = 10,
    ExitCode_Init_GPIO = 12,
    ExitCode_Init_PWM = 13,
    ExitCode_Init_Motor = 14,
} ExitCode;

static volatile sig_atomic_t exitCode = ExitCode_Success;

#include "parson.h" // used to parse Device Twin messages.

// Azure IoT defines.
static char *scopeId; // ScopeId for DPS
static IOTHUB_DEVICE_CLIENT_LL_HANDLE iothubClientHandle = NULL;
static const int keepalivePeriodSeconds = 20;
static bool iothubAuthenticated = false;

// Function declarations
static void SendEventCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *context);
static void DeviceTwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char *payload,
                               size_t payloadSize, void *userContextCallback);
static void TwinReportState(const char *jsonState);
static void ReportedStateCallback(int result, void *context);
static int DeviceMethodCallback(const char *methodName, const unsigned char *payload,
                                size_t payloadSize, unsigned char **response, size_t *responseSize,
                                void *userContextCallback);
static const char *GetReasonString(IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason);
static const char *GetAzureSphereProvisioningResultString(
    AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult);
static void SendTelemetry(const char *jsonMessage);
static void SetupAzureClient(void);
static void SendSimulatedTelemetry(void);
static void AzureTimerEventHandler(EventLoopTimer *timer);

// Initialization/Cleanup
static ExitCode InitPeripheralsAndHandlers(void);
static void CloseFdAndPrintError(int fd, const char *fdName);
static void ClosePeripheralsAndHandlers(void);

// LED
static int deviceTwinStatusLedGpioFd = -1;

// Motor Out
static int motorA = -1;
static int motorB = -1;
static PWM_ChannelId motorAChannel = 0;
static PWM_ChannelId motorBChannel = 1;

// Timer / polling
static EventLoop *eventLoop = NULL;
static EventLoopTimer *azureTimer = NULL;

// Azure IoT poll periods
static const int AzureIoTDefaultPollPeriodSeconds = 1;        // poll azure iot every second
static const int AzureIoTPollPeriodsPerTelemetry = 5;         // only send telemetry 1/5 of polls
static const int AzureIoTMinReconnectPeriodSeconds = 60;      // back off when reconnecting
static const int AzureIoTMaxReconnectPeriodSeconds = 10 * 60; // back off limit

static int azureIoTPollPeriodSeconds = -1;
static int telemetryCount = 0;

// State variables
static bool statusLedOn = false;

/// <summary>
///     Signal handler for termination requests. This handler must be async-signal-safe.
/// </summary>
static void TerminationHandler(int signalNumber)
{
    // Don't use Log_Debug here, as it is not guaranteed to be async-signal-safe.
    exitCode = ExitCode_TermHandler_SigTerm;
}

/// <summary>
///     Main entry point for this sample.
/// </summary>
int main(int argc, char *argv[])
{
    Log_Debug("Azure IoT Application starting.\n");

    bool isNetworkingReady = false;
    if ((Networking_IsNetworkingReady(&isNetworkingReady) == -1) || !isNetworkingReady)
    {
        Log_Debug("WARNING: Network is not ready. Device cannot connect until network is ready.\n");
    }

    if (argc > 1)
    {
        scopeId = argv[1];
        Log_Debug("Using Azure IoT DPS Scope ID %s\n", scopeId);
    }
    else
    {
        Log_Debug("ScopeId needs to be set in the app_manifest CmdArgs\n");
        return -1;
    }

    exitCode = InitPeripheralsAndHandlers();

    // Main loop
    while (exitCode == ExitCode_Success)
    {
        EventLoop_Run_Result result = EventLoop_Run(eventLoop, -1, true);
        // Continue if interrupted by signal, e.g. due to breakpoint being set.
        if (result == EventLoop_Run_Failed && errno != EINTR)
        {
            exitCode = ExitCode_Main_EventLoopFail;
        }
    }

    ClosePeripheralsAndHandlers();

    Log_Debug("Application exiting.\n");

    return exitCode;
}

/// <summary>
/// Azure timer event:  Check connection status and send telemetry
/// </summary>
static void AzureTimerEventHandler(EventLoopTimer *timer)
{
    if (ConsumeEventLoopTimerEvent(timer) != 0)
    {
        exitCode = ExitCode_AzureTimer_Consume;
        return;
    }

    bool isNetworkReady = false;
    if (Networking_IsNetworkingReady(&isNetworkReady) != -1)
    {
        if (isNetworkReady && !iothubAuthenticated)
        {
            SetupAzureClient();
        }
    }
    else
    {
        Log_Debug("Failed to get Network state\n");
    }

    if (iothubAuthenticated)
    {
        telemetryCount++;
        if (telemetryCount == AzureIoTPollPeriodsPerTelemetry)
        {
            telemetryCount = 0;
            SendSimulatedTelemetry();
        }
        IoTHubDeviceClient_LL_DoWork(iothubClientHandle);
    }
}

/// <summary>
///     Set up SIGTERM termination handler, initialize peripherals, and set up event handlers.
/// </summary>
/// <returns>
///     ExitCode_Success if all resources were allocated successfully; otherwise another
///     ExitCode value which indicates the specific failure.
/// </returns>
static ExitCode InitPeripheralsAndHandlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = TerminationHandler;
    sigaction(SIGTERM, &action, NULL);

    eventLoop = EventLoop_Create();
    if (eventLoop == NULL)
    {
        Log_Debug("Could not create event loop.\n");
        return ExitCode_Init_EventLoop;
    }

    // SAMPLE_LED is used to show Device Twin settings state
    Log_Debug("Opening SAMPLE_LED as output.\n");
    deviceTwinStatusLedGpioFd =
        GPIO_OpenAsOutput(SAMPLE_LED, GPIO_OutputMode_PushPull, GPIO_Value_High);
    if (deviceTwinStatusLedGpioFd == -1)
    {
        Log_Debug("ERROR: Could not open SAMPLE_LED: %s (%d).\n", strerror(errno), errno);
        return ExitCode_Init_TwinStatusLed;
    }

    // Motors
    if (0 > Motor_Init())
    {
        Log_Debug("ERROR: Could not Initialize motor\n");
        return ExitCode_Init_Motor;
    }

    Log_Debug("Opening Motor A.\n");
    motorA = Motor_Open(4, 5, 0, motorAChannel, 20000);
    Motor_Move(motorA, 40);
    if (motorA < 0)
    {
        Log_Debug("ERROR: Could not open motor A (%d): %s (%d).\n", motorA, strerror(errno), errno);
        return ExitCode_Init_Motor;
    }

    Log_Debug("Opening Motor B.\n");
    motorB = Motor_Open(6, 7, 0, motorBChannel, 20000);
    Motor_Move(motorB, 50);
    if (motorB < 0)
    {
        Log_Debug("ERROR: Could not open motor B (%d): %s (%d).\n", motorA, strerror(errno), errno);
        return ExitCode_Init_Motor;
    }

    azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;
    struct timespec azureTelemetryPeriod = {.tv_sec = azureIoTPollPeriodSeconds, .tv_nsec = 0};
    azureTimer =
        CreateEventLoopPeriodicTimer(eventLoop, &AzureTimerEventHandler, &azureTelemetryPeriod);
    if (azureTimer == NULL)
    {
        return ExitCode_Init_AzureTimer;
    }

    return ExitCode_Success;
}

/// <summary>
///     Closes a file descriptor and prints an error on failure.
/// </summary>
/// <param name="fd">File descriptor to close</param>
/// <param name="fdName">File descriptor name to use in error message</param>
static void CloseFdAndPrintError(int fd, const char *fdName)
{
    if (fd >= 0)
    {
        int result = close(fd);
        if (result != 0)
        {
            Log_Debug("ERROR: Could not close fd %s: %s (%d).\n", fdName, strerror(errno), errno);
        }
    }
}

/// <summary>
///     Close peripherals and handlers.
/// </summary>
static void ClosePeripheralsAndHandlers(void)
{
    DisposeEventLoopTimer(azureTimer);
    EventLoop_Close(eventLoop);

    Log_Debug("Closing file descriptors\n");

    // Leave the LEDs off
    if (deviceTwinStatusLedGpioFd >= 0)
    {
        GPIO_SetValue(deviceTwinStatusLedGpioFd, GPIO_Value_High);
    }

    CloseFdAndPrintError(deviceTwinStatusLedGpioFd, "StatusLed");

    Motor_Close(motorA);
    Motor_Close(motorB);
}

/// <summary>
///     Callback when the Azure IoT connection state changes.
///     This can indicate that a new connection attempt has succeeded or failed.
///     It can also indicate than an existing connection has expired due to SAS token expiry.
/// </summary>
static void ConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result,
                                     IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason,
                                     void *userContextCallback)
{
    iothubAuthenticated = (result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED);
    Log_Debug("Azure IoT connection status: %s\n", GetReasonString(reason));
    if (iothubAuthenticated)
    {
        // Send static device twin properties when connection is established
        TwinReportState(
            "{\"manufacturer\":\"Microsoft\",\"model\":\"Azure Sphere Sample Device\"}");
    }
}

/// <summary>
///     Sets up the Azure IoT Hub connection (creates the iothubClientHandle)
///     When the SAS Token for a device expires the connection needs to be recreated
///     which is why this is not simply a one time call.
/// </summary>
static void SetupAzureClient(void)
{
    if (iothubClientHandle != NULL)
    {
        IoTHubDeviceClient_LL_Destroy(iothubClientHandle);
    }

    AZURE_SPHERE_PROV_RETURN_VALUE provResult =
        IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning(scopeId, 10000,
                                                                          &iothubClientHandle);
    Log_Debug("IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning returned '%s'.\n",
              GetAzureSphereProvisioningResultString(provResult));

    if (provResult.result != AZURE_SPHERE_PROV_RESULT_OK)
    {

        // If we fail to connect, reduce the polling frequency, starting at
        // AzureIoTMinReconnectPeriodSeconds and with a backoff up to
        // AzureIoTMaxReconnectPeriodSeconds
        if (azureIoTPollPeriodSeconds == AzureIoTDefaultPollPeriodSeconds)
        {
            azureIoTPollPeriodSeconds = AzureIoTMinReconnectPeriodSeconds;
        }
        else
        {
            azureIoTPollPeriodSeconds *= 2;
            if (azureIoTPollPeriodSeconds > AzureIoTMaxReconnectPeriodSeconds)
            {
                azureIoTPollPeriodSeconds = AzureIoTMaxReconnectPeriodSeconds;
            }
        }

        struct timespec azureTelemetryPeriod = {azureIoTPollPeriodSeconds, 0};
        SetEventLoopTimerPeriod(azureTimer, &azureTelemetryPeriod);

        Log_Debug("ERROR: Failed to create IoTHub Handle - will retry in %i seconds.\n",
                  azureIoTPollPeriodSeconds);
        return;
    }

    // Successfully connected, so make sure the polling frequency is back to the default
    azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;
    struct timespec azureTelemetryPeriod = {.tv_sec = azureIoTPollPeriodSeconds, .tv_nsec = 0};
    SetEventLoopTimerPeriod(azureTimer, &azureTelemetryPeriod);

    iothubAuthenticated = true;

    if (IoTHubDeviceClient_LL_SetOption(iothubClientHandle, OPTION_KEEP_ALIVE,
                                        &keepalivePeriodSeconds) != IOTHUB_CLIENT_OK)
    {
        Log_Debug("ERROR: Failure setting Azure IoT Hub client option \"%s\".\n",
                  OPTION_KEEP_ALIVE);
        return;
    }

    IoTHubDeviceClient_LL_SetDeviceTwinCallback(iothubClientHandle, DeviceTwinCallback, NULL);
    IoTHubDeviceClient_LL_SetDeviceMethodCallback(iothubClientHandle, DeviceMethodCallback, NULL);
    IoTHubDeviceClient_LL_SetConnectionStatusCallback(iothubClientHandle, ConnectionStatusCallback,
                                                      NULL);
}

/// <summary>
///     Callback invoked when a Direct Method is received from Azure IoT Hub.
/// </summary>
static int DeviceMethodCallback(const char *methodName, const unsigned char *payload,
                                size_t payloadSize, unsigned char **response, size_t *responseSize,
                                void *userContextCallback)
{
    int result;
    char *responseString;

    Log_Debug("Received Device Method callback: Method name %s.\n", methodName);

    if (strcmp("TriggerAlarm", methodName) == 0)
    {
        // Output alarm using Log_Debug
        Log_Debug("  ----- ALARM TRIGGERED! -----\n");
        responseString = "\"Alarm Triggered\""; // must be a JSON string (in quotes)
        result = 200;
    }
    else
    {
        // All other method names are ignored
        responseString = "{}";
        result = -1;
    }

    // if 'response' is non-NULL, the Azure IoT library frees it after use, so copy it to heap
    *responseSize = strlen(responseString);
    *response = malloc(*responseSize);
    memcpy(*response, responseString, *responseSize);
    return result;
}

/// <summary>
///     Callback invoked when a Device Twin update is received from Azure IoT Hub.
/// </summary>
static void DeviceTwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char *payload,
                               size_t payloadSize, void *userContextCallback)
{
    size_t nullTerminatedJsonSize = payloadSize + 1;
    char *nullTerminatedJsonString = (char *)malloc(nullTerminatedJsonSize);
    if (nullTerminatedJsonString == NULL)
    {
        Log_Debug("ERROR: Could not allocate buffer for twin update payload.\n");
        abort();
    }

    // Copy the provided buffer to a null terminated buffer.
    memcpy(nullTerminatedJsonString, payload, payloadSize);
    // Add the null terminator at the end.
    nullTerminatedJsonString[nullTerminatedJsonSize - 1] = 0;

    JSON_Value *rootProperties = NULL;
    rootProperties = json_parse_string(nullTerminatedJsonString);
    if (rootProperties == NULL)
    {
        Log_Debug("WARNING: Cannot parse the string as JSON content.\n");
        goto cleanup;
    }

    JSON_Object *rootObject = json_value_get_object(rootProperties);
    JSON_Object *desiredProperties = json_object_dotget_object(rootObject, "desired");
    if (desiredProperties == NULL)
    {
        desiredProperties = rootObject;
    }

    // The desired properties should have a "StatusLED" object
    JSON_Object *LEDState = json_object_dotget_object(desiredProperties, "StatusLED");
    if (LEDState != NULL)
    {
        // ... with a "value" field which is a Boolean
        int statusLedValue = json_object_get_boolean(LEDState, "value");
        if (statusLedValue != -1)
        {
            statusLedOn = statusLedValue == 1;
            GPIO_SetValue(deviceTwinStatusLedGpioFd,
                          statusLedOn ? GPIO_Value_Low : GPIO_Value_High);
        }
    }

    // Report current status LED state
    if (statusLedOn)
    {
        TwinReportState("{\"StatusLED\":{\"value\":true}}");
    }
    else
    {
        TwinReportState("{\"StatusLED\":{\"value\":false}}");
    }

    // The desired properties should have a "SpeedMotorA" object
    JSON_Object *sMotorA = json_object_dotget_object(desiredProperties, "SpeedMotorA");
    if (sMotorA != NULL)
    {
        // ... with a "value" field which is of type double
        double speedValueA = json_object_get_number(sMotorA, "value");

        Log_Debug("Changing speed of Motor A to %d.\n", speedValueA);
        Motor_Move(motorA, (int) speedValueA);
    }
	
	// The desired properties should have a "SpeedMotorB" object
    JSON_Object *sMotorB = json_object_dotget_object(desiredProperties, "SpeedMotorB");
    if (sMotorB != NULL)
    {
        // ... with a "value" field which is of type double
        double speedValueB = json_object_get_number(sMotorB, "value");

        Log_Debug("Changing speed of Motor B to %d.\n", speedValueB);
        Motor_Move(motorB, (int) speedValueB);

    }
cleanup:
    // Release the allocated memory.
    json_value_free(rootProperties);
    free(nullTerminatedJsonString);
}

/// <summary>
///     Converts the Azure IoT Hub connection status reason to a string.
/// </summary>
static const char *GetReasonString(IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason)
{
    static char *reasonString = "unknown reason";
    switch (reason)
    {
    case IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN:
        reasonString = "IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN";
        break;
    case IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED:
        reasonString = "IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED";
        break;
    case IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL:
        reasonString = "IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL";
        break;
    case IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED:
        reasonString = "IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED";
        break;
    case IOTHUB_CLIENT_CONNECTION_NO_NETWORK:
        reasonString = "IOTHUB_CLIENT_CONNECTION_NO_NETWORK";
        break;
    case IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR:
        reasonString = "IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR";
        break;
    case IOTHUB_CLIENT_CONNECTION_OK:
        reasonString = "IOTHUB_CLIENT_CONNECTION_OK";
        break;
    }
    return reasonString;
}

/// <summary>
///     Converts AZURE_SPHERE_PROV_RETURN_VALUE to a string.
/// </summary>
static const char *GetAzureSphereProvisioningResultString(
    AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult)
{
    switch (provisioningResult.result)
    {
    case AZURE_SPHERE_PROV_RESULT_OK:
        return "AZURE_SPHERE_PROV_RESULT_OK";
    case AZURE_SPHERE_PROV_RESULT_INVALID_PARAM:
        return "AZURE_SPHERE_PROV_RESULT_INVALID_PARAM";
    case AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY:
        return "AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY";
    case AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY:
        return "AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY";
    case AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR:
        return "AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR";
    case AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR:
        return "AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR";
    default:
        return "UNKNOWN_RETURN_VALUE";
    }
}

/// <summary>
///     Sends telemetry to Azure IoT Hub
/// </summary>
static void SendTelemetry(const char *jsonMessage)
{
    Log_Debug("Sending Azure IoT Hub telemetry: %s.\n", jsonMessage);

    bool isNetworkingReady = false;
    if ((Networking_IsNetworkingReady(&isNetworkingReady) == -1) || !isNetworkingReady)
    {
        Log_Debug("WARNING: Cannot send Azure IoT Hub telemetry because the network is not up.\n");
        return;
    }

    IOTHUB_MESSAGE_HANDLE messageHandle = IoTHubMessage_CreateFromString(jsonMessage);

    if (messageHandle == 0)
    {
        Log_Debug("ERROR: unable to create a new IoTHubMessage.\n");
        return;
    }

    if (IoTHubDeviceClient_LL_SendEventAsync(iothubClientHandle, messageHandle, SendEventCallback,
                                             /*&callback_param*/ NULL) != IOTHUB_CLIENT_OK)
    {
        Log_Debug("ERROR: failure requesting IoTHubClient to send telemetry event.\n");
    }
    else
    {
        Log_Debug("INFO: IoTHubClient accepted the telemetry event for delivery.\n");
    }

    IoTHubMessage_Destroy(messageHandle);
}

/// <summary>
///     Callback invoked when the Azure IoT Hub send event request is processed.
/// </summary>
static void SendEventCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *context)
{
    Log_Debug("INFO: Azure IoT Hub send telemetry event callback: status code %d.\n", result);
}

/// <summary>
///     Enqueues a report containing Device Twin reported properties. The report is not sent
///     immediately, but it is sent on the next invocation of IoTHubDeviceClient_LL_DoWork().
/// </summary>
static void TwinReportState(const char *jsonState)
{
    if (iothubClientHandle == NULL)
    {
        Log_Debug("ERROR: Azure IoT Hub client not initialized.\n");
    }
    else
    {
        if (IoTHubDeviceClient_LL_SendReportedState(
                iothubClientHandle, (const unsigned char *)jsonState, strlen(jsonState),
                ReportedStateCallback, NULL) != IOTHUB_CLIENT_OK)
        {
            Log_Debug("ERROR: Azure IoT Hub client error when reporting state '%s'.\n", jsonState);
        }
        else
        {
            Log_Debug("INFO: Azure IoT Hub client accepted request to report state '%s'.\n",
                      jsonState);
        }
    }
}

/// <summary>
///     Callback invoked when the Device Twin report state request is processed by Azure IoT Hub
///     client.
/// </summary>
static void ReportedStateCallback(int result, void *context)
{
    Log_Debug("INFO: Azure IoT Hub Device Twin reported state callback: status code %d.\n", result);
}

#define TELEMETRY_BUFFER_SIZE 100

/// <summary>
///     Generate simulated telemetry and send to Azure IoT Hub.
/// </summary>
void SendSimulatedTelemetry(void)
{
    // generate a simulated temperature
    static float temperature = 50.0f;                    // starting temperature
    float delta = ((float)(rand() % 41)) / 20.0f - 1.0f; // between -1.0 and +1.0
    temperature += delta;

    char telemetryBuffer[TELEMETRY_BUFFER_SIZE];
    int len = snprintf(telemetryBuffer, TELEMETRY_BUFFER_SIZE, "{\"Temperature\":\"%3.2f\"}",
                       temperature);
    if (len < 0 || len >= TELEMETRY_BUFFER_SIZE)
    {
        Log_Debug("ERROR: Cannot write telemetry to buffer.\n");
        return;
    }
    SendTelemetry(telemetryBuffer);
}
