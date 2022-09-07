/*
* Vulkan texture loader
*
* Copyright(C) by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#include <VulkanTexture.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

void Texture::updateDescriptor()
{
    descriptor.sampler = sampler;
    descriptor.imageView = view;
    descriptor.imageLayout = imageLayout;
}

void Texture::destroy()
{
    vkDestroyImageView(device->logicalDevice, view, nullptr);
    vkDestroyImage(device->logicalDevice, image, nullptr);
    if (sampler)
    {
        vkDestroySampler(device->logicalDevice, sampler, nullptr);
    }
    vkFreeMemory(device->logicalDevice, deviceMemory, nullptr);
}

/**
* Load a 2D texture including all mip levels
*
* @param filename File to load
* @param format Vulkan format of the image data stored in the file
* @param device Vulkan device to create the texture on
* @param copyQueue Queue used for the texture staging copy commands (must support transfer)
* @param (Optional) imageUsageFlags Usage flags for the texture's image (defaults to VK_IMAGE_USAGE_SAMPLED_BIT)
* @param (Optional) imageLayout Usage layout for the texture (defaults VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
* @param (Optional) forceLinear Force linear tiling (not advised, defaults to false)
*
*/
void Texture2D::loadFromFile(std::string filename, VkFormat format, VulkanDevice *device, VkQueue copyQueue, VkImageUsageFlags imageUsageFlags, VkImageLayout imageLayout, bool forceLinear)
{
    this->device = device;
  
    int channels, texWidth, texHeight;
    stbi_uc* buffer = stbi_load(filename.c_str(), &texWidth, &texHeight, &channels, STBI_rgb_alpha);
    width = texWidth;
    height = texHeight;

    VkDeviceSize bufferSize = width * height * 4;
    mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    format = VK_FORMAT_R8G8B8A8_UNORM;

    VkFormatProperties formatProperties;

    mipLevels = static_cast<uint32_t>(floor(log2(std::max(width, height))) + 1.0);

    vkGetPhysicalDeviceFormatProperties(device->physicalDevice, format, &formatProperties);
    assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
    assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);

    VkMemoryAllocateInfo memAllocInfo{};
    memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    VkMemoryRequirements memReqs{};

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = bufferSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));
    vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
    VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

    uint8_t* data;
    VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
    memcpy(data, buffer, bufferSize);
    vkUnmapMemory(device->logicalDevice, stagingMemory);

    VkImageCreateInfo imageCreateInfo{};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = format;
    imageCreateInfo.mipLevels = mipLevels;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.extent = { width, height, 1 };
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));
    vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
    VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

    VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.levelCount = 1;
    subresourceRange.layerCount = 1;

    {
        VkImageMemoryBarrier imageMemoryBarrier{};
        imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageMemoryBarrier.srcAccessMask = 0;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier.image = image;
        imageMemoryBarrier.subresourceRange = subresourceRange;
        vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
    }

    VkBufferImageCopy bufferCopyRegion = {};
    bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bufferCopyRegion.imageSubresource.mipLevel = 0;
    bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
    bufferCopyRegion.imageSubresource.layerCount = 1;
    bufferCopyRegion.imageExtent.width = width;
    bufferCopyRegion.imageExtent.height = height;
    bufferCopyRegion.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

    {
        VkImageMemoryBarrier imageMemoryBarrier{};
        imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        imageMemoryBarrier.image = image;
        imageMemoryBarrier.subresourceRange = subresourceRange;
        vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
    }

    device->flushCommandBuffer(copyCmd, copyQueue, true);

    vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
    vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

    // Generate the mip chain (we use jpg and png, so we need to create this manually)
    VkCommandBuffer blitCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    for (uint32_t i = 1; i < mipLevels; i++) {
        VkImageBlit imageBlit{};

        imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBlit.srcSubresource.layerCount = 1;
        imageBlit.srcSubresource.mipLevel = i - 1;
        imageBlit.srcOffsets[1].x = int32_t(width >> (i - 1));
        imageBlit.srcOffsets[1].y = int32_t(height >> (i - 1));
        imageBlit.srcOffsets[1].z = 1;

        imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBlit.dstSubresource.layerCount = 1;
        imageBlit.dstSubresource.mipLevel = i;
        imageBlit.dstOffsets[1].x = int32_t(width >> i);
        imageBlit.dstOffsets[1].y = int32_t(height >> i);
        imageBlit.dstOffsets[1].z = 1;

        VkImageSubresourceRange mipSubRange = {};
        mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        mipSubRange.baseMipLevel = i;
        mipSubRange.levelCount = 1;
        mipSubRange.layerCount = 1;

        {
            VkImageMemoryBarrier imageMemoryBarrier{};
            imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageMemoryBarrier.srcAccessMask = 0;
            imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageMemoryBarrier.image = image;
            imageMemoryBarrier.subresourceRange = mipSubRange;
            vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
        }

        vkCmdBlitImage(blitCmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);

        {
            VkImageMemoryBarrier imageMemoryBarrier{};
            imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            imageMemoryBarrier.image = image;
            imageMemoryBarrier.subresourceRange = mipSubRange;
            vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
        }
    }

    subresourceRange.levelCount = mipLevels;
    imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    {
        VkImageMemoryBarrier imageMemoryBarrier{};
        imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        imageMemoryBarrier.image = image;
        imageMemoryBarrier.subresourceRange = subresourceRange;
        vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
    }

    stbi_image_free(buffer);

    device->flushCommandBuffer(blitCmd, copyQueue, true);

    VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	samplerInfo.maxAnisotropy = 1.0;
	samplerInfo.anisotropyEnable = VK_FALSE;
	samplerInfo.maxLod = (float)mipLevels;
	samplerInfo.maxAnisotropy = 8.0f;
	samplerInfo.anisotropyEnable = VK_TRUE;
	VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerInfo, nullptr, &sampler));

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.layerCount = 1;
	viewInfo.subresourceRange.levelCount = mipLevels;
	VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewInfo, nullptr, &view));

    updateDescriptor();
}

/**
* Creates a 2D texture from a buffer
*
* @param buffer Buffer containing texture data to upload
* @param bufferSize Size of the buffer in machine units
* @param width Width of the texture to create
* @param height Height of the texture to create
* @param format Vulkan format of the image data stored in the file
* @param device Vulkan device to create the texture on
* @param copyQueue Queue used for the texture staging copy commands (must support transfer)
* @param (Optional) filter Texture filtering for the sampler (defaults to VK_FILTER_LINEAR)
* @param (Optional) imageUsageFlags Usage flags for the texture's image (defaults to VK_IMAGE_USAGE_SAMPLED_BIT)
* @param (Optional) imageLayout Usage layout for the texture (defaults VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
*/
void Texture2D::fromBuffer(void* buffer, VkDeviceSize bufferSize, VkFormat format, uint32_t texWidth, uint32_t texHeight, VulkanDevice *device, VkQueue copyQueue, VkFilter filter, VkImageUsageFlags imageUsageFlags, VkImageLayout imageLayout)
{
    assert(buffer);

    this->device = device;
    width = texWidth;
    height = texHeight;
    mipLevels = 1;

    VkMemoryAllocateInfo memAllocInfo = initializers::memoryAllocateInfo();
    VkMemoryRequirements memReqs;

    // Use a separate command buffer for texture loading
    VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

    // Create a host-visible staging buffer that contains the raw image data
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo bufferCreateInfo = initializers::bufferCreateInfo();
    bufferCreateInfo.size = bufferSize;
    // This buffer is used as a transfer source for the buffer copy
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

    // Get memory requirements for the staging buffer (alignment, memory type bits)
    vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);

    memAllocInfo.allocationSize = memReqs.size;
    // Get memory type index for a host visible buffer
    memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
    VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

    // Copy texture data into staging buffer
    uint8_t *data;
    VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
    memcpy(data, buffer, bufferSize);
    vkUnmapMemory(device->logicalDevice, stagingMemory);

    VkBufferImageCopy bufferCopyRegion = {};
    bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bufferCopyRegion.imageSubresource.mipLevel = 0;
    bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
    bufferCopyRegion.imageSubresource.layerCount = 1;
    bufferCopyRegion.imageExtent.width = width;
    bufferCopyRegion.imageExtent.height = height;
    bufferCopyRegion.imageExtent.depth = 1;
    bufferCopyRegion.bufferOffset = 0;

    // Create optimal tiled target image
    VkImageCreateInfo imageCreateInfo = initializers::imageCreateInfo();
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = format;
    imageCreateInfo.mipLevels = mipLevels;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.extent = { width, height, 1 };
    imageCreateInfo.usage = imageUsageFlags;
    // Ensure that the TRANSFER_DST bit is set for staging
    if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
    {
        imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));

    vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);

    memAllocInfo.allocationSize = memReqs.size;

    memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
    VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = mipLevels;
    subresourceRange.layerCount = 1;

    // Image barrier for optimal image (target)
    // Optimal image will be used as destination for the copy
    tools::setImageLayout(
            copyCmd,
            image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            subresourceRange);

    // Copy mip levels from staging buffer
    vkCmdCopyBufferToImage(
            copyCmd,
            stagingBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &bufferCopyRegion
    );

    // Change texture image layout to shader read after all mip levels have been copied
    this->imageLayout = imageLayout;
    tools::setImageLayout(
            copyCmd,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            imageLayout,
            subresourceRange);

    device->flushCommandBuffer(copyCmd, copyQueue);

    // Clean up staging resources
    vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
    vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

    // Create sampler
    VkSamplerCreateInfo samplerCreateInfo = {};
    samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCreateInfo.magFilter = filter;
    samplerCreateInfo.minFilter = filter;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.mipLodBias = 0.0f;
    samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
    samplerCreateInfo.minLod = 0.0f;
    samplerCreateInfo.maxLod = 0.0f;
    samplerCreateInfo.maxAnisotropy = 1.0f;
    VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr, &sampler));

    // Create image view
    VkImageViewCreateInfo viewCreateInfo = {};
    viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCreateInfo.pNext = NULL;
    viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCreateInfo.format = format;
    viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
    viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    viewCreateInfo.subresourceRange.levelCount = 1;
    viewCreateInfo.image = image;
    VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr, &view));

    // Update descriptor image info member that can be used for setting up descriptor sets
    updateDescriptor();
}

/**
* Load a 2D texture array including all mip levels
*
* @param filename File to load (supports .ktx)
* @param format Vulkan format of the image data stored in the file
* @param device Vulkan device to create the texture on
* @param copyQueue Queue used for the texture staging copy commands (must support transfer)
* @param (Optional) imageUsageFlags Usage flags for the texture's image (defaults to VK_IMAGE_USAGE_SAMPLED_BIT)
* @param (Optional) imageLayout Usage layout for the texture (defaults VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
*
*/
void Texture2DArray::loadFromFile(std::string filename, VkFormat format, VulkanDevice *device, VkQueue copyQueue, VkImageUsageFlags imageUsageFlags, VkImageLayout imageLayout)
{
//    ktxTexture* ktxTexture;
//    ktxResult result = loadKTXFile(filename, &ktxTexture);
//    assert(result == KTX_SUCCESS);
//
//    this->device = device;
//    width = ktxTexture->baseWidth;
//    height = ktxTexture->baseHeight;
//    layerCount = ktxTexture->numLayers;
//    mipLevels = ktxTexture->numLevels;
//
//    ktx_uint8_t *ktxTextureData = ktxTexture_GetData(ktxTexture);
//    ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);
//
//    VkMemoryAllocateInfo memAllocInfo = initializers::memoryAllocateInfo();
//    VkMemoryRequirements memReqs;
//
//    // Create a host-visible staging buffer that contains the raw image data
//    VkBuffer stagingBuffer;
//    VkDeviceMemory stagingMemory;
//
//    VkBufferCreateInfo bufferCreateInfo = initializers::bufferCreateInfo();
//    bufferCreateInfo.size = ktxTextureSize;
//    // This buffer is used as a transfer source for the buffer copy
//    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
//    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
//
//    VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));
//
//    // Get memory requirements for the staging buffer (alignment, memory type bits)
//    vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
//
//    memAllocInfo.allocationSize = memReqs.size;
//    // Get memory type index for a host visible buffer
//    memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
//
//    VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
//    VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));
//
//    // Copy texture data into staging buffer
//    uint8_t *data;
//    VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
//    memcpy(data, ktxTextureData, ktxTextureSize);
//    vkUnmapMemory(device->logicalDevice, stagingMemory);
//
//    // Setup buffer copy regions for each layer including all of its miplevels
//    std::vector<VkBufferImageCopy> bufferCopyRegions;
//
//    for (uint32_t layer = 0; layer < layerCount; layer++)
//    {
//        for (uint32_t level = 0; level < mipLevels; level++)
//        {
//            ktx_size_t offset;
//            KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, level, layer, 0, &offset);
//            assert(result == KTX_SUCCESS);
//
//            VkBufferImageCopy bufferCopyRegion = {};
//            bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//            bufferCopyRegion.imageSubresource.mipLevel = level;
//            bufferCopyRegion.imageSubresource.baseArrayLayer = layer;
//            bufferCopyRegion.imageSubresource.layerCount = 1;
//            bufferCopyRegion.imageExtent.width = ktxTexture->baseWidth >> level;
//            bufferCopyRegion.imageExtent.height = ktxTexture->baseHeight >> level;
//            bufferCopyRegion.imageExtent.depth = 1;
//            bufferCopyRegion.bufferOffset = offset;
//
//            bufferCopyRegions.push_back(bufferCopyRegion);
//        }
//    }
//
//    // Create optimal tiled target image
//    VkImageCreateInfo imageCreateInfo = initializers::imageCreateInfo();
//    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
//    imageCreateInfo.format = format;
//    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
//    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
//    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
//    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
//    imageCreateInfo.extent = { width, height, 1 };
//    imageCreateInfo.usage = imageUsageFlags;
//    // Ensure that the TRANSFER_DST bit is set for staging
//    if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
//    {
//        imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
//    }
//    imageCreateInfo.arrayLayers = layerCount;
//    imageCreateInfo.mipLevels = mipLevels;
//
//    VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));
//
//    vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);
//
//    memAllocInfo.allocationSize = memReqs.size;
//    memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
//
//    VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
//    VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));
//
//    // Use a separate command buffer for texture loading
//    VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
//
//    // Image barrier for optimal image (target)
//    // Set initial layout for all array layers (faces) of the optimal (target) tiled texture
//    VkImageSubresourceRange subresourceRange = {};
//    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//    subresourceRange.baseMipLevel = 0;
//    subresourceRange.levelCount = mipLevels;
//    subresourceRange.layerCount = layerCount;
//
//    tools::setImageLayout(
//            copyCmd,
//            image,
//            VK_IMAGE_LAYOUT_UNDEFINED,
//            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
//            subresourceRange);
//
//    // Copy the layers and mip levels from the staging buffer to the optimal tiled image
//    vkCmdCopyBufferToImage(
//            copyCmd,
//            stagingBuffer,
//            image,
//            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
//            static_cast<uint32_t>(bufferCopyRegions.size()),
//            bufferCopyRegions.data());
//
//    // Change texture image layout to shader read after all faces have been copied
//    this->imageLayout = imageLayout;
//    tools::setImageLayout(
//            copyCmd,
//            image,
//            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
//            imageLayout,
//            subresourceRange);
//
//    device->flushCommandBuffer(copyCmd, copyQueue);
//
//    // Create sampler
//    VkSamplerCreateInfo samplerCreateInfo = initializers::samplerCreateInfo();
//    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
//    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
//    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
//    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
//    samplerCreateInfo.addressModeV = samplerCreateInfo.addressModeU;
//    samplerCreateInfo.addressModeW = samplerCreateInfo.addressModeU;
//    samplerCreateInfo.mipLodBias = 0.0f;
//    samplerCreateInfo.maxAnisotropy = device->enabledFeatures.samplerAnisotropy ? device->properties.limits.maxSamplerAnisotropy : 1.0f;
//    samplerCreateInfo.anisotropyEnable = device->enabledFeatures.samplerAnisotropy;
//    samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
//    samplerCreateInfo.minLod = 0.0f;
//    samplerCreateInfo.maxLod = (float)mipLevels;
//    samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
//    VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr, &sampler));
//
//    // Create image view
//    VkImageViewCreateInfo viewCreateInfo = initializers::imageViewCreateInfo();
//    viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
//    viewCreateInfo.format = format;
//    viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
//    viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
//    viewCreateInfo.subresourceRange.layerCount = layerCount;
//    viewCreateInfo.subresourceRange.levelCount = mipLevels;
//    viewCreateInfo.image = image;
//    VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr, &view));
//
//    // Clean up staging resources
//    ktxTexture_Destroy(ktxTexture);
//    vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
//    vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);
//
//    // Update descriptor image info member that can be used for setting up descriptor sets
//    updateDescriptor();
}

/**
* Load a cubemap texture including all mip levels from a single file
*
* @param filename File to load (supports .ktx)
* @param format Vulkan format of the image data stored in the file
* @param device Vulkan device to create the texture on
* @param copyQueue Queue used for the texture staging copy commands (must support transfer)
* @param (Optional) imageUsageFlags Usage flags for the texture's image (defaults to VK_IMAGE_USAGE_SAMPLED_BIT)
* @param (Optional) imageLayout Usage layout for the texture (defaults VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
*
*/
void TextureCubeMap::loadFromFile(std::string filename, VkFormat format, VulkanDevice *device, VkQueue copyQueue, VkImageUsageFlags imageUsageFlags, VkImageLayout imageLayout)
{
    // ktxTexture* ktxTexture;
    // ktxResult result = loadKTXFile(filename, &ktxTexture);
    // assert(result == KTX_SUCCESS);

    // this->device = device;
    // width = ktxTexture->baseWidth;
    // height = ktxTexture->baseHeight;
    // mipLevels = ktxTexture->numLevels;

    // ktx_uint8_t *ktxTextureData = ktxTexture_GetData(ktxTexture);
    // ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);

    // VkMemoryAllocateInfo memAllocInfo = initializers::memoryAllocateInfo();
    // VkMemoryRequirements memReqs;

    // // Create a host-visible staging buffer that contains the raw image data
    // VkBuffer stagingBuffer;
    // VkDeviceMemory stagingMemory;

    // VkBufferCreateInfo bufferCreateInfo = initializers::bufferCreateInfo();
    // bufferCreateInfo.size = ktxTextureSize;
    // // This buffer is used as a transfer source for the buffer copy
    // bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    // bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

    // // Get memory requirements for the staging buffer (alignment, memory type bits)
    // vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);

    // memAllocInfo.allocationSize = memReqs.size;
    // // Get memory type index for a host visible buffer
    // memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
    // VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

    // // Copy texture data into staging buffer
    // uint8_t *data;
    // VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
    // memcpy(data, ktxTextureData, ktxTextureSize);
    // vkUnmapMemory(device->logicalDevice, stagingMemory);

    // // Setup buffer copy regions for each face including all of its mip levels
    // std::vector<VkBufferImageCopy> bufferCopyRegions;

    // for (uint32_t face = 0; face < 6; face++)
    // {
    //     for (uint32_t level = 0; level < mipLevels; level++)
    //     {
    //         ktx_size_t offset;
    //         KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, level, 0, face, &offset);
    //         assert(result == KTX_SUCCESS);

    //         VkBufferImageCopy bufferCopyRegion = {};
    //         bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    //         bufferCopyRegion.imageSubresource.mipLevel = level;
    //         bufferCopyRegion.imageSubresource.baseArrayLayer = face;
    //         bufferCopyRegion.imageSubresource.layerCount = 1;
    //         bufferCopyRegion.imageExtent.width = ktxTexture->baseWidth >> level;
    //         bufferCopyRegion.imageExtent.height = ktxTexture->baseHeight >> level;
    //         bufferCopyRegion.imageExtent.depth = 1;
    //         bufferCopyRegion.bufferOffset = offset;

    //         bufferCopyRegions.push_back(bufferCopyRegion);
    //     }
    // }

    // // Create optimal tiled target image
    // VkImageCreateInfo imageCreateInfo = initializers::imageCreateInfo();
    // imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    // imageCreateInfo.format = format;
    // imageCreateInfo.mipLevels = mipLevels;
    // imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    // imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    // imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // imageCreateInfo.extent = { width, height, 1 };
    // imageCreateInfo.usage = imageUsageFlags;
    // // Ensure that the TRANSFER_DST bit is set for staging
    // if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
    // {
    //     imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    // }
    // // Cube faces count as array layers in Vulkan
    // imageCreateInfo.arrayLayers = 6;
    // // This flag is required for cube map images
    // imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;


    // VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));

    // vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);

    // memAllocInfo.allocationSize = memReqs.size;
    // memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
    // VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

    // // Use a separate command buffer for texture loading
    // VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

    // // Image barrier for optimal image (target)
    // // Set initial layout for all array layers (faces) of the optimal (target) tiled texture
    // VkImageSubresourceRange subresourceRange = {};
    // subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    // subresourceRange.baseMipLevel = 0;
    // subresourceRange.levelCount = mipLevels;
    // subresourceRange.layerCount = 6;

    // tools::setImageLayout(
    //         copyCmd,
    //         image,
    //         VK_IMAGE_LAYOUT_UNDEFINED,
    //         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //         subresourceRange);

    // // Copy the cube map faces from the staging buffer to the optimal tiled image
    // vkCmdCopyBufferToImage(
    //         copyCmd,
    //         stagingBuffer,
    //         image,
    //         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //         static_cast<uint32_t>(bufferCopyRegions.size()),
    //         bufferCopyRegions.data());

    // // Change texture image layout to shader read after all faces have been copied
    // this->imageLayout = imageLayout;
    // tools::setImageLayout(
    //         copyCmd,
    //         image,
    //         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //         imageLayout,
    //         subresourceRange);

    // device->flushCommandBuffer(copyCmd, copyQueue);

    // // Create sampler
    // VkSamplerCreateInfo samplerCreateInfo = initializers::samplerCreateInfo();
    // samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    // samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
    // samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    // samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    // samplerCreateInfo.addressModeV = samplerCreateInfo.addressModeU;
    // samplerCreateInfo.addressModeW = samplerCreateInfo.addressModeU;
    // samplerCreateInfo.mipLodBias = 0.0f;
    // samplerCreateInfo.maxAnisotropy = device->enabledFeatures.samplerAnisotropy ? device->properties.limits.maxSamplerAnisotropy : 1.0f;
    // samplerCreateInfo.anisotropyEnable = device->enabledFeatures.samplerAnisotropy;
    // samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
    // samplerCreateInfo.minLod = 0.0f;
    // samplerCreateInfo.maxLod = (float)mipLevels;
    // samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    // VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr, &sampler));

    // // Create image view
    // VkImageViewCreateInfo viewCreateInfo = initializers::imageViewCreateInfo();
    // viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    // viewCreateInfo.format = format;
    // viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
    // viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    // viewCreateInfo.subresourceRange.layerCount = 6;
    // viewCreateInfo.subresourceRange.levelCount = mipLevels;
    // viewCreateInfo.image = image;
    // VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr, &view));

    // // Clean up staging resources
    // ktxTexture_Destroy(ktxTexture);
    // vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
    // vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

    // // Update descriptor image info member that can be used for setting up descriptor sets
    // updateDescriptor();
}
