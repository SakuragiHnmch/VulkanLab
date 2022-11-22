//
// Created by Junkang on 2022/11/20.
//

#include <vulkanexamplebase.h>
#include <VulkanObjModel.h>

#define ENABLE_VALIDATION true

class Shadow : public VulkanExampleBase {
public:
    std::vector<ObjModel*> demoModels;

    VkPipelineLayout pipelineLayout;
    VkDescriptorSet descriptorSet;
    VkDescriptorSetLayout descriptorSetLayout;

    glm::vec4 lightPos = glm::vec4(250.0f, 250.0f, 250.0f, 0.0f);

    Buffer bufferVS;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
        glm::mat4 normal;
        glm::mat4 view;
        glm::vec4 lightPos;
        glm::vec4 cameraPos;
    } uboVS;

    VkPipeline objPipeline;

    Shadow() : VulkanExampleBase(ENABLE_VALIDATION)
    {
        title = "Games 202 - Shadow";
        camera.type = Camera::CameraType::lookat;
        //camera.flipY = true;
        camera.setPosition(glm::vec3(-20.0f, 180.0f, 250.f));
        camera.setRotation(glm::vec3(15.0f, 0.0f, 0.0f));
        camera.setRotationSpeed(0.5f);
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
    }

    ~Shadow()
    {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class
        vkDestroyPipeline(device, objPipeline, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        for (auto demoModel : demoModels) {
            delete demoModel;
        }

        bufferVS.destroy();
    }

    void loadAssets()
    {
        std::vector<std::string> modelFiles = { "Marry" };
        for (auto i = 0; i < modelFiles.size(); i++) {
            auto* model = new ObjModel();
            model->LoadModelFromFile(getAssetPath() + "models/Shadow/" + modelFiles[i] + "/" + modelFiles[i] + ".obj", vulkanDevice, queue);
            demoModels.push_back(model);
        }
    }

    void buildCommandBuffers()
    {
        VkCommandBufferBeginInfo cmdBufInfo = initializers::commandBufferBeginInfo();

        VkClearValue clearValues[2];
        clearValues[0].color = defaultClearColor;
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo renderPassBeginInfo = initializers::renderPassBeginInfo();
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
        {
            renderPassBeginInfo.framebuffer = frameBuffers[i];

            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport = initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

            VkRect2D scissor = initializers::rect2D(width, height, 0, 0);
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

            for (auto model : demoModels) {
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, objPipeline);
                model->Draw(drawCmdBuffers[i], pipelineLayout);
            }

            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void setupDescriptorPool()
    {
        // for global uniform buffer
        std::vector<VkDescriptorPoolSize> poolSizes =
                {
                        initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
                };

        VkDescriptorPoolCreateInfo descriptorPoolInfo = initializers::descriptorPoolCreateInfo(
                        poolSizes.size(),
                        poolSizes.data(),
                        2);
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout()
    {
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
                {
                        // Binding 0 : Vertex shader uniform buffer
                        initializers::descriptorSetLayoutBinding(
                                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                VK_SHADER_STAGE_VERTEX_BIT,
                                0)
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
        VkDescriptorSetAllocateInfo allocInfo =
                initializers::descriptorSetAllocateInfo(
                        descriptorPool,
                        &descriptorSetLayout,
                        1);

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

        std::vector<VkWriteDescriptorSet> writeDescriptorSets =
                {
                        // Binding 0 : Vertex shader uniform buffer
                        initializers::writeDescriptorSet(
                                descriptorSet,
                                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                0,
                                &bufferVS.descriptor)
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
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers()
    {
        vulkanDevice->createBuffer(
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &bufferVS,
                sizeof(uboVS));
        VK_CHECK_RESULT(bufferVS.map());
        updateUniformBuffers();
    }

    void updateUniformBuffers()
    {
        uboVS.projection = camera.matrices.perspective;
        uboVS.view = camera.matrices.view;
        uboVS.model = glm::mat4(52.0f);
        uboVS.normal = glm::inverseTranspose(uboVS.model);
        uboVS.lightPos = lightPos;
        uboVS.cameraPos = glm::vec4(camera.position, 1.0);
        memcpy(bufferVS.mapped, &uboVS, sizeof(uboVS));
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
