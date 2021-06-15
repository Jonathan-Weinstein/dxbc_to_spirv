#include "SpirvRunner.h"

#include "VkSimpleInit.h"

#include <stdio.h>

#include <string.h>

// Simple Vulkan utils:
// -----------------------------------------------------------------------------

#include <stdlib.h>

#define VKR_CHECK(F)                                                           \
    do {                                                                       \
        VkResult _r = F;                                                        \
        if (_r) {                                                               \
            printf("\nFailed VkResult = %d at line %d.\n", _r, __LINE__);       \
            exit(_r);                                                           \
        }                                                                      \
    } while (0)


static VkPipeline
CreateComputePipeline(VkDevice device, VkShaderModule sm, VkPipelineLayout layout)
{
    VkPipeline pipeline = nullptr;

    VkPipelineShaderStageCreateInfo s = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    s.pName = "main";
    s.module = sm;
    s.stage = VK_SHADER_STAGE_COMPUTE_BIT;

    VkComputePipelineCreateInfo info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    info.stage = s;
    info.layout = layout;
    info.basePipelineIndex = -1;

    // there is also a disable optimizatiosn bit.

    //VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR
    // ^^ see actual GPU assembly?

    VKR_CHECK(vkCreateComputePipelines(device, nullptr, 1, &info, ALLOC_CBS, &pipeline));
    return pipeline;
}

static VkShaderModule
CreateShaderModule(VkDevice device, uint32_t nBytes, const void *spirv)
{
    VkShaderModule module = nullptr;
    VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = nBytes;
    info.pCode = static_cast<const uint32_t *>(spirv);
    VKR_CHECK(vkCreateShaderModule(device, &info, ALLOC_CBS, &module));
    return module;
}

template<uint32_t N> static VkShaderModule
CreateShaderModule(VkDevice device, const uint32_t (&a)[N])
{
    return CreateShaderModule(device, N * sizeof (uint32_t), a);
}

typedef struct BufferAndMemory {
    VkBuffer buffer;
    VkDeviceMemory memory;
} BufferAndMemory;

typedef struct ImageAndMemory {
    VkImage image;
    VkDeviceMemory memory;
} ImageAndMemory;

static int
FindMemoryType(const VkPhysicalDeviceMemoryProperties& memProps,
               uint32_t requiredTypeBits,
               VkMemoryPropertyFlags desiredMemPropFlags)
{
    int atLeastIndex = -1;
    const int nTypes = memProps.memoryTypeCount;
    for (int i = 0; i < nTypes; ++i) {
        if (requiredTypeBits & (1u << i)) {
            const VkMemoryPropertyFlags flags = memProps.memoryTypes[i].propertyFlags;
            if (flags == desiredMemPropFlags) {
                return i; // first exact index
            } else if (atLeastIndex < 0 &&
                       (flags & desiredMemPropFlags) == desiredMemPropFlags) {
                atLeastIndex = i; // first at least index
            }
        }
    }
    return atLeastIndex;
}

static void
CreateBufferAndMemory(VkDevice device,
                      const VkPhysicalDeviceMemoryProperties& memProps,
                      VkMemoryPropertyFlags desiredMemProps,
                      uint32_t bufferByteSize, VkBufferUsageFlags usage,
                      BufferAndMemory *p) {
    VkBufferCreateInfo bufInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufInfo.size = bufferByteSize;
    bufInfo.usage = usage;
    VKR_CHECK(vkCreateBuffer(device, &bufInfo, ALLOC_CBS, &p->buffer));

    VkMemoryRequirements bufReqs;
    vkGetBufferMemoryRequirements(device, p->buffer, &bufReqs);
    const int sMemIndex = FindMemoryType(memProps, bufReqs.memoryTypeBits, desiredMemProps);
    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = bufReqs.size;
    allocInfo.memoryTypeIndex = uint32_t(sMemIndex);
    printf("Buffer with {size=%d, usage=0x%X} using memTypeIndex=%d.\n",
           bufferByteSize, usage, allocInfo.memoryTypeIndex);
    if (sMemIndex < 0) {
        puts("no good buffer mem type index");
        exit(2);
    }
    VKR_CHECK(vkAllocateMemory(device, &allocInfo, ALLOC_CBS, &p->memory));
    VKR_CHECK(vkBindBufferMemory(device, p->buffer, p->memory, 0));
}

static void
CreateImageAndMemory(VkDevice device,
                     const VkPhysicalDeviceMemoryProperties& memProps,
                     VkMemoryPropertyFlags desiredMemProps,
                     const VkImageCreateInfo& imageInfo,
                     ImageAndMemory *p) {
    VKR_CHECK(vkCreateImage(device, &imageInfo, ALLOC_CBS, &p->image));

    VkMemoryRequirements imageReqs;
    vkGetImageMemoryRequirements(device, p->image, &imageReqs);
    const int sMemIndex = FindMemoryType(memProps, imageReqs.memoryTypeBits, desiredMemProps);
    VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = imageReqs.size;
    allocInfo.memoryTypeIndex = uint32_t(sMemIndex);
    printf("Image using memTypeIndex=%d.\n", allocInfo.memoryTypeIndex);
    if (sMemIndex < 0) {
        puts("no good image mem type index");
        exit(3);
    }
    VKR_CHECK(vkAllocateMemory(device, &allocInfo, ALLOC_CBS, &p->memory));
    VKR_CHECK(vkBindImageMemory(device, p->image, p->memory, 0));
}

static void
DestroyBufferAndFreeMemory(VkDevice device, BufferAndMemory a)
{
    vkDestroyBuffer(device, a.buffer, ALLOC_CBS);
    vkFreeMemory(device, a.memory, ALLOC_CBS);
}

static void
DestroyImageAndFreeMemory(VkDevice device, ImageAndMemory a)
{
    vkDestroyImage(device, a.image, ALLOC_CBS);
    vkFreeMemory(device, a.memory, ALLOC_CBS);
}


struct SpirvRunner {
    VulkanCore core;
    VkCommandPool cmdpool;
    VkCommandBuffer cmdbuf;
    VkDescriptorSetLayout descSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPhysicalDeviceMemoryProperties memProps;
    BufferAndMemory readbackbuf; // CS can write into this too.
    void *pReadbackMappedPtr;
    VkBufferView bufview;
};

void DeleteSpirvRunner(SpirvRunner *r)
{
    VkDevice const device = r->core.device;
    if (r->bufview) {
        vkDestroyBufferView(device, r->bufview, ALLOC_CBS);
    }
    DestroyBufferAndFreeMemory(device, r->readbackbuf);
    if (r->pipelineLayout) {
        vkDestroyPipelineLayout(device, r->pipelineLayout, ALLOC_CBS);
    }
    if (r->descSetLayout) {
        vkDestroyDescriptorSetLayout(device, r->descSetLayout, ALLOC_CBS);
    }
    if (r->cmdpool) {
        if (r->cmdbuf) {
            vkFreeCommandBuffers(device, r->cmdpool, 1, &r->cmdbuf);
        }
        vkDestroyCommandPool(device, r->cmdpool, ALLOC_CBS);
    }
    DestroyCore(r->core);
    delete r;
}

SpirvRunner *NewSpirvRunner()
{
    SpirvRunner *const r = new SpirvRunner();
    VkResult result = ContructCore(&r->core, -1, -1, true); // bValidate
    if (result != VK_SUCCESS) {
        printf("device creation failed with %d\n", int(result));
        DeleteSpirvRunner(r);
        return nullptr;
    }

    vkGetPhysicalDeviceMemoryProperties(r->core.physicalDevice, &r->memProps);
    

    VkDevice const device = r->core.device;
    {
        const VkCommandPoolCreateInfo cmdPoolInfo = {
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr,
            0, // flags
            r->core.universalFamilyIndex};
        VKR_CHECK(vkCreateCommandPool(device, &cmdPoolInfo, ALLOC_CBS, &r->cmdpool));

        const VkCommandBufferAllocateInfo cmdBufAllocInfo = {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, r->cmdpool,
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            1 // commandBufferCount
        };
        VKR_CHECK(vkAllocateCommandBuffers(device, &cmdBufAllocInfo, &r->cmdbuf));
    }

    const unsigned ByteCap = 4096;
    CreateBufferAndMemory(device, r->memProps,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        ByteCap ,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        &r->readbackbuf);
    VKR_CHECK(vkMapMemory(device, r->readbackbuf.memory, 0, VK_WHOLE_SIZE, 0, &r->pReadbackMappedPtr));
    memset(r->pReadbackMappedPtr, 0xCC, ByteCap );

    {
        VkBufferViewCreateInfo viewinfo = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
        viewinfo.range = VK_WHOLE_SIZE;
        viewinfo.format = VK_FORMAT_R32_UINT;
        viewinfo.buffer = r->readbackbuf.buffer;
        vkCreateBufferView(device, &viewinfo, ALLOC_CBS, &r->bufview);
    }

    {
        VkDescriptorSetLayoutBinding binding = {}; // set = 0, binding = 0
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        binding.descriptorCount = 1;

        VkDescriptorSetLayoutCreateInfo descSetInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        descSetInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        descSetInfo.bindingCount = 1;
        descSetInfo.pBindings = &binding;
        vkCreateDescriptorSetLayout(device, &descSetInfo, ALLOC_CBS, &r->descSetLayout);

        VkPipelineLayoutCreateInfo pl = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &r->descSetLayout;
        vkCreatePipelineLayout(device, &pl, ALLOC_CBS, &r->pipelineLayout);
    }

    return r;
}

const uint32_t *RunSimpleCompute(SpirvRunner *r, const void *spvCpde, unsigned numCodeBytes)
{
    VkDevice const device = r->core.device;

    VkShaderModule const shaderModule = CreateShaderModule(device, numCodeBytes, spvCpde);
    VkPipeline const pipeline = CreateComputePipeline(device, shaderModule, r->pipelineLayout);
    vkDestroyShaderModule(device, shaderModule, ALLOC_CBS);

    vkResetCommandPool(device, r->cmdpool, 0);

    {
        const VkCommandBufferBeginInfo cmdBufbeginInfo = {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};

        VKR_CHECK(vkBeginCommandBuffer(r->cmdbuf, &cmdBufbeginInfo));
    }
    vkCmdBindPipeline(r->cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    {
        VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.descriptorCount = 1;
        write.pTexelBufferView = &r->bufview; // I guess this is a ptr because of descriptor arrays
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;

        vkCmdPushDescriptorSetKHR(r->cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, r->pipelineLayout, 0, 1, &write);
    }
    vkCmdDispatch(r->cmdbuf, 64, 1, 1);
    {
        VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT };
        vkCmdPipelineBarrier(r->cmdbuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0x0,
            1, &mb, 0, nullptr, 0, nullptr);
    }
    vkEndCommandBuffer(r->cmdbuf);

    // submit and WFI:
    {
        VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &r->cmdbuf;
        VKR_CHECK(vkQueueSubmit(r->core.universalQueue0, 1, &submitInfo, VkFence(nullptr)));
        VKR_CHECK(vkQueueWaitIdle(r->core.universalQueue0));
    }

    vkDestroyPipeline(device, pipeline, ALLOC_CBS);

    return reinterpret_cast<uint32_t *>(r->pReadbackMappedPtr);
}


// never agian