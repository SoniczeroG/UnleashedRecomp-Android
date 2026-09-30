//
// plume
//
// SPIR-V rewrite used by the Vulkan backend's fixed-size descriptor array fallback.
//
// Shaders written for bindless descriptor heaps declare unsized arrays such as
// `Texture2D g_Heap[] : register(t0, space0)`, which DXC emits as
//     OpCapability RuntimeDescriptorArray
//     OpExtension "SPV_EXT_descriptor_indexing"
//     %arr = OpTypeRuntimeArray %type_2d_image
//     %ptr = OpTypePointer UniformConstant %arr
// Drivers without descriptorIndexing.runtimeDescriptorArray (e.g. stock Mali Bifrost) reject
// such modules at pipeline creation. This pass rewrites every runtime array that is the pointee
// of a UniformConstant pointer into `OpTypeArray %elem %N` (N chosen per element kind: images
// vs samplers), removes the RuntimeDescriptorArray capability and the descriptor-indexing
// extension, and optionally declares SampledImageArrayDynamicIndexing. Runtime arrays inside
// storage-buffer structs are left untouched.
//

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace plume {
namespace spirv_fixup {
    enum : uint32_t {
        SpvMagic = 0x07230203,
        OpExtension = 10,
        OpCapability = 17,
        OpTypeInt = 21,
        OpTypeImage = 25,
        OpTypeSampler = 26,
        OpTypeSampledImage = 27,
        OpTypeArray = 28,
        OpTypeRuntimeArray = 29,
        OpTypePointer = 32,
        OpConstant = 43,
        StorageClassUniformConstant = 0,
        CapabilitySampledImageArrayDynamicIndexing = 29,
        CapabilityRuntimeDescriptorArray = 5302,
    };

    struct Result {
        bool modified = false;
        bool error = false;
        uint32_t rewrittenArrays = 0;
    };

    inline std::string readLiteralString(const uint32_t *operands, uint32_t operandWords) {
        const char *chars = reinterpret_cast<const char *>(operands);
        const size_t maxLength = size_t(operandWords) * 4;
        return std::string(chars, strnlen(chars, maxLength));
    }

    // Rewrites `words` in place. imageArraySize applies to arrays of OpTypeImage/OpTypeSampledImage,
    // samplerArraySize to arrays of OpTypeSampler. A size of 0 leaves that kind untouched.
    inline Result rewriteRuntimeDescriptorArrays(std::vector<uint32_t> &words, uint32_t imageArraySize, uint32_t samplerArraySize, bool addDynamicIndexingCapability) {
        Result result;
        if ((words.size() < 5) || (words[0] != SpvMagic)) {
            result.error = true;
            return result;
        }

        struct Instruction {
            size_t offset;
            uint32_t wordCount;
            uint32_t opcode;
        };

        std::vector<Instruction> instructions;
        std::unordered_map<uint32_t, uint32_t> runtimeArrayElement; // runtime array id -> element type id
        std::unordered_set<uint32_t> imageTypes;
        std::unordered_set<uint32_t> samplerTypes;
        std::unordered_set<uint32_t> descriptorRuntimeArrays;
        uint32_t uintTypeId = 0;
        size_t uintTypeInstruction = SIZE_MAX;
        bool hasRuntimeDescriptorArrayCapability = false;
        bool hasDynamicIndexingCapability = false;

        for (size_t offset = 5; offset < words.size();) {
            const uint32_t wordCount = words[offset] >> 16;
            const uint32_t opcode = words[offset] & 0xFFFF;
            if ((wordCount == 0) || ((offset + wordCount) > words.size())) {
                result.error = true;
                return result;
            }

            const uint32_t *operands = &words[offset + 1];
            switch (opcode) {
            case OpCapability:
                if ((wordCount >= 2) && (operands[0] == CapabilityRuntimeDescriptorArray)) {
                    hasRuntimeDescriptorArrayCapability = true;
                }
                else if ((wordCount >= 2) && (operands[0] == CapabilitySampledImageArrayDynamicIndexing)) {
                    hasDynamicIndexingCapability = true;
                }
                break;
            case OpTypeInt:
                if ((wordCount == 4) && (operands[1] == 32) && (operands[2] == 0)) {
                    uintTypeId = operands[0];
                    uintTypeInstruction = instructions.size();
                }
                break;
            case OpTypeImage:
            case OpTypeSampledImage:
                imageTypes.insert(operands[0]);
                break;
            case OpTypeSampler:
                samplerTypes.insert(operands[0]);
                break;
            case OpTypeRuntimeArray:
                if (wordCount == 3) {
                    runtimeArrayElement[operands[0]] = operands[1];
                }
                break;
            case OpTypePointer:
                if ((wordCount == 4) && (operands[1] == StorageClassUniformConstant) && (runtimeArrayElement.count(operands[2]) != 0)) {
                    descriptorRuntimeArrays.insert(operands[2]);
                }
                break;
            default:
                break;
            }

            instructions.push_back({ offset, wordCount, opcode });
            offset += wordCount;
        }

        // Only rewrite arrays whose element kind we have a size for.
        std::unordered_map<uint32_t, uint32_t> arraySizes; // runtime array id -> fixed length
        for (uint32_t arrayId : descriptorRuntimeArrays) {
            const uint32_t elementId = runtimeArrayElement[arrayId];
            if ((imageArraySize > 0) && (imageTypes.count(elementId) != 0)) {
                arraySizes[arrayId] = imageArraySize;
            }
            else if ((samplerArraySize > 0) && (samplerTypes.count(elementId) != 0)) {
                arraySizes[arrayId] = samplerArraySize;
            }
        }

        // Nothing to rewrite, but the module may still declare the descriptor-indexing extension
        // (e.g. a vertex shader sharing a header with bindless pixel shaders); strip it below.
        const bool hasDescriptorIndexingExtension = [&]() {
            for (const Instruction &instruction : instructions) {
                if ((instruction.opcode == OpExtension) && (readLiteralString(&words[instruction.offset + 1], instruction.wordCount - 1) == "SPV_EXT_descriptor_indexing")) {
                    return true;
                }
            }

            return false;
        }();

        if (arraySizes.empty() && (!descriptorRuntimeArrays.empty() || (!hasDescriptorIndexingExtension && !hasRuntimeDescriptorArrayCapability))) {
            return result;
        }

        // Every descriptor runtime array must be rewritten before the capability can be dropped.
        const bool allRewritten = (arraySizes.size() == descriptorRuntimeArrays.size());

        // Find where the first rewritten array is declared: the uint type and length constants are
        // inserted right before it (types and constants may interleave in that section).
        size_t firstArrayInstruction = SIZE_MAX;
        for (size_t i = 0; i < instructions.size(); i++) {
            if ((instructions[i].opcode == OpTypeRuntimeArray) && (arraySizes.count(words[instructions[i].offset + 1]) != 0)) {
                firstArrayInstruction = i;
                break;
            }
        }

        uint32_t bound = words[3];
        const bool rewritingArrays = !arraySizes.empty();
        const bool moveUintType = rewritingArrays && (uintTypeId != 0) && (uintTypeInstruction > firstArrayInstruction);
        const bool createUintType = rewritingArrays && (uintTypeId == 0);
        if (createUintType) {
            uintTypeId = bound++;
        }

        std::unordered_map<uint32_t, uint32_t> lengthConstants; // length -> constant id
        for (const auto &it : arraySizes) {
            if (lengthConstants.count(it.second) == 0) {
                lengthConstants[it.second] = bound++;
            }
        }

        std::vector<uint32_t> output;
        output.reserve(words.size() + 16);
        output.insert(output.end(), words.begin(), words.begin() + 5);

        bool dynamicIndexingEmitted = hasDynamicIndexingCapability || !addDynamicIndexingCapability || !rewritingArrays;
        for (size_t i = 0; i < instructions.size(); i++) {
            const Instruction &instruction = instructions[i];
            const uint32_t *source = &words[instruction.offset];

            if (instruction.opcode == OpCapability) {
                if (allRewritten && (source[1] == CapabilityRuntimeDescriptorArray)) {
                    continue;
                }

                output.insert(output.end(), source, source + instruction.wordCount);
                if (!dynamicIndexingEmitted) {
                    output.push_back((2u << 16) | OpCapability);
                    output.push_back(CapabilitySampledImageArrayDynamicIndexing);
                    dynamicIndexingEmitted = true;
                }

                continue;
            }

            if ((instruction.opcode == OpExtension) && allRewritten) {
                if (readLiteralString(source + 1, instruction.wordCount - 1) == "SPV_EXT_descriptor_indexing") {
                    continue;
                }
            }

            if ((instruction.opcode == OpTypeInt) && moveUintType && (i == uintTypeInstruction)) {
                continue;
            }

            if (i == firstArrayInstruction) {
                if (createUintType || moveUintType) {
                    output.push_back((4u << 16) | OpTypeInt);
                    output.push_back(uintTypeId);
                    output.push_back(32);
                    output.push_back(0);
                }

                for (const auto &it : lengthConstants) {
                    output.push_back((4u << 16) | OpConstant);
                    output.push_back(uintTypeId);
                    output.push_back(it.second);
                    output.push_back(it.first);
                }
            }

            if (instruction.opcode == OpTypeRuntimeArray) {
                auto sizeIt = arraySizes.find(source[1]);
                if (sizeIt != arraySizes.end()) {
                    output.push_back((4u << 16) | OpTypeArray);
                    output.push_back(source[1]);
                    output.push_back(source[2]);
                    output.push_back(lengthConstants[sizeIt->second]);
                    result.rewrittenArrays++;
                    continue;
                }
            }

            output.insert(output.end(), source, source + instruction.wordCount);
        }

        output[3] = bound;
        words.swap(output);
        result.modified = true;
        return result;
    }
}
}
