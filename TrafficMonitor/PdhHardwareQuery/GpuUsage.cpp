#include "stdafx.h"
#include "GpuUsage.h"

#pragma comment(lib, "dxgi.lib")

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
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            || desc.DedicatedVideoMemory == 0)
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
    if (!isInitialized || !RefreshAdapters() || m_capacity_by_luid.empty() || !QueryValues(values))
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
