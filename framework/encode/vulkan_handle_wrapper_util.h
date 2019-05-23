/*
** Copyright (c) 2019 LunarG, Inc.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef GFXRECON_ENCODE_VULKAN_HANDLE_WRAPPER_UTIL_H
#define GFXRECON_ENCODE_VULKAN_HANDLE_WRAPPER_UTIL_H

#include "encode/vulkan_handle_wrappers.h"
#include "format/format.h"
#include "format/format_util.h"
#include "util/defines.h"

#include <algorithm>
#include <cassert>
#include <vector>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(encode)

typedef format::HandleId (*PFN_GetHandleId)();
typedef std::vector<format::HandleId> HandleStore;
typedef std::vector<const void*>      HandleArrayStore;

struct HandleArrayUnwrapMemory
{
    size_t                   handle_store_count{ 0 };
    std::vector<HandleStore> handle_stores;
};

template <typename T>
T GetWrappedHandle(const T& handle)
{
    return (handle != VK_NULL_HANDLE) ? reinterpret_cast<HandleWrapper<T>*>(handle)->handle : VK_NULL_HANDLE;
}

template <typename T>
format::HandleId GetWrappedId(const T& handle)
{
    return (handle != VK_NULL_HANDLE) ? reinterpret_cast<HandleWrapper<T>*>(handle)->handle_id : 0;
}

template <typename ParentWrapper, typename Wrapper>
void CreateWrappedDispatchHandle(typename ParentWrapper::HandleType parent,
                                 typename Wrapper::HandleType*      handle,
                                 PFN_GetHandleId                    get_id)
{
    assert(handle != nullptr);
    if ((*handle) != VK_NULL_HANDLE)
    {
        Wrapper* wrapper         = new Wrapper;
        wrapper->dispatch_table_ = *reinterpret_cast<void**>(*handle);
        wrapper->handle          = (*handle);
        wrapper->handle_id       = get_id();

        if (parent != VK_NULL_HANDLE)
        {
            // VkQueue and VkCommandBuffer dispatch tables are not assigned until the handles reach the trampoline
            // function, which comes after the layer. The wrapper will be modified by the trampoline function, but the
            // wrapped handle will not, so we set it to the parent VkDevice object's dispatch table. This is also
            // applied to VkPhysicalDevice handles and their parent VkInstance.
            void* disp                         = *reinterpret_cast<void* const*>(parent);
            *reinterpret_cast<void**>(*handle) = disp;
        }

        (*handle) = reinterpret_cast<typename Wrapper::HandleType>(wrapper);
    }
}

template <typename ParentWrapper, typename Wrapper>
void CreateWrappedNonDispatchHandle(typename ParentWrapper::HandleType,
                                    typename Wrapper::HandleType* handle,
                                    PFN_GetHandleId               get_id)
{
    assert(handle != nullptr);
    if ((*handle) != VK_NULL_HANDLE)
    {
        Wrapper* wrapper   = new Wrapper;
        wrapper->handle    = (*handle);
        wrapper->handle_id = get_id();
        (*handle)          = reinterpret_cast<typename Wrapper::HandleType>(wrapper);
    }
}

template <typename ParentWrapper, typename Wrapper>
void CreateWrappedHandle(typename ParentWrapper::HandleType parent,
                         typename Wrapper::HandleType*      handle,
                         PFN_GetHandleId                    get_id)
{
    CreateWrappedNonDispatchHandle<ParentWrapper, Wrapper>(parent, handle, get_id);
}

template <>
inline void
CreateWrappedHandle<InstanceWrapper, InstanceWrapper>(VkInstance, VkInstance* handle, PFN_GetHandleId get_id)
{
    CreateWrappedDispatchHandle<InstanceWrapper, InstanceWrapper>(VK_NULL_HANDLE, handle, get_id);
}

template <>
inline void CreateWrappedHandle<InstanceWrapper, PhysicalDeviceWrapper>(VkInstance        parent,
                                                                        VkPhysicalDevice* handle,
                                                                        PFN_GetHandleId   get_id)
{
    assert(parent != VK_NULL_HANDLE);
    assert(handle != nullptr);

    auto parent_wrapper = reinterpret_cast<InstanceWrapper*>(parent);

    // Filter duplicate physical device retrieval.
    PhysicalDeviceWrapper* wrapper = nullptr;
    for (auto entry : parent_wrapper->child_physical_devices)
    {
        if (entry->handle == (*handle))
        {
            wrapper = entry;
            break;
        }
    }

    if (wrapper != nullptr)
    {
        (*handle) = reinterpret_cast<VkPhysicalDevice>(wrapper);
    }
    else
    {
        CreateWrappedDispatchHandle<InstanceWrapper, PhysicalDeviceWrapper>(parent, handle, get_id);
        parent_wrapper->child_physical_devices.push_back(reinterpret_cast<PhysicalDeviceWrapper*>(*handle));
    }
}

template <>
inline void
CreateWrappedHandle<PhysicalDeviceWrapper, DeviceWrapper>(VkPhysicalDevice, VkDevice* handle, PFN_GetHandleId get_id)
{
    CreateWrappedDispatchHandle<PhysicalDeviceWrapper, DeviceWrapper>(VK_NULL_HANDLE, handle, get_id);
}

template <>
inline void CreateWrappedHandle<DeviceWrapper, QueueWrapper>(VkDevice parent, VkQueue* handle, PFN_GetHandleId get_id)
{
    assert(parent != VK_NULL_HANDLE);
    assert(handle != nullptr);

    auto parent_wrapper = reinterpret_cast<DeviceWrapper*>(parent);

    // Filter duplicate physical device retrieval.
    QueueWrapper* wrapper = nullptr;
    for (auto entry : parent_wrapper->child_queues)
    {
        if (entry->handle == (*handle))
        {
            wrapper = entry;
            break;
        }
    }

    if (wrapper != nullptr)
    {
        (*handle) = reinterpret_cast<VkQueue>(wrapper);
    }
    else
    {
        CreateWrappedDispatchHandle<DeviceWrapper, QueueWrapper>(parent, handle, get_id);
        parent_wrapper->child_queues.push_back(reinterpret_cast<QueueWrapper*>(*handle));
    }
}

template <>
inline void CreateWrappedHandle<DeviceWrapper, CommandBufferWrapper>(VkDevice         parent,
                                                                     VkCommandBuffer* handle,
                                                                     PFN_GetHandleId  get_id)
{
    CreateWrappedDispatchHandle<DeviceWrapper, CommandBufferWrapper>(parent, handle, get_id);
}

template <>
inline void CreateWrappedHandle<PhysicalDeviceWrapper, DisplayKHRWrapper>(VkPhysicalDevice parent,
                                                                          VkDisplayKHR*    handle,
                                                                          PFN_GetHandleId  get_id)
{
    assert(parent != VK_NULL_HANDLE);
    assert(handle != nullptr);

    auto parent_wrapper = reinterpret_cast<PhysicalDeviceWrapper*>(parent);

    // Filter duplicate display retrieval.
    DisplayKHRWrapper* wrapper = nullptr;
    for (auto entry : parent_wrapper->child_displays)
    {
        if (entry->handle == (*handle))
        {
            wrapper = entry;
            break;
        }
    }

    if (wrapper != nullptr)
    {
        (*handle) = reinterpret_cast<VkDisplayKHR>(wrapper);
    }
    else
    {
        CreateWrappedNonDispatchHandle<PhysicalDeviceWrapper, DisplayKHRWrapper>(parent, handle, get_id);
        parent_wrapper->child_displays.push_back(reinterpret_cast<DisplayKHRWrapper*>(*handle));
    }
}

template <>
inline void CreateWrappedHandle<PhysicalDeviceWrapper, DisplayModeKHRWrapper>(VkPhysicalDevice  parent,
                                                                              VkDisplayModeKHR* handle,
                                                                              PFN_GetHandleId   get_id)
{
    assert(parent != VK_NULL_HANDLE);
    assert(handle != nullptr);

    auto parent_wrapper = reinterpret_cast<PhysicalDeviceWrapper*>(parent);

    // Display modes can either be retrieved or created; filter duplicate display mode retrieval.
    DisplayModeKHRWrapper* wrapper = nullptr;
    for (auto entry : parent_wrapper->child_display_modes)
    {
        if (entry->handle == (*handle))
        {
            wrapper = entry;
            break;
        }
    }

    if (wrapper != nullptr)
    {
        (*handle) = reinterpret_cast<VkDisplayModeKHR>(wrapper);
    }
    else
    {
        CreateWrappedNonDispatchHandle<PhysicalDeviceWrapper, DisplayModeKHRWrapper>(parent, handle, get_id);
        parent_wrapper->child_display_modes.push_back(reinterpret_cast<DisplayModeKHRWrapper*>(*handle));
    }
}

template <typename ParentWrapper, typename Wrapper>
void CreateWrappedHandles(typename ParentWrapper::HandleType parent,
                          typename Wrapper::HandleType*      handles,
                          uint32_t                           count,
                          PFN_GetHandleId                    get_id)
{
    if (handles != nullptr)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            CreateWrappedHandle<ParentWrapper, Wrapper>(parent, &handles[i], get_id);
        }
    }
}

template <typename Wrapper>
void DestroyWrappedHandle(typename Wrapper::HandleType handle)
{
    if (handle != VK_NULL_HANDLE)
    {
        delete reinterpret_cast<Wrapper*>(handle);
    }
}

template <>
inline void DestroyWrappedHandle<InstanceWrapper>(VkInstance handle)
{
    if (handle != VK_NULL_HANDLE)
    {
        // Destroy child wrappers.
        auto wrapper = reinterpret_cast<InstanceWrapper*>(handle);

        for (auto entry : wrapper->child_physical_devices)
        {
            delete entry;
        }

        delete wrapper;
    }
}

template <>
inline void DestroyWrappedHandle<PhysicalDeviceWrapper>(VkPhysicalDevice handle)
{
    if (handle != VK_NULL_HANDLE)
    {
        // Destroy child wrappers.
        auto wrapper = reinterpret_cast<PhysicalDeviceWrapper*>(handle);

        for (auto entry : wrapper->child_displays)
        {
            delete entry;
        }

        for (auto entry : wrapper->child_display_modes)
        {
            delete entry;
        }

        delete wrapper;
    }
}

template <>
inline void DestroyWrappedHandle<DeviceWrapper>(VkDevice handle)
{
    if (handle != VK_NULL_HANDLE)
    {
        // Destroy child wrappers.
        auto wrapper = reinterpret_cast<DeviceWrapper*>(handle);

        for (auto entry : wrapper->child_queues)
        {
            delete entry;
        }

        delete wrapper;
    }
}

// Unwrap individual handles.
template <typename Wrapper>
void UnwrapHandle(const typename Wrapper::HandleType* handle, HandleStore* handle_store)
{
    assert((handle != nullptr) && (handle_store != nullptr));

    if ((*handle) != VK_NULL_HANDLE)
    {
        handle_store->push_back(format::ToHandleId(*handle));
        (*const_cast<typename Wrapper::HandleType*>(handle)) = reinterpret_cast<Wrapper*>(*handle)->handle;
    }
}

// Unwrap statically sized arrays of handles.
template <typename Wrapper>
void UnwrapHandles(const typename Wrapper::HandleType* handles, uint32_t count, HandleStore* handle_store)
{
    if (handles != nullptr)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            UnwrapHandle<Wrapper>(&handles[i], handle_store);
        }
    }
}

// Unwrap dynamically allocated arrays of handles.
template <typename Wrapper>
void UnwrapHandles(const typename Wrapper::HandleType* const* handles,
                   uint32_t                                   count,
                   HandleArrayStore*                          handle_array_store,
                   HandleArrayUnwrapMemory*                   unwrap_memory)
{
    assert((handles != nullptr) && (handle_array_store != nullptr) && (unwrap_memory != nullptr));

    if ((*handles) != nullptr)
    {
        HandleStore* handle_store  = nullptr;
        auto         handle_stores = &unwrap_memory->handle_stores;
        handle_array_store->push_back(*handles);

        if (unwrap_memory->handle_store_count < handle_stores->size())
        {
            handle_store = &(*handle_stores)[unwrap_memory->handle_store_count];
            handle_store->clear();
        }
        else
        {
            handle_stores->emplace_back(HandleStore{});
            handle_store = &handle_stores->back();
            handle_store->reserve(count);
        }

        ++unwrap_memory->handle_store_count;

        for (uint32_t i = 0; i < count; ++i)
        {
            if ((*handles)[i] != VK_NULL_HANDLE)
            {
                handle_store->push_back(format::ToHandleId(reinterpret_cast<const Wrapper*>((*handles)[i])->handle));
            }
            else
            {
                handle_store->push_back(VK_NULL_HANDLE);
            }
        }

        (*const_cast<const typename Wrapper::HandleType**>(handles)) =
            reinterpret_cast<typename Wrapper::HandleType*>(handle_store->data());
    }
}

template <typename Wrapper>
void RewrapHandle(const typename Wrapper::HandleType* handle, HandleStore::const_iterator* handle_store_iter)
{
    assert((handle != nullptr) && (handle_store_iter != nullptr));

    if ((*handle) != VK_NULL_HANDLE)
    {
        (*const_cast<typename Wrapper::HandleType*>(handle)) =
            format::FromHandleId<typename Wrapper::HandleType>(**handle_store_iter);
        ++(*handle_store_iter);
    }
}

template <typename Wrapper>
void RewrapHandles(const typename Wrapper::HandleType* handles,
                   uint32_t                            count,
                   HandleStore::const_iterator*        handle_store_iter)
{
    if (handles != nullptr)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            RewrapHandle<Wrapper>(&handles[i], handle_store_iter);
        }
    }
}

template <typename Wrapper>
void RewrapHandles(const typename Wrapper::HandleType* const* handles,
                   uint32_t                                   count,
                   HandleArrayStore::const_iterator*          handle_array_store_iter)
{
    assert((handles != nullptr) && (handle_array_store_iter != nullptr));

    if ((*handles) != nullptr)
    {
        (*const_cast<const typename Wrapper::HandleType**>(handles)) =
            reinterpret_cast<const typename Wrapper::HandleType*>(**handle_array_store_iter);
        ++(*handle_array_store_iter);
    }
}

GFXRECON_END_NAMESPACE(encode)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_ENCODE_VULKAN_HANDLE_WRAPPER_UTIL_H