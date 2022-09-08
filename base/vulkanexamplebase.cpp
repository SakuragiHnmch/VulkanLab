#include "vulkanexamplebase.h"

std::vector<const char*> VulkanExampleBase::args;

VulkanExampleBase::VulkanExampleBase(bool enableValidation)
{
    // check for a valid asset path
    struct stat info;
    if (stat(getAssetPath().c_str(), &info) != 0)
    {
        std::cerr << "Error: Could not find asset path in " << getAssetPath() << "\n";
        exit(-1);
    }

    settings.validation = enableValidation;

    //commandline args
    commandLineParser.parse(args);
    if (commandLineParser.isSet("help")) {
        commandLineParser.printHelp();
        std::cin.get();
        exit(0);
    }
    if (commandLineParser.isSet("validation")) {
        settings.validation = true;
    }
    if (commandLineParser.isSet("vsync")) {
        settings.vsync = true;
    }
    if (commandLineParser.isSet("height")) {
        height = commandLineParser.getValueAsInt("height", width);
    }
    if (commandLineParser.isSet("width")) {
        width = commandLineParser.getValueAsInt("width", width);
    }
    if (commandLineParser.isSet("fullscreen")) {
        settings.fullscreen = true;
    }
    if (commandLineParser.isSet("benchmark")) {
        benchmark.active = true;
        tools::errorModeSilent = true;
    }
    if (commandLineParser.isSet("benchmarkwarmup")) {
        benchmark.warmup = commandLineParser.getValueAsInt("benchmarkwarmup", benchmark.warmup);
    }
    if (commandLineParser.isSet("benchmarkruntime")) {
        benchmark.duration = commandLineParser.getValueAsInt("benchmarkruntime", benchmark.duration);
    }
    if (commandLineParser.isSet("benchmarkresultfile")) {
        benchmark.filename = commandLineParser.getValueAsString("benchmarkresultfile", benchmark.filename);
    }
    if (commandLineParser.isSet("benchmarkresultframes")) {
        benchmark.outputFrameTimes = true;
    }
    if (commandLineParser.isSet("benchmarkframes")) {
        benchmark.outputFrames = commandLineParser.getValueAsInt("benchmarkframes", benchmark.outputFrames);
    }
}

VulkanExampleBase::~VulkanExampleBase()
{
    swapChain.cleanup();
    if (descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }

    vkFreeCommandBuffers(device, cmdPool, static_cast<uint32_t>(drawCmdBuffers.size()), drawCmdBuffers.data());

    if (renderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device, renderPass, nullptr);
    }
    for (uint32_t i = 0; i < frameBuffers.size(); i++)
    {
        vkDestroyFramebuffer(device, frameBuffers[i], nullptr);
    }

    for (auto& shaderModule : shaderModules)
    {
        vkDestroyShaderModule(device, shaderModule, nullptr);
    }
    vkDestroyImageView(device, depthStencil.view, nullptr);
    vkDestroyImage(device, depthStencil.image, nullptr);
    vkFreeMemory(device, depthStencil.mem, nullptr);

    vkDestroyPipelineCache(device, pipelineCache, nullptr);

    vkDestroyCommandPool(device, cmdPool, nullptr);

    vkDestroySemaphore(device, semaphores.presentComplete, nullptr);
    vkDestroySemaphore(device, semaphores.renderComplete, nullptr);
    for (auto& fence : waitFences) {
        vkDestroyFence(device, fence, nullptr);
    }

    if (settings.overlay) {
        overlay.freeResources();
    }

    delete vulkanDevice;

    if (settings.validation)
    {
        debug::freeDebugCallback(instance);
    }

    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();
}

std::string VulkanExampleBase::getWindowTitle()
{
    std::string device(deviceProperties.deviceName);
    std::string windowTitle;
    windowTitle = title + " - " + device;
    if (!settings.overlay) {
        windowTitle += " - " + std::to_string(frameCounter) + " fps";
    }
    return windowTitle;
}

bool VulkanExampleBase::initVulkan()
{
    VkResult err;
    err = createInstance(settings.validation);
    if (err) {
        tools::exitFatal("Could not create Vulkan instance : \n" + tools::errorString(err), err);
        return false;
    }

    if (settings.validation)
    {
        VkDebugReportFlagsEXT debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
        debug::setupDebugging(instance, debugReportFlags, VK_NULL_HANDLE);
    }

    // Physical device
    uint32_t gpuCount = 0;
    VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr));
    if (gpuCount == 0) {
        tools::exitFatal("No device with Vulkan support found", -1);
        return false;
    }
    std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
    err = vkEnumeratePhysicalDevices(instance, &gpuCount, physicalDevices.data());
    if (err) {
        tools::exitFatal("Could not enumerate physical devices : \n" + tools::errorString(err), err);
        return false;
    }

    // GPU selection

    // Select physical device to be used for the Vulkan example
    // Defaults to the first device unless specified by command line
    uint32_t selectedDevice = 0;

    // GPU selection via command line argument
    if (commandLineParser.isSet("gpuselection")) {
        uint32_t index = commandLineParser.getValueAsInt("gpuselection", 0);
        if (index > gpuCount - 1) {
            std::cerr << "Selected device index " << index << " is out of range, reverting to device 0 (use -listgpus to show available Vulkan devices)" << "\n";
        } else {
            selectedDevice = index;
        }
    }
    if (commandLineParser.isSet("gpulist")) {
        std::cout << "Available Vulkan devices" << "\n";
        for (uint32_t i = 0; i < gpuCount; i++) {
            VkPhysicalDeviceProperties deviceProperties;
            vkGetPhysicalDeviceProperties(physicalDevices[i], &deviceProperties);
            std::cout << "Device [" << i << "] : " << deviceProperties.deviceName << std::endl;
            std::cout << " Type: " << tools::physicalDeviceTypeString(deviceProperties.deviceType) << "\n";
            std::cout << " API: " << (deviceProperties.apiVersion >> 22) << "." << ((deviceProperties.apiVersion >> 12) & 0x3ff) << "." << (deviceProperties.apiVersion & 0xfff) << "\n";
        }
    }
    physicalDevice = physicalDevices[selectedDevice];

    // Store properties (including limits), features and memory properties of the physical device (so that examples can check against them)
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemoryProperties);

    // Derived examples can override this to set actual features (based on above readings) to enable for logical device creation
    getEnabledFeatures();

    // Vulkan device creation
    // This is handled by a separate class that gets a logical device representation
    // and encapsulates functions related to a device
    vulkanDevice = new VulkanDevice(physicalDevice);
    VkResult res = vulkanDevice->createLogicalDevice(enabledFeatures, enabledDeviceExtensions, deviceCreatepNextChain);
    if (res != VK_SUCCESS) {
        tools::exitFatal("Could not create Vulkan device: \n" + tools::errorString(res), res);
        return false;
    }
    device = vulkanDevice->logicalDevice;
    vkGetDeviceQueue(device, vulkanDevice->queueFamilyIndices.graphics, 0, &queue);

    VkBool32 validDepthFormat = tools::getSupportedDepthFormat(physicalDevice, &depthFormat);
    assert(validDepthFormat);
    swapChain.connect(instance, physicalDevice, device);

    // Create synchronization objects
    VkSemaphoreCreateInfo semaphoreCreateInfo = initializers::semaphoreCreateInfo();
    // Create a semaphore used to synchronize image presentation
    // Ensures that the image is displayed before we start submitting new commands to the queue
    VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphores.presentComplete));
    // Create a semaphore used to synchronize command submission
    // Ensures that the image is not presented until all commands have been submitted and executed
    VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphores.renderComplete));

    // Set up submit info structure
    // Semaphores will stay the same during application lifetime
    // Command buffer submission info is set by each example
    submitInfo = initializers::submitInfo();
    submitInfo.pWaitDstStageMask = &submitPipelineStages;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &semaphores.presentComplete;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &semaphores.renderComplete;

    return true;
}

GLFWwindow* VulkanExampleBase::setupWindow()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window = glfwCreateWindow(width, height, getWindowTitle().c_str(), nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
//    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetKeyCallback(window, sKeyCallback);
    glfwSetCursorPosCallback(window, sCursorPosCallback);
    glfwSetMouseButtonCallback(window, sMouseButtonCallback);

    glfwGetWindowUserPointer(window);
}

void VulkanExampleBase::prepare()
{
    if (vulkanDevice->enableDebugMarkers)
    {
        debugmarker::setup(device);
    }
    initSwapchain();
    createCommandPool();
    setupSwapChain();
    createCommandBuffers();
    createSynchronizationPrimitives();
    setupDepthStencil();
    setupRenderPass();
    createPipelineCache();
    setupFrameBuffer();
    settings.overlay = settings.overlay && (!benchmark.active);
    if (settings.overlay) {
        overlay.device = vulkanDevice;
        overlay.queue = queue;
        overlay.shaders = {
                loadShader(getShadersPath() + "base/uioverlay.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
                loadShader(getShadersPath() + "base/uioverlay.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
        };
        overlay.prepareResources();
        overlay.preparePipeline(pipelineCache, renderPass, swapChain.colorFormat, depthFormat);
    }
}

void VulkanExampleBase::renderLoop()
{
    if (benchmark.active) {
        benchmark.run([=] { render(); }, vulkanDevice->properties);
        vkDeviceWaitIdle(device);
        if (benchmark.filename != "") {
            benchmark.saveResults();
        }
        return;
    }

    destWidth = width;
    destHeight = height;
    lastTimestamp = std::chrono::high_resolution_clock::now();
    tPrevEnd = lastTimestamp;

    while (!(glfwWindowShouldClose(window))) {
        if (prepared) {
//            processInput()
            glfwPollEvents();
            nextFrame();
        }
    }

    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }
}

void VulkanExampleBase::sKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto* mInstance = reinterpret_cast<VulkanExampleBase*>(glfwGetWindowUserPointer(window));
    mInstance->keyCallback(key, action);
}
void VulkanExampleBase::sCursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    auto* mInstance = reinterpret_cast<VulkanExampleBase*>(glfwGetWindowUserPointer(window));
    mInstance->cursorPosCallback(xpos, ypos);
}
void VulkanExampleBase::sMouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    auto* mInstance = reinterpret_cast<VulkanExampleBase*>(glfwGetWindowUserPointer(window));
    mInstance->mouseButtonCallback(button, action);
}

void VulkanExampleBase::keyCallback(int key, int action)
{
    switch (action) {
        case GLFW_PRESS:
            switch (key) {
                case GLFW_KEY_W:
                    camera.keys.up = true;
                    break;
                case GLFW_KEY_S:
                    camera.keys.down = true;
                    break;
                case GLFW_KEY_A:
                    camera.keys.left = true;
                    break;
                case GLFW_KEY_D:
                    camera.keys.right = true;
                    break;
                default:
                    break;
            }
            break;
        case GLFW_RELEASE:
            switch (key) {
                case GLFW_KEY_W:
                    camera.keys.up = false;
                    break;
                case GLFW_KEY_S:
                    camera.keys.down = false;
                    break;
                case GLFW_KEY_A:
                    camera.keys.left = false;
                    break;
                case GLFW_KEY_D:
                    camera.keys.right = false;
                    break;
                default:
                    break;
            }
            break;
    }
}

void VulkanExampleBase::cursorPosCallback(double xpos, double ypos)
{
    handleMouseMove(xpos, ypos);
}

void VulkanExampleBase::mouseButtonCallback(int button, int action)
{
    switch (action) {
        case GLFW_PRESS:
            switch (button) {
                case GLFW_MOUSE_BUTTON_LEFT:
                    mouseButtons.left = true;
                    break;
                case GLFW_MOUSE_BUTTON_RIGHT:
                    mouseButtons.right = true;
                    break;
                case GLFW_MOUSE_BUTTON_MIDDLE:
                    mouseButtons.middle = true;
                    break;
            }
            break;
        case GLFW_RELEASE:
            switch (button) {
                case GLFW_MOUSE_BUTTON_LEFT:
                    mouseButtons.left = false;
                    break;
                case GLFW_MOUSE_BUTTON_RIGHT:
                    mouseButtons.right = false;
                    break;
                case GLFW_MOUSE_BUTTON_MIDDLE:
                    mouseButtons.middle = false;
                    break;
            }
            break;
    }
}

void VulkanExampleBase::nextFrame()
{
//    if (viewUpdated) {
//        viewUpdated = false;
//        viewChanged();
//    }

    viewChanged();
    render();
    frameCounter++;
    auto tEnd = std::chrono::high_resolution_clock::now();
    auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tPrevEnd).count();
    frameTimer = (float)tDiff / 1000.0f;
    camera.update(frameTimer);
    if (camera.moving())
    {
        viewUpdated = true;
    }
    // Convert to clamped timer value
    if (!paused)
    {
        timer += timerSpeed * frameTimer;
        if (timer > 1.0)
        {
            timer -= 1.0f;
        }
    }
    float fpsTimer = (float)(std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count());
    if (fpsTimer > 1000.0f)
    {
        lastFPS = static_cast<uint32_t>((float)frameCounter * (1000.0f / fpsTimer));
        frameCounter = 0;
        lastTimestamp = tEnd;
    }
    tPrevEnd = tEnd;

    updateOverlay();
}

void VulkanExampleBase::updateOverlay()
{
    if (!settings.overlay) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = frameTimer;

    io.MousePos = ImVec2(mousePos.x, mousePos.y);
    io.MouseDown[0] = mouseButtons.left && overlay.visible;
    io.MouseDown[1] = mouseButtons.right && overlay.visible;
    io.MouseDown[2] = mouseButtons.middle && overlay.visible;

    ImGui::NewFrame();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::SetNextWindowPos(ImVec2(10 * overlay.scale, 10 * overlay.scale));
    ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiSetCond_FirstUseEver);
    ImGui::Begin("Vulkan Example", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextUnformatted(title.c_str());
    ImGui::TextUnformatted(deviceProperties.deviceName);
    ImGui::Text("%.2f ms/frame (%.1d fps)", (1000.0f / lastFPS), lastFPS);
    ImGui::PushItemWidth(110.0f * overlay.scale);
    OnUpdateUIOverlay(&overlay);
    ImGui::PopItemWidth();
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::Render();

    if (overlay.update() || overlay.updated) {
        buildCommandBuffers();
        overlay.updated = false;
    }
}

VkResult VulkanExampleBase::createInstance(bool enableValidation)
{
    this->settings.validation = enableValidation;

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = name.c_str();
    appInfo.pEngineName = name.c_str();
    appInfo.apiVersion = apiVersion;

    std::vector<const char*> instanceExtensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_MVK_MACOS_SURFACE_EXTENSION_NAME,
            "VK_EXT_metal_surface"
    };

    // Get extensions supported by the instance and store for later use
    uint32_t extCount;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    if (extCount > 0)
    {
        std::vector<VkExtensionProperties> extensions(extCount);
        if (vkEnumerateInstanceExtensionProperties(nullptr, &extCount, &extensions.front()) == VK_SUCCESS)
        {
            for (VkExtensionProperties extension : extensions)
            {
                supportedInstanceExtensions.push_back(extension.extensionName);
            }
        }
    }

    if (std::find(enabledInstanceExtensions.begin(), enabledInstanceExtensions.end(), VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) == enabledInstanceExtensions.end())
    {
        enabledInstanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }

    if (enabledInstanceExtensions.size() > 0)
    {
        for (const char* enabledExtension : enabledInstanceExtensions)
        {
            // Output message if requested extension is not available
            if (std::find(supportedInstanceExtensions.begin(), supportedInstanceExtensions.end(), enabledExtension) == supportedInstanceExtensions.end())
            {
                std::cerr << "Enabled instance extension \"" << enabledExtension << "\" is not present at instance level\n";
            }
            instanceExtensions.push_back(enabledExtension);
        }
    }

    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pNext = NULL;
    instanceCreateInfo.pApplicationInfo = &appInfo;

    // When running on iOS/macOS with MoltenVK and VK_KHR_portability_enumeration is defined and supported by the instance, enable the extension and the flag
    if (std::find(supportedInstanceExtensions.begin(), supportedInstanceExtensions.end(), VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) != supportedInstanceExtensions.end())
    {
        instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instanceCreateInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    if (instanceExtensions.size() > 0)
    {
        if (settings.validation)
        {
            // Dependency when VK_EXT_DEBUG_MARKER is enabled
            instanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
            instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
        instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
    }

    // The VK_LAYER_KHRONOS_validation contains all current validation functionality.
    const char* validationLayerName = "VK_LAYER_KHRONOS_validation";
    if (settings.validation)
    {
        // Check if this layer is available at instance level
        uint32_t instanceLayerCount;
        vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr);
        std::vector<VkLayerProperties> instanceLayerProperties(instanceLayerCount);
        vkEnumerateInstanceLayerProperties(&instanceLayerCount, instanceLayerProperties.data());
        bool validationLayerPresent = false;
        for (VkLayerProperties layer : instanceLayerProperties) {
            if (strcmp(layer.layerName, validationLayerName) == 0) {
                validationLayerPresent = true;
                break;
            }
        }
        if (validationLayerPresent) {
            instanceCreateInfo.ppEnabledLayerNames = &validationLayerName;
            instanceCreateInfo.enabledLayerCount = 1;
        } else {
            std::cerr << "Validation layer VK_LAYER_KHRONOS_validation not present, validation is disabled";
        }
    }
    return vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
}

void VulkanExampleBase::initSwapchain()
{
    swapChain.initSurface(window);
}

void VulkanExampleBase::createCommandPool()
{
    VkCommandPoolCreateInfo cmdPoolInfo = {};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex = swapChain.queueNodeIndex;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &cmdPool));
}

void VulkanExampleBase::setupSwapChain()
{
    swapChain.create(&width, &height, settings.vsync, settings.fullscreen);
}

void VulkanExampleBase::createSynchronizationPrimitives()
{
    // Wait fences to sync command buffer access
    VkFenceCreateInfo fenceCreateInfo = initializers::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
    waitFences.resize(drawCmdBuffers.size());
    for (auto& fence : waitFences) {
        VK_CHECK_RESULT(vkCreateFence(device, &fenceCreateInfo, nullptr, &fence));
    }
}

void VulkanExampleBase::createCommandBuffers()
{
    // Create one command buffer for each swap chain image and reuse for rendering
    drawCmdBuffers.resize(swapChain.imageCount);

    VkCommandBufferAllocateInfo cmdBufAllocateInfo =
            initializers::commandBufferAllocateInfo(
                    cmdPool,
                    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    static_cast<uint32_t>(drawCmdBuffers.size()));

    VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, drawCmdBuffers.data()));
}

void VulkanExampleBase::setupDepthStencil()
{
    VkImageCreateInfo imageCI{};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = depthFormat;
    imageCI.extent = { width, height, 1 };
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &depthStencil.image));
    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(device, depthStencil.image, &memReqs);

    VkMemoryAllocateInfo memAllloc{};
    memAllloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllloc.allocationSize = memReqs.size;
    memAllloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(device, &memAllloc, nullptr, &depthStencil.mem));
    VK_CHECK_RESULT(vkBindImageMemory(device, depthStencil.image, depthStencil.mem, 0));

    // Images aren't directly accessed in Vulkan, but rather through views described by a subresource range
    // This allows for multiple views of one image with differing ranges (e.g. for different layers)
    VkImageViewCreateInfo imageViewCI{};
    imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCI.image = depthStencil.image;
    imageViewCI.format = depthFormat;
    imageViewCI.subresourceRange.baseMipLevel = 0;
    imageViewCI.subresourceRange.levelCount = 1;
    imageViewCI.subresourceRange.baseArrayLayer = 0;
    imageViewCI.subresourceRange.layerCount = 1;
    imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    // Stencil aspect should only be set on depth + stencil formats (VK_FORMAT_D16_UNORM_S8_UINT..VK_FORMAT_D32_SFLOAT_S8_UINT
    if (depthFormat >= VK_FORMAT_D16_UNORM_S8_UINT) {
        imageViewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCI, nullptr, &depthStencil.view));
}

// Render passes are a new concept in Vulkan. They describe the attachments used during rendering and may contain multiple subpasses with attachment dependencies
// This allows the driver to know up-front what the rendering will look like and is a good opportunity to optimize especially on tile-based renderers (with multiple subpasses)
// Using sub pass dependencies also adds implicit layout transitions for the attachment used, so we don't need to add explicit image memory barriers to transform them
void VulkanExampleBase::setupRenderPass()
{
// This example will use a single render pass with one subpass

    // Descriptors for the attachments used by this renderpass
    std::array<VkAttachmentDescription, 2> attachments = {};

    // Color attachment
    attachments[0].format = swapChain.colorFormat;                                  // Use the color format selected by the swapchain
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;                                 // We don't use multi sampling in this example
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;                            // Clear this attachment at the start of the render pass
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;                          // Keep its contents after the render pass is finished (for displaying it)
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;                 // We don't use stencil, so don't care for load
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;               // Same for store
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;                       // Layout at render pass start. Initial doesn't matter, so we use undefined
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;                   // Layout to which the attachment is transitioned when the render pass is finished
    // As we want to present the color buffer to the swapchain, we transition to PRESENT_KHR
    // Depth attachment
    attachments[1].format = depthFormat;                                           // A proper depth format is selected in the example base
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;                           // Clear depth at start of first subpass
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;                     // We don't need depth after render pass has finished (DONT_CARE may result in better performance)
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;                // No stencil
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;              // No Stencil
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;                      // Layout at render pass start. Initial doesn't matter, so we use undefined
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // Transition to depth/stencil attachment

    // Setup attachment references
    VkAttachmentReference colorReference = {};
    colorReference.attachment = 0;                                    // Attachment 0 is color
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // Attachment layout used as color during the subpass

    VkAttachmentReference depthReference = {};
    depthReference.attachment = 1;                                            // Attachment 1 is color
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // Attachment used as depth/stencil used during the subpass

    // Setup a single subpass reference
    VkSubpassDescription subpassDescription = {};
    subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDescription.colorAttachmentCount = 1;                            // Subpass uses one color attachment
    subpassDescription.pColorAttachments = &colorReference;                 // Reference to the color attachment in slot 0
    subpassDescription.pDepthStencilAttachment = &depthReference;           // Reference to the depth attachment in slot 1
    subpassDescription.inputAttachmentCount = 0;                            // Input attachments can be used to sample from contents of a previous subpass
    subpassDescription.pInputAttachments = nullptr;                         // (Input attachments not used by this example)
    subpassDescription.preserveAttachmentCount = 0;                         // Preserved attachments can be used to loop (and preserve) attachments through subpasses
    subpassDescription.pPreserveAttachments = nullptr;                      // (Preserve attachments not used by this example)
    subpassDescription.pResolveAttachments = nullptr;                       // Resolve attachments are resolved at the end of a sub pass and can be used for e.g. multi sampling

    // Setup subpass dependencies
    // These will add the implicit attachment layout transitions specified by the attachment descriptions
    // The actual usage layout is preserved through the layout specified in the attachment reference
    // Each subpass dependency will introduce a memory and execution dependency between the source and dest subpass described by
    // srcStageMask, dstStageMask, srcAccessMask, dstAccessMask (and dependencyFlags is set)
    // Note: VK_SUBPASS_EXTERNAL is a special constant that refers to all commands executed outside of the actual renderpass)
    std::array<VkSubpassDependency, 2> dependencies;

    // First dependency at the start of the renderpass
    // Does the transition from final to initial layout
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;                             // Producer of the dependency
    dependencies[0].dstSubpass = 0;                                               // Consumer is our single subpass that will wait for the execution dependency
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // Match our pWaitDstStageMask when we vkQueueSubmit
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // is a loadOp stage for color attachments
    dependencies[0].srcAccessMask = 0;                                            // semaphore wait already does memory dependency for us
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;         // is a loadOp CLEAR access mask for color attachments
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Second dependency at the end the renderpass
    // Does the transition from the initial to the final layout
    // Technically this is the same as the implicit subpass dependency, but we are gonna state it explicitly here
    dependencies[1].srcSubpass = 0;                                               // Producer of the dependency is our single subpass
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;                             // Consumer are all commands outside of the renderpass
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // is a storeOp stage for color attachments
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;          // Do not block any subsequent work
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;         // is a storeOp `STORE` access mask for color attachments
    dependencies[1].dstAccessMask = 0;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Create the actual renderpass
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());  // Number of attachments used by this render pass
    renderPassInfo.pAttachments = attachments.data();                            // Descriptions of the attachments used by the render pass
    renderPassInfo.subpassCount = 1;                                             // We only use one subpass in this example
    renderPassInfo.pSubpasses = &subpassDescription;                             // Description of that subpass
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size()); // Number of subpass dependencies
    renderPassInfo.pDependencies = dependencies.data();                          // Subpass dependencies used by the render pass

    VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
}

void VulkanExampleBase::getEnabledFeatures() {}

void VulkanExampleBase::createPipelineCache()
{
    VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
    pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &pipelineCache));
}

void VulkanExampleBase::setupFrameBuffer()
{
    VkImageView attachments[2];

    // Depth/Stencil attachment is the same for all frame buffers
    attachments[1] = depthStencil.view;

    VkFramebufferCreateInfo frameBufferCreateInfo = {};
    frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    frameBufferCreateInfo.pNext = NULL;
    frameBufferCreateInfo.renderPass = renderPass;
    frameBufferCreateInfo.attachmentCount = 2;
    frameBufferCreateInfo.pAttachments = attachments;
    frameBufferCreateInfo.width = width;
    frameBufferCreateInfo.height = height;
    frameBufferCreateInfo.layers = 1;

    // Create frame buffers for every swap chain image
    frameBuffers.resize(swapChain.imageCount);
    for (uint32_t i = 0; i < frameBuffers.size(); i++)
    {
        attachments[0] = swapChain.buffers[i].view;
        VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &frameBuffers[i]));
    }
}

VkPipelineShaderStageCreateInfo VulkanExampleBase::loadShader(std::string fileName, VkShaderStageFlagBits stage)
{
    VkPipelineShaderStageCreateInfo shaderStage = {};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = stage;
    shaderStage.module = tools::loadShader(fileName.c_str(), device);
    shaderStage.pName = "main";
    assert(shaderStage.module != VK_NULL_HANDLE);
    shaderModules.push_back(shaderStage.module);
    return shaderStage;
}

std::string VulkanExampleBase::getShadersPath() const
{
    return getAssetPath() + "shaders/";
}

void VulkanExampleBase::handleMouseMove(int32_t x, int32_t y)
{
    int32_t dx = (int32_t)mousePos.x - x;
    int32_t dy = (int32_t)mousePos.y - y;

    bool handled = false;

    if (settings.overlay) {
        ImGuiIO& io = ImGui::GetIO();
        handled = io.WantCaptureMouse && overlay.visible;
    }
    mouseMoved((float)x, (float)y, handled);

    if (handled) {
        mousePos = glm::vec2((float)x, (float)y);
        return;
    }

    if (mouseButtons.left) {
        camera.rotate(glm::vec3(dy * camera.rotationSpeed, -dx * camera.rotationSpeed, 0.0f));
        viewUpdated = true;
    }
    if (mouseButtons.right) {
        camera.translate(glm::vec3(-0.0f, 0.0f, dy * .005f));
        viewUpdated = true;
    }
    if (mouseButtons.middle) {
        camera.translate(glm::vec3(-dx * 0.005f, -dy * 0.005f, 0.0f));
        viewUpdated = true;
    }
    mousePos = glm::vec2((float)x, (float)y);
}

void VulkanExampleBase::mouseDragged(float x, float y)
{
    handleMouseMove(static_cast<int32_t>(x), static_cast<int32_t>(y));
}

void VulkanExampleBase::windowResize()
{
    if (!prepared) {
        return;
    }
    prepared = false;
    resized = true;

    // Ensure all operations on the device have been finished before destroying resources
    vkDeviceWaitIdle(device);

    // Recreate swap chain
    width = destWidth;
    height = destHeight;
    setupSwapChain();

    // Recreate the frame buffers
    vkDestroyImageView(device, depthStencil.view, nullptr);
    vkDestroyImage(device, depthStencil.image, nullptr);
    vkFreeMemory(device, depthStencil.mem, nullptr);
    setupDepthStencil();
    for (uint32_t i = 0; i < frameBuffers.size(); i++) {
        vkDestroyFramebuffer(device, frameBuffers[i], nullptr);
    }
    setupFrameBuffer();

    if ((width > 0.0f) && (height > 0.0f)) {
        if (settings.overlay) {
            overlay.resize(width, height);
        }
    }

    // Command buffers need to be recreated as they may store
    // references to the recreated frame buffer
    vkFreeCommandBuffers(device, cmdPool, static_cast<uint32_t>(drawCmdBuffers.size()), drawCmdBuffers.data());
    createCommandBuffers();
    buildCommandBuffers();

    // Recreate fences in case number of swapchain images has changed on resize
    for (auto& fence : waitFences) {
        vkDestroyFence(device, fence, nullptr);
    }
    createSynchronizationPrimitives();

    vkDeviceWaitIdle(device);

    if ((width > 0.0f) && (height > 0.0f)) {
        camera.updateAspectRatio((float)width / (float)height);
    }

    // Notify derived class
    windowResized();
    viewChanged();

    prepared = true;
}

void VulkanExampleBase::windowWillResize(float x, float y)
{
    resizing = true;
    if (prepared)
    {
        destWidth = x;
        destHeight = y;
        windowResize();
    }
}

void VulkanExampleBase::windowDidResize()
{
    resizing = false;
}

void VulkanExampleBase::drawUI(const VkCommandBuffer commandBuffer)
{
    if (settings.overlay && overlay.visible) {
        const VkViewport viewport = initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
        const VkRect2D scissor = initializers::rect2D(width, height, 0, 0);
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        overlay.draw(commandBuffer);
    }
}

void VulkanExampleBase::prepareFrame()
{
    // Acquire the next image from the swap chain, and set the presentComplete signaled
    VkResult res = swapChain.acquireNextImage(semaphores.presentComplete, &currentBuffer);

    // Recreate the swapchain if it's no longer compatible with the surface (OUT_OF_DATE)
    // If no longer optimal (VK_SUBOPTIMAL_KHR), wait until submitFrame() in case number of swapchain images will change on resize
    if ((res == VK_ERROR_OUT_OF_DATE_KHR) || (res == VK_SUBOPTIMAL_KHR)) {
        if (res == VK_ERROR_OUT_OF_DATE_KHR) {
            windowResize();
        }
        return;
    }
}

void VulkanExampleBase::presentFrame()
{
    VkResult result = swapChain.queuePresent(queue, currentBuffer, semaphores.renderComplete);
    if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
        windowResize();
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return;
        }
    }
    else {
        VK_CHECK_RESULT(result);
    }
    VK_CHECK_RESULT(vkQueueWaitIdle(queue));
}

void VulkanExampleBase::renderFrame()
{
    VulkanExampleBase::prepareFrame();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
    VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    VulkanExampleBase::presentFrame();
}

void VulkanExampleBase::windowResized() {}

void VulkanExampleBase::viewChanged() {}

void VulkanExampleBase::keyPressed(uint32_t) {}

void VulkanExampleBase::mouseMoved(double x, double y, bool & handled) {}

void VulkanExampleBase::buildCommandBuffers() {}

void VulkanExampleBase::OnUpdateUIOverlay(UIOverlay *overlay) {}

CommandLineParser::CommandLineParser()
{
    add("help", { "--help" }, 0, "Show help");
    add("validation", {"-v", "--validation"}, 0, "Enable validation layers");
    add("vsync", {"-vs", "--vsync"}, 0, "Enable V-Sync");
    add("fullscreen", { "-f", "--fullscreen" }, 0, "Start in fullscreen mode");
    add("width", { "-w", "--width" }, 1, "Set window width");
    add("height", { "-h", "--height" }, 1, "Set window height");
    add("shaders", { "-s", "--shaders" }, 1, "Select shader type to use (glsl or hlsl)");
    add("gpuselection", { "-g", "--gpu" }, 1, "Select GPU to run on");
    add("gpulist", { "-gl", "--listgpus" }, 0, "Display a list of available Vulkan devices");
    add("benchmark", { "-b", "--benchmark" }, 0, "Run example in benchmark mode");
    add("benchmarkwarmup", { "-bw", "--benchwarmup" }, 1, "Set warmup time for benchmark mode in seconds");
    add("benchmarkruntime", { "-br", "--benchruntime" }, 1, "Set duration time for benchmark mode in seconds");
    add("benchmarkresultfile", { "-bf", "--benchfilename" }, 1, "Set file name for benchmark results");
    add("benchmarkresultframes", { "-bt", "--benchframetimes" }, 0, "Save frame times to benchmark results file");
    add("benchmarkframes", { "-bfs", "--benchmarkframes" }, 1, "Only render the given number of frames");
}

void CommandLineParser::add(std::string name, std::vector<std::string> commands, bool hasValue, std::string help)
{
    options[name].commands = commands;
    options[name].help = help;
    options[name].set = false;
    options[name].hasValue = hasValue;
    options[name].value = "";
}

void CommandLineParser::printHelp()
{
    std::cout << "Available command line options:\n";
    for (auto option : options) {
        std::cout << " ";
        for (size_t i = 0; i < option.second.commands.size(); i++) {
            std::cout << option.second.commands[i];
            if (i < option.second.commands.size() - 1) {
                std::cout << ", ";
            }
        }
        std::cout << ": " << option.second.help << "\n";
    }
    std::cout << "Press any key to close...";
}

void CommandLineParser::parse(std::vector<const char*> arguments)
{
    bool printHelp = false;
    // Known arguments
    for (auto& option : options) {
        for (auto& command : option.second.commands) {
            for (size_t i = 0; i < arguments.size(); i++) {
                if (strcmp(arguments[i], command.c_str()) == 0) {
                    option.second.set = true;
                    // Get value
                    if (option.second.hasValue) {
                        if (arguments.size() > i + 1) {
                            option.second.value = arguments[i + 1];
                        }
                        if (option.second.value == "") {
                            printHelp = true;
                            break;
                        }
                    }
                }
            }
        }
    }
    // Print help for unknown arguments or missing argument values
    if (printHelp) {
        options["help"].set = true;
    }
}

bool CommandLineParser::isSet(std::string name)
{
    return ((options.find(name) != options.end()) && options[name].set);
}

std::string CommandLineParser::getValueAsString(std::string name, std::string defaultValue)
{
    assert(options.find(name) != options.end());
    std::string value = options[name].value;
    return (value != "") ? value : defaultValue;
}

int32_t CommandLineParser::getValueAsInt(std::string name, int32_t defaultValue)
{
    assert(options.find(name) != options.end());
    std::string value = options[name].value;
    if (value != "") {
        char* numConvPtr;
        int32_t intVal = strtol(value.c_str(), &numConvPtr, 10);
        return (intVal > 0) ? intVal : defaultValue;
    } else {
        return defaultValue;
    }
    return int32_t();
}