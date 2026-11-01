#!/usr/bin/env python3
"""Expands a ToolKit .shader file and its includes into one GLSL ES translation unit.

ToolKit keeps shaders in an XML wrapper: a header that names the includes and the defines a variant can
take, and a <source> section holding GLSL. The engine inlines every include at the top of the main source
before compiling (Shader::HandleShaderIncludes), so the compiler sees one file. Mali Offline Compiler works
on that one file, which is what this produces.

    ./assemble-forward-shader.py <shader dir> defaultFragment.shader > forward.frag
    malioc --fragment -c Mali-G71 -D ShadowPCF=4 -D ShadingMode=1 forward.frag

The defines a variant needs are those the engine sets at build time (ShadowPCF, ShadingMode,
CascadeBlend, DrawAlphaMasked, highlightCascades for the forward shader).
"""
import os
import re
import sys


def parse(shader_dir, name):
    text = open(os.path.join(shader_dir, name), encoding='utf-8', errors='replace').read()
    includes = re.findall(r'<include\s+name\s*=\s*"([^"]+)"', text)
    match = re.search(r'<source>(.*?)</source>', text, re.S)
    body = match.group(1) if match else ''
    return includes, body.replace('<!--', '').replace('-->', '')


def assemble(shader_dir, root):
    seen, out = set(), []

    def emit(name):
        if name in seen:
            return
        seen.add(name)
        includes, body = parse(shader_dir, name)
        for include in includes:
            emit(include)
        out.append(f"// ==== {name} ====\n{body}\n")

    emit(root)
    header = ("#version 300 es\n"
              "precision highp float;\n"
              "precision highp sampler2D;\n"
              "precision highp sampler2DArray;\n"
              "precision highp samplerCube;\n"
              "precision highp int;\n")
    return header + '\n'.join(out)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    print(assemble(sys.argv[1], sys.argv[2]))
