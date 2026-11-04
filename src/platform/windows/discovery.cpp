#include <NeForce/core/string/format.hpp>
#include <NeForce/core/string/utf.hpp>
#include <NeForce/core/string/string.hpp>
#include <winsock2.h>
#include <windows.h>
#include <winsvc.h>
#include "../platform_backend.hpp"

namespace neforce::neflux::platform {

    string docker_host() { return "npipe:////./pipe/docker_engine"; }

    void discover_ownership(inventory& data) { static_cast<void>(data); }

    void discover_virtual_machines(inventory& data) { static_cast<void>(data); }


        string service_start_mode(DWORD type) {
            switch (type) {
                case SERVICE_BOOT_START:
                    return "boot";
                case SERVICE_SYSTEM_START:
                    return "system";
                case SERVICE_AUTO_START:
                    return "auto";
                case SERVICE_DEMAND_START:
                    return "manual";
                case SERVICE_DISABLED:
                    return "disabled";
                default:
                    return "unknown";
            }
        }

        void service_state(const DWORD state, string& active, string& sub) {
            switch (state) {
                case SERVICE_RUNNING:
                    active = "active";
                    sub = "running";
                    break;
                case SERVICE_STOPPED:
                    active = "inactive";
                    sub = "stopped";
                    break;
                case SERVICE_START_PENDING:
                    active = "activating";
                    sub = "start-pending";
                    break;
                case SERVICE_STOP_PENDING:
                    active = "deactivating";
                    sub = "stop-pending";
                    break;
                case SERVICE_PAUSED:
                    active = "active";
                    sub = "paused";
                    break;
                case SERVICE_PAUSE_PENDING:
                    active = "deactivating";
                    sub = "pause-pending";
                    break;
                case SERVICE_CONTINUE_PENDING:
                    active = "activating";
                    sub = "continue-pending";
                    break;
                default:
                    active = "unknown";
                    sub = "unknown";
                    break;
            }
        }

        void discover_windows_services(inventory& data) {
            const ::SC_HANDLE manager =
                    ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
            if (manager == nullptr) {
                data.notes_.push_back(format("Windows SCM: error {}", ::GetLastError()));
                return;
            }
            DWORD needed = 0;
            DWORD returned = 0;
            DWORD resume = 0;
            ::EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, nullptr, 0,
                                    &needed, &returned, &resume, nullptr);
            if (needed == 0) {
                data.notes_.push_back(format("Windows SCM: error {}", ::GetLastError()));
                ::CloseServiceHandle(manager);
                return;
            }
            vector<BYTE> buffer(static_cast<size_t>(needed) + 8192);
            for (;;) {
                if (::EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                                            buffer.data(), static_cast<DWORD>(buffer.size()), &needed, &returned,
                                            &resume, nullptr) == FALSE) {
                    if (::GetLastError() == ERROR_MORE_DATA) {
                        buffer.resize(static_cast<size_t>(needed) + 8192);
                        continue;
                    }
                    data.notes_.push_back(format("Windows SCM: error {}", ::GetLastError()));
                    break;
                }
                auto* entries = reinterpret_cast<::ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
                for (DWORD index = 0; index < returned; ++index) {
                    const auto& raw = entries[index];
                    service item;
                    item.name_ = wcharacter::to_string(raw.lpServiceName);
                    item.scope_ = "system";
                    item.description_ = wcharacter::to_string(raw.lpDisplayName);
                    item.extra_ = json::object();
                    item.extra_.set("display_name", json(item.description_));
                    service_state(raw.ServiceStatusProcess.dwCurrentState, item.active_, item.sub_);
                    if (raw.ServiceStatusProcess.dwProcessId != 0) {
                        item.pid_ = raw.ServiceStatusProcess.dwProcessId;
                    }
                    const ::SC_HANDLE handle = ::OpenServiceW(manager, raw.lpServiceName, SERVICE_QUERY_CONFIG);
                    if (handle != nullptr) {
                        DWORD config_size = 0;
                        ::QueryServiceConfigW(handle, nullptr, 0, &config_size);
                        if (config_size != 0) {
                            vector<BYTE> config(config_size);
                            if (::QueryServiceConfigW(handle,
                                                      reinterpret_cast<::LPQUERY_SERVICE_CONFIGW>(config.data()),
                                                      config_size, &config_size) == TRUE) {
                                const auto* value = reinterpret_cast<::LPQUERY_SERVICE_CONFIGW>(config.data());
                                item.load_ = service_start_mode(value->dwStartType);
                                item.extra_.set("start_mode", json(item.load_));
                                item.extra_.set("binary_path", json(wcharacter::to_string(value->lpBinaryPathName)));
                            }
                        }
                        DWORD description_size = 0;
                        ::QueryServiceConfig2W(handle, SERVICE_CONFIG_DESCRIPTION, nullptr, 0, &description_size);
                        if (description_size != 0) {
                            vector<BYTE> description(description_size);
                            if (::QueryServiceConfig2W(handle, SERVICE_CONFIG_DESCRIPTION, description.data(),
                                                       description_size, &description_size) == TRUE) {
                                const auto* value = reinterpret_cast<::LPSERVICE_DESCRIPTIONW>(description.data());
                                if (value->lpDescription != nullptr) {
                                    item.description_ = wcharacter::to_string(value->lpDescription);
                                }
                            }
                        }
                        ::CloseServiceHandle(handle);
                    }
                    if (item.load_.empty()) {
                        item.load_ = "unknown";
                    }
                    if (item.description_.empty()) {
                        item.description_ = "Windows service";
                    }
                    data.services_.push_back(move(item));
                }
                if (returned == 0 || resume == 0) {
                    break;
                }
            }
            ::CloseServiceHandle(manager);
            data.service_managers_.push_back("Windows SCM");
        }

} // namespace neforce::neflux::platform
