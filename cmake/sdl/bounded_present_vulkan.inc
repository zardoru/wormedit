/* Wormedit's SDL override: private helpers for bounded presentation recovery.
 * Presentation fences, unlike submission fences, prove WSI payload retirement.
 * This text is inserted after SDL's VULKAN_INTERNAL_TRY_AGAIN definition. */
static Uint32 WORMEDIT_CheckFence(VulkanRenderer *renderer, VkFence fence)
{
    VkResult result;
    if (fence == VK_NULL_HANDLE) {
        return 1;
    }
    result = renderer->vkGetFenceStatus(renderer->logicalDevice, fence);
    if (result == VK_SUCCESS) {
        return 1;
    }
    if (result == VK_NOT_READY) {
        return VULKAN_INTERNAL_TRY_AGAIN;
    }
    SDL_SetError("Vulkan presentation fence query failed (%d)", result);
    return 0;
}

static void WORMEDIT_CleanIdle(VulkanRenderer *renderer)
{
    for (Sint32 i = renderer->submittedCommandBufferCount - 1; i >= 0; --i) {
        VULKAN_INTERNAL_CleanCommandBuffer(renderer, renderer->submittedCommandBuffers[i], false);
    }
    VULKAN_INTERNAL_PerformPendingDestroys(renderer);
}

static Uint32 WORMEDIT_TryIdle(VulkanRenderer *renderer, WindowData *windowData)
{
    Uint32 ready;
    /* Never free old WSI objects based on GPU completion alone. */
    for (Uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        ready = WORMEDIT_CheckFence(renderer, windowData->wormeditPresentFences[i]);
        if (ready != 1) {
            SDL_SetBooleanProperty(SDL_GetWindowProperties(windowData->window), "wormedit.gpu.presentation_pending", ready == VULKAN_INTERNAL_TRY_AGAIN);
            return ready;
        }
    }
    SDL_LockMutex(renderer->submitLock);
    for (Uint32 i = 0; i < renderer->submittedCommandBufferCount; ++i) {
        ready = WORMEDIT_CheckFence(renderer, renderer->submittedCommandBuffers[i]->inFlightFence->fence);
        if (ready != 1) {
            SDL_UnlockMutex(renderer->submitLock);
            SDL_SetBooleanProperty(SDL_GetWindowProperties(windowData->window), "wormedit.gpu.presentation_pending", ready == VULKAN_INTERNAL_TRY_AGAIN);
            return ready;
        }
    }
    WORMEDIT_CleanIdle(renderer);
    SDL_UnlockMutex(renderer->submitLock);
    SDL_SetBooleanProperty(SDL_GetWindowProperties(windowData->window), "wormedit.gpu.presentation_pending", false);
    return 1;
}

static bool WORMEDIT_HasSurfaceMaintenance(const VkExtensionProperties *extensions, Uint32 count)
{
    bool surface = false;
    bool capabilities = false;
    for (Uint32 i = 0; i < count; ++i) {
        surface |= SDL_strcmp(extensions[i].extensionName, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME) == 0;
        capabilities |= SDL_strcmp(extensions[i].extensionName, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) == 0;
    }
    return surface && capabilities;
}

static bool WORMEDIT_QueryMaintenance(VulkanRenderer *renderer)
{
    PFN_vkGetPhysicalDeviceFeatures2KHR query;
    VkPhysicalDeviceFeatures2 features;
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance;
    if (!renderer->wormeditSurfaceMaintenance || !renderer->supports.EXT_swapchain_maintenance1) {
        return false;
    }
    query = (PFN_vkGetPhysicalDeviceFeatures2KHR)vkGetInstanceProcAddr(renderer->instance, "vkGetPhysicalDeviceFeatures2KHR");
    if (query == NULL) {
        return false;
    }
    SDL_zero(maintenance);
    maintenance.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
    SDL_zero(features);
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &maintenance;
    query(renderer->physicalDevice, &features);
    return maintenance.swapchainMaintenance1 == VK_TRUE;
}

static void WORMEDIT_ReleaseWindowFences(VulkanRenderer *renderer, WindowData *windowData)
{
    for (Uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (windowData->inFlightFences[i] != NULL) {
            VULKAN_ReleaseFence((SDL_GPURenderer *)renderer, windowData->inFlightFences[i]);
            windowData->inFlightFences[i] = NULL;
        }
    }
}

static void WORMEDIT_PreserveError(const char *context)
{
    char cause[1024];
    SDL_strlcpy(cause, SDL_GetError(), sizeof(cause));
    SDL_SetError("%s: %s", context, cause);
}
