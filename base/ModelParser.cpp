//
// Created by Junkang on 2023/5/29.
//

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "ModelParser.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE

#include "ModelParser.h"

using namespace MParser;

VkDescriptorSetLayout MParser::descriptorSetLayoutImage = VK_NULL_HANDLE;
VkDescriptorSetLayout MParser::descriptorSetLayoutUbo = VK_NULL_HANDLE;
VkMemoryPropertyFlags MParser::memoryPropertyFlags = 0;
uint32_t MParser::descriptorBindingFlags = DescriptorBindingFlags::ImageBaseColor;

void Texture::destroy()
{
    if (device)
    {
        vkDestroyImageView(device->logicalDevice, view, nullptr);
        vkDestroyImage(device->logicalDevice, image, nullptr);
        vkFreeMemory(device->logicalDevice, deviceMemory, nullptr);
        vkDestroySampler(device->logicalDevice, sampler, nullptr);
    }
}

void Texture::load(const aiScene *scene, std::string fileName, std::string filePath, VulkanDevice *device, VkQueue copyQueue) {
    this->device = device;

    bool isKtx = false;
    // Image points to an external ktx file
    if (fileName.find_last_of('.') != std::string::npos) {
        if (fileName.substr(fileName.find_last_of('.') + 1) == "ktx") {
            isKtx = true;
        }
    }

    VkFormat format;

    // Texture was loaded using STB_Image
    if (!isKtx) {
        void* texData;
        int texWidth;
        int texHeight;
        int comp;

        if (scene->HasTextures()) {
            // image data embedded in model
            const auto* aiTexData = scene->GetEmbeddedTexture(fileName.c_str());
            texData = stbi_load_from_memory(reinterpret_cast<unsigned char *>(aiTexData->pcData), aiTexData->mWidth, &texWidth, &texHeight, &comp, 0);
        } else {
            // image need to be load from external
            auto path = filePath + '/' + fileName;
            texData = stbi_load(path.c_str(), &texWidth, &texHeight, &comp, 0);
        }

        VkDeviceSize bufferSize = 0;
        bool deleteBuffer = false;

        unsigned char* buffer = nullptr;
        if (comp == 3) {
            // Most devices don't support RGB only on Vulkan so convert if necessary
            // TODO: Check actual format support and transform only if required
            bufferSize = texWidth * texHeight * 4;
            buffer = new unsigned char[bufferSize];
            auto* rgba = buffer;
            auto* rgb = static_cast<unsigned char*>(texData);
            for (size_t i = 0; i < texWidth * texHeight; ++i) {
                for (int32_t j = 0; j < 3; ++j) {
                    rgba[j] = rgb[j];
                }
                rgba += 4;
                rgb += 3;
            }
            deleteBuffer = true;
        }
        else {
            buffer = static_cast<unsigned char*>(texData);
            bufferSize = texWidth * texHeight * comp;
        }

        stbi_image_free(texData);


        format = VK_FORMAT_R8G8B8A8_UNORM;

        VkFormatProperties formatProperties;

        width = texWidth;
        height = texHeight;
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

        // Generate the mip chain (glTF uses jpg and png, so we need to create this manually)
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

        if (deleteBuffer) {
            delete[] buffer;
        }

        device->flushCommandBuffer(blitCmd, copyQueue, true);
    } else {
        // Texture is stored in an external ktx file
        std::string path = filePath + '/' + fileName;

        ktxTexture* ktxTexture;

        auto r = KTX_SUCCESS;

        if (!tools::fileExists(path)) {
            tools::exitFatal("Could not load texture from " + path + "\n\nThe file may be part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download the latest version.", -1);
        }
        r = ktxTexture_CreateFromNamedFile(path.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
        assert(r == KTX_SUCCESS);

        this->device = device;
        width = ktxTexture->baseWidth;
        height = ktxTexture->baseHeight;
        mipLevels = ktxTexture->numLevels;

        ktx_uint8_t* ktxTextureData = ktxTexture_GetData(ktxTexture);
        ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);
        // @todo: Use ktxTexture_GetVkFormat(ktxTexture)
        format = VK_FORMAT_R8G8B8A8_UNORM;

        // Get device properties for the requested texture format
        VkFormatProperties formatProperties;
        vkGetPhysicalDeviceFormatProperties(device->physicalDevice, format, &formatProperties);

        VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;

        VkBufferCreateInfo bufferCreateInfo = initializers::bufferCreateInfo();
        bufferCreateInfo.size = ktxTextureSize;
        // This buffer is used as a transfer source for the buffer copy
        bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

        VkMemoryAllocateInfo memAllocInfo = initializers::memoryAllocateInfo();
        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
        memAllocInfo.allocationSize = memReqs.size;
        memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
        VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

        uint8_t* data;
        VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
        memcpy(data, ktxTextureData, ktxTextureSize);
        vkUnmapMemory(device->logicalDevice, stagingMemory);

        std::vector<VkBufferImageCopy> bufferCopyRegions;
        for (uint32_t i = 0; i < mipLevels; i++)
        {
            ktx_size_t offset;
            KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, i, 0, 0, &offset);
            assert(result == KTX_SUCCESS);
            VkBufferImageCopy bufferCopyRegion = {};
            bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            bufferCopyRegion.imageSubresource.mipLevel = i;
            bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
            bufferCopyRegion.imageSubresource.layerCount = 1;
            bufferCopyRegion.imageExtent.width = std::max(1u, ktxTexture->baseWidth >> i);
            bufferCopyRegion.imageExtent.height = std::max(1u, ktxTexture->baseHeight >> i);
            bufferCopyRegion.imageExtent.depth = 1;
            bufferCopyRegion.bufferOffset = offset;
            bufferCopyRegions.push_back(bufferCopyRegion);
        }

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
        imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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

        tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
        vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());
        tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
        device->flushCommandBuffer(copyCmd, copyQueue);
        this->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
        vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

        ktxTexture_Destroy(ktxTexture);
    }

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
    VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerInfo, nullptr, &sampler));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.layerCount = 1;
    viewInfo.subresourceRange.levelCount = mipLevels;
    VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewInfo, nullptr, &view));

    descriptor.sampler = sampler;
    descriptor.imageView = view;
    descriptor.imageLayout = imageLayout;
}

/*
	glTF material
*/
void Material::createDescriptorSet(VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout, uint32_t descriptorBindingFlags)
{
    VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
    descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocInfo.descriptorPool = descriptorPool;
    descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
    descriptorSetAllocInfo.descriptorSetCount = 1;
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &descriptorSet));
    std::vector<VkDescriptorImageInfo> imageDescriptors{};
    std::vector<VkWriteDescriptorSet> writeDescriptorSets{};
    if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
        imageDescriptors.emplace_back(baseColorTexture->descriptor);
        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.dstSet = descriptorSet;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.pImageInfo = &baseColorTexture->descriptor;
        writeDescriptorSets.emplace_back(writeDescriptorSet);
    }
    if (normalTexture && descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
        imageDescriptors.emplace_back(normalTexture->descriptor);
        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.dstSet = descriptorSet;
        writeDescriptorSet.dstBinding = 1;
        writeDescriptorSet.pImageInfo = &normalTexture->descriptor;
        writeDescriptorSets.emplace_back(writeDescriptorSet);
    }

    if (descriptorBindingFlags & DescriptorBindingFlags::ImagePbr) {
        VkWriteDescriptorSet writeDescriptorSet{};

        if (occlusionTexture == nullptr) {
            imageDescriptors.emplace_back(emptyTexture->descriptor);
            writeDescriptorSet.pImageInfo = &emptyTexture->descriptor;
        } else {
            imageDescriptors.emplace_back(occlusionTexture->descriptor);
            writeDescriptorSet.pImageInfo = &occlusionTexture->descriptor;
        }

        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.dstSet = descriptorSet;
        writeDescriptorSet.dstBinding = 2;
        writeDescriptorSets.emplace_back(writeDescriptorSet);
    }

    if (descriptorBindingFlags & DescriptorBindingFlags::ImagePbr) {
        VkWriteDescriptorSet writeDescriptorSet{};

        if (metallicRoughnessTexture == nullptr) {
            imageDescriptors.emplace_back(emptyTexture->descriptor);
            writeDescriptorSet.pImageInfo = &emptyTexture->descriptor;
        } else {
            imageDescriptors.emplace_back(metallicRoughnessTexture->descriptor);
            writeDescriptorSet.pImageInfo = &metallicRoughnessTexture->descriptor;
        }

        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.dstSet = descriptorSet;
        writeDescriptorSet.dstBinding = 3;
        writeDescriptorSets.emplace_back(writeDescriptorSet);
    }

    vkUpdateDescriptorSets(device->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}


/*
	glTF primitive
*/
void Mesh::setDimensions(glm::vec3 min, glm::vec3 max) {
    dimensions.min = min;
    dimensions.max = max;
    dimensions.size = max - min;
    dimensions.center = (min + max) / 2.0f;
    dimensions.radius = glm::distance(min, max) / 2.0f;
}

/*
	glTF mesh
*/
Geometry::Geometry(VulkanDevice *device, glm::mat4 matrix) {
    this->device = device;
    this->uniformBlock.matrix = matrix;
    VK_CHECK_RESULT(device->createBuffer(
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            sizeof(uniformBlock),
            &uniformBuffer.buffer,
            &uniformBuffer.memory,
            &uniformBlock));
    VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, uniformBuffer.memory, 0, sizeof(uniformBlock), 0, &uniformBuffer.mapped));
    uniformBuffer.descriptor = { uniformBuffer.buffer, 0, sizeof(uniformBlock) };
};

Geometry::~Geometry() {
    vkDestroyBuffer(device->logicalDevice, uniformBuffer.buffer, nullptr);
    vkFreeMemory(device->logicalDevice, uniformBuffer.memory, nullptr);
    for(auto* mesh : meshes)
    {
        delete mesh;
    }
}

glm::mat4 Node::localMatrix() {
    return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), scale) * matrix;
}

glm::mat4 Node::getMatrix() {
    glm::mat4 m = localMatrix();
    Node *p = parent;
    while (p) {
        m = p->localMatrix() * m;
        p = p->parent;
    }
    return m;
}

void Node::update() {
    if (geo) {
        glm::mat4 m = getMatrix();
        if (skin) {
            geo->uniformBlock.matrix = m;
            // Update join matrices
            for (size_t i = 0; i < skin->joints.size(); i++) {
                Node *jointNode = skin->joints[i];
                // embedded node model matrix in jointMatrix
                glm::mat4 jointMat = jointNode->getMatrix() * skin->inverseBindMatrices[i];
                geo->uniformBlock.jointMatrix[i] = jointMat;
            }
            geo->uniformBlock.jointcount = (float)skin->joints.size();
            memcpy(geo->uniformBuffer.mapped, &geo->uniformBlock, sizeof(geo->uniformBlock));
        } else {
            memcpy(geo->uniformBuffer.mapped, &m, sizeof(glm::mat4));
        }
    }

    for (auto& child : children) {
        child->update();
    }
}

Node::~Node() {
    if (geo) {
        delete geo;
    }
    for (auto& child : children) {
        delete child;
    }
}

VkVertexInputBindingDescription Vertex::vertexInputBindingDescription;
std::vector<VkVertexInputAttributeDescription> Vertex::vertexInputAttributeDescriptions;
VkPipelineVertexInputStateCreateInfo Vertex::pipelineVertexInputStateCreateInfo;

VkVertexInputBindingDescription Vertex::inputBindingDescription(uint32_t binding) {
    return VkVertexInputBindingDescription({ binding, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX });
}

VkVertexInputAttributeDescription Vertex::inputAttributeDescription(uint32_t binding, uint32_t location, VertexComponent component) {
    switch (component) {
        case VertexComponent::Position:
            return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos) });
        case VertexComponent::Normal:
            return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) });
        case VertexComponent::UV:
            return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) });
        case VertexComponent::Color:
            return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color) });
        case VertexComponent::Tangent:
            return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)} );
        case VertexComponent::Joint0:
            return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, joint0) });
        case VertexComponent::Weight0:
            return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weight0) });
        default:
            return VkVertexInputAttributeDescription({});
    }
}

std::vector<VkVertexInputAttributeDescription> Vertex::inputAttributeDescriptions(uint32_t binding, const std::vector<VertexComponent> components) {
    std::vector<VkVertexInputAttributeDescription> result;
    uint32_t location = 0;
    for (VertexComponent component : components) {
        result.push_back(Vertex::inputAttributeDescription(binding, location, component));
        location++;
    }
    return result;
}

/** @brief Returns the default pipeline vertex input state create info structure for the requested vertex components */
VkPipelineVertexInputStateCreateInfo* Vertex::getPipelineVertexInputState(const std::vector<VertexComponent> components) {
    vertexInputBindingDescription = Vertex::inputBindingDescription(0);
    Vertex::vertexInputAttributeDescriptions = Vertex::inputAttributeDescriptions(0, components);
    pipelineVertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    pipelineVertexInputStateCreateInfo.vertexBindingDescriptionCount = 1;
    pipelineVertexInputStateCreateInfo.pVertexBindingDescriptions = &Vertex::vertexInputBindingDescription;
    pipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(Vertex::vertexInputAttributeDescriptions.size());
    pipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions = Vertex::vertexInputAttributeDescriptions.data();
    return &pipelineVertexInputStateCreateInfo;
}

Texture* Model::getTexture(uint32_t index)
{

    if (index < textures.size()) {
        return textures[index];
    }
    return nullptr;
}

void Model::createEmptyTexture(VkQueue transferQueue)
{
    emptyTexture.device = device;
    emptyTexture.width = 1;
    emptyTexture.height = 1;
    emptyTexture.layerCount = 1;
    emptyTexture.mipLevels = 1;

    size_t bufferSize = emptyTexture.width * emptyTexture.height * 4;
    unsigned char* buffer = new unsigned char[bufferSize];
    memset(buffer, 0, bufferSize);

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkBufferCreateInfo bufferCreateInfo = initializers::bufferCreateInfo();
    bufferCreateInfo.size = bufferSize;
    // This buffer is used as a transfer source for the buffer copy
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

    VkMemoryAllocateInfo memAllocInfo = initializers::memoryAllocateInfo();
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
    VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

    // Copy texture data into staging buffer
    uint8_t* data;
    VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
    memcpy(data, buffer, bufferSize);
    vkUnmapMemory(device->logicalDevice, stagingMemory);

    VkBufferImageCopy bufferCopyRegion = {};
    bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bufferCopyRegion.imageSubresource.layerCount = 1;
    bufferCopyRegion.imageExtent.width = emptyTexture.width;
    bufferCopyRegion.imageExtent.height = emptyTexture.height;
    bufferCopyRegion.imageExtent.depth = 1;

    // Create optimal tiled target image
    VkImageCreateInfo imageCreateInfo = initializers::imageCreateInfo();
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.extent = { emptyTexture.width, emptyTexture.height, 1 };
    imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &emptyTexture.image));

    vkGetImageMemoryRequirements(device->logicalDevice, emptyTexture.image, &memReqs);
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &emptyTexture.deviceMemory));
    VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, emptyTexture.image, emptyTexture.deviceMemory, 0));

    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.layerCount = 1;

    VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    tools::setImageLayout(copyCmd, emptyTexture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
    vkCmdCopyBufferToImage(copyCmd, stagingBuffer, emptyTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);
    tools::setImageLayout(copyCmd, emptyTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
    device->flushCommandBuffer(copyCmd, transferQueue);
    emptyTexture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Clean up staging resources
    vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
    vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

    VkSamplerCreateInfo samplerCreateInfo = initializers::samplerCreateInfo();
    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
    samplerCreateInfo.maxAnisotropy = 1.0f;
    VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr, &emptyTexture.sampler));

    VkImageViewCreateInfo viewCreateInfo = initializers::imageViewCreateInfo();
    viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    viewCreateInfo.subresourceRange.levelCount = 1;
    viewCreateInfo.image = emptyTexture.image;
    VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr, &emptyTexture.view));

    emptyTexture.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    emptyTexture.descriptor.imageView = emptyTexture.view;
    emptyTexture.descriptor.sampler = emptyTexture.sampler;
}

/*
	glTF model loading and rendering class
*/
Model::~Model()
{
    vkDestroyBuffer(device->logicalDevice, vertices.buffer, nullptr);
    vkFreeMemory(device->logicalDevice, vertices.memory, nullptr);
    vkDestroyBuffer(device->logicalDevice, indices.buffer, nullptr);
    vkFreeMemory(device->logicalDevice, indices.memory, nullptr);
    for (auto texture : textures) {
        texture->destroy();
    }
    for (auto node : nodes) {
        delete node;
    }
    for (auto skin : skins) {
        delete skin;
    }
    if (descriptorSetLayoutUbo != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device->logicalDevice, descriptorSetLayoutUbo, nullptr);
        descriptorSetLayoutUbo = VK_NULL_HANDLE;
    }
    if (descriptorSetLayoutImage != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device->logicalDevice, descriptorSetLayoutImage, nullptr);
        descriptorSetLayoutImage = VK_NULL_HANDLE;
    }
    vkDestroyDescriptorPool(device->logicalDevice, descriptorPool, nullptr);
    emptyTexture.destroy();
}

void Model::loadNode(const aiScene* scene, aiNode* node, Node* parent)
{
    auto* mNode = new Node{};

    mNode->parent = parent;
    mNode->matrix = glm::transpose(glm::make_mat4x4(&node->mTransformation.a1));

    // Node with children
    for (int m = 0; m < node->mNumChildren; m++) {
        loadNode(scene, node->mChildren[m], mNode);
    }

    const auto localMatrix = mNode->getMatrix();
    // Node contains mesh data
    if (node->mNumMeshes > 0) {
        auto* geo = new Geometry(device, mNode->matrix);

        for (int k = 0; k < node->mNumMeshes; k++) {
            auto* mesh = scene->mMeshes[node->mMeshes[k]];

            auto indexStart = static_cast<uint32_t>(indexBuffer.size());
            auto vertexStart = static_cast<uint32_t>(vertexBuffer.size());
            uint32_t indexCount = 0;
            uint32_t vertexCount = 0;

            glm::vec3 posMin {};
            glm::vec3 posMax {};

            // mesh indices
            {
                auto indexOffset = static_cast<uint32_t>(indexBuffer.size());

                for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
                    const auto& face = mesh->mFaces[i];
                    for (unsigned int j = 0; j < face.mNumIndices; j++) {
                        indexBuffer.emplace_back(face.mIndices[j] + indexOffset);
                        indexCount++;
                    }
                }
            }

            // mesh vertices
            {
                vertexCount = static_cast<uint32_t>(mesh->mNumVertices);

                for (uint32_t i = 0; i < vertexCount; i++) {
                    Vertex vert {};
                    glm::vec3 vector;

                    // pos
                    vector.x = mesh->mVertices[i].x;
                    vector.y = mesh->mVertices[i].y;
                    vector.z = mesh->mVertices[i].z;

                    vert.pos = glm::vec4(vector, 1.0f);

                    // normal
                    if (mesh->HasNormals()) {
                        vector.x = mesh->mNormals[i].x;
                        vector.y = mesh->mNormals[i].y;
                        vector.z = mesh->mNormals[i].z;
                        vert.normal = vector;
                    }

                    if (mesh->mTextureCoords[0]) {
                        vert.uv.x = mesh->mTextureCoords[0][i].x;
                        vert.uv.y = mesh->mTextureCoords[0][i].y;
                    }

                    if (mesh->HasVertexColors(0)) {
                        vert.color.r = mesh->mColors[0][i].r;
                        vert.color.g = mesh->mColors[0][i].g;
                        vert.color.b = mesh->mColors[0][i].b;
                        vert.color.a = mesh->mColors[0][i].a;
                    } else {
                        vert.color = glm::vec4(1.0f);
                    }

                    vertexBuffer.emplace_back(vert);
                }
            }

            auto* mMesh = new Mesh(indexStart, indexCount, vertexStart, vertexCount, materials[mesh->mMaterialIndex]);
            mMesh->setDimensions(
                glm::make_vec3(&mesh->mAABB.mMin.x),
                glm::make_vec3(&mesh->mAABB.mMax.x)
            );
        }

        mNode->geo = geo;
    }

    linearNodes.emplace_back(mNode);
    parent->children.emplace_back(mNode);
}

//void Model::loadSkins(const aiScene* scene)
//{
//    for (tinygltf::Skin &source : gltfModel.skins) {
//        Skin *newSkin = new Skin{};
//        newSkin->name = source.name;
//
//        // Find skeleton root node
//        if (source.skeleton > -1) {
//            newSkin->skeletonRoot = nodeFromIndex(source.skeleton);
//        }
//
//        // Find joint nodes
//        for (int jointIndex : source.joints) {
//            Node* node = nodeFromIndex(jointIndex);
//            if (node) {
//                newSkin->joints.push_back(nodeFromIndex(jointIndex));
//            }
//        }
//
//        // Get inverse bind matrices from buffer
//        if (source.inverseBindMatrices > -1) {
//            const tinygltf::Accessor &accessor = gltfModel.accessors[source.inverseBindMatrices];
//            const tinygltf::BufferView &bufferView = gltfModel.bufferViews[accessor.bufferView];
//            const tinygltf::Buffer &buffer = gltfModel.buffers[bufferView.buffer];
//            newSkin->inverseBindMatrices.resize(accessor.count);
//            memcpy(newSkin->inverseBindMatrices.data(), &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::mat4));
//        }
//
//        skins.push_back(newSkin);
//    }
//}

Texture* Model::loadMaterialTexture(const aiScene* scene, const aiMaterial* mat, aiTextureType type, VkQueue transferQueue) const
{
    if (mat->GetTextureCount(type) == 0) {
        return nullptr;
    }

    // though a material can has multiple textures of one type, here we get the first texture of each type
    aiString fileName;
    mat->GetTexture(type, 0, &fileName);

    auto* tex = new Texture();
    tex->load(scene, std::string(fileName.C_Str()), path, device, transferQueue);

    return tex;
}

void Model::loadMaterials(const aiScene *scene, VkQueue transferQueue)
{
    // Create an empty texture to be used for empty material images
    createEmptyTexture(transferQueue);

    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        auto* material = scene->mMaterials[i];

        auto* mMaterial = new Material(device, &emptyTexture);
        mMaterial->baseColorTexture = loadMaterialTexture(scene, material, aiTextureType_DIFFUSE, transferQueue);
        mMaterial->normalTexture = loadMaterialTexture(scene, material, aiTextureType_NORMALS, transferQueue);

        materials.emplace_back(std::move(mMaterial));
    }
}

//void Model::loadAnimations(tinygltf::Model &gltfModel)
//{
//    for (tinygltf::Animation &anim : gltfModel.animations) {
//        Animation animation{};
//        animation.name = anim.name;
//        if (anim.name.empty()) {
//            animation.name = std::to_string(animations.size());
//        }
//
//        // Samplers
//        for (auto &samp : anim.samplers) {
//            AnimationSampler sampler{};
//
//            if (samp.interpolation == "LINEAR") {
//                sampler.interpolation = AnimationSampler::InterpolationType::LINEAR;
//            }
//            if (samp.interpolation == "STEP") {
//                sampler.interpolation = AnimationSampler::InterpolationType::STEP;
//            }
//            if (samp.interpolation == "CUBICSPLINE") {
//                sampler.interpolation = AnimationSampler::InterpolationType::CUBICSPLINE;
//            }
//
//            // Read sampler input time values
//            {
//                const tinygltf::Accessor &accessor = gltfModel.accessors[samp.input];
//                const tinygltf::BufferView &bufferView = gltfModel.bufferViews[accessor.bufferView];
//                const tinygltf::Buffer &buffer = gltfModel.buffers[bufferView.buffer];
//
//                assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
//
//                float *buf = new float[accessor.count];
//                memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(float));
//                for (size_t index = 0; index < accessor.count; index++) {
//                    sampler.inputs.push_back(buf[index]);
//                }
//                delete[] buf;
//                for (auto input : sampler.inputs) {
//                    if (input < animation.start) {
//                        animation.start = input;
//                    };
//                    if (input > animation.end) {
//                        animation.end = input;
//                    }
//                }
//            }
//
//            // Read sampler output T/R/S values
//            {
//                const tinygltf::Accessor &accessor = gltfModel.accessors[samp.output];
//                const tinygltf::BufferView &bufferView = gltfModel.bufferViews[accessor.bufferView];
//                const tinygltf::Buffer &buffer = gltfModel.buffers[bufferView.buffer];
//
//                assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
//
//                switch (accessor.type) {
//                    case TINYGLTF_TYPE_VEC3: {
//                        glm::vec3 *buf = new glm::vec3[accessor.count];
//                        memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::vec3));
//                        for (size_t index = 0; index < accessor.count; index++) {
//                            sampler.outputsVec4.push_back(glm::vec4(buf[index], 0.0f));
//                        }
//                        delete[] buf;
//                        break;
//                    }
//                    case TINYGLTF_TYPE_VEC4: {
//                        glm::vec4 *buf = new glm::vec4[accessor.count];
//                        memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::vec4));
//                        for (size_t index = 0; index < accessor.count; index++) {
//                            sampler.outputsVec4.push_back(buf[index]);
//                        }
//                        delete[] buf;
//                        break;
//                    }
//                    default: {
//                        std::cout << "unknown type" << std::endl;
//                        break;
//                    }
//                }
//            }
//
//            animation.samplers.push_back(sampler);
//        }
//
//        // Channels
//        for (auto &source: anim.channels) {
//            AnimationChannel channel{};
//
//            if (source.target_path == "rotation") {
//                channel.path = AnimationChannel::PathType::ROTATION;
//            }
//            if (source.target_path == "translation") {
//                channel.path = AnimationChannel::PathType::TRANSLATION;
//            }
//            if (source.target_path == "scale") {
//                channel.path = AnimationChannel::PathType::SCALE;
//            }
//            if (source.target_path == "weights") {
//                std::cout << "weights not yet supported, skipping channel" << std::endl;
//                continue;
//            }
//            channel.samplerIndex = source.sampler;
//            channel.node = nodeFromIndex(source.target_node);
//            if (!channel.node) {
//                continue;
//            }
//
//            animation.channels.push_back(channel);
//        }
//
//        animations.push_back(animation);
//    }
//}

void Model::loadFromFile(std::string filename, VulkanDevice *device, VkQueue transferQueue, uint32_t fileLoadingFlags)
{
    Assimp::Importer importer;
    const auto* scene = importer.ReadFile(filename, aiProcess_Triangulate);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "failed to load model file: " << path << std::endl;
        exit(-1);
    }

    assert(scene->HasMaterials() && scene->HasMeshes());

    size_t pos = filename.find_last_of('/');
    path = filename.substr(0, pos);

    this->device = device;

    loadMaterials(scene, transferQueue);

    rootNode = new Node();
    for (int i = 0; i < scene->mRootNode->mNumChildren; i++) {
        loadNode(scene, scene->mRootNode->mChildren[i], rootNode);
    }

//    if (gltfModel.animations.size() > 0) {
//        loadAnimations(gltfModel);
//    }
//    loadSkins(gltfModel);

//    for (auto node : linearNodes) {
//        // Assign skins
//        if (node->skinIndex > -1) {
//            node->skin = skins[node->skinIndex];
//        }
//        // Initial pose
//        if (node->mesh) {
//            node->update();
//        }
//    }


    // Pre-Calculations for requested features
    if ((fileLoadingFlags & FileLoadingFlags::PreTransformVertices) || (fileLoadingFlags & FileLoadingFlags::PreMultiplyVertexColors) || (fileLoadingFlags & FileLoadingFlags::FlipY)) {
        const bool preTransform = fileLoadingFlags & FileLoadingFlags::PreTransformVertices;
        const bool preMultiplyColor = fileLoadingFlags & FileLoadingFlags::PreMultiplyVertexColors;
        const bool flipY = fileLoadingFlags & FileLoadingFlags::FlipY;
        for (Node* node : linearNodes) {
            if (node->geo) {
                const glm::mat4 localMatrix = node->getMatrix();
                for (auto* mesh : node->geo->meshes) {
                    for (uint32_t i = 0; i < mesh->vertexCount; i++) {
                        Vertex& vertex = vertexBuffer[mesh->firstVertex + i];
                        // Pre-transform vertex positions by node-hierarchy
                        if (preTransform) {
                            vertex.pos = localMatrix * vertex.pos;
                            vertex.normal = glm::normalize(glm::mat3(localMatrix) * vertex.normal);
                        }
                        // Flip Y-Axis of vertex positions
                        if (flipY) {
                            vertex.pos.y *= -1.0f;
                            vertex.normal.y *= -1.0f;
                        }
                        // Pre-Multiply vertex colors with material base color
                        if (preMultiplyColor) {
                            vertex.color = mesh->material->baseColorFactor * vertex.color;
                        }
                    }
                }
            }
        }
    }

    size_t vertexBufferSize = vertexBuffer.size() * sizeof(Vertex);
    size_t indexBufferSize = indexBuffer.size() * sizeof(uint32_t);
    indices.count = static_cast<uint32_t>(indexBuffer.size());
    vertices.count = static_cast<uint32_t>(vertexBuffer.size());

    assert((vertexBufferSize > 0) && (indexBufferSize > 0));

    struct StagingBuffer {
        VkBuffer buffer;
        VkDeviceMemory memory;
    } vertexStaging, indexStaging;

    // Create staging buffers
    // Vertex data
    VK_CHECK_RESULT(device->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vertexBufferSize,
            &vertexStaging.buffer,
            &vertexStaging.memory,
            vertexBuffer.data()));
    // Index data
    VK_CHECK_RESULT(device->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            indexBufferSize,
            &indexStaging.buffer,
            &indexStaging.memory,
            indexBuffer.data()));

    // Create device local buffers
    // Vertex buffer
    VK_CHECK_RESULT(device->createBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | memoryPropertyFlags,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            vertexBufferSize,
            &vertices.buffer,
            &vertices.memory));
    // Index buffer
    VK_CHECK_RESULT(device->createBuffer(
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | memoryPropertyFlags,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            indexBufferSize,
            &indices.buffer,
            &indices.memory));

    // Copy from staging buffers
    VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

    VkBufferCopy copyRegion = {};

    copyRegion.size = vertexBufferSize;
    vkCmdCopyBuffer(copyCmd, vertexStaging.buffer, vertices.buffer, 1, &copyRegion);

    copyRegion.size = indexBufferSize;
    vkCmdCopyBuffer(copyCmd, indexStaging.buffer, indices.buffer, 1, &copyRegion);

    device->flushCommandBuffer(copyCmd, transferQueue, true);

    vkDestroyBuffer(device->logicalDevice, vertexStaging.buffer, nullptr);
    vkFreeMemory(device->logicalDevice, vertexStaging.memory, nullptr);
    vkDestroyBuffer(device->logicalDevice, indexStaging.buffer, nullptr);
    vkFreeMemory(device->logicalDevice, indexStaging.memory, nullptr);

    getSceneDimensions();

    // Setup descriptors
    uint32_t uboCount{ 0 };
    uint32_t imageCount{ 0 };
    for (auto node : linearNodes) {
        if (node->geo) {
            uboCount++;
        }
    }
    for (auto material : materials) {
        if (material->baseColorTexture != nullptr) {
            imageCount++;
        }
    }
    std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uboCount },
    };
    if (imageCount > 0) {
        if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
            poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageCount });
        }
        if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
            poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageCount });
        }
        if (descriptorBindingFlags & DescriptorBindingFlags::ImagePbr) {
            poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageCount * 2 });
        }
    }
    VkDescriptorPoolCreateInfo descriptorPoolCI{};
    descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolCI.pPoolSizes = poolSizes.data();
    descriptorPoolCI.maxSets = uboCount + imageCount;
    VK_CHECK_RESULT(vkCreateDescriptorPool(device->logicalDevice, &descriptorPoolCI, nullptr, &descriptorPool));

    // Descriptors for per-node uniform buffers
    {
        // Layout is global, so only create if it hasn't already been created before
        if (descriptorSetLayoutUbo == VK_NULL_HANDLE) {
            std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
                    initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
            };
            VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{};
            descriptorLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
            descriptorLayoutCI.pBindings = setLayoutBindings.data();
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->logicalDevice, &descriptorLayoutCI, nullptr, &descriptorSetLayoutUbo));
        }
        for (auto node : nodes) {
            prepareNodeDescriptor(node, descriptorSetLayoutUbo);
        }
    }

    // Descriptors for per-material images
    {
        // Layout is global, so only create if it hasn't already been created before
        if (descriptorSetLayoutImage == VK_NULL_HANDLE) {
            std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
            if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
                // albedo / diffuse map, binding = 0
                setLayoutBindings.push_back(initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, static_cast<uint32_t>(setLayoutBindings.size())));
            }
            if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
                // normal map, binding = 1
                setLayoutBindings.push_back(initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, static_cast<uint32_t>(setLayoutBindings.size())));
            }
            if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
                // ao map, binding = 2
                setLayoutBindings.push_back(initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, static_cast<uint32_t>(setLayoutBindings.size())));
                // metallicRoughness map, binding = 3
                setLayoutBindings.push_back(initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, static_cast<uint32_t>(setLayoutBindings.size())));
            }
            VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{};
            descriptorLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
            descriptorLayoutCI.pBindings = setLayoutBindings.data();
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->logicalDevice, &descriptorLayoutCI, nullptr, &descriptorSetLayoutImage));
        }
        for (auto& material : materials) {
            if (material->baseColorTexture != nullptr) {
                material->createDescriptorSet(descriptorPool, descriptorSetLayoutImage, descriptorBindingFlags);
            }
        }
    }
}

void Model::bindBuffers(VkCommandBuffer commandBuffer)
{
    const VkDeviceSize offsets[1] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    buffersBound = true;
}

void Model::drawNode(Node *node, VkCommandBuffer commandBuffer, uint32_t renderFlags, VkPipelineLayout pipelineLayout, uint32_t bindImageSet)
{
    if (node->geo) {
        if (renderFlags & RenderFlags::RenderAnimation) {
            // descriptorset of jointMatrices put in set 2
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 2, 1, &node->geo->uniformBuffer.descriptorSet, 0, nullptr);
        }

        for (auto* mesh : node->geo->meshes) {
            bool skip = false;
            const auto* material = mesh->material;
            if (renderFlags & RenderFlags::RenderOpaqueNodes) {
                skip = (material->alphaMode != Material::ALPHAMODE_OPAQUE);
            }
            if (renderFlags & RenderFlags::RenderAlphaMaskedNodes) {
                skip = (material->alphaMode != Material::ALPHAMODE_MASK);
            }
            if (renderFlags & RenderFlags::RenderAlphaBlendedNodes) {
                skip = (material->alphaMode != Material::ALPHAMODE_BLEND);
            }
            if (!skip) {
                if (renderFlags & RenderFlags::BindImages) {
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, bindImageSet, 1, &material->descriptorSet, 0, nullptr);
                }

                vkCmdDrawIndexed(commandBuffer, mesh->indexCount, 1, mesh->firstIndex, 0, 0);
            }
        }
    }
    for (auto& child : node->children) {
        drawNode(child, commandBuffer, renderFlags, pipelineLayout, bindImageSet);
    }
}

void Model::draw(VkCommandBuffer commandBuffer, uint32_t renderFlags, VkPipelineLayout pipelineLayout, uint32_t bindImageSet)
{
    if (!buffersBound) {
        const VkDeviceSize offsets[1] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
        vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    }
    for (auto& node : nodes) {
        drawNode(node, commandBuffer, renderFlags, pipelineLayout, bindImageSet);
    }
}

void Model::getNodeDimensions(Node *node, glm::vec3 &min, glm::vec3 &max)
{
    if (node->geo) {
        for (auto* mesh : node->geo->meshes) {
            glm::vec4 locMin = glm::vec4(mesh->dimensions.min, 1.0f) * node->getMatrix();
            glm::vec4 locMax = glm::vec4(mesh->dimensions.max, 1.0f) * node->getMatrix();
            if (locMin.x < min.x) { min.x = locMin.x; }
            if (locMin.y < min.y) { min.y = locMin.y; }
            if (locMin.z < min.z) { min.z = locMin.z; }
            if (locMax.x > max.x) { max.x = locMax.x; }
            if (locMax.y > max.y) { max.y = locMax.y; }
            if (locMax.z > max.z) { max.z = locMax.z; }
        }
    }
    for (auto child : node->children) {
        getNodeDimensions(child, min, max);
    }
}

void Model::getSceneDimensions()
{
    dimensions.min = glm::vec3(FLT_MAX);
    dimensions.max = glm::vec3(-FLT_MAX);
    for (auto node : nodes) {
        getNodeDimensions(node, dimensions.min, dimensions.max);
    }
    dimensions.size = dimensions.max - dimensions.min;
    dimensions.center = (dimensions.min + dimensions.max) / 2.0f;
    dimensions.radius = glm::distance(dimensions.min, dimensions.max) / 2.0f;
}

void Model::updateAnimation(uint32_t index, float inTime)
{
    if (index > static_cast<uint32_t>(animations.size()) - 1) {
        std::cout << "No animation with index " << index << std::endl;
        return;
    }
    auto* animation = animations[index];

    bool updated = false;
    for (auto& channel : animation->channels) {
        AnimationSampler &sampler = animation->samplers[channel.samplerIndex];
        if (sampler.inputs.size() > sampler.outputsVec4.size()) {
            continue;
        }

        float time = fmod(inTime, sampler.inputs.back());

        for (auto i = 0; i < sampler.inputs.size() - 1; i++) {
            if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1])) {
                float u = std::max(0.0f, time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);
                if (u <= 1.0f) {
                    switch (channel.path) {
                        case AnimationChannel::PathType::TRANSLATION: {
                            glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
                            channel.node->translation = glm::vec3(trans);
                            break;
                        }
                        case AnimationChannel::PathType::SCALE: {
                            glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
                            channel.node->scale = glm::vec3(trans);
                            break;
                        }
                        case AnimationChannel::PathType::ROTATION: {
                            glm::quat q1;
                            q1.x = sampler.outputsVec4[i].x;
                            q1.y = sampler.outputsVec4[i].y;
                            q1.z = sampler.outputsVec4[i].z;
                            q1.w = sampler.outputsVec4[i].w;
                            glm::quat q2;
                            q2.x = sampler.outputsVec4[i + 1].x;
                            q2.y = sampler.outputsVec4[i + 1].y;
                            q2.z = sampler.outputsVec4[i + 1].z;
                            q2.w = sampler.outputsVec4[i + 1].w;
                            channel.node->rotation = glm::normalize(glm::slerp(q1, q2, u));
                            break;
                        }
                    }
                    updated = true;
                }
            }
        }
    }
    if (updated) {
        for (auto &node : nodes) {
            node->update();
        }
    }
}

/*
	Helper functions
*/
Node* Model::findNode(Node *parent, uint32_t index) {
    Node* nodeFound = nullptr;
    if (parent->index == index) {
        return parent;
    }
    for (auto& child : parent->children) {
        nodeFound = findNode(child, index);
        if (nodeFound) {
            break;
        }
    }
    return nodeFound;
}

Node* Model::nodeFromIndex(uint32_t index) {
    Node* nodeFound = nullptr;
    for (auto &node : nodes) {
        nodeFound = findNode(node, index);
        if (nodeFound) {
            break;
        }
    }
    return nodeFound;
}

void Model::prepareNodeDescriptor(Node* node, VkDescriptorSetLayout descriptorSetLayout) {
    if (node->geo) {
        VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
        descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocInfo.descriptorPool = descriptorPool;
        descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
        descriptorSetAllocInfo.descriptorSetCount = 1;
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &node->geo->uniformBuffer.descriptorSet));

        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.dstSet = node->geo->uniformBuffer.descriptorSet;
        writeDescriptorSet.dstBinding = 0;
        writeDescriptorSet.pBufferInfo = &node->geo->uniformBuffer.descriptor;

        vkUpdateDescriptorSets(device->logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
    }
    for (auto& child : node->children) {
        prepareNodeDescriptor(child, descriptorSetLayout);
    }
}
