#include "stdafx.h"
#include "GpuUsage.h"

#pragma comment(lib, "dxgi.lib")

namespace
{
    constexpr int NVML_SUCCESS = 0;
    constexpr UINT NVIDIA_VENDOR_ID = 0x10de;

    HMODULE LoadNvml()
    {
        HMODULE module = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module != nullptr)
            return module;

        // 标准版 NVIDIA 驱动将 NVML 安装在 NVSMI 目录。
        wchar_t path[MAX_PATH];
        const DWORD length = ExpandEnvironmentStringsW(
            L"%ProgramW6432%\\NVIDIA Corporation\\NVSMI\\nvml.dll", path, _countof(path));
        if (length == 0 || length > _countof(path))
            return nullptr;

        return LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    }

    class CNvmlGpuUsage
    {
    public:
        CNvmlGpuUsage()
        {
            if (m_module == nullptr)
                return;

            const auto init = reinterpret_cast<Init>(GetProcAddress(m_module, "nvmlInit_v2"));
            m_shutdown = reinterpret_cast<Shutdown>(GetProcAddress(m_module, "nvmlShutdown"));
            m_get_count = reinterpret_cast<GetCount>(GetProcAddress(m_module, "nvmlDeviceGetCount_v2"));
            m_get_device = reinterpret_cast<GetDevice>(GetProcAddress(m_module, "nvmlDeviceGetHandleByIndex_v2"));
            m_get_utilization = reinterpret_cast<GetUtilization>(GetProcAddress(m_module, "nvmlDeviceGetUtilizationRates"));
            if (init && m_shutdown && m_get_count && m_get_device && m_get_utilization)
                m_initialized = init() == NVML_SUCCESS;
        }

        ~CNvmlGpuUsage()
        {
            if (m_initialized)
                m_shutdown();
            if (m_module != nullptr)
                FreeLibrary(m_module);
        }

        CNvmlGpuUsage(const CNvmlGpuUsage&) = delete;
        CNvmlGpuUsage& operator=(const CNvmlGpuUsage&) = delete;

        std::optional<int> GetUsage() const
        {
            unsigned int count = 0;
            if (!m_initialized || m_get_count(&count) != NVML_SUCCESS || count == 0)
                return std::nullopt;

            int usage = -1;
            for (unsigned int index = 0; index < count; ++index)
            {
                Device* device = nullptr;
                Utilization utilization{};
                if (m_get_device(index, &device) == NVML_SUCCESS
                    && m_get_utilization(device, &utilization) == NVML_SUCCESS
                    && utilization.gpu <= 100)
                {
                    usage = max(usage, static_cast<int>(utilization.gpu));
                }
            }
            return usage;
        }

    private:
        // 与 NVIDIA 驱动导出的 NVML 接口保持相同的 ABI。
        struct Device;
        struct Utilization
        {
            unsigned int gpu;
            unsigned int memory;
        };

        using Init = int (__cdecl*)();
        using Shutdown = int (__cdecl*)();
        using GetCount = int (__cdecl*)(unsigned int*);
        using GetDevice = int (__cdecl*)(unsigned int, Device**);
        using GetUtilization = int (__cdecl*)(Device*, Utilization*);

        HMODULE m_module = LoadNvml();
        Shutdown m_shutdown{};
        GetCount m_get_count{};
        GetDevice m_get_device{};
        GetUtilization m_get_utilization{};
        bool m_initialized{};
    };
}

std::optional<int> GetNvidiaGpuUsage()
{
    static const CNvmlGpuUsage nvml;
    return nvml.GetUsage();
}

///////////////////////////////////////////////////////////////////////////////////////////
// CPdhGPUUsage 实现
///////////////////////////////////////////////////////////////////////////////////////////
CPdhGPUUsage::CPdhGPUUsage()
: CPdhQuery(_T("\\GPU Engine(*)\\Utilization Percentage"))
{
}

CPdhGPUUsage::~CPdhGPUUsage()
{
}

bool CPdhGPUUsage::GetGpuUsage(int& usage)
{
    if (isInitialized)
    {
        std::vector<CounterValueItem> valueItems;
        if (QueryValues(valueItems))
        {
            if (!valueItems.empty())
            {
                //获取所有类型的利用率
                std::map<std::wstring, double> gpu_usage_map;
                for (const auto& item : valueItems)
                {
                    std::wstring item_name = item.name;
                    size_t index = item.name.rfind(L'_');
                    if (index != std::wstring::npos)
                        item_name = item.name.substr(index + 1);
                    gpu_usage_map[item_name] += item.value;
                }
                //查找所有类型中最大的值作为总利用率（同Windows任务管理器的处理）
                double max_value = 0;
                for (const auto& item : gpu_usage_map)
                {
                    if (item.second > max_value)
                        max_value = item.second;
                }

                usage = static_cast<int>(max_value + 0.5);  // 四舍五入
                usage = min(max(usage, 0), 100);        // 限制在0-100范围
                return true;
            }
        }
    }

    return false;
}

CPdhGpuMemoryUsage::CPdhGpuMemoryUsage()
    : CPdhQuery(_T("\\GPU Adapter Memory(*)\\Dedicated Usage"))
{
}

bool CPdhGpuMemoryUsage::RefreshAdapters()
{
    if (m_factory && m_factory->IsCurrent())
        return true;

    m_factory.Release();
    m_capacity_by_luid.clear();
    m_has_nvidia_adapter = false;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&m_factory))))
        return false;

    for (UINT index = 0; ; ++index)
    {
        CComPtr<IDXGIAdapter1> adapter;
        const HRESULT result = m_factory->EnumAdapters1(index, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND)
            return true;
        if (FAILED(result))
        {
            m_factory.Release();
            return false;
        }

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            continue;

        if (desc.VendorId == NVIDIA_VENDOR_ID)
            m_has_nvidia_adapter = true;
        if (desc.DedicatedVideoMemory == 0)
            continue;

        CString luid;
        luid.Format(L"luid_0x%08lx_0x%08lx",
            static_cast<ULONG>(desc.AdapterLuid.HighPart), desc.AdapterLuid.LowPart);
        m_capacity_by_luid[luid.GetString()] = static_cast<double>(desc.DedicatedVideoMemory);
    }
}

int CPdhGpuMemoryUsage::GetUsage()
{
    std::vector<CounterValueItem> values;
    if (!RefreshAdapters() || !isInitialized || m_capacity_by_luid.empty() || !QueryValues(values))
        return -1;

    std::map<std::wstring, double> usage_by_luid;
    for (const auto& item : values)
    {
        // PDH names each physical GPU node by adapter LUID and a _phys_ suffix.
        CString instance(item.name.c_str());
        instance.MakeLower();
        const int suffix = instance.Find(L"_phys_");
        if (suffix >= 0 && item.value >= 0)
            usage_by_luid[instance.Left(suffix).GetString()] += item.value;
    }

    double used_bytes = 0;
    double capacity_bytes = 0;
    for (const auto& entry : usage_by_luid)
    {
        const auto capacity = m_capacity_by_luid.find(entry.first);
        if (capacity == m_capacity_by_luid.end())
            continue;

        used_bytes += min(entry.second, capacity->second);
        capacity_bytes += capacity->second;
    }

    if (capacity_bytes == 0)
        return -1;
    return static_cast<int>(100 * used_bytes / capacity_bytes + 0.5);
}
