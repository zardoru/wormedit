#!/usr/bin/env python3
"""Generate private SDL TU overrides. Exact-source checks fail closed on upgrades."""
import argparse
import hashlib
import json
from pathlib import Path

AUDITED_SHA256 = {'src/render/gpu/SDL_render_gpu.c': '0a9e81047ce3685b9378331be62dee83a3785d132a19a607a49e37cacc092703', 'src/gpu/vulkan/SDL_gpu_vulkan.c': '761536b1454184d8c59095773806f397d14eb805974076c19e8a5bb65d022bad'}

METAL_SHA256 = {'src/gpu/metal/SDL_gpu_metal.m': '5d6988a80d8de43254b96c0e91b94f351a708bb5d57eeb45f919c5b9c65d63e3'}

def audited_sources(platform):
    return METAL_SHA256 if platform == "metal" else AUDITED_SHA256

def replace_once(text, old, new):
    count = text.count(old)
    if count != 1:
        raise ValueError(f"expected one SDL anchor, found {count}: {old[:90]!r}")
    return text.replace(old, new, 1)

def patch_render(text):
    old = "bool result = SDL_WaitAndAcquireGPUSwapchainTexture(data->state.command_buffer, renderer->window, &swapchain, &swapchain_texture_width, &swapchain_texture_height);"
    new = """bool bounded = SDL_GetBooleanProperty(SDL_GetGPUDeviceProperties(data->device), "wormedit.gpu.bounded_present", false);
        bool result = bounded
            ? SDL_AcquireGPUSwapchainTexture(data->state.command_buffer, renderer->window, &swapchain, &swapchain_texture_width, &swapchain_texture_height)
            : SDL_WaitAndAcquireGPUSwapchainTexture(data->state.command_buffer, renderer->window, &swapchain, &swapchain_texture_width, &swapchain_texture_height);
        SDL_SetBooleanProperty(SDL_GetWindowProperties(renderer->window), "wormedit.gpu.presentation_pending", bounded && result && swapchain == NULL);"""
    text = replace_once(text, old, new)
    old = "SDL_SetPointerProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_GPU_DEVICE_POINTER, data->device);"
    return replace_once(text, old, old + """
    SDL_SetBooleanProperty(SDL_GetRendererProperties(renderer), "wormedit.gpu.bounded_present",
        SDL_GetBooleanProperty(SDL_GetGPUDeviceProperties(data->device), "wormedit.gpu.bounded_present", false));""")

def patch_metal(text):
    anchor = "    windowData->layer.device = renderer->device;"
    text = replace_once(text, anchor, anchor + """
    /* Wormedit: retain Apple's finite drawable timeout on supported platforms. */
    if (@available(macOS 10.13, iOS 11.0, tvOS 11.0, *)) {
        windowData->layer.allowsNextDrawableTimeout = YES;
    }""")
    anchor = "        windowData->drawable = [windowData->layer nextDrawable];"
    return replace_once(text, anchor, anchor + """
        if (windowData->drawable == nil) {
            windowData->texture.handle = nil;
            return true; /* *texture remains NULL; no nil drawable is presented. */
        }""")

def patch_vulkan(text, helpers):
    text = replace_once(text, "    Uint8 EXT_texture_compression_astc_hdr;", "    Uint8 EXT_texture_compression_astc_hdr;\n    Uint8 EXT_swapchain_maintenance1;")
    text = replace_once(text, "    SDL_GPUFence *inFlightFences[MAX_FRAMES_IN_FLIGHT];", "    SDL_GPUFence *inFlightFences[MAX_FRAMES_IN_FLIGHT];\n    VkFence wormeditPresentFences[MAX_FRAMES_IN_FLIGHT];")
    text = replace_once(text, "    bool supportsPhysicalDeviceProperties2;", "    bool supportsPhysicalDeviceProperties2;\n    bool wormeditSurfaceMaintenance;\n    bool wormeditBoundedPresent;")
    anchor = "// Error Handling"
    declarations = """static void VULKAN_INTERNAL_CleanCommandBuffer(VulkanRenderer *, VulkanCommandBuffer *, bool);
static void VULKAN_INTERNAL_PerformPendingDestroys(VulkanRenderer *);
static void VULKAN_ReleaseFence(SDL_GPURenderer *, SDL_GPUFence *);

"""
    text = replace_once(text, anchor, declarations + anchor)
    text = replace_once(text, "#define VULKAN_INTERNAL_TRY_AGAIN 2", "#define VULKAN_INTERNAL_TRY_AGAIN 2\n\n" + helpers)
    text = replace_once(text, "else CHECK(EXT_texture_compression_astc_hdr)", "else CHECK(EXT_texture_compression_astc_hdr) else CHECK(EXT_swapchain_maintenance1)")
    text = replace_once(text, "supports->EXT_texture_compression_astc_hdr);", "supports->EXT_texture_compression_astc_hdr + supports->EXT_swapchain_maintenance1);")
    text = replace_once(text, "    CHECK(EXT_texture_compression_astc_hdr)\n#undef CHECK", "    CHECK(EXT_texture_compression_astc_hdr)\n    CHECK(EXT_swapchain_maintenance1)\n#undef CHECK")
    text = replace_once(text, "    bool *supportsPortabilityEnumeration,\n    int *firstUnsupportedExtensionIndex)", "    bool *supportsPortabilityEnumeration,\n    bool *wormeditSurfaceMaintenance,\n    int *firstUnsupportedExtensionIndex)")
    anchor = "    // Only needed for MoltenVK!"
    text = replace_once(text, anchor, "    *wormeditSurfaceMaintenance = WORMEDIT_HasSurfaceMaintenance(availableExtensions, extensionCount);\n\n" + anchor)
    text = replace_once(text, "instanceExtensionCount + 4 + extraInstanceExtensionCount);", "instanceExtensionCount + 6 + extraInstanceExtensionCount);")
    text = replace_once(text, "            &renderer->supportsPortabilityEnumeration,\n            &firstUnsupportedExtensionIndex)", "            &renderer->supportsPortabilityEnumeration,\n            &renderer->wormeditSurfaceMaintenance,\n            &firstUnsupportedExtensionIndex)")
    anchor = "    if (renderer->supportsDebugUtils) {\n        // Append the debug extension"
    new = """    renderer->wormeditSurfaceMaintenance &= renderer->supportsPhysicalDeviceProperties2;
    if (!renderer->wormeditSurfaceMaintenance) {
        SDL_SetError("Bounded Vulkan presentation requires VK_EXT_surface_maintenance1, VK_KHR_get_surface_capabilities2 and VK_KHR_get_physical_device_properties2.");
        SDL_stack_free((char *)instanceExtensionNames);
        return false;
    }
    if (renderer->wormeditSurfaceMaintenance) {
        *nextInstanceExtensionNamePtr++ = VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME;
        *nextInstanceExtensionNamePtr++ = VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
        instanceExtensionCount += 2;
    }

""" + anchor
    text = replace_once(text, anchor, new)
    anchor = "    VkPhysicalDevicePortabilitySubsetFeaturesKHR portabilityFeatures;"
    text = replace_once(text, anchor, anchor + "\n    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT wormeditMaintenance;")
    anchor = "    deviceCreateInfo.enabledExtensionCount = GetDeviceExtensionCount("
    new = """    renderer->wormeditBoundedPresent = WORMEDIT_QueryMaintenance(renderer);
    if (!renderer->wormeditBoundedPresent) {
        SDL_SetError("Bounded Vulkan presentation requires VK_EXT_swapchain_maintenance1 with the swapchainMaintenance1 feature; update the Vulkan driver.");
        return 0;
    }
    renderer->supports.EXT_swapchain_maintenance1 = renderer->wormeditBoundedPresent;
    SDL_SetBooleanProperty(renderer->props, "wormedit.gpu.bounded_present", renderer->wormeditBoundedPresent);
""" + anchor
    text = replace_once(text, anchor, new)
    anchor = "#ifdef HAVE_GPU_OPENXR\n    if (renderer->xrInstance) {\n        XrResult xrResult;\n        PFN_xrCreateVulkanDeviceKHR"
    new = """    if (renderer->wormeditBoundedPresent) {
        SDL_zero(wormeditMaintenance);
        wormeditMaintenance.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        wormeditMaintenance.swapchainMaintenance1 = VK_TRUE;
        wormeditMaintenance.pNext = (void *)deviceCreateInfo.pNext;
        deviceCreateInfo.pNext = &wormeditMaintenance;
    }

""" + anchor
    text = replace_once(text, anchor, new)
    anchor = "    windowData->needsSwapchainRecreate = false;\n    return true;"
    new = """    if (renderer->wormeditBoundedPresent) {
        VkFenceCreateInfo fenceInfo;
        SDL_zero(fenceInfo);
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vulkanResult = renderer->vkCreateFence(renderer->logicalDevice, &fenceInfo, NULL, &windowData->wormeditPresentFences[i]);
            if (vulkanResult != VK_SUCCESS) {
                VULKAN_INTERNAL_DestroySwapchain(renderer, windowData);
                CHECK_VULKAN_ERROR_AND_RETURN(vulkanResult, vkCreateFence, false);
            }
        }
    }
""" + anchor
    text = replace_once(text, anchor, new)
    anchor = "    SDL_free(windowData->renderFinishedSemaphore);\n    windowData->renderFinishedSemaphore = NULL;"
    new = """    for (i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (windowData->wormeditPresentFences[i] != VK_NULL_HANDLE) {
            renderer->vkDestroyFence(renderer->logicalDevice, windowData->wormeditPresentFences[i], NULL);
            windowData->wormeditPresentFences[i] = VK_NULL_HANDLE;
        }
    }
""" + anchor
    text = replace_once(text, anchor, new)
    anchor = "    VULKAN_Wait(driverData);\n\n    for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1) {"
    new = """    VULKAN_Wait(driverData);
    if (renderer->wormeditBoundedPresent) {
        for (i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (windowData->wormeditPresentFences[i] != VK_NULL_HANDLE) {
                renderer->vkWaitForFences(renderer->logicalDevice, 1, &windowData->wormeditPresentFences[i], VK_TRUE, SDL_MAX_UINT64);
            }
        }
    }

    for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1) {"""
    text = replace_once(text, anchor, new)
    old = """    if (!VULKAN_Wait((SDL_GPURenderer *)renderer)) {
        return false;
    }

    for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1) {
        if (windowData->inFlightFences[i] != NULL) {
            VULKAN_ReleaseFence(
                (SDL_GPURenderer *)renderer,
                windowData->inFlightFences[i]);
            windowData->inFlightFences[i] = NULL;
        }
    }

#ifdef SDL_VIDEO_DRIVER_PRIVATE"""
    new = """    if (renderer->wormeditBoundedPresent) {
        Uint32 ready = WORMEDIT_TryIdle(renderer, windowData);
        if (ready != 1) {
            windowData->needsSwapchainRecreate = true;
            return ready;
        }
    } else if (!VULKAN_Wait((SDL_GPURenderer *)renderer)) {
        return false;
    }
    WORMEDIT_ReleaseWindowFences(renderer, windowData);

#ifdef SDL_VIDEO_DRIVER_PRIVATE"""
    text = replace_once(text, old, new)
    # The removed loop was the only use of i in this function.
    text = replace_once(text, """    WindowData *windowData)
{
    Uint32 i;

    if (renderer->wormeditBoundedPresent)""", """    WindowData *windowData)
{
    if (renderer->wormeditBoundedPresent)""")
    anchor = "    if (windowData->needsSurfaceRecreate) {\n        SDL_VideoDevice *videoDevice"
    new = """    if (windowData->needsSurfaceRecreate) {
        if (renderer->wormeditBoundedPresent) {
            Uint32 ready = WORMEDIT_TryIdle(renderer, windowData);
            if (ready != 1) {
                return ready == VULKAN_INTERNAL_TRY_AGAIN;
            }
            WORMEDIT_ReleaseWindowFences(renderer, windowData);
            VULKAN_INTERNAL_DestroySwapchain(renderer, windowData);
        }
        SDL_VideoDevice *videoDevice"""
    text = replace_once(text, anchor, new)
    anchor = """                windowData->surface,
                NULL);
        if (!videoDevice->Vulkan_CreateSurface("""
    text = replace_once(text, anchor, """                windowData->surface,
                NULL);
        windowData->surface = VK_NULL_HANDLE;
        if (!videoDevice->Vulkan_CreateSurface(""")
    text = replace_once(text, "            if (windowData->inFlightFences[windowData->frameCounter] != NULL) {", "            if (!renderer->wormeditBoundedPresent && windowData->inFlightFences[windowData->frameCounter] != NULL) {")
    anchor = "    // Finally, try to acquire!"
    new = """    if (renderer->wormeditBoundedPresent) {
        Uint32 ready = WORMEDIT_CheckFence(renderer, windowData->wormeditPresentFences[windowData->frameCounter]);
        if (ready != 1) {
            return ready == VULKAN_INTERNAL_TRY_AGAIN;
        }
    }

""" + anchor
    text = replace_once(text, anchor, new)
    text = replace_once(text, "            windowData->swapchain,\n            SDL_MAX_UINT64,", "            windowData->swapchain,\n            renderer->wormeditBoundedPresent && !block ? 0 : SDL_MAX_UINT64,")
    anchor = "        // Surface lost — flag for surface + swapchain recreation on next call"
    new = """        if (renderer->wormeditBoundedPresent && (acquireResult == VK_NOT_READY || acquireResult == VK_TIMEOUT)) {
            return true;
        }
        if (renderer->wormeditBoundedPresent && acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            windowData->needsSwapchainRecreate = true;
            return true;
        }

""" + anchor
    text = replace_once(text, anchor, new)
    anchor = "        // If acquisition is invalid, let's try to recreate"
    new = """        if (renderer->wormeditBoundedPresent) {
            SDL_SetError("Vulkan swapchain acquisition failed (%d)", acquireResult);
            return false;
        }

""" + anchor
    text = replace_once(text, anchor, new)
    anchor = """        presentResult = renderer->vkQueuePresentKHR(
            renderer->unifiedQueue,
            &presentInfo);"""
    new = """        VkSwapchainPresentFenceInfoEXT wormeditPresentInfo;
        if (renderer->wormeditBoundedPresent) {
            VkFence *fence = &presentData->windowData->wormeditPresentFences[presentData->windowData->frameCounter];
            vulkanResult = renderer->vkResetFences(renderer->logicalDevice, 1, fence);
            if (vulkanResult != VK_SUCCESS) {
                VULKAN_INTERNAL_ReleaseCommandBuffer(vulkanCommandBuffer);
                SDL_UnlockMutex(renderer->submitLock);
                CHECK_VULKAN_ERROR_AND_RETURN(vulkanResult, vkResetFences, false);
            }
            SDL_zero(wormeditPresentInfo);
            wormeditPresentInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
            wormeditPresentInfo.swapchainCount = 1;
            wormeditPresentInfo.pFences = fence;
            presentInfo.pNext = &wormeditPresentInfo;
        }
""" + anchor
    text = replace_once(text, anchor, new)
    old = """    for (i = renderer->submittedCommandBufferCount - 1; i >= 0; i -= 1) {
        commandBuffer = renderer->submittedCommandBuffers[i];
        VULKAN_INTERNAL_CleanCommandBuffer(renderer, commandBuffer, false);
    }

    VULKAN_INTERNAL_PerformPendingDestroys(renderer);"""
    text = replace_once(text, old, "    WORMEDIT_CleanIdle(renderer);")
    text = replace_once(text, """    VulkanCommandBuffer *commandBuffer;
    VkResult result;
    Sint32 i;

    SDL_LockMutex(renderer->submitLock);""", """    VkResult result;

    SDL_LockMutex(renderer->submitLock);""")
    text = replace_once(text, 'SET_STRING_ERROR("Failed to initialize Vulkan!");', 'WORMEDIT_PreserveError("Failed to initialize bounded Vulkan presentation");')
    text = replace_once(text, 'SET_STRING_ERROR("Failed to create logical device!");', 'WORMEDIT_PreserveError("Failed to create bounded Vulkan device");')
    return text

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", choices=("linux", "metal"), default="linux")
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--helpers", type=Path, required=True)
    args = parser.parse_args()
    output = {}
    audited = audited_sources(args.platform)
    for relative, expected in audited.items():
        source = args.source / relative
        content = source.read_bytes()
        digest = hashlib.sha256(content).hexdigest()
        if digest != expected:
            raise ValueError(f"{source}: unaudited SDL source {digest}; expected {expected}; selected SDL tree was not modified")
        text = content.decode()
        output[relative] = patch_metal(text) if args.platform == "metal" else (
            patch_render(text) if "render/gpu" in relative else patch_vulkan(text, args.helpers.read_text()))
    manifest = {}
    for relative, text in output.items():
        destination = args.output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        text = "/* Altered SDL source: Wormedit bounded presentation override. See cmake/sdl/README.md. */\n" + text
        if not destination.exists() or destination.read_text() != text:
            destination.write_text(text)
        manifest[relative] = {"source_sha256": audited[relative], "generated_sha256": hashlib.sha256(text.encode()).hexdigest()}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

if __name__ == "__main__":
    main()
