// SPDX-License-Identifier: GPL-2.0-only
#include <stratosphere.hpp>
#include <stratosphere/fssrv/sf/fssrv_sf_i_file_system_proxy_for_loader.hpp>
#include "ldr_filesystem_proxy.hpp"

namespace ams::fs::impl {
    // Existing libstratosphere client adapter, declared in fs's internal
    // fs_file_system_proxy_service_object.hpp. It forwards code-filesystem
    // commands through this process's initialized libnx fsldr session and
    // wraps returned filesystem/file objects in the normal service adapters.
    sf::SharedPointer<fssrv::sf::IFileSystemProxyForLoader> GetFileSystemProxyForLoaderServiceObject();
}

namespace ams::ldr {
    namespace {
        struct FileSystemProxyOptions {
            static constexpr size_t PointerBufferSize = 0x800;
            static constexpr size_t MaxDomains = 4;
            static constexpr size_t MaxDomainObjects = 128;
            static constexpr bool CanDeferInvokeRequest = false;
            static constexpr bool CanManageMitmServers = false;
        };
        // FS accepts only one native fsp-ldr connection. Nexus owns it. HOC
        // sees this ordinary CMIF/domain endpoint instead, including normal
        // filesystem and file children. No HOC binary changes are needed.
        constexpr auto ProxyService = sm::ServiceName::Encode("nxl:fs");
        constexpr size_t ProxySessions = 2;
        using ProxyManager = sf::hipc::ServerManager<1,FileSystemProxyOptions,16>;
        ProxyManager g_proxy_manager;
        constinit os::ThreadType g_proxy_thread;
        alignas(os::ThreadStackAlignment) constinit u8 g_proxy_stack[32_KB];

        void ProxyThread(void *) {
            g_proxy_manager.LoopProcess();
        }
    }

    void StartFileSystemProxy() {
        // InitializeSystemModule has already initialized fsldr and registered
        // our PID with native FS. The client adapter's SetCurrentProcess is
        // deliberately a no-op: a second loader must not rebind that shared
        // native object to a different PID. No raw cloned FS session escapes.
        auto object=fs::impl::GetFileSystemProxyForLoaderServiceObject();
        AMS_ABORT_UNLESS(object != nullptr);
        R_ABORT_UNLESS(g_proxy_manager.RegisterObjectForServer(std::move(object),ProxyService,ProxySessions));
        R_ABORT_UNLESS(os::CreateThread(std::addressof(g_proxy_thread),ProxyThread,nullptr,
            g_proxy_stack,sizeof(g_proxy_stack),os::GetThreadPriority(os::GetCurrentThread())));
        os::SetThreadNamePointer(std::addressof(g_proxy_thread),"nxl::FsProxy");
        os::StartThread(std::addressof(g_proxy_thread));
    }
}
