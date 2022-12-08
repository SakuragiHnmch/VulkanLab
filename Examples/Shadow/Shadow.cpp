//
// Created by Junkang on 2022/11/20.
//

#include <vulkanexamplebase.h>
#include <VulkanObjModel.h>

#define ENABLE_VALIDATION true

class Shadow : public VulkanExampleBase {
public:
    // Keep depth range as small as possible
    // for better shadow map precision
    float zNear = 1.0f;
    float zFar = 96.0f;

    // Depth bias (and slope) are used to avoid shadowing artifacts
    // Constant depth bias factor (always applied)
    float depthBiasConstant = 1.25f;
    // Slope depth bias factor, applied depending on polygon's slope
    float depthBiasSlope = 1.75f;

    std::vector<ObjModel*> demoModels;

    VkPipelineLayout pipelineLayout;
    VkDescriptorSetLayout descriptorSetLayout;

    struct {
        VkDescriptorSet offscreen;
        VkDescriptorSet scene;
        VkDescriptorSet debug;
    } descriptorSets;

    glm::vec4 lightPos = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    Buffer offscreenUBO;
    Buffer sceneUBO;
    struct {
        glm::mat4 depthMVP;
    } uboOffscreenVS;
    struct {
        glm::mat4 projection;
        glm::mat4 model;
        glm::mat4 normal;
        glm::mat4 view;
        glm::mat4 depthMVP;
        glm::vec4 lightPos;
        glm::vec4 cameraPos;
    } uboVS;

    VkPipeline objPipeline;
    VkPipeline offscreenPipeline;
    VkPipeline debugPipeline;

    // Framebuffer for offscreen rendering
    struct FrameBufferAttachment {
        VkImage image;
        VkDeviceMemory mem;
        VkImageView view;
    };

    struct OffscreenPass {
        int width, height;
        VkFramebuffer framebuffer;
        FrameBufferAttachment depth;
        VkRenderPass renderPass;
        VkSampler depthSampler;
        VkDescriptorImageInfo descriptor;
    } offscreenPass;

    bool displayShadowMap = true;

    Shadow() : VulkanExampleBase(ENABLE_VALIDATION)
    {
        title = "Games 202 - Shadow";
        camera.type = Camera::CameraType::firstperson;
        //camera.flipY = true;
        camera.setPosition(glm::vec3(0.0f, -3.5f, -4.0f));
        camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
        camera.setRotationSpeed(0.5f);
        camera.setPerspective(75.0f, (float)width / (float)height, 1.0f, 256.0f);
    }

    ~Shadow()
    {
        vkDestroySampler(device, offscreenPass.depthSampler, nullptr);

        vkDestroyImageView(device, offscreenPass.depth.view, nullptr);
        vkDestroyImage(device, offscreenPass.depth.image, nullptr);
        vkFreeMemory(device, offscreenPass.depth.mem, nullptr);

        vkDestroyFramebuffer(device, offscreenPass.framebuffer, nullptr);
        vkDestroyRenderPass(device, offscreenPass.renderPass, nullptr);


        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class
        vkDestroyPipeline(device, objPipeline, nullptr);
        vkDestroyPipeline(device, offscreenPipeline, nullptr);
        vkDestroyPipeline(device, debugPipeline, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        for (auto demoModel : demoModels) {
            delete demoModel;
        }

        sceneUBO.destroy();
        offscreenUBO.destroy();
    }

    // Set up a separate render pass for the offscreen frame buffer
    // This is necessary as the offscreen frame buffer attachments use formats different to those from the example render pass
    void prepareOffscreenRenderpass()
    {
        VkAttachmentDescription attachmentDescription{};
        attachmentDescription.format = VK_FORMAT_D16_UNORM; //16 bits of depth is enough for such a small scene
        attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Clear depth at the begining of the render pass
        attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // We will read from depth, so it is important to store the depth attachment results
        attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;					// We don't care about initial layout of the attachment
        attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;// Attachment will be transitioned to shader read at render pass end

        VkAttachmentReference depthReference{};
        depthReference.attachment = 0;
        depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // Attachment will be used as depth/stencil during render pass

        VkSubpassDescription subpassDescription{};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = 0;                  // No colorAttachment
        subpassDescription.pDepthStencilAttachment = &depthReference; // Reference to our depth attachment

        // Use subPass dependencies for layout transitions
        std::array<VkSubpassDependency, 2> dependencies;  // VkSubpassDependency的作用是对不同的subpass进行同步，确保前一个subpass内的attachment能正确地被后一个subpass使用

        // 在这个subPass执行前，color(depth)Attachment的状态
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;   // 只有一个VkSubpassDescription，即第0个
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        // VK_SUBPASS_EXTERNAL means anything outside of a given render pass scope.
        // When used for srcSubpass it specifies anything that happened before the render pass.
        // And when used for dstSubpass it specifies anything that happens after the render pass.

        // 在这个subPass执行之后，color(depth)Attachment的状态，
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo renderPassCreateInfo = initializers::renderPassCreateInfo();
        renderPassCreateInfo.attachmentCount = 1;
        renderPassCreateInfo.pAttachments = &attachmentDescription;
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = &subpassDescription;
        renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassCreateInfo.pDependencies = dependencies.data();

        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &offscreenPass.renderPass));
    }

    // setup the offscreen framebuffer for rendering the scene from light`s point of view
    // The depth attachment of this framebuffer will then be used to sample from in the fragment shader of the shadowing pass
    void prepareOffscreenFrameBuffer()
    {
        offscreenPass.width = 2048;
        offscreenPass.height = 2048;

        VkImageCreateInfo imageCreateInfo = initializers::imageCreateInfo();
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.extent.width = offscreenPass.width;
        imageCreateInfo.extent.height = offscreenPass.height;
        imageCreateInfo.extent.depth = 1;
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.format = VK_FORMAT_D16_UNORM;
        imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &offscreenPass.depth.image));

        VkMemoryAllocateInfo memAlloc = initializers::memoryAllocateInfo();
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, offscreenPass.depth.image, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &offscreenPass.depth.mem));
        VK_CHECK_RESULT(vkBindImageMemory(device, offscreenPass.depth.image, offscreenPass.depth.mem, 0));

        VkImageViewCreateInfo depthStencilView = initializers::imageViewCreateInfo();
        depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthStencilView.format = VK_FORMAT_D16_UNORM;
        depthStencilView.subresourceRange = {};
        depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthStencilView.subresourceRange.baseMipLevel = 0;
        depthStencilView.subresourceRange.levelCount = 1;
        depthStencilView.subresourceRange.baseArrayLayer = 0;
        depthStencilView.subresourceRange.layerCount = 1;
        depthStencilView.image = offscreenPass.depth.image;
        VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &offscreenPass.depth.view));

        // Create sampler to sample from to depth attachment
        // Used to sample in the fragment shader for shadowed rendering
        VkFilter shadowmap_filter = tools::formatIsFilterable(physicalDevice, VK_FORMAT_D16_UNORM, VK_IMAGE_TILING_OPTIMAL) ?
                                    VK_FILTER_LINEAR :
                                    VK_FILTER_NEAREST;
        VkSamplerCreateInfo sampler = initializers::samplerCreateInfo();
        sampler.magFilter = shadowmap_filter;
        sampler.minFilter = shadowmap_filter;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeV = sampler.addressModeU;
        sampler.addressModeW = sampler.addressModeU;
        sampler.mipLodBias = 0.0f;
        sampler.maxAnisotropy = 1.0f;
        sampler.minLod = 0.0f;
        sampler.maxLod = 1.0f;
        sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &offscreenPass.depthSampler));

        prepareOffscreenRenderpass();

        // Create frame buffer
        VkFramebufferCreateInfo fbufCreateInfo = initializers::framebufferCreateInfo();
        fbufCreateInfo.renderPass = offscreenPass.renderPass;
        fbufCreateInfo.attachmentCount = 1;
        fbufCreateInfo.pAttachments = &offscreenPass.depth.view;
        fbufCreateInfo.width = offscreenPass.width;
        fbufCreateInfo.height = offscreenPass.height;
        fbufCreateInfo.layers = 1;

        VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &offscreenPass.framebuffer));
    }

    void loadAssets()
    {
        std::vector<std::string> modelFiles = { "floor", "Marry" };
        for (auto i = 0; i < modelFiles.size(); i++) {
            auto* model = new ObjModel(vulkanDevice);
            model->LoadModelFromFile(getAssetPath() + "models/Shadow/" + modelFiles[i] + "/" + modelFiles[i] + ".obj", vulkanDevice, queue);
            demoModels.push_back(model);
        }
    }

    void buildCommandBuffers()
    {
        VkCommandBufferBeginInfo cmdBufInfo = initializers::commandBufferBeginInfo();

        VkClearValue clearValues[2];
        VkViewport viewport;
        VkRect2D scissor;

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
        {
            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            // First render pass: Generate shadow map by rendering the scene from light's POV
            {
                clearValues[0].depthStencil = { 1.0f, 0 };

                VkRenderPassBeginInfo renderPassBeginInfo = initializers::renderPassBeginInfo();
                renderPassBeginInfo.renderPass = offscreenPass.renderPass;
                renderPassBeginInfo.framebuffer = offscreenPass.framebuffer;
                renderPassBeginInfo.renderArea.extent.width = offscreenPass.width;
                renderPassBeginInfo.renderArea.extent.height = offscreenPass.height;
                renderPassBeginInfo.clearValueCount = 1;
                renderPassBeginInfo.pClearValues = clearValues;

                vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                viewport = initializers::viewport((float)offscreenPass.width, (float)offscreenPass.height, 0.0f, 1.0f);
                vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

                scissor = initializers::rect2D(offscreenPass.width, offscreenPass.height, 0, 0);
                vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

                // Set depth bias (aka "Polygon offset")
                // Required to avoid shadow mapping artifacts
                vkCmdSetDepthBias(
                        drawCmdBuffers[i],
                        depthBiasConstant,
                        0.0f,
                        depthBiasSlope);

                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, offscreenPipeline);
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.offscreen, 0, nullptr);

                for (auto model : demoModels) {
                    model->Draw(drawCmdBuffers[i], pipelineLayout);
                }

                vkCmdEndRenderPass(drawCmdBuffers[i]);
            }

            // Note: Explicit synchronization is not required between the render pass, as this is done implicit via sub pass dependencies
            // Second pass: Scene rendering with applied shadow map
            {
                clearValues[0].color = defaultClearColor;
                clearValues[1].depthStencil = { 1.0f, 0 };

                VkRenderPassBeginInfo renderPassBeginInfo = initializers::renderPassBeginInfo();
                renderPassBeginInfo.renderPass = renderPass;
                renderPassBeginInfo.framebuffer = frameBuffers[i];
                renderPassBeginInfo.renderArea.extent.width = width;
                renderPassBeginInfo.renderArea.extent.height = height;
                renderPassBeginInfo.clearValueCount = 2;
                renderPassBeginInfo.pClearValues = clearValues;

                vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                viewport = initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
                vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

                scissor = initializers::rect2D(width, height, 0, 0);
                vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

//                if (displayShadowMap) {
//                    vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.debug, 0,
//                                            nullptr);
//                    vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, debugPipeline);
//                    vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
//
//                }

                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, objPipeline);

                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                        &descriptorSets.scene, 0, NULL);

                for (auto model: demoModels) {
                    model->Draw(drawCmdBuffers[i], pipelineLayout);
                }

                drawUI(drawCmdBuffers[i]);

                vkCmdEndRenderPass(drawCmdBuffers[i]);

            }

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void setupDescriptorPool()
    {
        // for global uniform buffer
        std::vector<VkDescriptorPoolSize> poolSizes =
                {
                        initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3),
                        initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3)
                };

        VkDescriptorPoolCreateInfo descriptorPoolInfo = initializers::descriptorPoolCreateInfo(
                        poolSizes.size(),
                        poolSizes.data(),
                        3);
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout()
    {
        // Shared pipeline layout for all pipelines used in this sample
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
                {
                        // Binding 0 : Vertex shader uniform buffer
                        initializers::descriptorSetLayoutBinding(
                                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                0),
                        // Binding 1 : Fragment shader image sampler (shadow map)
                        initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
                };

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = initializers::descriptorSetLayoutCreateInfo(
                        setLayoutBindings.data(),
                        setLayoutBindings.size());

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout));

        std::vector<VkDescriptorSetLayout> layouts = { descriptorSetLayout, demoModels[0]->GetDescriptorSetLayout() };
        VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
                initializers::pipelineLayoutCreateInfo(
                        layouts.data(),
                        static_cast<uint32_t>(layouts.size()));

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
    }

    void setupDescriptorSet()
    {
        std::vector<VkWriteDescriptorSet> writeDescriptorSets;

        VkDescriptorSetAllocateInfo allocInfo =
                initializers::descriptorSetAllocateInfo(
                        descriptorPool,
                        &descriptorSetLayout,
                        1);

        // Image descriptor for the shadowMap attachment
        VkDescriptorImageInfo shadowMapDescriptor =
                initializers::descriptorImageInfo(
                        offscreenPass.depthSampler,
                        offscreenPass.depth.view,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                        );

        // DebugDisplay
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.debug));
        writeDescriptorSets = {
                initializers::writeDescriptorSet(descriptorSets.debug, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &sceneUBO.descriptor),
                initializers::writeDescriptorSet(descriptorSets.debug, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &shadowMapDescriptor)
        };
        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);

        // Offscreen shadow map generation
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.offscreen));
        writeDescriptorSets = {
                // Binding 0 : Vertex shader Uniform buffer
                initializers::writeDescriptorSet(
                        descriptorSets.offscreen,
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        0,
                        &offscreenUBO.descriptor
                        )
        };
        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);


        // Scene rendering with shadowmap applied
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.scene));
        writeDescriptorSets = {
                // Binding 0 : Vertex shader uniform buffer
                initializers::writeDescriptorSet(
                        descriptorSets.scene,
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        0,
                        &sceneUBO.descriptor),
                initializers::writeDescriptorSet(
                        descriptorSets.scene,
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        1,
                        &shadowMapDescriptor
                        )
        };
        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines()
    {
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
        VkPipelineRasterizationStateCreateInfo rasterizationState = initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE,0);
        VkPipelineColorBlendAttachmentState blendAttachmentState = initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
        VkPipelineColorBlendStateCreateInfo colorBlendState = initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
        VkPipelineDepthStencilStateCreateInfo depthStencilState = initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
        VkPipelineViewportStateCreateInfo viewportState = initializers::pipelineViewportStateCreateInfo(1, 1, 0);
        VkPipelineMultisampleStateCreateInfo multisampleState = initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
        std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState = initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables, 0);
        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = initializers::pipelineCreateInfo(pipelineLayout, renderPass, 0);
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();

        // debug quad display
        rasterizationState.cullMode = VK_CULL_MODE_NONE;
        shaderStages[0] = loadShader(getShadersPath() + "Shadow/quad.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = loadShader(getShadersPath() + "Shadow/quad.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VkPipelineVertexInputStateCreateInfo emptyInputState = initializers::pipelineVertexInputStateCreateInfo();
        pipelineCreateInfo.pVertexInputState = &emptyInputState;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &debugPipeline));

        // Scene rendering with shadow applied
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        pipelineCreateInfo.pVertexInputState = &vertexInputInfo;

        // Default mesh rendering pipeline
        shaderStages[0] = loadShader(getShadersPath() + "Shadow/phong.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = loadShader(getShadersPath() + "Shadow/phong.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &objPipeline));

        // Offscreen Pipeline(vertex shader only)
        shaderStages[0] = loadShader(getShadersPath() + "Shadow/offscreen.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        pipelineCreateInfo.stageCount = 1;
        colorBlendState.attachmentCount = 0;  // no colorAttachment used
        depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        rasterizationState.depthBiasEnable = VK_TRUE;
        // Add depth bias to dynamic state, so we can change it at runtime
        dynamicStateEnables.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
        dynamicState =
                initializers::pipelineDynamicStateCreateInfo(
                        dynamicStateEnables.data(),
                        dynamicStateEnables.size(),
                        0);
        pipelineCreateInfo.renderPass = offscreenPass.renderPass;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &offscreenPipeline));
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers()
    {
        // Offscreen vertex shader uniform buffer block
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &offscreenUBO,
                sizeof(uboOffscreenVS)));

        vulkanDevice->createBuffer(
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &sceneUBO,
                sizeof(uboVS));
        VK_CHECK_RESULT(offscreenUBO.map());
        VK_CHECK_RESULT(sceneUBO.map());
        updateUniformBuffers();
    }

    void updateUniformBuffers()
    {
        // Animate the light source
        lightPos.x = -cos(glm::radians(timer * 360.0f)) * 20.0f;
        lightPos.y = 25.0f + sin(glm::radians(timer * 360.0f)) * 10.0f;
        lightPos.z = -12.5f + sin(glm::radians(timer * 360.0f)) * 2.5f;

        // Matrix from light's point of view
        glm::mat4 depthProjectionMatrix = glm::perspective(glm::radians(45.0f), 1.0f, zNear, zFar);
        glm::mat4 depthViewMatrix = glm::lookAt(glm::vec3(lightPos.x, lightPos.y, lightPos.z), glm::vec3(0.0f), glm::vec3(0, 1, 0));
        glm::mat4 depthModelMatrix = glm::mat4(1.0f);
        uboOffscreenVS.depthMVP = depthProjectionMatrix * depthViewMatrix * depthModelMatrix;
        memcpy(offscreenUBO.mapped, &uboOffscreenVS, sizeof(uboOffscreenVS));

        uboVS.projection = camera.matrices.perspective;
        uboVS.view = camera.matrices.view;
        uboVS.model = glm::mat4(1.0f);
        uboVS.normal = glm::inverseTranspose(uboVS.model);
        uboVS.depthMVP = uboOffscreenVS.depthMVP;
        uboVS.lightPos = lightPos;
        uboVS.cameraPos = glm::vec4(camera.position, 1.0);
        memcpy(sceneUBO.mapped, &uboVS, sizeof(uboVS));
    }

    void draw()
    {
        VulkanExampleBase::prepareFrame();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
        VulkanExampleBase::presentFrame();
    }

    void prepare()
    {
        VulkanExampleBase::prepare();
        loadAssets();
        prepareOffscreenFrameBuffer();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render()
    {
        if (!prepared)
            return;
        draw();
    }

    virtual void viewChanged()
    {
        updateUniformBuffers();
    }

};

int main(const int argc, const char *argv[])
{

    for (size_t i = 0; i < argc; i++) {
        Shadow::args.push_back(argv[i]);
    };

    Shadow* app = new Shadow();
    app->setupWindow();
    app->initVulkan();
    app->prepare();
    app->renderLoop();
    delete(app);

    return 0;
}
