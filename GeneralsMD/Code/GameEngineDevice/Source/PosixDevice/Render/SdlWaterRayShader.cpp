#include "SdlWaterRayFilter.h"
#include "SdlWaterRayQueryCode.h"
#include "../../../../Libraries/Source/SPIRV-Cross/spirv.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_set>

namespace {

using Word = uint32_t;
using Words = std::vector<Word>;

struct Instruction {
    const Word *words;
    size_t count;

    Instruction(const Word *data, size_t size) : words(data), count(size) {}
    Instruction(const Words &data) : Instruction(data.data(), data.size()) {}
    template<size_t N> Instruction(const Word (&data)[N]) : Instruction(data, N) {}
    template<size_t N> Instruction(const std::array<Word,N> &data) : Instruction(data.data(), N) {}

    const Word *begin() const { return words; }
    const Word *end() const { return words+count; }
    const Word *data() const { return words; }
    size_t size() const { return count; }
    Word operator[](size_t index) const { return words[index]; }
};

constexpr Word HeaderWords = 5;
constexpr Word BoundIndex = 3;
constexpr char HelperName[] = "ZHR_WaterRayFirstStep(vf3;vf3;f1;vu4;";

unsigned opcode(Instruction instruction)
{
    return instruction[0] & 0xffffu;
}

void append(Words &destination, Instruction instruction)
{
    if (instruction.size())
        destination.insert(destination.end(), instruction.begin(), instruction.end());
}

bool uniqueType(unsigned op)
{
    return op == spv::OpTypeBool || op == spv::OpTypeInt || op == spv::OpTypeFloat ||
        op == spv::OpTypeVector || op == spv::OpTypePointer;
}

bool named(Instruction instruction, const char *name, size_t size)
{
    return instruction.size() >= 3 && (instruction.size() - 2) * sizeof(Word) >= size &&
        std::memcmp(instruction.data() + 2, name, size) == 0;
}

struct Relocate {
    std::array<Word, WaterRayQueryCode::Bound> map {};
    Word next;

    Word id(Word original)
    {
        if (!map[original])
            map[original] = next++;
        return map[original];
    }

    void apply(Instruction table, Words &output, const std::vector<Instruction> &oldTypes = {})
    {
        for (size_t offset = 0; offset < table.size();) {
            const Word mask = table[offset++];
            const Instruction record(table.data()+offset, table[offset] >> 16);
            offset += record.size();
            const size_t start = output.size();
            append(output, record);
            for (size_t index = 1; index < record.size(); ++index) {
                if (mask & (1u << index))
                    output[start + index] = id(record[index]);
            }
            if (!uniqueType(opcode(record)))
                continue;
            // Non-aggregate SPIR-V types must have a single ID. Match the type's
            // resolved storage class and referred IDs, independently of its result ID.
            const auto match = std::find_if(oldTypes.begin(), oldTypes.end(), [&](Instruction old) {
                return old[0] == record[0] && std::equal(old.begin() + 2, old.end(), output.begin() + start + 2);
            });
            if (match != oldTypes.end()) {
                map[record[1]] = (*match)[1];
                output.resize(start);
            }
        }
    }
};

} // namespace

bool SdlInstallWaterRayQuery(std::vector<unsigned char> &bytes)
{
    if (bytes.size() < HeaderWords * sizeof(Word) || bytes.size() % sizeof(Word) != 0)
        return false;
    Words source(bytes.size() / sizeof(Word));
    std::memcpy(source.data(), bytes.data(), bytes.size());
    if (source[0] != spv::MagicNumber || source[1] != 0x00010000u || !source[BoundIndex])
        return false; // SPIR-V 1.0 has the legacy entry point interface layout.

    std::vector<Instruction> program, types;
    Word helper = 0, glsl = 0;
    size_t capabilityEnd = 0, extensionEnd = 0, typesStart = 0, functionsStart = 0;
    for (size_t offset = HeaderWords; offset < source.size();) {
        const size_t count = source[offset] >> 16;
        if (!count || count > source.size() - offset)
            return false;
        const Instruction instruction(source.data() + offset, count);
        const unsigned op = opcode(instruction);
        if (op == spv::OpCapability)
            capabilityEnd = program.size() + 1;
        if (op == spv::OpExtension)
            extensionEnd = program.size() + 1;
        if (op == spv::OpName && named(instruction, HelperName, sizeof(HelperName))) {
            if (helper)
                return false;
            helper = instruction[1];
        }
        if (op == spv::OpExtInstImport && named(instruction, "GLSL.std.450", sizeof("GLSL.std.450")))
            glsl = instruction[1];
        if (uniqueType(op))
            types.push_back(instruction);
        if (!typesStart && op >= spv::OpTypeVoid && op <= spv::OpTypeForwardPointer)
            typesStart = program.size();
        if (!functionsStart && op == spv::OpFunction)
            functionsStart = program.size();
        program.push_back(instruction);
        offset += count;
    }
    if (!helper || !capabilityEnd || !typesStart || !functionsStart ||
        (WaterRayQueryCode::GlslImport && !glsl))
        return false;
    extensionEnd = std::max(extensionEnd, capabilityEnd);

    size_t bodyStart = 0, bodyEnd = 0;
    Relocate relocate {{}, source[BoundIndex]};
    if constexpr (WaterRayQueryCode::GlslImport)
        relocate.map[WaterRayQueryCode::GlslImport] = glsl;
    for (size_t index = functionsStart; index < program.size(); ++index) {
        const Instruction function = program[index];
        if (opcode(function) != spv::OpFunction || function.size() != 5 || function[2] != helper)
            continue;
        size_t next = index + 1;
        for (Word parameter : WaterRayQueryCode::ParameterIds) {
            if (next == program.size() || opcode(program[next]) != spv::OpFunctionParameter ||
                program[next].size() != 3)
                return false;
            relocate.map[parameter] = program[next++][2];
        }
        if (next == program.size() || opcode(program[next]) != spv::OpLabel)
            return false;
        bodyStart = next;
        while (next < program.size() && opcode(program[next]) != spv::OpFunctionEnd)
            ++next;
        if (next == program.size())
            return false;
        bodyEnd = next;
        break;
    }
    if (!bodyStart)
        return false;

    std::unordered_set<Word> typeIds, discarded;
    for (size_t i = typesStart; i < functionsStart; ++i) {
        const auto instruction = program[i];
        const unsigned op = opcode(instruction);
        if (op >= spv::OpTypeVoid && op < spv::OpTypeForwardPointer)
            typeIds.insert(instruction[1]);
    }
    for (size_t i = bodyStart; i < bodyEnd; ++i) {
        const auto instruction = program[i];
        if (opcode(instruction) == spv::OpLabel)
            discarded.insert(instruction[1]);
        else if (instruction.size() >= 3 && typeIds.count(instruction[1]))
            discarded.insert(instruction[2]);
    }

    Words declarations, body, decorations;
    relocate.apply(WaterRayQueryCode::Declarations, declarations, types);
    relocate.apply(WaterRayQueryCode::Body, body);
    relocate.apply(WaterRayQueryCode::Decorations, decorations);

    Words modified(source.begin(), source.begin() + HeaderWords);
    bool addressModel = false;
    for (size_t index = 0; index < program.size(); ++index) {
        if (index == capabilityEnd)
            relocate.apply(WaterRayQueryCode::Capabilities, modified);
        if (index == extensionEnd)
            relocate.apply(WaterRayQueryCode::Extensions, modified);
        if (index == typesStart)
            append(modified, decorations);
        if (index == functionsStart)
            append(modified, declarations);
        if (index == bodyStart) {
            append(modified, body);
            index = bodyEnd;
        }
        const auto instruction = program[index];
        const unsigned op = opcode(instruction);
        if ((op == spv::OpName || op == spv::OpDecorate) && discarded.count(instruction[1]))
            continue;
        append(modified, instruction);
        if (op == spv::OpMemoryModel) {
            if (addressModel || instruction.size() != 3 || instruction[1] != spv::AddressingModelLogical)
                return false;
            if constexpr (WaterRayQueryCode::PhysicalAddressing)
                modified[modified.size() - 2] = spv::AddressingModelPhysicalStorageBuffer64;
            addressModel = true;
        }
    }
    if (!addressModel)
        return false;
    modified[BoundIndex] = relocate.next;
    bytes.resize(modified.size() * sizeof(Word));
    std::memcpy(bytes.data(), modified.data(), bytes.size());
    return true;
}
