//
// Created by Junkang on 2022/11/18.
//

#ifndef LEARN_VULKAN_VULKANOBJMODEL_H
#define LEARN_VULKAN_VULKANOBJMODEL_H

#include <vector>
#include "VulkanDevice.h"
#include "VulkanTexture.h"

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

struct BufferSection {
    VkBuffer buffer = {};
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;

    BufferSection() = default;

    BufferSection(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size)
        : buffer(buffer),
          offset(offset),
          size(size)
    {}
};

struct MeshPart {
    BufferSection vertex_buffer_section = {};
    BufferSection index_buffer_section = {};
    BufferSection material_uniform_buffer_section = {};
    size_t index_count = 0;
    VkDescriptorSet material_descriptor_set = {};

    Texture2D* albedo_map = nullptr;
    Texture2D* normal_map = nullptr;

    MeshPart(const BufferSection& vertex_buffer_section, const BufferSection& index_buffer_section, size_t index_count)
        : vertex_buffer_section(vertex_buffer_section)
        , index_buffer_section(index_buffer_section)
        , index_count(index_count)
    {}

    void destroy() {
        if (albedo_map) {
            albedo_map->destroy();
            delete albedo_map;
        }
        if (normal_map) {
            normal_map->destroy();
            delete normal_map;
        }
    }
};

template <class T>
void hash_combine(std::size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

struct Vertex
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 tex_coord;
    glm::vec3 normal;

    using index_t = uint32_t;

    bool operator==(const Vertex& other) const noexcept
    {
        return pos == other.pos && color == other.color && tex_coord == other.tex_coord && normal == other.normal;
    }

    size_t hash() const
    {
        size_t seed = 0;
        hash_combine(seed, pos);
        hash_combine(seed, color);
        hash_combine(seed, tex_coord);
        hash_combine(seed, normal);
        return seed;
    }

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions(4);

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, tex_coord);

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex, normal);

        return attributeDescriptions;
    }
};


class ObjModel {
public:
    ObjModel() = default;
    ~ObjModel() = default;

    VkDescriptorSetLayout GetDescriptorSetLayout() const {
        return material_descriptor_set_layout;
    }

    void LoadModelFromFile(std::string filename, VulkanDevice* device, VkQueue transferQueue);
    void Draw(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout);

    const std::vector<MeshPart>& GetMeshParts()
    {
        return mesh_parts;
    }

private:
    VkBuffer buffer;
    VkDeviceMemory buffer_memory;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkDeviceMemory> image_memories;
    VkBuffer uniform_buffer;
    VkDeviceMemory uniform_buffer_memory;
    VkDescriptorPool descriptorPool;
    VkDescriptorSetLayout material_descriptor_set_layout = VK_NULL_HANDLE;

    std::vector<MeshPart> mesh_parts;
    std::vector<Texture2D> textures;
};

#endif //LEARN_VULKAN_VULKANOBJMODEL_H
