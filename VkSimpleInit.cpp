/*
    Make a VkInstance with version 1.1+,
    then make a VkDevice with the stuff that has to be
    enabled for this demo, like
   VkPhysicalDeviceFeatures::pipelineStatisticsQuery.
*/

#include "VkSimpleInit.h"
#include "common.h"
#include "VulkanAPI.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static VkBool32 VKAPI_PTR
DebugUtilsMessengerCallbackEXT(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *) // pUserData
{
    const char *severity;
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        severity = "ERROR";
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        severity = "WARNING";
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        severity = "INFO";
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
        severity = "VERBOSE";
    else
        severity = "???";

    const char *type;
    if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
        type = "PERFORMANCE";
    if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
        type = "VALIDATION";
    if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
        type = "GENERAL";
    else
        type = "???";

    fprintf(stderr, "DebugUtilsMessage: severity=%s, type=%s, {message}={%s}\n",
            severity, type, pCallbackData->pMessage);

    return VK_FALSE; // The application should always return VK_FALSE.
}

static VkResult
ConstructLib(VulkanLib *lib, bool bValidate) {
    *lib = {};
    const uint32_t minApiVersionNeeded = VK_API_VERSION_1_1;
    if (volkInitialize() != VK_SUCCESS ||
        volkGetInstanceVersion() < minApiVersionNeeded) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkValidationFeaturesEXT validationFeaturesInfo = { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
    //VkValidationFeatureEnableEXT extraValidationEnables[] = { VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT };

    VkApplicationInfo appInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.apiVersion = minApiVersionNeeded;
    VkInstanceCreateInfo createInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &appInfo;

    const char *instanceExtensions[16];
    uint nInstanceExtensions = 0;
    const char *layers[16];
    uint nLayers = 0;

    if (bValidate) {
        layers[nLayers++] = "VK_LAYER_KHRONOS_validation";

        instanceExtensions[nInstanceExtensions++] =
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

        instanceExtensions[nInstanceExtensions++] =
            VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;

        ASSERT(createInfo.pNext == nullptr);
#if 0
        validationFeaturesInfo.enabledValidationFeatureCount = 1;
        validationFeaturesInfo.pEnabledValidationFeatures = extraValidationEnables;
        createInfo.pNext = &validationFeaturesInfo;
#endif
    }

    createInfo.ppEnabledLayerNames = nLayers ? layers : nullptr;
    createInfo.enabledLayerCount = nLayers;

    createInfo.ppEnabledExtensionNames =
        nInstanceExtensions ? instanceExtensions : nullptr;
    createInfo.enabledExtensionCount = nInstanceExtensions;

    for (uint i = 0; i < nLayers; ++i) {
        printf("Layer [%s] enabled.\n", layers[i]);
    }

    for (uint i = 0; i < nInstanceExtensions; ++i) {
        printf("Instance extension [%s] enabled\n", instanceExtensions[i]);
    }

    printf("In vkCreateInstance...");
    // This takes like 1.5+ seconds on my PC that has a
    // Intel HD 630 + a NV Quadro P1000, this related?
    // https://github.com/KhronosGroup/Vulkan-LoaderAndValidationLayers/issues/2191
    VkResult res = vkCreateInstance(&createInfo, nullptr, &lib->instance);
    puts(" done.");
    if (res == VK_SUCCESS) {
        volkLoadInstanceOnly(lib->instance);
        if (bValidate) {
            VkDebugUtilsMessengerCreateInfoEXT debugInfo = {
                VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                nullptr,
                0, // flags
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                    // VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                    // VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                    0,
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                    // VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
                    // VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                    0,
                DebugUtilsMessengerCallbackEXT,
                nullptr // pUserData
            };
            VkResult debugRes = vkCreateDebugUtilsMessengerEXT(
                lib->instance, &debugInfo, nullptr, &lib->debugUtilsMessenger);
            if (debugRes != VK_SUCCESS) {
                fprintf(stderr,
                        "vkCreateDebugUtilsMessengerEXT returned %d, "
                        "continuing anyway.\n",
                        debugRes);
            }
        }
    }
    return res;
}

static void
DestructLib(const VulkanLib &lib) {
    if (lib.debugUtilsMessenger) {
        vkDestroyDebugUtilsMessengerEXT(lib.instance, lib.debugUtilsMessenger,
                                        nullptr);
    }

    if (lib.instance) {
        vkDestroyInstance(lib.instance, nullptr);
    }
}

struct ExtensionPropsSet {
    const VkExtensionProperties *begin;
    const VkExtensionProperties *end;
};

static void
QueryDeviceExtProps(VkPhysicalDevice physDev,
                    ExtensionPropsSet *props) // OUT
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physDev, nullptr, &count, nullptr);
    VkExtensionProperties *a = nullptr;
    if (count >= 1 && count < 2048) {
        a = (VkExtensionProperties *)malloc(count *
                                            sizeof *a); // 260 bytes per element
        vkEnumerateDeviceExtensionProperties(physDev, nullptr, &count, a);
    }
    props->begin = a;
    props->end = a + count;
}

static void
FreeExtProps(const ExtensionPropsSet *props) {
    free((void *)props->begin);
}

static bool
HasExtension(const ExtensionPropsSet *props, const char *extName) {
    const VkExtensionProperties *p = props->begin;
    const VkExtensionProperties *const pEnd = props->end;
    for (; p != pEnd; ++p) {
        if (strcmp(extName, p->extensionName) == 0) {
            return true;
        }
    }
    return false;
}

static const char *
GetDeviceTypeString(VkPhysicalDeviceType type) {
    static const char *const DeviceTypeNames[] = {
        "other",      // VK_PHYSICAL_DEVICE_TYPE_OTHER = 0,
        "integrated", // VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU = 1,
        "discrete",   // VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU = 2,
        "virtual",    // VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU = 3,
        "CPU"         // VK_PHYSICAL_DEVICE_TYPE_CPU = 4
    };
    return size_t(type) < lengthof(DeviceTypeNames) ? DeviceTypeNames[type]
                                                    : "???";
}

static VkPhysicalDevice
PickPhysicalDeviceAndFindFamilies(
    VkInstance instance, uint32_t prefVendorID,
    int prefPhysicalDeviceType, // -1 if no preference
    uint32_t *pUniversalFamily) {
    VkPhysicalDevice physicalDevices[16];
    uint32_t physicalDeviceCount = lengthof(physicalDevices);
    if (VkResult r = vkEnumeratePhysicalDevices(instance, &physicalDeviceCount,
                                                physicalDevices)) {
        printf("vkEnumeratePhysicalDevices returned %d.\n", r);
        return nullptr;
    }
    printf("physical device count: %d\n", physicalDeviceCount);

    int prefIndex = -1;
    int prefScore = 0;

    *pUniversalFamily = VK_QUEUE_FAMILY_IGNORED;

    for (uint32_t physDevIndex = 0; physDevIndex < physicalDeviceCount;
         ++physDevIndex) {
        VkPhysicalDevice const physdev = physicalDevices[physDevIndex];
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physdev, &props);
        printf("PhysDev[%d]: type=%s, deviceName=%s, deviceID=%d, "
               "vendorID=0x%X, driverVersion=0x%X\n",
               physDevIndex, GetDeviceTypeString(props.deviceType),
               props.deviceName, props.deviceID, props.vendorID,
               props.driverVersion);

        {
            ExtensionPropsSet extSet;
            QueryDeviceExtProps(physdev, &extSet);
            const bool hasPushDesc =
                HasExtension(&extSet, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
            FreeExtProps(&extSet);
            if (!hasPushDesc) {
                printf("Cannot use this physical device, lacks %s\n",
                       VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
                continue;
            }
        }

        {
            VkPhysicalDeviceTransformFeedbackFeaturesEXT xfbFeatures = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT};
            VkPhysicalDeviceFeatures2 features2;
            features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features2.pNext = &xfbFeatures;
            vkGetPhysicalDeviceFeatures2(physdev, &features2);

            bool missing = false;
            if (!features2.features.robustBufferAccess) {
                puts("missing robustBufferAccess");
                missing = true;
            }
            if (missing) {
                printf(
                    "Cannot use this physical device, lacks features above.\n");
                continue;
            }
        }

        VkQueueFamilyProperties familyProps[32];
        uint32_t numFamilies = lengthof(familyProps);
        vkGetPhysicalDeviceQueueFamilyProperties(physdev, &numFamilies,
                                                 familyProps);
        ASSERT(numFamilies < 32u);
        ASSERT(numFamilies);
        printf("number of queue families: %d\n", numFamilies);

        int32_t sUniversalFam = -1;
        for (uint32_t fam = 0; fam < numFamilies; ++fam) {
            printf("family[%u].queueFlags = 0x%X\n", fam,
                   familyProps[fam].queueFlags);
            constexpr VkFlags universalFlags = VK_QUEUE_GRAPHICS_BIT |
                                               VK_QUEUE_COMPUTE_BIT |
                                               VK_QUEUE_TRANSFER_BIT;
            if ((familyProps[fam].queueFlags & universalFlags) ==
                    universalFlags &&
                sUniversalFam < 0) {
                sUniversalFam = int(fam);
            }
        }
        if (sUniversalFam < 0) {
            printf(
                "PhysDev has no universal graphics|compute|transfer queue.\n");
            continue;
        }

        int score = int(physicalDeviceCount -
                        physDevIndex); // first has highest bonus, last has 1
        score += (props.vendorID == prefVendorID ? 128 : 0);
        score += (props.deviceType == prefPhysicalDeviceType ? 64 : 0);

        if (score > prefScore) {
            prefScore = score;
            prefIndex = int(physDevIndex);
            *pUniversalFamily = uint(sUniversalFam);
        }
    }

    if (prefIndex >= 0) {
        ASSERT(unsigned(prefIndex) < physicalDeviceCount);
        ASSERT(int32_t(*pUniversalFamily) >= 0);

        printf("Selected PhysDev[%d]\n", prefIndex);
        return physicalDevices[prefIndex];
    } else {
        printf("ERROR: No suitable PhysDev found\n");
        return nullptr;
    }
}

static VkResult
CreateDevice(VkPhysicalDevice physicalDevice, uint32_t universalFamily,
             VkDevice *pDevice) {
    const float queuePriorities[] = {1.0f};

    VkDeviceQueueCreateInfo queueInfo = {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = universalFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = queuePriorities;

    const char *const DeviceExtensions[] = {
        VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
    /* Difference from VkPhysicalDeviceFeatures (the .features member) is that
     * this has sType/pNext: */
    VkPhysicalDeviceFeatures2 features2 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.features.robustBufferAccess = true;

    VkDeviceCreateInfo createInfo = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;

    createInfo.ppEnabledExtensionNames = DeviceExtensions;
    createInfo.enabledExtensionCount = lengthof(DeviceExtensions);

    VkPhysicalDeviceTransformFeedbackFeaturesEXT xfbFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT,
        nullptr,
        VK_TRUE, // transformFeedback
        VK_TRUE  // geometryStreams
    };

    features2.pNext = &xfbFeatures;
    createInfo.pNext = &features2;

    *pDevice = nullptr;
    return vkCreateDevice(physicalDevice, &createInfo, ALLOC_CBS, pDevice);
}

VkResult
ContructCore(VulkanCore *pObjects, uint32_t prefVendorID,
             int prefPhysicalDeviceType, bool bValidate) {
    *pObjects = {};

    ConstructLib(&pObjects->lib, bValidate);
    VkPhysicalDevice physdev = PickPhysicalDeviceAndFindFamilies(
        pObjects->lib.instance, prefVendorID, prefPhysicalDeviceType,
        &pObjects->universalFamilyIndex);
    if (!physdev) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult res = CreateDevice(physdev, pObjects->universalFamilyIndex,
                                &pObjects->device);
    if (res != VK_SUCCESS) {
        return res;
    }
    pObjects->physicalDevice = physdev;

    const VkDevice device = pObjects->device;
    volkLoadDevice(device);
    // Get queue[0] of the universal family:
    vkGetDeviceQueue(device, pObjects->universalFamilyIndex, 0,
                     &pObjects->universalQueue0);
    if (!vkCmdPushDescriptorSetKHR) {
        puts("\n\n vkCmdPushDescriptorSetKHR nullptr");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return VK_SUCCESS;
}

void
DestroyCore(const VulkanCore &core) {
    if (core.device) {
        vkDestroyDevice(core.device, ALLOC_CBS);
    }
    DestructLib(core.lib);
}
