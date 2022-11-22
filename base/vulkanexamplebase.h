#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cassert>
#include <vector>
#include <array>
#include <unordered_map>
#include <numeric>
#include <ctime>
#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>
#include <string>
#include <sys/stat.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "vulkan/vulkan.h"

#include "keycodes.hpp"
#include "VulkanTools.h"
#include "VulkanDebug.h"
#include "VulkanUIOverlay.h"
#include "VulkanSwapChain.h"
#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "VulkanTexture.h"
#include "VulkanInitializers.hpp"
#include "camera.hpp"
#include "benchmark.hpp"

class CommandLineParser{
public:
    struct CommandLineOption {
        std::vector<std::string> commands;
        std::string value;
        bool hasValue = false;
        std::string help;
        bool set = false;
    };
    std::unordered_map<std::string, CommandLineOption> options;
    CommandLineParser();
    void add(std::string name, std::vector<std::string> commands, bool hasValue, std::string help);
    void printHelp();
    void parse(std::vector<const char*> arguments);
    bool isSet(std::string name);
    std::string getValueAsString(std::string name, std::string defaultValue);
    int32_t getValueAsInt(std::string name, int32_t defaultValue);
};

class VulkanExampleBase {
private:
    uint32_t destWidth;
    uint32_t destHeight;
    bool resizing = false;

    std::string getWindowTitle();
    void windowResize();
    void handleMouseMove(int32_t x, int32_t y);
    void nextFrame();
    void updateOverlay();
    void createPipelineCache();
    void createCommandPool();
    void createSynchronizationPrimitives();
    void initSwapchain();
    void setupSwapChain();
    void createCommandBuffers();

    static void sKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void sCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void sMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
protected:
    std::string getShadersPath() const;

    uint32_t frameCounter = 0;
    uint32_t lastFPS = 0;
    std::chrono::time_point<std::chrono::high_resolution_clock> lastTimestamp, tPrevEnd;
    VkInstance instance;
    std::vector<std::string> supportedInstanceExtensions;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties deviceProperties; // for checking device limits
    VkPhysicalDeviceFeatures deviceFeatures; // for checking if a feature is available
    VkPhysicalDeviceMemoryProperties deviceMemoryProperties; // Stores all available memory (type) properties for the physical device
    VkPhysicalDeviceFeatures enabledFeatures{}; // Set of physical device features to be enabled for this example (must be set in the derived constructor)
    // Set of device extensions to be enabled for this example (must be set in the derived constructor)
    std::vector<const char*> enabledDeviceExtensions;
    std::vector<const char*> enabledInstanceExtensions;
    void* deviceCreatepNextChain = nullptr; // Optional pNext structure for passing extension structures to device creation
    VkDevice device;
    VkQueue queue;
    VkFormat depthFormat;
    VkCommandPool cmdPool;
    VkPipelineStageFlags submitPipelineStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo;
    std::vector<VkCommandBuffer> drawCmdBuffers;
    VkRenderPass renderPass = VK_NULL_HANDLE; // Global render pass for frame buffer writes
    std::vector<VkFramebuffer>frameBuffers;
    uint32_t currentBuffer = 0;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkShaderModule> shaderModules; // stored for cleanup
    VkPipelineCache pipelineCache;
    VulkanSwapChain swapChain; // Wraps the swap chain to present images (framebuffers) to the windowing system
    struct {
        // Swap chain image presentation
        VkSemaphore presentComplete;
        // Command buffer submission and execution
        VkSemaphore renderComplete;
    } semaphores;
    std::vector<VkFence> waitFences;

public:
    bool prepared    = false;
    bool resized     = false;
    bool viewUpdated = false;
    uint32_t width   = 1280;
    uint32_t height  = 720;
    float frameTimer = 1.0f;
    float timer      = 0.0f;
    float timerSpeed = 0.25f;
    bool paused      = false;

    static std::vector<const char*> args;

    UIOverlay overlay;
    CommandLineParser commandLineParser;
    Benchmark benchmark;
    VulkanDevice *vulkanDevice;
    Camera camera;
    glm::vec2 mousePos;

    std::string title   = "Vulkan Example";
    std::string name    = "vulkanExample";
    uint32_t apiVersion = VK_API_VERSION_1_0;

    VkClearColorValue defaultClearColor = { { 0.025f, 0.025f, 0.025f, 1.0f } };

    struct Settings {
        bool validation = false;
        bool fullscreen = false;
        bool vsync      = false;
        bool overlay    = true;
    } settings;

    struct {
        VkImage image;
        VkDeviceMemory mem;
        VkImageView view;
    } depthStencil;

    struct {
        glm::vec2 axisLeft = glm::vec2(0.0f);
        glm::vec2 axisRight = glm::vec2(0.0f);
    } gamePadState;

    struct {
        bool left = false;
        bool right = false;
        bool middle = false;
    } mouseButtons;

    GLFWwindow* window;
    bool quit = false;

    VulkanExampleBase(bool enableValidation = false);
    virtual ~VulkanExampleBase();

    // Setup the vulkan instance, enable required extensions and connect to the physical device (GPU)
    bool initVulkan();
    GLFWwindow* setupWindow();
    void mouseDragged(float x, float y);
    void windowWillResize(float x, float y);
    void windowDidResize();

    void keyCallback(int key, int action);
    void cursorPosCallback(double xpos, double ypos);
    void mouseButtonCallback(int button, int action);

    virtual VkResult createInstance(bool enableValidation);
    /**(Pure virtual) Render function to be implemented by the sample application */
    virtual void render() = 0;
    virtual void viewChanged();
    virtual void keyPressed(uint32_t);
    virtual void mouseMoved(double x, double y, bool &handled);
    virtual void windowResized();
    /**Called when resources have been recreated that require a rebuild of the command buffers (e.g. frame buffer), to be implemented by the sample application */
    virtual void buildCommandBuffers();
    virtual void setupDepthStencil();
    virtual void setupFrameBuffer();
    virtual void setupRenderPass();
    virtual void getEnabledFeatures();

    /**Prepares all Vulkan resources and functions required to run the sample */
    virtual void prepare();

    /** Loads a SPIR-V shader file for the given shader stage */
    VkPipelineShaderStageCreateInfo loadShader(std::string fileName, VkShaderStageFlagBits stage);

    /**Entry point for the main render loop */
    void renderLoop();

    /**Adds the drawing commands for the ImGui overlay to the given command buffer */
    void drawUI(const VkCommandBuffer commandBuffer);

    /** Prepare the next frame for workload submission by acquiring the next swap chain image */
    void prepareFrame();
    void presentFrame();
    /**Default image acquire + submission and command buffer submission function */
    virtual void renderFrame();

    /**Called when the UI overlay is updating, can be used to add custom elements to the overlay */
    virtual void OnUpdateUIOverlay(UIOverlay* overlay);
};
