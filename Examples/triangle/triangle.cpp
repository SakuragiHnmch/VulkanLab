#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fstream>
#include <vector>
#include <exception>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"

#define ENABLE_VALIDATION false
// Set to "true" to use staging buffers for uploading vertex and index data to device local memory
// See "prepareVertices" for details on what's staging and on why to use it
#define USE_STAGING true

class VulkanExample : public VulkanExampleBase {
public:
    // Vertex layout
    struct Vertex {
        float position[3];
        float normal[3];
        float texCoords[2];
    };

    // Vertex buffer and attributes
    struct {
        VkDeviceMemory memory; // Handle to the device memory for this buffer
        VkBuffer buffer;       // Handle to the Vulkan buffer object that the memory is bound to
        uint32_t count;
    } vertices;

    // Uniform buffer block object
    struct {
        VkDeviceMemory memory;
        VkBuffer buffer;
        VkDescriptorBufferInfo descriptor;
    } uniformBufferVS;

    struct {
        VkDeviceMemory memory;
        VkBuffer buffer;
        VkDescriptorBufferInfo descriptor;
    } uniformBufferFS;

    struct {
        glm::mat4 projectionMatrix;
        glm::mat4 modelMatrix;
        glm::mat4 viewMatrix;
    } uboVS;

    struct {
        glm::vec3 position;
        glm::vec3 viewPos;
        glm::vec3 ambient;
        glm::vec3 diffuse;
        glm::vec3 specular;
    } uboFS;

    struct LightMap {
        Texture2D diffuseMap;
        Texture2D specularMap;
    } lightMap;

    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;

    glm::vec3 lightPos = glm::vec3(.2f, 1.0f, 2.0f);

    VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
    {
        title = "Vulkan Example - Basic indexed triangle";
        // Setup a default look-at camera
        camera.type = Camera::CameraType::lookat;
        camera.setPosition(glm::vec3(0.0f, 0.0f, -2.5f));
        camera.setRotation(glm::vec3(0.0f));
        camera.setPerspective(60.0f, (float)width / (float)height, 1.0f, 256.0f);
        // Values not set here are initialized in the base class constructor
    }

    ~VulkanExample()
    {
        // Clean up used Vulkan resources
        // Note: Inherited destructor cleans up resources stored in base class
        vkDestroyPipeline(device, pipeline, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        vkDestroyBuffer(device, vertices.buffer, nullptr);
        vkFreeMemory(device, vertices.memory, nullptr);

        vkDestroyBuffer(device, uniformBufferVS.buffer, nullptr);
        vkFreeMemory(device, uniformBufferVS.memory, nullptr);

        vkDestroyBuffer(device, uniformBufferFS.buffer, nullptr);
        vkFreeMemory(device, uniformBufferFS.memory, nullptr);
    }

    void loadAssets()
    {
        lightMap.diffuseMap.loadFromFile(getAssetPath() + "images/container.png", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
        lightMap.specularMap.loadFromFile(getAssetPath() + "images/container_specular.png", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
    }

    // Get a new command buffer from the command pool
    // If begin is true, the command buffer is also started so we can start adding commands
    VkCommandBuffer getCommandBuffer(bool begin)
    {
        VkCommandBuffer cmdBuffer;

        VkCommandBufferAllocateInfo cmdBufAllocateInfo = {};
        cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufAllocateInfo.commandPool = cmdPool;
        cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufAllocateInfo.commandBufferCount = 1;

        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &cmdBuffer));

        // If requested, also start the new command buffer
        if (begin)
        {
            VkCommandBufferBeginInfo cmdBufInfo = initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
        }

        return cmdBuffer;
    }

    // End the command buffer and submit it to the queue
    // Uses a fence to ensure command buffer has finished executing before deleting it
    void flushCommandBuffer(VkCommandBuffer commandBuffer)
    {
        assert(commandBuffer != VK_NULL_HANDLE);

        VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        // Create fence to ensure that the command buffer has finished executing
        VkFenceCreateInfo fenceCreateInfo = {};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags = 0;
        VkFence fence;
        VK_CHECK_RESULT(vkCreateFence(device, &fenceCreateInfo, nullptr, &fence));

        // Submit to the queue
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
        // Wait for the fence to signal that command buffer has finished executing
        VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, cmdPool, 1, &commandBuffer);
    }

    // Prepare vertex and index buffers for an indexed triangle
    // Also uploads them to device local memory using staging and initializes vertex input and attribute binding to match the vertex shader
    void prepareVertices()
    {
        // A note on memory management in Vulkan in general:
        // This is a very complex topic and while it's fine for an example application to small individual memory allocations that is not
        // what should be done a real-world application, where you should allocate large chunks of memory at once instead.

        // Setup vertices
        std::vector<Vertex> vertexBuffer = {
            {{-0.5f, -0.5f, -0.5f},  {0.0f,  0.0f, -1.0f},  {0.0f,  0.0f}},
            {{ 0.5f, -0.5f, -0.5f},  {0.0f,  0.0f, -1.0f},  {1.0f,  0.0f}},
            {{ 0.5f,  0.5f, -0.5f},  {0.0f,  0.0f, -1.0f},  {1.0f,  1.0f}},
            {{ 0.5f,  0.5f, -0.5f},  {0.0f,  0.0f, -1.0f},  {1.0f,  1.0f}},
            {{-0.5f,  0.5f, -0.5f},  {0.0f,  0.0f, -1.0f},  {0.0f,  1.0f}},
            {{-0.5f, -0.5f, -0.5f},  {0.0f,  0.0f, -1.0f},  {0.0f,  0.0f}},

            {{-0.5f, -0.5f,  0.5f},  {0.0f,  0.0f,  1.0f},  {0.0f,  0.0f}},
            {{ 0.5f, -0.5f,  0.5f},  {0.0f,  0.0f,  1.0f},  {1.0f,  0.0f}},
            {{ 0.5f,  0.5f,  0.5f},  {0.0f,  0.0f,  1.0f},  {1.0f,  1.0f}},
            {{ 0.5f,  0.5f,  0.5f},  {0.0f,  0.0f,  1.0f},  {1.0f,  1.0f}},
            {{-0.5f,  0.5f,  0.5f},  {0.0f,  0.0f,  1.0f},  {0.0f,  1.0f}},
            {{-0.5f, -0.5f,  0.5f},  {0.0f,  0.0f,  1.0f},  {0.0f,  0.0f}},

            {{-0.5f,  0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f},  {1.0f,  0.0f}},
            {{-0.5f,  0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f},  {1.0f,  1.0f}},
            {{-0.5f, -0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f},  {0.0f,  1.0f}},
            {{-0.5f, -0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f},  {0.0f,  1.0f}},
            {{-0.5f, -0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f},  {0.0f,  0.0f}},
            {{-0.5f,  0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f},  {1.0f,  0.0f}},

            {{ 0.5f,  0.5f,  0.5f},  {1.0f,  0.0f,  0.0f},  {1.0f,  0.0f}},
            {{ 0.5f,  0.5f, -0.5f},  {1.0f,  0.0f,  0.0f},  {1.0f,  1.0f}},
            {{ 0.5f, -0.5f, -0.5f},  {1.0f,  0.0f,  0.0f},  {0.0f,  1.0f}},
            {{ 0.5f, -0.5f, -0.5f},  {1.0f,  0.0f,  0.0f},  {0.0f,  1.0f}},
            {{ 0.5f, -0.5f,  0.5f},  {1.0f,  0.0f,  0.0f},  {0.0f,  0.0f}},
            {{ 0.5f,  0.5f,  0.5f},  {1.0f,  0.0f,  0.0f},  {1.0f,  0.0f}},

            {{-0.5f, -0.5f, -0.5f},  {0.0f, -1.0f,  0.0f},  {0.0f,  1.0f}},
            {{ 0.5f, -0.5f, -0.5f},  {0.0f, -1.0f,  0.0f},  {1.0f,  1.0f}},
            {{ 0.5f, -0.5f,  0.5f},  {0.0f, -1.0f,  0.0f},  {1.0f,  0.0f}},
            {{ 0.5f, -0.5f,  0.5f},  {0.0f, -1.0f,  0.0f},  {1.0f,  0.0f}},
            {{-0.5f, -0.5f,  0.5f},  {0.0f, -1.0f,  0.0f},  {0.0f,  0.0f}},
            {{-0.5f, -0.5f, -0.5f},  {0.0f, -1.0f,  0.0f},  {0.0f,  1.0f}},

            {{-0.5f,  0.5f, -0.5f},  {0.0f,  1.0f,  0.0f},  {0.0f,  1.0f}},
            {{ 0.5f,  0.5f, -0.5f},  {0.0f,  1.0f,  0.0f},  {1.0f,  1.0f}},
            {{ 0.5f,  0.5f,  0.5f},  {0.0f,  1.0f,  0.0f},  {1.0f,  0.0f}},
            {{ 0.5f,  0.5f,  0.5f},  {0.0f,  1.0f,  0.0f},  {1.0f,  0.0f}},
            {{-0.5f,  0.5f,  0.5f},  {0.0f,  1.0f,  0.0f},  {0.0f,  0.0f}},
            {{-0.5f,  0.5f, -0.5f},  {0.0f,  1.0f,  0.0f},  {0.0f,  1.0f}}
        };
        uint32_t vertexBufferSize = static_cast<uint32_t>(vertexBuffer.size()) * sizeof(Vertex);
        vertices.count = static_cast<uint32_t>(vertexBuffer.size());

        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        VkMemoryRequirements memReqs;

        void *data;

        // Static data like vertex and index buffer should be stored on the device memory
        // for optimal (and fastest) access by the GPU
        //
        // To achieve this we use so-called "staging buffers" :
        // - Create a buffer that's visible to the host (and can be mapped)
        // - Copy the data to this buffer
        // - Create another buffer that's local on the device (VRAM) with the same size
        // - Copy the data from the host to the device using a command buffer
        // - Delete the host visible (staging) buffer
        // - Use the device local buffers for rendering

        struct StagingBuffer {
            VkDeviceMemory memory;
            VkBuffer buffer;
        };

        struct {
            StagingBuffer vertices;
        } stagingBuffers;

        // vertex buffer

        auto vertexBufferInfo = initializers::bufferCreateInfo();
        vertexBufferInfo.size = vertexBufferSize;
        // buffer is used as the copy source
        vertexBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        // Create a host-visible buffer to copy the vertex data to (staging buffer)
        VK_CHECK_RESULT(vkCreateBuffer(device, &vertexBufferInfo, nullptr, &stagingBuffers.vertices.buffer));

        vkGetBufferMemoryRequirements(device, stagingBuffers.vertices.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        // Request a host visible memory type that can be used to copy our data
        // Also request it to be coherent, so that writes are visible to the GPU right after unmapping the buffer
        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &stagingBuffers.vertices.memory));

        // Map and copy
        VK_CHECK_RESULT(vkMapMemory(device, stagingBuffers.vertices.memory, 0, memAlloc.allocationSize, 0, &data));
        memcpy(data, vertexBuffer.data(), vertexBufferSize);
        vkUnmapMemory(device, stagingBuffers.vertices.memory);
        VK_CHECK_RESULT(vkBindBufferMemory(device, stagingBuffers.vertices.buffer, stagingBuffers.vertices.memory, 0));

        // Create a device local buffer to which the (host local) vertex data will be copied and which will be used for rendering
        vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VK_CHECK_RESULT(vkCreateBuffer(device, &vertexBufferInfo, nullptr, &vertices.buffer));
        vkGetBufferMemoryRequirements(device, vertices.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &vertices.memory));
        VK_CHECK_RESULT(vkBindBufferMemory(device, vertices.buffer, vertices.memory, 0));

        //Buffer copies have to be submitted to a queue, so we need a command buffer for them
        //Note: some devices offer a dedicatied transfer queue (with only the transfer bit set ) that may be faster when doing lots of copies
        auto copyCmd = getCommandBuffer(true);

        // Put buffer region copies into command buffer
        VkBufferCopy copyRegion = {};

        // vertex buffer
        copyRegion.size = vertexBufferSize;
        vkCmdCopyBuffer(copyCmd, stagingBuffers.vertices.buffer, vertices.buffer, 1, &copyRegion);

        // Flushing the command buffer will also submit it to the queue and uses a fence to ensure that all commands have been executed before returning
        flushCommandBuffer(copyCmd);

        // Destroy staging buffers
        // Note: Staging buffer must not be deleted before the copies have been submitted and executed
        vkDestroyBuffer(device, stagingBuffers.vertices.buffer, nullptr);
        vkFreeMemory(device, stagingBuffers.vertices.memory, nullptr);
    }

    void setupDescriptorPool()
    {
        // We need to tell the API the number of max. requested descriptors per type
        VkDescriptorPoolSize typeCounts[4];
        // This example only uses one descriptor type (uniform buffer) and only requests one descriptor of this type
        typeCounts[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        typeCounts[0].descriptorCount = 1;
        // For additional types you need to add new entries in the type count list
        // E.g. for two combined image samplers :
        typeCounts[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        typeCounts[1].descriptorCount = 1;
        typeCounts[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        typeCounts[2].descriptorCount = 1;
        typeCounts[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        typeCounts[3].descriptorCount = 1;

        // Create the global descriptor pool
        // All descriptors used in this example are allocated from this pool
        VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
        descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolInfo.pNext = nullptr;
        descriptorPoolInfo.poolSizeCount = 4;
        descriptorPoolInfo.pPoolSizes = typeCounts;
        // Set the max. number of descriptor sets that can be requested from this pool (requesting beyond this limit will result in an error)
        descriptorPoolInfo.maxSets = 1;

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout()
    {
        // Setup layout of descriptors used in this example
        // Basically connects the different shader stages to descriptors for binding uniform buffers, image samplers, etc.
        // So every shader binding should map to one descriptor set layout binding

        // Binding 0: Uniform buffer (Vertex shader)
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr;

        // Binding 1: light map samplers (Fragment shader)
        VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
        samplerLayoutBinding.binding = 1;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        samplerLayoutBinding.pImmutableSamplers = nullptr;

        // Binding 2: light map samplers (Fragment shader)
        VkDescriptorSetLayoutBinding samplerLayoutBinding2 = {};
        samplerLayoutBinding2.binding = 2;
        samplerLayoutBinding2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding2.descriptorCount = 1;
        samplerLayoutBinding2.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        samplerLayoutBinding2.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding uboFSLayoutBinding = {};
        uboFSLayoutBinding.binding = 3;
        uboFSLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboFSLayoutBinding.descriptorCount = 1;
        uboFSLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        uboFSLayoutBinding.pImmutableSamplers = nullptr;

        std::array<VkDescriptorSetLayoutBinding, 4> bindings = {uboLayoutBinding, samplerLayoutBinding, samplerLayoutBinding2, uboFSLayoutBinding};
        VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
        descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorLayout.pNext = nullptr;
        descriptorLayout.bindingCount = 4;
        descriptorLayout.pBindings = bindings.data();

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

        // Create the pipeline layout that is used to generate the rendering pipelines that are based on this descriptor set layout
        // In a more complex scenario you would have different pipeline layouts for different descriptor set layouts that could be reused
        VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {};
        pPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pPipelineLayoutCreateInfo.pNext = nullptr;
        pPipelineLayoutCreateInfo.setLayoutCount = 1;
        pPipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
    }

    void setupDescriptorSet()
    {
        // Allocate a new descriptor set from the global descriptor pool
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

        // Update the descriptor set determining the shader binding points
        // For every binding point used in a shader there needs to be one
        // descriptor set matching that binding point

        std::array<VkWriteDescriptorSet, 4> descriptorWrites{};

        // Binding 0 : Uniform buffer
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSet;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].pBufferInfo = &uniformBufferVS.descriptor;
        descriptorWrites[0].dstBinding = 0;

        // Binding 1: diffuse
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &lightMap.diffuseMap.descriptor;

        // Binding 2: specular
        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = descriptorSet;
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &lightMap.specularMap.descriptor;

        // Binding 3 : Uniform buffer
        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = descriptorSet;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[3].pBufferInfo = &uniformBufferFS.descriptor;
        descriptorWrites[3].dstBinding = 3;

        vkUpdateDescriptorSets(device, 4, descriptorWrites.data(), 0, nullptr);
    }

    void preparePipelines()
    {
        // Create the graphics pipeline used in this example
        // Vulkan uses the concept of rendering pipelines to encapsulate fixed states, replacing OpenGL's complex state machine
        // A pipeline is then stored and hashed on the GPU making pipeline changes very fast
        // Note: There are still a few dynamic states that are not directly part of the pipeline (but the info that they are used is)

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        // The layout used for this pipeline (can be shared among multiple pipelines using the same layout)
        pipelineCreateInfo.layout = pipelineLayout;
        // Renderpass this pipeline is attached to
        pipelineCreateInfo.renderPass = renderPass;

        // Construct the different states making up the pipeline

        // Input assembly state describes how primitives are assembled
        // This pipeline will assemble vertex data as a triangle lists (though we only use one triangle)
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
        inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        // Rasterization state
        VkPipelineRasterizationStateCreateInfo rasterizationState = {};
        rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizationState.cullMode = VK_CULL_MODE_NONE;
        rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizationState.depthClampEnable = VK_FALSE;
        rasterizationState.rasterizerDiscardEnable = VK_FALSE;
        rasterizationState.depthBiasEnable = VK_FALSE;
        rasterizationState.lineWidth = 1.0f;

        // Color blend state describes how blend factors are calculated (if used)
        // We need one blend attachment state per color attachment (even if blending is not used)
        VkPipelineColorBlendAttachmentState blendAttachmentState[1] = {};
        blendAttachmentState[0].colorWriteMask = 0xf;
        blendAttachmentState[0].blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo colorBlendState = {};
        colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendState.attachmentCount = 1;
        colorBlendState.pAttachments = blendAttachmentState;

        // Viewport state sets the number of viewports and scissor used in this pipeline
        // Note: This is actually overridden by the dynamic states (see below)
        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // Enable dynamic states
        // Most states are baked into the pipeline, but there are still a few dynamic states that can be changed within a command buffer
        // To be able to change these we need do specify which dynamic states will be changed using this pipeline. Their actual states are set later on in the command buffer.
        // For this example we will set the viewport and scissor using dynamic states
        std::vector<VkDynamicState> dynamicStateEnables;
        dynamicStateEnables.push_back(VK_DYNAMIC_STATE_VIEWPORT);
        dynamicStateEnables.push_back(VK_DYNAMIC_STATE_SCISSOR);
        VkPipelineDynamicStateCreateInfo dynamicState = {};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.pDynamicStates = dynamicStateEnables.data();
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

        // Depth and stencil state containing depth and stencil compare and test operations
        // We only use depth tests and want depth tests and writes to be enabled and compare with less or equal
        VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
        depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilState.depthTestEnable = VK_TRUE;
        depthStencilState.depthWriteEnable = VK_TRUE;
        depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencilState.depthBoundsTestEnable = VK_FALSE;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
        depthStencilState.stencilTestEnable = VK_FALSE;
        depthStencilState.front = depthStencilState.back;

        // Multi sampling state
        // This example does not make use of multi sampling (for anti-aliasing), the state must still be set and passed to the pipeline
        VkPipelineMultisampleStateCreateInfo multisampleState = {};
        multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampleState.pSampleMask = nullptr;

        // Vertex input descriptions
        // Specifies the vertex input parameters for a pipeline

        // Vertex input binding
        // This example uses a single vertex input binding at binding point 0 (see vkCmdBindVertexBuffers)
        VkVertexInputBindingDescription vertexInputBinding = {};
        vertexInputBinding.binding = 0;
        vertexInputBinding.stride = sizeof(Vertex);
        vertexInputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        // Input attribute bindings describe shader attribute locations and memory layouts
        std::array<VkVertexInputAttributeDescription, 3> vertexInputAttributs;
        // These match the following shader layout (see triangle.vert):
        //	layout (location = 0) in vec3 inPos;
        //	layout (location = 1) in vec3 inColor;
        // Attribute location 0: Position
        vertexInputAttributs[0].binding = 0;
        vertexInputAttributs[0].location = 0;
        // Position attribute is three 32 bit signed (SFLOAT) floats (R32 G32 B32)
        vertexInputAttributs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertexInputAttributs[0].offset = offsetof(Vertex, position);
        // Attribute location 1: Color
        vertexInputAttributs[1].binding = 0;
        vertexInputAttributs[1].location = 1;
        // Color attribute is three 32 bit signed (SFLOAT) floats (R32 G32 B32)
        vertexInputAttributs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertexInputAttributs[1].offset = offsetof(Vertex, normal);

        vertexInputAttributs[2].binding = 0;
        vertexInputAttributs[2].location = 2;
        vertexInputAttributs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertexInputAttributs[2].offset = offsetof(Vertex, texCoords);

        // Vertex input state used for pipeline creation
        VkPipelineVertexInputStateCreateInfo vertexInputState = {};
        vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputState.vertexBindingDescriptionCount = 1;
        vertexInputState.pVertexBindingDescriptions = &vertexInputBinding;
        vertexInputState.vertexAttributeDescriptionCount = 3;
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributs.data();

        // Shaders
        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

        // Vertex shader
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        // Set pipeline stage for this shader
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        // Load binary SPIR-V shader
        shaderStages[0].module = tools::loadShader((getShadersPath() + "triangle/cube.vert.spv").c_str(), device);
        // Main entry point for the shader
        shaderStages[0].pName = "main";
        assert(shaderStages[0].module != VK_NULL_HANDLE);

        // Fragment shader
        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        // Set pipeline stage for this shader
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        // Load binary SPIR-V shader
        shaderStages[1].module = tools::loadShader((getShadersPath() + "triangle/cube.frag.spv").c_str(), device);
        // Main entry point for the shader
        shaderStages[1].pName = "main";
        assert(shaderStages[1].module != VK_NULL_HANDLE);

        // Set pipeline shader stage info
        pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCreateInfo.pStages = shaderStages.data();

        // Assign the pipeline states to the pipeline creation info structure
        pipelineCreateInfo.pVertexInputState = &vertexInputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;

        // Create rendering pipeline using the specified states
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));

        // Shader modules are no longer needed once the graphics pipeline has been created
        vkDestroyShaderModule(device, shaderStages[0].module, nullptr);
        vkDestroyShaderModule(device, shaderStages[1].module, nullptr);
    }

    void prepareUniformBuffers()
    {
        // Prepare and initialize a uniform buffer block containing shader uniforms
        // Single uniforms like in OpenGL are no longer present in Vulkan. All Shader uniforms are passed via uniform buffer blocks
        VkMemoryRequirements memReqs;

        // Vertex shader uniform buffer block
        VkBufferCreateInfo bufferInfo = {};
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = nullptr;
        allocInfo.allocationSize = 0;
        allocInfo.memoryTypeIndex = 0;

        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(uboVS);
        // This buffer will be used as a uniform buffer
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        // Create a new buffer
        VK_CHECK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &uniformBufferVS.buffer));
        // Get memory requirements including size, alignment and memory type
        vkGetBufferMemoryRequirements(device, uniformBufferVS.buffer, &memReqs);
        allocInfo.allocationSize = memReqs.size;
        // Get the memory type index that supports host visible memory access
        // Most implementations offer multiple memory types and selecting the correct one to allocate memory from is crucial
        // We also want the buffer to be host coherent so we don't have to flush (or sync after every update.
        // Note: This may affect performance so you might not want to do this in a real world application that updates buffers on a regular base
        allocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        // Allocate memory for the uniform buffer
        VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &(uniformBufferVS.memory)));
        // Bind memory to buffer
        VK_CHECK_RESULT(vkBindBufferMemory(device, uniformBufferVS.buffer, uniformBufferVS.memory, 0));

        // Store information in the uniform's descriptor that is used by the descriptor set
        uniformBufferVS.descriptor.buffer = uniformBufferVS.buffer;
        uniformBufferVS.descriptor.offset = 0;
        uniformBufferVS.descriptor.range = sizeof(uboVS);

        bufferInfo.size = sizeof(uboFS);
        VK_CHECK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &uniformBufferFS.buffer));
        vkGetBufferMemoryRequirements(device, uniformBufferFS.buffer, &memReqs);
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &(uniformBufferFS.memory)));
        VK_CHECK_RESULT(vkBindBufferMemory(device, uniformBufferFS.buffer, uniformBufferFS.memory, 0));

        uniformBufferFS.descriptor.buffer = uniformBufferFS.buffer;
        uniformBufferFS.descriptor.offset = 0;
        uniformBufferFS.descriptor.range = sizeof(uboFS);

        updateUniformBuffers();
    }

    void updateUniformBuffers()
    {
        // Pass matrices to the shaders
        uboVS.projectionMatrix = camera.matrices.perspective;
        uboVS.viewMatrix = camera.matrices.view;
        uboVS.modelMatrix = glm::mat4(1.0f);

        // Map uniform buffer and update it
        uint8_t *pData;
        VK_CHECK_RESULT(vkMapMemory(device, uniformBufferVS.memory, 0, sizeof(uboVS), 0, (void **)&pData));
        memcpy(pData, &uboVS, sizeof(uboVS));
        // Unmap after data has been copied
        // Note: Since we requested a host coherent memory type for the uniform buffer, the write is instantly visible to the GPU
        vkUnmapMemory(device, uniformBufferVS.memory);

        lightPos.x = 1.0f + sin(glfwGetTime()) * 2.0f;
        lightPos.y = sin(glfwGetTime() / 2.0f) * 1.0f;

        uboFS.position = lightPos;
        uboFS.viewPos = camera.position;
        uboFS.ambient = glm::vec3(.2f);
        uboFS.diffuse = glm::vec3(.5f);
        uboFS.specular = glm::vec3(1.0f);

        VK_CHECK_RESULT(vkMapMemory(device, uniformBufferFS.memory, 0, sizeof(uboFS), 0, (void**)&pData));
        memcpy(pData, &uboFS, sizeof(uboFS));
        vkUnmapMemory(device, uniformBufferFS.memory);
    }

    // BUild separate command buffers for every framebuffer image
    // Unlike in OpenGL, all rendering commands are recorded once into command buffers that are then resubmmitted to the queue
    // This allows to generate work upfront and from multiple threads, one of the biggest advantages of Vulkan
    void buildCommandBuffers()
    {
        VkCommandBufferBeginInfo cmdBufInfo = {};
        cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufInfo.pNext = nullptr;

        // Set clear values for all framebuffer attachments with loadOp set to clear
        // We use two attachments (color and depth) that are cleared at the start of the subpass and as such we need to set clear values for both
        VkClearValue clearValues[2];
        clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 1.0f } };
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.pNext = nullptr;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
        {
            // Set target frame buffer
            renderPassBeginInfo.framebuffer = frameBuffers[i];

            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            // Start the first sub pass specified in our default render pass setup by the base class
            // This will clear the color and depth attachment
            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            // Update dynamic viewport state
            VkViewport viewport = {};
            viewport.height = (float)height;
            viewport.width = (float)width;
            viewport.minDepth = (float) 0.0f;
            viewport.maxDepth = (float) 1.0f;
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

            // Update dynamic scissor state
            VkRect2D scissor = {};
            scissor.extent.width = width;
            scissor.extent.height = height;
            scissor.offset.x = 0;
            scissor.offset.y = 0;
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            // Bind descriptor sets describing shader binding points
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

            // Bind the rendering pipeline
            // The pipeline (state object) contains all states of the rendering pipeline,
            // binding it will set all the states specified at pipeline creation time
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            // Bind triangle vertex buffer (contains position and colors)
            VkDeviceSize offsets[1] = { 0 };
            vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &vertices.buffer, offsets);


            // Draw indexed triangle
            // vkCmdDrawIndexed(drawCmdBuffers[i], indices.count, 1, 0, 0, 1);
            vkCmdDraw(drawCmdBuffers[i], vertices.count, 1, 0, 0);

            // Draw UIOverlay
            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);
            // Ending the render pass will add an implicit barrier transitioning the frame buffer color attachment to
            // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR for presenting it to the windowing system

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }

    }

    void prepare()
    {
        VulkanExampleBase::prepare();
        loadAssets();
        prepareVertices();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        buildCommandBuffers();
        prepared = true;
    }

    void draw()
    {
        VulkanExampleBase::prepareFrame();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
        VulkanExampleBase::presentFrame();
    }

    virtual void render()
    {
        if (!prepared)
            return;
        draw();
    }

    // This function is called by the base example class each time the view is changed by user input
    virtual void viewChanged()
    {
        updateUniformBuffers();
    }
};

VULKAN_EXAMPLE_MAIN()