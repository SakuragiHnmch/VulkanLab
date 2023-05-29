//
// Created by Junkang on 2023/5/29.
//

#pragma once

#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>

#include "vulkan/vulkan.h"
#include "VulkanDevice.h"

#include <ktx.h>
#include <ktxvulkan.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace MParser
{
    enum DescriptorBindingFlags {
        ImageBaseColor = 0x00000001,
        ImageNormalMap = 0x00000002,
        ImagePbr = 0x00000004,
    };

    extern VkDescriptorSetLayout descriptorSetLayoutImage;
    extern VkDescriptorSetLayout descriptorSetLayoutUbo;
    extern VkMemoryPropertyFlags memoryPropertyFlags;
    extern uint32_t descriptorBindingFlags;

    struct Node;

    struct Texture {
        VulkanDevice* device = nullptr;
        VkImage image;
        VkImageLayout imageLayout;
        VkDeviceMemory deviceMemory;
        VkImageView view;
        uint32_t width, height;
        uint32_t mipLevels;
        uint32_t layerCount;
        VkDescriptorImageInfo descriptor;
        VkSampler sampler;

        void load(const aiScene* scene, std::string fileName, std::string filePath, VulkanDevice* device, VkQueue copyQueue);

        void destroy();

    };

    struct Material {
        VulkanDevice* device = nullptr;
        enum AlphaMode { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
        AlphaMode alphaMode = ALPHAMODE_OPAQUE;
        float alphaCutoff = 1.0f;
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        glm::vec4 baseColorFactor = glm::vec4(1.0f);
        Texture* baseColorTexture = nullptr;
        Texture* metallicRoughnessTexture = nullptr;
        Texture* normalTexture = nullptr;
        Texture* occlusionTexture = nullptr;
        Texture* emissiveTexture = nullptr;

        Texture* specularGlossinessTexture = nullptr;
        Texture* diffuseTexture = nullptr;

        Texture* emptyTexture = nullptr;

        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

        Material(VulkanDevice* device, Texture* emptyTex) : device(device), emptyTexture(emptyTex) {};
        void createDescriptorSet(VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout, uint32_t descriptorBindingFlags);
    };

    struct Mesh {
        uint32_t firstIndex;
        uint32_t indexCount;
        uint32_t firstVertex;
        uint32_t vertexCount;
        Material* material;

        struct Dimensions {
            glm::vec3 min = glm::vec3(FLT_MAX);
            glm::vec3 max = glm::vec3(-FLT_MAX);
            glm::vec3 size;
            glm::vec3 center;
            float radius;
        } dimensions;

        void setDimensions(glm::vec3 min, glm::vec3 max);

        Mesh(uint32_t firstIndex, uint32_t indexCount, uint32_t firstVertex, uint32_t vertexCount, Material* material)
            : firstIndex(firstIndex),
              indexCount(indexCount),
              firstVertex(firstVertex),
              vertexCount(vertexCount),
              material(material) {};
    };

    struct Geometry {
        VulkanDevice* device;

        std::vector<Mesh*> meshes;
        std::string name;

        struct UniformBuffer {
            VkBuffer buffer;
            VkDeviceMemory memory;
            VkDescriptorBufferInfo descriptor;
            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
            void* mapped;
        } uniformBuffer;

        struct UniformBlock {
            glm::mat4 matrix;
            glm::mat4 jointMatrix[64]{};
            float jointcount{ 0 };
        } uniformBlock;

        Geometry(VulkanDevice* device, glm::mat4 matrix);
        ~Geometry();
    };

    /*
        glTF skin
    */
    struct Skin {
        std::string name;
        Node* skeletonRoot = nullptr;
        std::vector<glm::mat4> inverseBindMatrices;
        std::vector<Node*> joints;
    };

    /*
        glTF node
    */
    struct Node {
        Node* parent;
        uint32_t index;
        std::vector<Node*> children;
        glm::mat4 matrix;
        std::string name;
        Geometry* geo;
        Skin* skin;
        int32_t skinIndex = -1;
        glm::vec3 translation{};
        glm::vec3 scale{ 1.0f };
        glm::quat rotation{};
        glm::mat4 localMatrix();
        glm::mat4 getMatrix();
        void update();
        ~Node();
    };

    /*
        glTF animation channel
    */
    struct AnimationChannel {
        enum PathType { TRANSLATION, ROTATION, SCALE };
        PathType path;
        Node* node;
        uint32_t samplerIndex;
    };

    /*
        glTF animation sampler
    */
    struct AnimationSampler {
        enum InterpolationType { LINEAR, STEP, CUBICSPLINE };
        InterpolationType interpolation;
        std::vector<float> inputs;
        std::vector<glm::vec4> outputsVec4;
    };

    /*
        glTF animation
    */
    struct Animation {
        std::string name;
        std::vector<AnimationSampler> samplers;
        std::vector<AnimationChannel> channels;
        float start = std::numeric_limits<float>::max();
        float end = std::numeric_limits<float>::min();
    };

    /*
        glTF default vertex layout with easy Vulkan mapping functions
    */
    enum class VertexComponent { Position, Normal, UV, Color, Tangent, Joint0, Weight0 };

    struct Vertex {
        glm::vec4 pos;
        glm::vec3 normal;
        glm::vec2 uv;
        glm::vec4 color;
        glm::vec4 joint0;
        glm::vec4 weight0;
        glm::vec4 tangent;
        static VkVertexInputBindingDescription vertexInputBindingDescription;
        static std::vector<VkVertexInputAttributeDescription> vertexInputAttributeDescriptions;
        static VkPipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo;
        static VkVertexInputBindingDescription inputBindingDescription(uint32_t binding);
        static VkVertexInputAttributeDescription inputAttributeDescription(uint32_t binding, uint32_t location, VertexComponent component);
        static std::vector<VkVertexInputAttributeDescription> inputAttributeDescriptions(uint32_t binding, const std::vector<VertexComponent> components);
        /** @brief Returns the default pipeline vertex input state create info structure for the requested vertex components */
        static VkPipelineVertexInputStateCreateInfo* getPipelineVertexInputState(const std::vector<VertexComponent> components);
    };

    enum FileLoadingFlags {
        None = 0x00000000,
        PreTransformVertices = 0x00000001,
        PreMultiplyVertexColors = 0x00000002,
        FlipY = 0x00000004,
        DontLoadImages = 0x00000008
    };

    enum RenderFlags {
        BindImages = 0x00000001,
        RenderOpaqueNodes = 0x00000002,
        RenderAlphaMaskedNodes = 0x00000004,
        RenderAlphaBlendedNodes = 0x00000008,
        RenderAnimation = 0x00000010,
    };

    /*
        glTF model loading and rendering class
    */
    class Model {
    private:
        Texture* getTexture(uint32_t index);
        Texture emptyTexture;
        void createEmptyTexture(VkQueue transferQueue);

        Node* rootNode;
    public:
        VulkanDevice* device;
        VkDescriptorPool descriptorPool;

        struct Vertices {
            uint32_t count;
            VkBuffer buffer;
            VkDeviceMemory memory;
        } vertices;

        struct Indices {
            uint32_t count;
            VkBuffer buffer;
            VkDeviceMemory memory;
        } indices;

        std::vector<uint32_t> indexBuffer {};
        std::vector<Vertex> vertexBuffer {};

        std::vector<Node*> nodes;
        std::vector<Node*> linearNodes;

        std::vector<Skin*> skins;

        std::vector<Texture*> textures;
        std::vector<Material*> materials;
        std::vector<Animation*> animations;

        struct Dimensions {
            glm::vec3 min = glm::vec3(FLT_MAX);
            glm::vec3 max = glm::vec3(-FLT_MAX);
            glm::vec3 size;
            glm::vec3 center;
            float radius;
        } dimensions;

        bool metallicRoughnessWorkflow = true;
        bool buffersBound = false;
        std::string path;

        Model() {};
        ~Model();
        void loadNode(const aiScene* scene, aiNode* node, Node* parent);
//        void loadSkins(const aiScene* scene);
        Texture* loadMaterialTexture(const aiScene* scene, const aiMaterial* mat, aiTextureType type, VkQueue queue) const;
        void loadMaterials(const aiScene* scene, VkQueue transferQueue);
//        void loadAnimations(const aiScene* scene);
        void loadFromFile(std::string filename, VulkanDevice* device, VkQueue transferQueue, uint32_t fileLoadingFlags = FileLoadingFlags::None);
        void bindBuffers(VkCommandBuffer commandBuffer);
        void drawNode(Node* node, VkCommandBuffer commandBuffer, uint32_t renderFlags = 0, VkPipelineLayout pipelineLayout = VK_NULL_HANDLE, uint32_t bindImageSet = 1);
        void draw(VkCommandBuffer commandBuffer, uint32_t renderFlags = 0, VkPipelineLayout pipelineLayout = VK_NULL_HANDLE, uint32_t bindImageSet = 1);
        void getNodeDimensions(Node* node, glm::vec3& min, glm::vec3& max);
        void getSceneDimensions();
        void updateAnimation(uint32_t index, float time);
        Node* findNode(Node* parent, uint32_t index);
        Node* nodeFromIndex(uint32_t index);
        void prepareNodeDescriptor(Node* node, VkDescriptorSetLayout descriptorSetLayout);
    };
}
