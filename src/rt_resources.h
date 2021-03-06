#pragma once

#include "acceleration_structure.h"
#include "matrix.h"
#include "vk.h"

struct GPU_Mesh;
struct Rt_Uniform_Buffer;

struct Raytracing_Resources {
    VkPhysicalDeviceRayTracingPropertiesKHR properties;
    Vk_Intersection_Accelerator accelerator;
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkDescriptorSet descriptor_set;
    Vk_Buffer shader_binding_table;
    Vk_Buffer uniform_buffer;
    Rt_Uniform_Buffer* mapped_uniform_buffer;

    void create(const GPU_Mesh& gpu_mesh, VkImageView texture_view, VkSampler sampler);
    void destroy();
    void update_output_image_descriptor(VkImageView output_image_view);
    void update(const Matrix3x4& model_transform, const Matrix3x4& camera_to_world_transform);

private:
    void create_pipeline(const GPU_Mesh& gpu_mesh, VkImageView texture_view, VkSampler sampler);
};
