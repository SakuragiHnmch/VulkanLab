//
// Created by Junkang on 2022/11/18.
//

#include "VulkanObjModel.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <unordered_map>

namespace std {
    // hash function for Vertex
    template<> struct hash<Vertex>
    {
        size_t operator()(Vertex const& vertex) const
        {
            return vertex.hash();
        }
    };
}

// uniform buffer object for model transformation
struct MaterialUbo
{
    int has_albedo_map;
    int has_normal_map;
};

struct MeshMaterialGroup // grouped by material
{
    std::vector<Vertex> vertices = {};
    std::vector<Vertex::index_t> vertex_indices = {};

    std::string albedo_map_path = "";
    std::string normal_map_path = "";
};

std::vector<MeshMaterialGroup> LoadModel(const std::string path)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;

    std::string folder = path.substr(0, path.find_last_of("/\\")) + "/";
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, path.c_str(), folder.c_str())) {
        throw std::runtime_error(err);
    }

    assert(attrib.normals.size() > 0);

    // Group parts of the same material together, +1 for unknown material
    std::vector<MeshMaterialGroup> groups(materials.size() + 1);

    for (size_t i = 0; i < materials.size(); i++)
    {
        if (materials[i].diffuse_texname != "")
        {
            groups[i + 1].albedo_map_path = folder + materials[i].diffuse_texname;
        }
        if (materials[i].normal_texname != "")
        {
            groups[i + 1].normal_map_path = folder + materials[i].normal_texname;
        }
        else if (materials[i].bump_texname != "")
        {
            // CryEngine sponza scene uses keyword "bump" to store normal
            groups[i + 1].normal_map_path = folder + materials[i].bump_texname;
        }
    }

    std::vector<std::unordered_map<Vertex, size_t>> unique_vertices_per_group(materials.size() + 1);

    // 逐顶点遍历，构建vertexBuffer和 indexBuffer
    auto appendVertex = [&unique_vertices_per_group, &groups](const Vertex& vertex, int material_id) {
        auto& unique_vertices = unique_vertices_per_group[material_id + 1]; // 顶点与其索引的键值对
        auto& group = groups[material_id + 1];
        if (unique_vertices.count(vertex) == 0) {
            unique_vertices[vertex] = group.vertices.size(); // auto incrementing size;
            group.vertices.push_back(vertex);
        }
        group.vertex_indices.push_back(static_cast<Vertex::index_t>(unique_vertices[vertex])); // vertex_indices即为indexBuffer的值
    };

    for (const auto& shape : shapes)
    {
        size_t indexOffset = 0;
        for (size_t n = 0; n < shape.mesh.num_face_vertices.size(); n++)
        {
            // per face
            auto ngon = shape.mesh.num_face_vertices[n];
            auto material_id = shape.mesh.material_ids[n];
            for (size_t f = 0; f < ngon; f++)
            {
                const auto& index = shape.mesh.indices[indexOffset + f];

                Vertex vertex;

                vertex.pos = {
                        attrib.vertices[3 * index.vertex_index + 0],
                        attrib.vertices[3 * index.vertex_index + 1],
                        attrib.vertices[3 * index.vertex_index + 2]
                };

                vertex.tex_coord = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };

                vertex.normal = {
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2]
                };

                appendVertex(vertex, material_id);

            }
            indexOffset += ngon;
        }
    }

    return groups;
}

/**
 *  Load Obj model and allocate vulkan resources
 */
void ObjModel::LoadModelFromFile(std::string filename, VulkanDevice* device, VkQueue transferQueue)
{
    auto groups = LoadModel(filename);

    VkDeviceSize buffer_size = 0;
    for (const auto& group : groups) {
        if (group.vertex_indices.size() <= 0) {
            continue;
        }
        VkDeviceSize vertex_section_size = sizeof(group.vertices[0]) * group.vertices.size();
        VkDeviceSize index_section_size = sizeof(group.vertex_indices[0]) * group.vertex_indices.size();
        buffer_size += vertex_section_size;
        buffer_size += index_section_size;
    }

    // Create a large device local buffer
    VK_CHECK_RESULT(device->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            buffer_size,
            &buffer, &buffer_memory
            ));

    // Upload buffers using staging buffer
    VkDeviceSize current_offset = 0;
    for (auto& group : groups) {
        if (group.vertex_indices.size() <= 0) {
            continue;
        }

        VkDeviceSize vertex_section_size = sizeof(group.vertices[0]) * group.vertices.size();
        VkDeviceSize index_section_size = sizeof(group.vertex_indices[0]) * group.vertex_indices.size();

        // copy vertex data
        BufferSection vertex_buffer_section = {buffer, current_offset, vertex_section_size};
        {
            VkBuffer staging_buffer;
            VkDeviceMemory staging_buffer_memory;
            VK_CHECK_RESULT(device->createBuffer(
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    vertex_section_size,
                    &staging_buffer, &staging_buffer_memory, group.vertices.data()
                    ));

            VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
            VkBufferCopy copyRegion = {};
            copyRegion.size = vertex_section_size;
            copyRegion.dstOffset = current_offset;
            vkCmdCopyBuffer(copyCmd, staging_buffer, buffer, 1, &copyRegion);
            device->flushCommandBuffer(copyCmd, transferQueue, true);

            vkDestroyBuffer(device->logicalDevice, staging_buffer, nullptr);
            vkFreeMemory(device->logicalDevice, staging_buffer_memory, nullptr);

            current_offset += vertex_section_size;
        }

        // copy index data
        BufferSection index_buffer_section = { buffer, current_offset, index_section_size };
        {
            VkBuffer staging_buffer;
            VkDeviceMemory staging_buffer_memory;
            VK_CHECK_RESULT(device->createBuffer(
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    index_section_size,
                    &staging_buffer, &staging_buffer_memory, group.vertex_indices.data()
            ));

            VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
            VkBufferCopy copyRegion = {};
            copyRegion.size = index_section_size;
            copyRegion.dstOffset = current_offset;
            vkCmdCopyBuffer(copyCmd, staging_buffer, buffer, 1, &copyRegion);
            device->flushCommandBuffer(copyCmd, transferQueue, true);

            vkDestroyBuffer(device->logicalDevice, staging_buffer, nullptr);
            vkFreeMemory(device->logicalDevice, staging_buffer_memory, nullptr);

            current_offset += index_section_size;
        }

        MeshPart part = { vertex_buffer_section, index_buffer_section, group.vertex_indices.size() };

        if (!group.albedo_map_path.empty()) {
            part.albedo_map = new Texture2D();
            part.albedo_map->loadFromFile(group.albedo_map_path, VK_FORMAT_R8G8B8A8_UNORM, device, transferQueue);
        }

        if (!group.normal_map_path.empty()) {
            part.normal_map = new Texture2D();
            part.normal_map->loadFromFile(group.normal_map_path, VK_FORMAT_R8G8B8A8_UNORM, device, transferQueue);
        }

        mesh_parts.emplace_back(part);
    }

    // descriptor pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(mesh_parts.size()) },
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(mesh_parts.size()) },
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(mesh_parts.size()) },
    };
    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = initializers::descriptorPoolCreateInfo(
            static_cast<uint32_t>(poolSizes.size()),
            poolSizes.data(),
            static_cast<uint32_t>(mesh_parts.size())
            );
    VK_CHECK_RESULT(vkCreateDescriptorPool(device->logicalDevice, &descriptorPoolCreateInfo, nullptr, &descriptorPool));

    // material_descriptor_set_layout
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
            initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
            initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
            initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2)
    };
    VkDescriptorSetLayoutCreateInfo layoutCreateInfo = initializers::descriptorSetLayoutCreateInfo(
            bindings.data(),
            static_cast<uint32_t>(bindings.size())
            );
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->logicalDevice, &layoutCreateInfo, nullptr, &material_descriptor_set_layout));

    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(device->physicalDevice, &physical_device_properties);
    auto min_alignment = physical_device_properties.limits.minUniformBufferOffsetAlignment;
    VkDeviceSize alignment_offset = ((sizeof(MaterialUbo) - 1) / min_alignment + 1) * min_alignment;

    VkDeviceSize uniform_buffer_size = alignment_offset * mesh_parts.size();
    VK_CHECK_RESULT(device->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            uniform_buffer_size,
            &uniform_buffer, &uniform_buffer_memory
            ));

    VkDeviceSize uniform_buffer_total_offset = 0;
    for (auto& part : mesh_parts) {
        VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
        descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocInfo.descriptorPool = descriptorPool;
        descriptorSetAllocInfo.pSetLayouts = &material_descriptor_set_layout;
        descriptorSetAllocInfo.descriptorSetCount = 1;
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &part.material_descriptor_set));

        std::vector<VkWriteDescriptorSet> writes {};
        MaterialUbo ubo {0, 0};

        VkDescriptorBufferInfo bufferInfo {};
        bufferInfo.buffer = uniform_buffer;
        bufferInfo.offset = uniform_buffer_total_offset;
        bufferInfo.range = sizeof(MaterialUbo);

        VkWriteDescriptorSet write0 {};
        write0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write0.descriptorCount = 1;
        write0.dstSet = part.material_descriptor_set;
        write0.dstBinding = 0;
        write0.pBufferInfo = &bufferInfo;
        writes.emplace_back(write0);

        if (part.albedo_map) {
            VkWriteDescriptorSet write1 {};
            write1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write1.descriptorCount = 1;
            write1.dstSet = part.material_descriptor_set;
            write1.dstBinding = 0;
            write1.pImageInfo = &part.albedo_map->descriptor;
            writes.emplace_back(write1);

            ubo.has_albedo_map = 1;
        }

        if (part.normal_map) {
            VkWriteDescriptorSet write2 {};
            write2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write2.descriptorCount = 1;
            write2.dstSet = part.material_descriptor_set;
            write2.dstBinding = 0;
            write2.pImageInfo = &part.normal_map->descriptor;
            writes.emplace_back(write2);

            ubo.has_normal_map = 1;
        }

        vkUpdateDescriptorSets(device->logicalDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        // update unifrom buffer
        VkBuffer staging_buffer;
        VkDeviceMemory staging_memory;
        VK_CHECK_RESULT(device->createBuffer(
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                sizeof(MaterialUbo),
                &staging_buffer, &staging_memory, &ubo
                ));
        VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
        VkBufferCopy copyRegion = {};
        copyRegion.size = sizeof(MaterialUbo);
        copyRegion.dstOffset = uniform_buffer_total_offset;
        vkCmdCopyBuffer(copyCmd, staging_buffer, uniform_buffer, 1, &copyRegion);
        device->flushCommandBuffer(copyCmd, transferQueue, true);

        vkDestroyBuffer(device->logicalDevice, staging_buffer, nullptr);
        vkFreeMemory(device->logicalDevice, staging_memory, nullptr);

        // update uniform buffer offset
        uniform_buffer_total_offset += alignment_offset;
    }
}

void ObjModel::Draw(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout)
{
    for (const auto& part : mesh_parts) {
        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &part.vertex_buffer_section.buffer, &part.vertex_buffer_section.offset);
        vkCmdBindIndexBuffer(cmdBuffer, part.index_buffer_section.buffer, part.index_buffer_section.offset, VK_INDEX_TYPE_UINT32);

        // 前一个参数first set指的是当前绑定的descriptorSet数组，从第几个set开始（shader中的 set = n），在同一个renderPass里面可以调用多次
        // 比如说，第一次调用vkCmdBindDescriptorSets，绑定了四个descriptorSets，那么第二次调用vkCmdBindDescriptorSets则需要传入firstSet=4（set从0开始计数）
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                1, 1, &part.material_descriptor_set,
                                0, nullptr);
        vkCmdDrawIndexed(cmdBuffer, part.index_count, 1, 0, 0, 0);
    }
}
