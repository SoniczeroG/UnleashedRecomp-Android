//
// plume
//
// Descriptor/pipeline self-test for the fixed-size descriptor array fallback.
//
// Runs once at device creation on devices without descriptor indexing and logs, for a matrix of
// tiny shaders and pipeline layouts, whether vkCreateGraphicsPipelines succeeds. The results
// isolate which construct a driver rejects (descriptor arrays at all, dynamic indexing, array size,
// per-stage totals across several sets, separate sampler arrays, or the SPIR-V rewrite) without
// needing validation layers on the device.
//
// Shaders (compiled with DXC -spirv, embedded in plume_vulkan_selftest_spirv.h):
//   vs      fullscreen triangle
//   p0      constant color, no resources
//   p1      single Texture2D, Load
//   p2_N    Texture2D[N], Load at push-constant index        (N = 1, 16, 64, 85, 128, 256)
//   p3_256  Texture2D[256], Load at constant index 3
//   p4_N    Texture2D[N] + SamplerState[16] (set 3), Sample at push-constant indices
//   p5      Texture2D[] runtime array (rewritten by spirv_fixup before use)
//

#pragma once

#include "plume_vulkan_selftest_spirv.h"
#include "plume_vulkan_spirv_fixup.h"

#include <cstdio>
#include <vector>

namespace plume {
namespace selftest {
    struct Context {
        VkDevice device = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkShaderModule vertexShader = VK_NULL_HANDLE;
    };

    inline VkShaderModule createModule(VkDevice device, const uint32_t *code, size_t sizeBytes) {
        VkShaderModuleCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.pCode = code;
        info.codeSize = sizeBytes;
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }

        return module;
    }

    inline VkDescriptorSetLayout createSetLayout(VkDevice device, VkDescriptorType type, uint32_t count, VkShaderStageFlags stageFlags) {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = 0;
        binding.descriptorCount = count;
        binding.descriptorType = type;
        binding.stageFlags = stageFlags;

        VkDescriptorSetLayoutCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = (count > 0) ? 1 : 0;
        info.pBindings = (count > 0) ? &binding : nullptr;

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        vkCreateDescriptorSetLayout(device, &info, nullptr, &layout);
        return layout;
    }

    // textureSets: number of identical sampled-image set layouts (sets 0..n-1).
    // samplerCount > 0 appends a sampler set layout at samplerSet (padding with empty sets).
    inline VkResult runCase(const Context &ctx, const char *name, const uint32_t *psCode, size_t psSize,
        uint32_t textureSets, uint32_t textureCount, uint32_t samplerCount,
        uint32_t samplerSet = 3, VkShaderStageFlags stageFlags = VK_SHADER_STAGE_ALL) {
        VkDevice device = ctx.device;
        std::vector<VkDescriptorSetLayout> setLayouts;
        for (uint32_t i = 0; i < textureSets; i++) {
            setLayouts.push_back(createSetLayout(device, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, textureCount, stageFlags));
        }

        if (samplerCount > 0) {
            while (setLayouts.size() < samplerSet) {
                setLayouts.push_back(createSetLayout(device, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 0, stageFlags));
            }

            setLayouts.push_back(createSetLayout(device, VK_DESCRIPTOR_TYPE_SAMPLER, samplerCount, stageFlags));
        }

        VkPushConstantRange pushRange = {};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.size = 8;

        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = uint32_t(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkResult res = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);

        VkShaderModule pixelShader = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (res == VK_SUCCESS) {
            pixelShader = createModule(device, psCode, psSize);
            if (pixelShader == VK_NULL_HANDLE) {
                res = VK_ERROR_INVALID_SHADER_NV;
            }
        }

        if (res == VK_SUCCESS) {
            VkPipelineShaderStageCreateInfo stages[2] = {};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = ctx.vertexShader;
            stages[0].pName = "main";
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = pixelShader;
            stages[1].pName = "main";

            VkPipelineVertexInputStateCreateInfo vertexInput = {};
            vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

            VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkPipelineViewportStateCreateInfo viewport = {};
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount = 1;
            viewport.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo raster = {};
            raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisample = {};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineColorBlendAttachmentState blendAttachment = {};
            blendAttachment.colorWriteMask = 0xF;
            VkPipelineColorBlendStateCreateInfo blend = {};
            blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = 1;
            blend.pAttachments = &blendAttachment;

            VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo dynamic = {};
            dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates = dynamicStates;

            VkGraphicsPipelineCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            info.stageCount = 2;
            info.pStages = stages;
            info.pVertexInputState = &vertexInput;
            info.pInputAssemblyState = &inputAssembly;
            info.pViewportState = &viewport;
            info.pRasterizationState = &raster;
            info.pMultisampleState = &multisample;
            info.pColorBlendState = &blend;
            info.pDynamicState = &dynamic;
            info.layout = pipelineLayout;
            info.renderPass = ctx.renderPass;
            res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
        }

        fprintf(stderr, "SelfTest %-36s sets=%u tex=%u samplers=%u stages=0x%X -> %s (%d)\n",
            name, textureSets, textureCount, samplerCount, uint32_t(stageFlags), (res == VK_SUCCESS) ? "OK" : "FAIL", int32_t(res));

        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        if (pixelShader != VK_NULL_HANDLE) vkDestroyShaderModule(device, pixelShader, nullptr);
        if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        for (VkDescriptorSetLayout setLayout : setLayouts) {
            if (setLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        }

        return res;
    }

    inline void runDescriptorSelfTest(VkDevice device) {
        using namespace selftest_spirv;

        Context ctx;
        ctx.device = device;

        VkAttachmentDescription attachment = {};
        attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &attachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &ctx.renderPass) != VK_SUCCESS) {
            fprintf(stderr, "SelfTest: vkCreateRenderPass failed, skipping.\n");
            return;
        }

        ctx.vertexShader = createModule(device, vs, sizeof(vs));
        if (ctx.vertexShader == VK_NULL_HANDLE) {
            fprintf(stderr, "SelfTest: vertex shader module failed, skipping.\n");
            vkDestroyRenderPass(device, ctx.renderPass, nullptr);
            return;
        }

#define PLUME_SELFTEST(NAME, CODE, SETS, TEX, SAMP) runCase(ctx, NAME, CODE, sizeof(CODE), SETS, TEX, SAMP)
        fprintf(stderr, "SelfTest: begin\n");
        PLUME_SELFTEST("no resources", p0, 0, 0, 0);
        PLUME_SELFTEST("single texture", p1, 1, 1, 0);
        PLUME_SELFTEST("array[1] dynamic index", p2_1, 1, 1, 0);
        PLUME_SELFTEST("array[16] dynamic index", p2_16, 1, 16, 0);
        PLUME_SELFTEST("array[64] dynamic index", p2_64, 1, 64, 0);
        PLUME_SELFTEST("array[128] dynamic index", p2_128, 1, 128, 0);
        PLUME_SELFTEST("array[256] dynamic index", p2_256, 1, 256, 0);
        PLUME_SELFTEST("array[256] constant index", p3_256, 1, 256, 0);
        PLUME_SELFTEST("array[85] x3 sets + samplers", p2_85, 3, 85, 128);
        PLUME_SELFTEST("array[256] x3 sets + samplers", p2_256, 3, 256, 128);
        PLUME_SELFTEST("tex[16]+sampler[16] dynamic", p4_16, 3, 16, 16);
        PLUME_SELFTEST("tex[256]+sampler[16] dynamic", p4_256, 1, 256, 16);
#undef PLUME_SELFTEST

        // Same shapes with bindings visible only to the vertex+fragment stages instead of ALL.
        const VkShaderStageFlags vsfs = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        runCase(ctx, "array[256] dynamic, VS|FS only", p2_256, sizeof(p2_256), 1, 256, 0, 3, vsfs);
        runCase(ctx, "array[256] x3 + samplers, VS|FS only", p2_256, sizeof(p2_256), 3, 256, 128, 3, vsfs);
        runCase(ctx, "array[85] x3 + samplers, VS|FS only", p2_85, sizeof(p2_85), 3, 85, 128, 3, vsfs);

        // ImGui-shaped layout: tex[256] in set 0, sampler[128] in set 1.
        runCase(ctx, "imgui-like tex[256]+smp[128]", p6, sizeof(p6), 1, 256, 128, 1);
        runCase(ctx, "imgui-like, VS|FS only", p6, sizeof(p6), 1, 256, 128, 1, vsfs);

        // The production path: an unsized array rewritten by spirv_fixup, with and without the
        // added SampledImageArrayDynamicIndexing capability.
        for (int addCapability = 1; addCapability >= 0; addCapability--) {
            for (uint32_t size : { 16u, 256u }) {
                std::vector<uint32_t> words(p5, p5 + (sizeof(p5) / sizeof(uint32_t)));
                spirv_fixup::rewriteRuntimeDescriptorArrays(words, size, 0, addCapability != 0);
                char name[64];
                snprintf(name, sizeof(name), "rewritten[%u]%s", size, addCapability ? " +dyn-index cap" : "");
                runCase(ctx, name, words.data(), words.size() * sizeof(uint32_t), 1, size, 0);
            }
        }

        fprintf(stderr, "SelfTest: end\n");
        vkDestroyShaderModule(device, ctx.vertexShader, nullptr);
        vkDestroyRenderPass(device, ctx.renderPass, nullptr);
    }
}
}
