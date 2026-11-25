// Peanuts Native for Linux: the VK_KHR_android_surface types the replay's tables name (the NDK's header). The surface itself is made by SDL on Linux
// (pnl_window_create_surface), so vkCreateAndroidSurfaceKHR is looked up but never called.
#pragma once
#include <vulkan/vulkan_core.h>
#ifdef __cplusplus
extern "C" {
#endif
#define VK_KHR_android_surface 1
struct ANativeWindow;
#define VK_KHR_ANDROID_SURFACE_SPEC_VERSION 6
#define VK_KHR_ANDROID_SURFACE_EXTENSION_NAME "VK_KHR_android_surface"
typedef VkFlags VkAndroidSurfaceCreateFlagsKHR;
typedef struct VkAndroidSurfaceCreateInfoKHR {
    VkStructureType sType; const void *pNext; VkAndroidSurfaceCreateFlagsKHR flags; struct ANativeWindow *window;
} VkAndroidSurfaceCreateInfoKHR;
typedef VkResult (VKAPI_PTR *PFN_vkCreateAndroidSurfaceKHR)(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface);
#ifdef __cplusplus
}
#endif
