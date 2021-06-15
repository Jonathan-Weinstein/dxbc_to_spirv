#pragma once

#include "VulkanAPI.h"

struct VulkanLib {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugUtilsMessenger;
    // volk has dll/so handles
};

struct VulkanCore {
    VkDevice device;
    VkQueue universalQueue0;
    uint32_t universalFamilyIndex;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties physicalDeviceProperties;

    VulkanLib lib;
};

VkResult ContructCore(VulkanCore *pObjects, uint32_t prefVendorID,
                      int prefPhysicalDeviceType, bool bValidate);

void DestroyCore(const VulkanCore &core);
