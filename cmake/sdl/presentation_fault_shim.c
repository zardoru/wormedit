/* Test-only Linux Vulkan dispatch interception. Never linked into Wormedit/SDL.
 * Injects compositor-withheld acquisition and pending presentation fences.
 * Build: cc -std=c11 -shared -fPIC presentation_fault_shim.c -ldl -o shim.so */
#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static PFN_vkGetInstanceProcAddr real_instance_proc;
static PFN_vkGetDeviceProcAddr real_device_proc;
static PFN_vkAcquireNextImageKHR real_acquire;
static PFN_vkGetFenceStatus real_fence_status;
static PFN_vkQueuePresentKHR real_present;
static PFN_vkDeviceWaitIdle real_idle;
static PFN_vkDestroyFence real_destroy_fence;
static PFN_vkCreateSwapchainKHR real_create_swapchain;
static PFN_vkEnumerateDeviceExtensionProperties real_device_extensions;
static VkFence present_fences[64];
static unsigned fence_count;
static char previous_mode[64];
static int injected_once;

static void log_event(const char *event, unsigned long long value)
{
    const char *path = getenv("WORMEDIT_PRESENT_FAULT_LOG");
    FILE *file = path ? fopen(path, "a") : NULL;
    if (file) {
        fprintf(file, "{\"event\":\"%s\",\"value\":%llu}\n", event, value);
        fclose(file);
    }
}
static const char *mode(void)
{
    static char current[64];
    const char *path = getenv("WORMEDIT_PRESENT_FAULT_CONTROL");
    FILE *file = path ? fopen(path, "r") : NULL;
    strcpy(current, "resume");
    if (file) {
        if (fscanf(file, "%63s", current) != 1) strcpy(current, "resume");
        fclose(file);
    }
    if (strcmp(previous_mode, current)) {
        strcpy(previous_mode, current);
        injected_once = 0;
    }
    return current;
}
static int is_present_fence(VkFence fence)
{
    for (unsigned i = 0; i < fence_count; ++i) if (present_fences[i] == fence) return 1;
    return 0;
}
static VKAPI_ATTR VkResult VKAPI_CALL acquire(VkDevice device, VkSwapchainKHR swapchain,
    uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *index)
{
    const char *fault = mode();
    log_event("acquire_timeout", timeout);
    if (timeout != 0) {
        log_event("unbounded_acquire", timeout);
        _exit(86);
    }
    if (!strcmp(fault, "withhold")) {
        log_event("withheld", 1);
        return VK_NOT_READY;
    }
    if (!strcmp(fault, "out_of_date")) {
        log_event("out_of_date", 1);
        return VK_ERROR_OUT_OF_DATE_KHR;
    }
    if (!strcmp(fault, "surface_lost_once") && !injected_once) {
        injected_once = 1;
        log_event("surface_lost", 1);
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    VkResult result = real_acquire(device, swapchain, timeout, semaphore, fence, index);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) log_event("acquired", *index);
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL fence_status(VkDevice device, VkFence fence)
{
    if (!strcmp(mode(), "fences") && is_present_fence(fence)) {
        log_event("presentation_pending", 1);
        return VK_NOT_READY;
    }
    return real_fence_status(device, fence);
}
static VKAPI_ATTR VkResult VKAPI_CALL present(VkQueue queue, const VkPresentInfoKHR *info)
{
    const VkBaseInStructure *chain = info->pNext;
    int found = 0;
    while (chain) {
        if (chain->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT) {
            const VkSwapchainPresentFenceInfoEXT *fences = (const void *)chain;
            for (unsigned i = 0; i < fences->swapchainCount; ++i) {
                if (!is_present_fence(fences->pFences[i]) && fence_count < 64)
                    present_fences[fence_count++] = fences->pFences[i];
            }
            found = 1;
        }
        chain = chain->pNext;
    }
    log_event(found ? "present_fence" : "missing_present_fence", info->swapchainCount);
    if (!found) _exit(88);
    return real_present(queue, info);
}
static VKAPI_ATTR VkResult VKAPI_CALL idle(VkDevice device)
{
    if (strcmp(mode(), "resume")) {
        log_event("forbidden_device_idle", 1);
        _exit(87);
    }
    return real_idle(device);
}
static VKAPI_ATTR void VKAPI_CALL destroy_fence(VkDevice device, VkFence fence, const VkAllocationCallbacks *allocator)
{
    for (unsigned i = 0; i < fence_count; ++i) if (present_fences[i] == fence) {
        present_fences[i] = present_fences[--fence_count];
        break;
    }
    real_destroy_fence(device, fence, allocator);
}
static VKAPI_ATTR VkResult VKAPI_CALL create_swapchain(VkDevice device, const VkSwapchainCreateInfoKHR *info,
    const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain)
{
    log_event("present_mode", info->presentMode);
    return real_create_swapchain(device, info, allocator, swapchain);
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL device_proc(VkDevice device, const char *name)
{
    PFN_vkVoidFunction result = real_device_proc(device, name);
#define HOOK(symbol, slot, function) if (!strcmp(name, #symbol)) { slot = (PFN_##symbol)result; return (PFN_vkVoidFunction)function; }
    HOOK(vkAcquireNextImageKHR, real_acquire, acquire)
    HOOK(vkGetFenceStatus, real_fence_status, fence_status)
    HOOK(vkQueuePresentKHR, real_present, present)
    HOOK(vkDeviceWaitIdle, real_idle, idle)
    HOOK(vkDestroyFence, real_destroy_fence, destroy_fence)
    HOOK(vkCreateSwapchainKHR, real_create_swapchain, create_swapchain)
#undef HOOK
    return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL device_extensions(VkPhysicalDevice device, const char *layer,
    uint32_t *count, VkExtensionProperties *properties)
{
    VkResult result = real_device_extensions(device, layer, count, properties);
    if (properties && !strcmp(mode(), "unsupported") && result == VK_SUCCESS) {
        uint32_t kept = 0;
        for (uint32_t i = 0; i < *count; ++i) {
            if (!strcmp(properties[i].extensionName, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)) {
                log_event("maintenance_hidden", 1);
            } else {
                properties[kept++] = properties[i];
            }
        }
        *count = kept;
    }
    return result;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL instance_proc(VkInstance instance, const char *name)
{
    PFN_vkVoidFunction result = real_instance_proc(instance, name);
    if (!strcmp(name, "vkEnumerateDeviceExtensionProperties")) {
        real_device_extensions = (PFN_vkEnumerateDeviceExtensionProperties)result;
        return (PFN_vkVoidFunction)device_extensions;
    }
    if (!strcmp(name, "vkGetDeviceProcAddr")) {
        real_device_proc = (PFN_vkGetDeviceProcAddr)result;
        return (PFN_vkVoidFunction)device_proc;
    }
    return result;
}
/* SDL obtains Vulkan entry points with dlsym on its private loader handle, so
 * intercept that lookup rather than relying on global vk symbol interposition. */
void *dlsym(void *handle, const char *name)
{
    typedef void *(*lookup_t)(void *, const char *);
    lookup_t lookup = (lookup_t)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
    void *result = lookup(handle, name);
    if (!strcmp(name, "vkGetInstanceProcAddr")) {
        real_instance_proc = (PFN_vkGetInstanceProcAddr)result;
        return (void *)instance_proc;
    }
    return result;
}
