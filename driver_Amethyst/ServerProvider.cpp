#include "ServerProvider.h"

#include <shellapi.h>
#include <filesystem>
#include <iostream>

#include "BodyTracker.h"
#include "InterfaceHookInjector.h"
#include "Logging.h"

// Wide String to UTF8 String
inline std::string WStringToString(const std::wstring& w_str)
{
    const int count = WideCharToMultiByte(CP_UTF8, 0, w_str.c_str(), w_str.length(), nullptr, 0, nullptr, nullptr);
    std::string str(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, w_str.c_str(), -1, str.data(), count, nullptr, nullptr);
    return str;
}

ServerProvider::~ServerProvider() = default;

ServerProvider::ServerProvider(): driver_service_(winrt::make_self<DriverService>())
{
}

vr::EVRInitError ServerProvider::Init(vr::IVRDriverContext* pDriverContext)
{
    // Use the driver context (sets up a big set of globals)
    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext)

    logMessage("Injecting server driver hooks...");
    InjectHooks(this, pDriverContext);

    // Append default trackers
    logMessage("Adding default trackers...");

    // Add 1 tracker for each role
    for (uint32_t role = 0; role <= static_cast<int>(Tracker_Keyboard); role++)
        driver_service_.get()->AddTracker(
            ITrackerType_Role_Serial.at(static_cast<ITrackerType>(role)), static_cast<ITrackerType>(role));

    // Log the prepended trackers
    for (auto& tracker : driver_service_.get()->TrackerVector())
        logMessage(std::format("Registered a tracker: ({})", tracker.get_serial()));

    logMessage("Setting up the server runner...");
    if (const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); hr != S_OK)
    {
        logMessage(std::format("Could not set up the driver service! HRESULT error: {}, {}",
                               hr, WStringToString(winrt::impl::message_from_hresult(hr).c_str())));

        return vr::VRInitError_Driver_Failed;
    }
    if (const auto hr = driver_service_.get()->SetupService(CLSID_DriverService); hr.has_value())
    {
        logMessage(std::format("Could not set up the driver service! HRESULT error: {}, {}",
                               hr.value().code().value, WStringToString(hr.value().message().c_str())));

        return vr::VRInitError_Driver_Failed;
    }

    // That's all, mark as okay
    return vr::VRInitError_None;
}

void ServerProvider::Cleanup()
{
    logMessage("Disabling server driver hooks...");
    DisableHooks();
}

const char* const* ServerProvider::GetInterfaceVersions()
{
    return vr::k_InterfaceVersions;
}

void ServerProvider::RunFrame()
{
    driver_service_.get()->UpdateTrackers();
}

bool ServerProvider::ShouldBlockStandbyMode()
{
    return false;
}

void ServerProvider::EnterStandby()
{
}

void ServerProvider::LeaveStandby()
{
}

bool ServerProvider::HandleDevicePoseUpdated(uint32_t openVRID, vr::DriverPose_t& pose)
{
    // Apply pose overrides for selected IDs
    if (openVRID > 0)
    {
        //pose.qRotation = convert(dbgRot);
        //pose.vecPosition[0] = dbgPos(0);
        //pose.vecPosition[1] = dbgPos(1);
        //pose.vecPosition[2] = dbgPos(2);
    }

    return true;
}

class DriverWatchdog : public vr::IVRWatchdogProvider
{
public:
    DriverWatchdog() = default;
    virtual ~DriverWatchdog() = default;

    vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override
    {
        VR_INIT_WATCHDOG_DRIVER_CONTEXT(pDriverContext);
        return vr::VRInitError_None;
    }

    void Cleanup() override
    {
    }
};

extern "C" __declspec(dllexport) void* HmdDriverFactory(const char* pInterfaceName, int* pReturnCode)
{
    static ServerProvider k2_server_provider;
    static DriverWatchdog k2_watchdog_driver;

    if (0 == strcmp(vr::IServerTrackedDeviceProvider_Version, pInterfaceName))
    {
        return &k2_server_provider;
    }
    if (0 == strcmp(vr::IVRWatchdogProvider_Version, pInterfaceName))
    {
        return &k2_watchdog_driver;
    }

    (*pReturnCode) = vr::VRInitError_None;

    if (pReturnCode)
        *pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
}
