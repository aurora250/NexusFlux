#include "../platform_backend.hpp"
#include <NeFlux/gpu.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/container/set.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeForce/core/string/utf.hpp>
#include <windows.h>
#include <dxgi.h>

namespace neforce::neflux::platform {

    string nvml_library_name() { return "nvml.dll"; }

    namespace {
        string gpu_identity(const gpu& item) {
            string name = lower_ascii(item.name_.view());
            name.trim();
            string vendor = lower_ascii(item.vendor_.view());
            vendor.trim();
            return format("{}|{}", vendor, name);
        }
    }

        vector<gpu> read_dxgi(uint32_t first_id) {
            vector<gpu> out;
            IDXGIFactory* factory = nullptr;
            if (CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory)) != S_OK ||
                factory == nullptr) {
                return out;
            }
            IDXGIAdapter* adapter = nullptr;
            set<string> seen;
            for (UINT index = 0; factory->EnumAdapters(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
                if (adapter == nullptr) {
                    continue;
                }
                DXGI_ADAPTER_DESC description{};
                if (adapter->GetDesc(&description) == S_OK) {
                    const string name = wcharacter::to_string(wstring_view(description.Description));
                    if (name.empty() || name.contains("Microsoft Basic Render Driver") ||
                        description.DedicatedVideoMemory == 0) {
                        adapter->Release();
                        adapter = nullptr;
                        continue;
                    }
                    gpu item;
                    item.id_ = first_id + static_cast<uint32_t>(out.size());
                    item.name_ = name;
                    item.vendor_ = description.VendorId == 0x1002
                                           ? "AMD"
                                           : (description.VendorId == 0x10DE
                                                      ? "NVIDIA"
                                                      : (description.VendorId == 0x8086 ? "Intel" : "DRM"));
                    item.backend_ = "DXGI";
                    item.memory_total_ = description.DedicatedVideoMemory;
                    const string pci_id = format("{:04x}:{:04x}:{:08x}:{:02x}", description.VendorId,
                                                 description.DeviceId, description.SubSysId, description.Revision);
                    item.uuid_ = pci_id;
                    item.extra_ = json::object();
                    item.extra_.set("pci_id", json(pci_id));
                    item.extra_.set("shared_memory_bytes", json(static_cast<uint64_t>(description.SharedSystemMemory)));
                    item.process_status_ = "DXGI adapter · per-process counters unavailable";
                    const string identity = gpu_identity(item);
                    if (!seen.insert(identity).second) {
                        adapter->Release();
                        adapter = nullptr;
                        continue;
                    }
                    out.push_back(move(item));
                }
                adapter->Release();
                adapter = nullptr;
            }
            factory->Release();
            return out;
        }


    vector<gpu> read_platform_gpus(uint32_t first_id) { return read_dxgi(first_id); }

} // namespace neforce::neflux::platform
