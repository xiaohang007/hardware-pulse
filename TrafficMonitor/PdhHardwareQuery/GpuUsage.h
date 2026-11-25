#pragma once
#include "PdhQuery.h"
#include <atlbase.h>
#include <dxgi.h>

// GPU利用率监控类
class CPdhGPUUsage : public CPdhQuery
{
public:
    CPdhGPUUsage();
    ~CPdhGPUUsage();

    // 获取GPU利用率（0-100）
    // 返回值: true=成功, false=失败
    // 注意：首次调用通常返回false（PDH需要预热），建议间隔200ms以上再次调用
    bool GetGpuUsage(/*out*/ int& usage);

};

// 系统范围的独立显存使用率。
class CPdhGpuMemoryUsage : private CPdhQuery
{
public:
    CPdhGpuMemoryUsage();
    int GetUsage();

private:
    bool RefreshAdapters();

    CComPtr<IDXGIFactory1> m_factory;
    std::map<std::wstring, double> m_capacity_by_luid;
};
