"""Extract the fragment ray query and its SPIR-V declarations.

Compile SdlWaterRayQuery.frag with glslangValidator --target-env vulkan1.2 --target-env spirv1.0
before passing the resulting SPIR-V file and SdlWaterRayQueryCode.h here.
"""

import argparse
import re
import shlex
import struct
import subprocess
from pathlib import Path

from dataclasses import dataclass


@dataclass
class Instruction:
    opcode: str
    words: tuple[int, ...]
    mask: int
    result: int

    def ids(self):
        return {word for i, word in enumerate(self.words) if self.mask & (1 << i)}


def instructions(shader):
    text = subprocess.check_output(["spirv-dis", "--raw-id", shader], text=True)
    names = [shlex.split(line,posix=False) for line in text.splitlines()
        if line.strip() and not line.startswith(";")]
    binary = struct.unpack(f"<{shader.stat().st_size // 4}I", shader.read_bytes())
    out = []
    offset = 5
    for parts in names:
        count = binary[offset] >> 16
        opcode = parts[2 if parts[1:2] == ["="] else 0]
        operands = parts[3 if parts[1:2] == ["="] else 1:]
        result = int(parts[0][1:]) if parts[1:2] == ["="] else 0
        words = binary[offset:offset + count]
        if result:
            # A result type precedes the result ID in binary instructions.
            slot = 0 if words[1] == result else 1
            operands.insert(slot, f"%{result}")
        mask = 0
        index = 1
        for field in operands:
            if re.fullmatch(r"%[0-9]+", field):
                assert index < count and int(field[1:]) == words[index], parts
                mask |= 1 << index
            index += (len(field[1:-1].encode()) + 4) // 4 if field.startswith('"') else 1
        out.append(Instruction(opcode, words, mask, result))
        offset += count
    assert offset == len(binary)
    return text, out, binary[3]


def static_words(out, name, records):
    if not records:
        out.append(f"inline constexpr std::array<uint32_t, 0> {name} = {{}};")
        return
    out.append(f"inline constexpr uint32_t {name}[] = {{")
    for record in records:
        words = ", ".join(f"0x{word:08x}u" for word in record.words)
        out.append(f"    0x{record.mask:08x}u, {words}, // {record.opcode}")
    out.append("};")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("shader", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    text, code, bound = instructions(args.shader)
    function = int(re.search(r'OpName %([0-9]+) "ZHR_WaterRayFirstStep\(', text)[1])
    start = next(i for i, instruction in enumerate(code) if instruction.result == function)
    end = next(i for i in range(start + 1, len(code)) if code[i].opcode == "OpFunctionEnd")
    parameters = code[start + 1:start + 5]
    assert all(parameter.opcode == "OpFunctionParameter" for parameter in parameters)
    body = code[start + 5:end]
    assert body[0].opcode == "OpLabel"
    first_function = next(i for i, instruction in enumerate(code) if instruction.opcode == "OpFunction")

    definitions = {instruction.result: instruction for instruction in code[:first_function]
        if instruction.result}
    resolved = set(parameter.result for parameter in parameters)
    resolved.update(instruction.result for instruction in body if instruction.result)
    required = set().union(*(instruction.ids() for instruction in body)) - resolved
    while required:
        identity = required.pop()
        children = definitions[identity].ids() - {identity}
        resolved.add(identity)
        required.update(children - resolved)

    import_id = next((instruction.result for instruction in code
                      if instruction.opcode == "OpExtInstImport" and instruction.result in resolved), 0)
    declarations = [instruction for instruction in code[:first_function]
        if (instruction.result in resolved and instruction.result != import_id)
        or (instruction.opcode == "OpTypeForwardPointer" and instruction.words[1] in resolved)]
    decorations = [instruction for instruction in code[:first_function]
        if instruction.opcode in ("OpDecorate", "OpMemberDecorate") and instruction.words[1] in resolved]
    capabilities = [instruction for instruction in code
        if instruction.opcode == "OpCapability" and instruction.words[1] != 1]
    extensions = [instruction for instruction in code if instruction.opcode == "OpExtension"]
    words = ["// Generated from SdlWaterRayQuery.frag. Every instruction starts with an ID operand mask.",
        "#pragma once", "#include <array>", "#include <cstdint>", "namespace WaterRayQueryCode {",
        f"inline constexpr uint32_t Bound = {bound};",
        f"inline constexpr uint32_t GlslImport = {import_id};",
        "inline constexpr bool PhysicalAddressing = " +
            ("true;" if "OpMemoryModel PhysicalStorageBuffer64 GLSL450" in text else "false;"),
        "inline constexpr uint32_t ParameterIds[] = {" +
            ", ".join(str(parameter.result) for parameter in parameters) + "};"]
    static_words(words, "Capabilities", capabilities)
    static_words(words, "Extensions", extensions)
    static_words(words, "Declarations", declarations)
    static_words(words, "Decorations", decorations)
    static_words(words, "Body", body)
    words.append("}")
    args.output.write_text("\n".join(words) + "\n")


if __name__ == "__main__":
    main()
