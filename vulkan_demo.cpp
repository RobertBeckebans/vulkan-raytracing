#include "common.h"
#include "device_initialization.h"
#include "swapchain_initialization.h"
#include "vulkan_demo.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include <glm/gtx/hash.hpp>

#include <array>
#include <chrono>
#include <functional>
#include <unordered_map>

namespace {
const std::string model_path = "data/chalet.obj";
const std::string texture_path = "data/chalet.jpg";

struct Uniform_Buffer_Object {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 tex_coord;

    bool operator==(const Vertex& other) const {
        return pos == other.pos && color == other.color && tex_coord == other.tex_coord;
    }
};

std::vector<Vertex> vertices;
std::vector<uint32_t> indices;
}

namespace std {
    template<> struct hash<Vertex> {
        size_t operator()(Vertex const& vertex) const {
            return ((hash<glm::vec3>()(vertex.pos) ^
                (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
                (hash<glm::vec2>()(vertex.tex_coord) << 1);
        }
    };
}

static void load_model() {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, model_path.c_str()))
        error("failed to load obj model: " + model_path);

    std::unordered_map<Vertex, std::size_t> unique_vertices;
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex;
            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };
            vertex.tex_coord = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                1.0 - attrib.texcoords[2 * index.texcoord_index + 1]
            };
            vertex.color = {1.0f, 1.0f, 1.0f};

            if (unique_vertices.count(vertex) == 0) {
                unique_vertices[vertex] = vertices.size();
                vertices.push_back(vertex);
            }
            indices.push_back((uint32_t)unique_vertices[vertex]);
        }
    }
}

static VkRenderPass CreateRenderPass(VkDevice device, VkFormat attachmentImageFormat, VkFormat depth_image_format)
{
    VkAttachmentDescription attachmentDescription;
    attachmentDescription.flags = 0;
    attachmentDescription.format = attachmentImageFormat;
    attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
    attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth_attachment;
    depth_attachment.flags = 0;
    depth_attachment.format = depth_image_format;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::vector<VkAttachmentDescription> attachments{attachmentDescription, depth_attachment};

    VkAttachmentReference attachmentReference;
    attachmentReference.attachment = 0;
    attachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref;
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassDescription;
    subpassDescription.flags = 0;
    subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDescription.inputAttachmentCount = 0;
    subpassDescription.pInputAttachments = nullptr;
    subpassDescription.colorAttachmentCount = 1;
    subpassDescription.pColorAttachments = &attachmentReference;
    subpassDescription.pResolveAttachments = nullptr;
    subpassDescription.pDepthStencilAttachment = &depth_attachment_ref;
    subpassDescription.preserveAttachmentCount = 0;
    subpassDescription.pPreserveAttachments = nullptr;

    std::array<VkSubpassDependency, 2> dependencies;
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassCreateInfo;
    renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfo.pNext = nullptr;
    renderPassCreateInfo.flags = 0;
    renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassCreateInfo.pAttachments = attachments.data();
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpassDescription;
    renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassCreateInfo.pDependencies = dependencies.data();

    VkRenderPass renderPass;
    VkResult result = vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &renderPass);
    CheckVkResult(result, "vkCreateRenderPass");
    return renderPass;
}

static VkPipelineShaderStageCreateInfo GetPipelineShaderStageCreateInfo(VkShaderStageFlagBits stage,
    VkShaderModule shaderModule, const char* entryPoint)
{
    VkPipelineShaderStageCreateInfo createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    createInfo.pNext = nullptr;
    createInfo.flags = 0;
    createInfo.stage = stage;
    createInfo.module = shaderModule;
    createInfo.pName = entryPoint;
    createInfo.pSpecializationInfo = nullptr;
    return createInfo;
}


static VkFormat find_format_with_features(VkPhysicalDevice physical_device, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &properties);

        if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & features) == features)
            return format;
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features)
            return format;
    }
    error("failed to find format with requested features");
    return VK_FORMAT_UNDEFINED; // never get here
}

static VkFormat find_depth_format(VkPhysicalDevice physical_device) {
    return find_format_with_features(physical_device, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

Vulkan_Demo::Vulkan_Demo(uint32_t windowWidth, uint32_t windowHeight)
: windowWidth(windowWidth)
, windowHeight(windowHeight)
{
}

void Vulkan_Demo::CreateResources(HWND windowHandle)
{
    instance = CreateInstance();
    physicalDevice = SelectPhysicalDevice(instance);

    surface = create_surface(instance, windowHandle);

    DeviceInfo deviceInfo = CreateDevice(physicalDevice, surface);
    device = deviceInfo.device;
    graphicsQueueFamilyIndex = deviceInfo.graphicsQueueFamilyIndex;
    graphicsQueue = deviceInfo.graphicsQueue;
    presentationQueueFamilyIndex = deviceInfo.presentationQueueFamilyIndex;
    presentationQueue = deviceInfo.presentationQueue;

    allocator = std::make_unique<Device_Memory_Allocator>(physicalDevice, device);

    Swapchain_Info swapchain_info = create_swapchain(physicalDevice, device, surface);
    swapchain = swapchain_info.swapchain;
    swapchainImages = swapchain_info.images;

    for (auto image : swapchainImages) {
        auto image_view = create_image_view(device, image, swapchain_info.surface_format, VK_IMAGE_ASPECT_COLOR_BIT);
        swapchainImageViews.push_back(image_view);
    }

    renderPass = CreateRenderPass(device, swapchain_info.surface_format, find_depth_format(physicalDevice));

    create_descriptor_set_layout();
    CreatePipeline();

    load_model();

    create_command_pool();
    create_vertex_buffer();
    create_index_buffer();
    create_uniform_buffer();
    create_texture();
    create_texture_sampler();
    create_depth_buffer_resources();
    create_framebuffers();
    create_semaphores();

    create_descriptor_pool();
    create_descriptor_set();

    create_command_buffers();
    record_command_buffers();
}

void Vulkan_Demo::CleanupResources() 
{
    auto destroy_buffer = [this](VkBuffer& buffer) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    };
    auto destroy_image = [this](VkImage& image) {
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
    };
    auto destroy_image_view = [this](VkImageView& image_view) {
        vkDestroyImageView(device, image_view, nullptr);
        image_view = VK_NULL_HANDLE;
    };
    auto destroy_semaphore = [this](VkSemaphore& semaphore) {
        vkDestroySemaphore(device, semaphore, nullptr);
        semaphore = VK_NULL_HANDLE;
    };

    VkResult result = vkDeviceWaitIdle(device);
    CheckVkResult(result, "vkDeviceWaitIdle");

    vkDestroyCommandPool(device, command_pool, nullptr);
    command_pool = VK_NULL_HANDLE;

    vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
    descriptor_set_layout = VK_NULL_HANDLE;

    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    pipeline_layout = VK_NULL_HANDLE;

    vkDestroyPipeline(device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;

    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    descriptor_pool = VK_NULL_HANDLE;

    destroy_buffer(vertex_buffer);
    destroy_buffer(index_buffer);
    destroy_buffer(uniform_staging_buffer);
    destroy_buffer(uniform_buffer);

    vkDestroySampler(device, texture_image_sampler, nullptr);
    texture_image_sampler = VK_NULL_HANDLE;

    destroy_image(texture_image);
    destroy_image_view(texture_image_view);

    destroy_image(depth_image);
    destroy_image_view(depth_image_view);

    for (auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    framebuffers.clear();

    destroy_semaphore(imageAvailableSemaphore);
    destroy_semaphore(renderingFinishedSemaphore);

    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    swapchainImageViews.clear();

    vkDestroyRenderPass(device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;

    vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
    swapchainImages.clear();

    allocator.reset();

    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;

    vkDestroySurfaceKHR(instance, surface, nullptr);
    surface = VK_NULL_HANDLE;

    vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
}

void Vulkan_Demo::create_descriptor_set_layout() {
    std::array<VkDescriptorSetLayoutBinding, 2> descriptor_bindings;
    descriptor_bindings[0].binding = 0;
    descriptor_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_bindings[0].descriptorCount = 1;
    descriptor_bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    descriptor_bindings[0].pImmutableSamplers = nullptr;

    descriptor_bindings[1].binding = 1;
    descriptor_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_bindings[1].descriptorCount = 1;
    descriptor_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    descriptor_bindings[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.bindingCount = static_cast<uint32_t>(descriptor_bindings.size());
    desc.pBindings = descriptor_bindings.data();

    VkResult result = vkCreateDescriptorSetLayout(device, &desc, nullptr, &descriptor_set_layout);
    check_vk_result(result, "vkCreateDescriptorSetLayout");
}

void Vulkan_Demo::CreatePipeline()
{
    ShaderModule vertexShaderModule(device, "shaders/vert.spv");
    ShaderModule fragmentShaderModule(device, "shaders/frag.spv");

    std::vector<VkPipelineShaderStageCreateInfo> shaderStageCreateInfos
    {
        GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT,
            vertexShaderModule.GetHandle(), "main"),
        GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT,
            fragmentShaderModule.GetHandle(), "main")
    };

    VkVertexInputBindingDescription vertexBindingDescription;
    vertexBindingDescription.binding = 0;
    vertexBindingDescription.stride = sizeof(Vertex);
    vertexBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> vertexAttributeDescriptions;
    vertexAttributeDescriptions[0].location = 0;
    vertexAttributeDescriptions[0].binding = vertexBindingDescription.binding;
    vertexAttributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexAttributeDescriptions[0].offset = offsetof(struct Vertex, pos);

    vertexAttributeDescriptions[1].location = 1;
    vertexAttributeDescriptions[1].binding = vertexBindingDescription.binding;
    vertexAttributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexAttributeDescriptions[1].offset = offsetof(struct Vertex, color);

    vertexAttributeDescriptions[2].location = 2;
    vertexAttributeDescriptions[2].binding = vertexBindingDescription.binding;
    vertexAttributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttributeDescriptions[2].offset = offsetof(struct Vertex, tex_coord);

    VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo;
    vertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputStateCreateInfo.pNext = nullptr;
    vertexInputStateCreateInfo.flags = 0;
    vertexInputStateCreateInfo.vertexBindingDescriptionCount = 1;
    vertexInputStateCreateInfo.pVertexBindingDescriptions = &vertexBindingDescription;
    vertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescriptions.size());
    vertexInputStateCreateInfo.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo;
    inputAssemblyStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyStateCreateInfo.pNext = nullptr;
    inputAssemblyStateCreateInfo.flags = 0;
    inputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssemblyStateCreateInfo.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(windowWidth);
    viewport.height = static_cast<float>(windowHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor;
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = windowWidth;
    scissor.extent.height = windowHeight;

    VkPipelineViewportStateCreateInfo viewportStateCreateInfo;
    viewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportStateCreateInfo.pNext = nullptr;
    viewportStateCreateInfo.flags = 0;
    viewportStateCreateInfo.viewportCount = 1;
    viewportStateCreateInfo.pViewports = &viewport;
    viewportStateCreateInfo.scissorCount = 1;
    viewportStateCreateInfo.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo resterizationStateCreateInfo;
    resterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    resterizationStateCreateInfo.pNext = nullptr;
    resterizationStateCreateInfo.flags = 0;
    resterizationStateCreateInfo.depthClampEnable = VK_FALSE;
    resterizationStateCreateInfo.rasterizerDiscardEnable = VK_FALSE;
    resterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
    resterizationStateCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    resterizationStateCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    resterizationStateCreateInfo.depthBiasEnable = VK_FALSE;
    resterizationStateCreateInfo.depthBiasConstantFactor = 0.0f;
    resterizationStateCreateInfo.depthBiasClamp = 0.0f;
    resterizationStateCreateInfo.depthBiasSlopeFactor = 0.0f;
    resterizationStateCreateInfo.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo;
    multisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleStateCreateInfo.pNext = nullptr;
    multisampleStateCreateInfo.flags = 0;
    multisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampleStateCreateInfo.sampleShadingEnable = VK_FALSE;
    multisampleStateCreateInfo.minSampleShading = 1.0f;
    multisampleStateCreateInfo.pSampleMask = nullptr;
    multisampleStateCreateInfo.alphaToCoverageEnable = VK_FALSE;
    multisampleStateCreateInfo.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
    depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.pNext = nullptr;
    depth_stencil_state.flags = 0;
    depth_stencil_state.depthTestEnable = VK_TRUE;
    depth_stencil_state.depthWriteEnable = VK_TRUE;
    depth_stencil_state.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
    depth_stencil_state.stencilTestEnable = VK_FALSE;
    depth_stencil_state.front = {};
    depth_stencil_state.back = {};
    depth_stencil_state.minDepthBounds = 0.0;
    depth_stencil_state.maxDepthBounds = 0.0;

    VkPipelineColorBlendAttachmentState colorBlendAttachmentState;
    colorBlendAttachmentState.blendEnable = VK_FALSE;
    colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachmentState.colorWriteMask = 
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo;
    colorBlendStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendStateCreateInfo.pNext = nullptr;
    colorBlendStateCreateInfo.flags = 0;
    colorBlendStateCreateInfo.logicOpEnable = VK_FALSE;
    colorBlendStateCreateInfo.logicOp = VK_LOGIC_OP_COPY;
    colorBlendStateCreateInfo.attachmentCount = 1;
    colorBlendStateCreateInfo.pAttachments = &colorBlendAttachmentState;
    colorBlendStateCreateInfo.blendConstants[0] = 0.0f;
    colorBlendStateCreateInfo.blendConstants[1] = 0.0f;
    colorBlendStateCreateInfo.blendConstants[2] = 0.0f;
    colorBlendStateCreateInfo.blendConstants[3] = 0.0f;

    VkPipelineLayoutCreateInfo layoutCreateInfo;
    layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCreateInfo.pNext = nullptr;
    layoutCreateInfo.flags = 0;
    layoutCreateInfo.setLayoutCount = 1;
    layoutCreateInfo.pSetLayouts = &descriptor_set_layout;
    layoutCreateInfo.pushConstantRangeCount = 0;
    layoutCreateInfo.pPushConstantRanges = nullptr;

    VkResult result = vkCreatePipelineLayout(device, &layoutCreateInfo, nullptr, &pipeline_layout);
    check_vk_result(result, "vkCreatePipelineLayout");

    VkGraphicsPipelineCreateInfo pipelineCreateInfo;
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.pNext = nullptr;
    pipelineCreateInfo.flags = 0;
    pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStageCreateInfos.size());
    pipelineCreateInfo.pStages = shaderStageCreateInfos.data();
    pipelineCreateInfo.pVertexInputState = &vertexInputStateCreateInfo;
    pipelineCreateInfo.pInputAssemblyState = &inputAssemblyStateCreateInfo;
    pipelineCreateInfo.pTessellationState = nullptr;
    pipelineCreateInfo.pViewportState = &viewportStateCreateInfo;
    pipelineCreateInfo.pRasterizationState = &resterizationStateCreateInfo;
    pipelineCreateInfo.pMultisampleState = &multisampleStateCreateInfo;
    pipelineCreateInfo.pDepthStencilState = &depth_stencil_state;
    pipelineCreateInfo.pColorBlendState = &colorBlendStateCreateInfo;
    pipelineCreateInfo.pDynamicState = nullptr;
    pipelineCreateInfo.layout = pipeline_layout;
    pipelineCreateInfo.renderPass = renderPass;
    pipelineCreateInfo.subpass = 0;
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineCreateInfo.basePipelineIndex = -1;

    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline);
    CheckVkResult(result, "vkCreateGraphicsPipelines");
}

void Vulkan_Demo::create_command_pool() {
    VkCommandPoolCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.queueFamilyIndex = graphicsQueueFamilyIndex;

    VkResult result = vkCreateCommandPool(device, &desc, nullptr, &command_pool);
    check_vk_result(result, "vkCreateCommandPool");
}

void Vulkan_Demo::create_vertex_buffer() {
    const VkDeviceSize size = vertices.size() * sizeof(Vertex);
    vertex_buffer = create_buffer(device, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, *allocator);

    VkBuffer staging_buffer = create_staging_buffer(device, size, *allocator, vertices.data());
    Scope_Exit_Action destroy_staging_buffer([&staging_buffer, this]() {
        vkDestroyBuffer(device, staging_buffer, nullptr);
    });

    record_and_run_commands(device, command_pool, graphicsQueue, [&staging_buffer, &size, this](VkCommandBuffer command_buffer) {
        VkBufferCopy region;
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size = size;
        vkCmdCopyBuffer(command_buffer, staging_buffer, vertex_buffer, 1, &region);
        // NOTE: we do not need memory barrier here since record_and_run_commands performs queue wait operation
    });
}

void Vulkan_Demo::create_index_buffer() {
    const VkDeviceSize size = indices.size() * sizeof(indices[0]);
    index_buffer = create_buffer(device, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, *allocator);

    VkBuffer staging_buffer = create_staging_buffer(device, size, *allocator, indices.data());
    Scope_Exit_Action destroy_staging_buffer([&staging_buffer, this]() {
        vkDestroyBuffer(device, staging_buffer, nullptr);
    });

    record_and_run_commands(device, command_pool, graphicsQueue, [&staging_buffer, &size, this](VkCommandBuffer command_buffer) {
        VkBufferCopy region;
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size = size;
        vkCmdCopyBuffer(command_buffer, staging_buffer, index_buffer, 1, &region);
        // NOTE: we do not need memory barrier here since record_and_run_commands performs queue wait operation
    });
}

void Vulkan_Demo::create_uniform_buffer() {
    auto size = static_cast<VkDeviceSize>(sizeof(Uniform_Buffer_Object));
    uniform_staging_buffer = create_permanent_staging_buffer(device, size, *allocator, uniform_staging_buffer_memory);
    uniform_buffer = create_buffer(device, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, *allocator);
}

void Vulkan_Demo::create_descriptor_pool() {
    std::array<VkDescriptorPoolSize, 2> pool_sizes;
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = 1;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.maxSets = 1;
    desc.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    desc.pPoolSizes = pool_sizes.data();

    VkResult result = vkCreateDescriptorPool(device, &desc, nullptr, &descriptor_pool);
    check_vk_result(result, "vkCreateDescriptorPool");
}

void Vulkan_Demo::create_descriptor_set() {
    VkDescriptorSetAllocateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    desc.pNext = nullptr;
    desc.descriptorPool = descriptor_pool;
    desc.descriptorSetCount = 1;
    desc.pSetLayouts = &descriptor_set_layout;

    VkResult result = vkAllocateDescriptorSets(device, &desc, &descriptor_set);
    check_vk_result(result, "vkAllocateDescriptorSets");

    VkDescriptorBufferInfo buffer_info;
    buffer_info.buffer = uniform_buffer;
    buffer_info.offset = 0;
    buffer_info.range = sizeof(Uniform_Buffer_Object);

    VkDescriptorImageInfo image_info;
    image_info.sampler = texture_image_sampler;
    image_info.imageView = texture_image_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 2> descriptor_writes;
    descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[0].pNext = nullptr;
    descriptor_writes[0].dstSet = descriptor_set;
    descriptor_writes[0].dstBinding = 0;
    descriptor_writes[0].dstArrayElement = 0;
    descriptor_writes[0].descriptorCount = 1;
    descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_writes[0].pImageInfo = nullptr;
    descriptor_writes[0].pBufferInfo = &buffer_info;
    descriptor_writes[0].pTexelBufferView = nullptr;

    descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_writes[1].dstSet = descriptor_set;
    descriptor_writes[1].dstBinding = 1;
    descriptor_writes[1].dstArrayElement = 0;
    descriptor_writes[1].descriptorCount = 1;
    descriptor_writes[1].pNext = nullptr;
    descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_writes[1].pImageInfo = &image_info;
    descriptor_writes[1].pBufferInfo = nullptr;
    descriptor_writes[1].pTexelBufferView = nullptr;

    vkUpdateDescriptorSets(device, (uint32_t)descriptor_writes.size(), descriptor_writes.data(), 0, nullptr);
}

void Vulkan_Demo::create_texture() {
    int image_width, image_height, image_component_count;

    auto rgba_pixels = stbi_load(texture_path.c_str(), &image_width, &image_height, &image_component_count, STBI_rgb_alpha);
    if (rgba_pixels == nullptr) {
        error("failed to load image file");
    }

    VkImage staging_image = create_staging_texture(device, image_width, image_height, VK_FORMAT_R8G8B8A8_UNORM, *allocator, rgba_pixels, 4);

    Scope_Exit_Action destroy_staging_image_action([this, &staging_image]() {
        vkDestroyImage(device, staging_image, nullptr);
    });
    stbi_image_free(rgba_pixels);

    texture_image = ::create_texture(device, image_width, image_height, VK_FORMAT_R8G8B8A8_UNORM, *allocator);

    record_and_run_commands(device, command_pool, graphicsQueue,
        [&staging_image, &image_width, &image_height, this](VkCommandBuffer command_buffer) {

        record_image_layout_transition(command_buffer, staging_image, VK_FORMAT_R8G8B8A8_UNORM,
            VK_ACCESS_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        record_image_layout_transition(command_buffer, texture_image, VK_FORMAT_R8G8B8A8_UNORM,
            0, VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // copy staging image's data to device local image
        VkImageSubresourceLayers subresource_layers;
        subresource_layers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource_layers.mipLevel = 0;
        subresource_layers.baseArrayLayer = 0;
        subresource_layers.layerCount = 1;

        VkImageCopy region;
        region.srcSubresource = subresource_layers;
        region.srcOffset = {0, 0, 0};
        region.dstSubresource = subresource_layers;
        region.dstOffset = {0, 0, 0};
        region.extent.width = image_width;
        region.extent.height = image_height;

        vkCmdCopyImage(command_buffer,
            staging_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            texture_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region);

        record_image_layout_transition(command_buffer, texture_image, VK_FORMAT_R8G8B8A8_UNORM,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    texture_image_view = create_image_view(device, texture_image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
}

void Vulkan_Demo::create_texture_sampler() {
    VkSamplerCreateInfo desc;

    desc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.magFilter = VK_FILTER_LINEAR;
    desc.minFilter = VK_FILTER_LINEAR;
    desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    desc.mipLodBias = 0.0f;
    desc.anisotropyEnable = VK_TRUE;
    desc.maxAnisotropy = 16;
    desc.compareEnable = VK_FALSE;
    desc.compareOp = VK_COMPARE_OP_ALWAYS;
    desc.minLod = 0.0f;
    desc.maxLod = 0.0f;
    desc.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    desc.unnormalizedCoordinates = VK_FALSE;

    VkResult result = vkCreateSampler(device, &desc, nullptr, &texture_image_sampler);
    check_vk_result(result, "vkCreateSampler");
}

void Vulkan_Demo::create_depth_buffer_resources() {
    VkFormat depth_format = find_depth_format(physicalDevice);
    depth_image = create_depth_attachment_image(device, windowWidth, windowHeight, depth_format, *allocator);
    depth_image_view = create_image_view(device, depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);

    record_and_run_commands(device, command_pool, graphicsQueue, [&depth_format, this](VkCommandBuffer command_buffer) {
        record_image_layout_transition(command_buffer, depth_image, depth_format, 0, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    });
}

void Vulkan_Demo::create_framebuffers() {
    std::array<VkImageView, 2> attachments = {VK_NULL_HANDLE, depth_image_view};

    VkFramebufferCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;
    desc.renderPass = renderPass;
    desc.attachmentCount = static_cast<uint32_t>(attachments.size());
    desc.pAttachments = attachments.data();
    desc.width = windowWidth;
    desc.height = windowHeight;
    desc.layers = 1;

    framebuffers.resize(swapchainImages.size());
    for (std::size_t i = 0; i < framebuffers.size(); i++) {
        attachments[0] = swapchainImageViews[i]; // set color attachment
        VkResult result = vkCreateFramebuffer(device, &desc, nullptr, &framebuffers[i]);
        check_vk_result(result, "vkCreateFramebuffer");
    }
}

void Vulkan_Demo::create_semaphores() {
    VkSemaphoreCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    desc.pNext = nullptr;
    desc.flags = 0;

    VkResult result = vkCreateSemaphore(device, &desc, nullptr, &imageAvailableSemaphore);
    check_vk_result(result, "vkCreateSemaphore");

    result = vkCreateSemaphore(device, &desc, nullptr, &renderingFinishedSemaphore);
    check_vk_result(result, "vkCreateSemaphore");
}

void Vulkan_Demo::create_command_buffers() {
    VkCommandBufferAllocateInfo alloc_info;
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.pNext = nullptr;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = static_cast<uint32_t>(swapchainImages.size());

    command_buffers.resize(swapchainImages.size());
    VkResult result = vkAllocateCommandBuffers(device, &alloc_info, command_buffers.data());
    check_vk_result(result, "vkAllocateCommandBuffers");
}

void Vulkan_Demo::record_command_buffers() {
    VkImageSubresourceRange subresource_range;
    subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresource_range.baseMipLevel = 0;
    subresource_range.levelCount = 1;
    subresource_range.baseArrayLayer = 0;
    subresource_range.layerCount = 1;

    VkCommandBufferBeginInfo begin_info;
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = nullptr;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    begin_info.pInheritanceInfo = nullptr;

    for (std::size_t i = 0; i < command_buffers.size(); i++) {
        VkCommandBuffer command_buffer = command_buffers[i];

        VkResult result = vkBeginCommandBuffer(command_buffer, &begin_info);
        check_vk_result(result, "vkBeginCommandBuffer");

        if (presentationQueue != graphicsQueue) {
            VkImageMemoryBarrier barrierFromPresentToDraw;
            barrierFromPresentToDraw.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrierFromPresentToDraw.pNext = nullptr;
            barrierFromPresentToDraw.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            barrierFromPresentToDraw.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            barrierFromPresentToDraw.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrierFromPresentToDraw.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrierFromPresentToDraw.srcQueueFamilyIndex = presentationQueueFamilyIndex;
            barrierFromPresentToDraw.dstQueueFamilyIndex = graphicsQueueFamilyIndex;
            barrierFromPresentToDraw.image = swapchainImages[i];
            barrierFromPresentToDraw.subresourceRange = subresource_range;

            vkCmdPipelineBarrier(
                command_buffer,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrierFromPresentToDraw
            );
        }

        std::array<VkClearValue, 2> clear_values;
        clear_values[0].color = {1.0f, 0.8f, 0.4f, 0.0f};
        clear_values[1].depthStencil = {1.0, 0};

        VkRenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.pNext = nullptr;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.framebuffer = framebuffers[i];
        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = {windowWidth, windowHeight};
        renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clear_values.size());
        renderPassBeginInfo.pClearValues = clear_values.data();

        vkCmdBeginRenderPass(command_buffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &offset);
        vkCmdBindIndexBuffer(command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
        vkCmdDrawIndexed(command_buffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
        vkCmdEndRenderPass(command_buffer);

        if (presentationQueue != graphicsQueue) {
            VkImageMemoryBarrier barrierFromDrawToPresent;
            barrierFromDrawToPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrierFromDrawToPresent.pNext = nullptr;
            barrierFromDrawToPresent.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            barrierFromDrawToPresent.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            barrierFromDrawToPresent.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrierFromDrawToPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrierFromDrawToPresent.srcQueueFamilyIndex = graphicsQueueFamilyIndex;
            barrierFromDrawToPresent.dstQueueFamilyIndex = presentationQueueFamilyIndex;
            barrierFromDrawToPresent.image = swapchainImages[i];
            barrierFromDrawToPresent.subresourceRange = subresource_range;

            vkCmdPipelineBarrier(
                command_buffer,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrierFromDrawToPresent
            );
        }

        result = vkEndCommandBuffer(command_buffer);
        check_vk_result(result, "vkEndCommandBuffer");
    }
}

void Vulkan_Demo::update_uniform_buffer() {
    static auto start_time = std::chrono::high_resolution_clock::now();

    auto current_time = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count() / 1000.f;
    //time = 0.0;

    Uniform_Buffer_Object ubo;
    ubo.model = glm::rotate(glm::mat4(), time * glm::radians(30.0f), glm::vec3(0, 1, 0)) *
        glm::rotate(glm::mat4(), glm::radians(-90.0f), glm::vec3(1, 0, 0));

    ubo.view = glm::lookAt(glm::vec3(0.5, 1.4, 2.8), glm::vec3(0, 0.3, 0), glm::vec3(0, 1, 0));

    // Vulkan clip space has inverted Y and half Z.
    const glm::mat4 clip(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, -1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        0.0f, 0.0f, 0.5f, 1.0f);

    ubo.proj = clip * glm::perspective(glm::radians(45.0f), windowWidth / (float)windowHeight, 0.1f, 10.0f);

    void* data;
    VkResult result = vkMapMemory(device, uniform_staging_buffer_memory, 0, sizeof(ubo), 0, &data);
    check_vk_result(result, "vkMapMemory");
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(device, uniform_staging_buffer_memory);

    // TODO: this commands is slow since it waits until queue finishes specified operation
    record_and_run_commands(device, command_pool, graphicsQueue, [this](VkCommandBuffer command_buffer) {
        VkBufferCopy region;
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size = sizeof(Uniform_Buffer_Object);
        vkCmdCopyBuffer(command_buffer, uniform_staging_buffer, uniform_buffer, 1, &region);
    });
}

void Vulkan_Demo::RunFrame()
{
    update_uniform_buffer();

    uint32_t swapchain_image_index;
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &swapchain_image_index);
    check_vk_result(result, "vkAcquireNextImageKHR");

    VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;

    VkSubmitInfo submitInfo;
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = nullptr;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask = &waitDstStageMask;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &command_buffers[swapchain_image_index];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderingFinishedSemaphore;

    result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    CheckVkResult(result, "vkQueueSubmit");

    VkPresentInfoKHR presentInfo;
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderingFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &swapchain_image_index;
    presentInfo.pResults = nullptr;

    result = vkQueuePresentKHR(presentationQueue, &presentInfo);
    check_vk_result(result, "vkQueuePresentKHR");
}