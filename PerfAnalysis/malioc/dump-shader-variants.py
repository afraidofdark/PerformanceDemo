#!/usr/bin/env python3
"""Writes shader variants to files, and optionally costs each one with Mali Offline Compiler.

A ToolKit .shader declares the defines a variant may set in its header, and the engine picks a value for
each at build time (ShadowPCF from the shadow settings, ShadingMode from the renderer, DrawAlphaMasked per
material, ...). On device that means a rebuild and a device run per combination to compare two of them.
This writes the variants out instead, so malioc can cost them offline in a couple of seconds each - a
different question from what the device measures, but it ranks the levers inside the shader without
touching the device at all.

    ./dump-shader-variants.py <shader dir> defaultFragment.shader <out dir>
    ./dump-shader-variants.py <shader dir> defaultFragment.shader <out dir> --malioc <malioc> --core Mali-G71

Without --malioc only the variant sources are written. With it, every variant is compiled and the report's
cycle split is printed as a table, which is where a "which of these is worth trying on the device" answer
comes from.
"""
import itertools
import os
import re
import subprocess
import sys

VARIANTS = {
    "forward-baseline":   {},
    "forward-shadow0":    {"ShadowPCF": 0},
    "forward-shadow9":    {"ShadowPCF": 9},
    "forward-noblend":    {"CascadeBlend": 0},
    "forward-alphamask":  {"DrawAlphaMasked": 1},
    "forward-albedo":     {"ShadingMode": 2},
    "forward-normal":     {"ShadingMode": 3},
}

DEFAULTS = {
    "DrawAlphaMasked": 0,
    "ShadowPCF": 4,
    "CascadeBlend": 1,
    "highlightCascades": 0,
    "ShadingMode": 1,
}


def parse(shader_dir, name):
    text = open(os.path.join(shader_dir, name), encoding='utf-8', errors='replace').read()
    includes = re.findall(r'<include\s+name\s*=\s*"([^"]+)"', text)
    defines = dict(re.findall(r'<define\s+name\s*=\s*"([^"]+)"\s+val\s*=\s*"([^"]+)"', text))
    match = re.search(r'<source>(.*?)</source>', text, re.S)
    body = match.group(1) if match else ''
    return includes, defines, body.replace('<!--', '').replace('-->', '')


def assemble(shader_dir, root):
    seen, out = set(), []

    def emit(name):
        if name in seen:
            return
        seen.add(name)
        includes, _, body = parse(shader_dir, name)
        for include in includes:
            emit(include)
        out.append(f"// ==== {name} ====\n{body}\n")

    emit(root)
    return '\n'.join(out)


def variant_source(shader_dir, root, values):
    header = ("#version 300 es\n"
              "precision highp float;\n"
              "precision highp sampler2D;\n"
              "precision highp sampler2DArray;\n"
              "precision highp samplerCube;\n"
              "precision highp int;\n")
    prologue = "".join(f"#define {name} {value}\n" for name, value in sorted(values.items()))
    return header + prologue + assemble(shader_dir, root)


def cost(malioc, core, path):
    out = subprocess.run([malioc, "--fragment", "-c", core, path], capture_output=True, text=True).stdout
    def num(pattern):
        match = re.search(pattern, out)
        return match.group(1) if match else "?"

    return {
        "A": num(r"Total instruction cycles:\s+([\d.]+)"),
        "LS": num(r"Total instruction cycles:\s+[\d.]+\s+([\d.]+)"),
        "T": num(r"Total instruction cycles:\s+[\d.]+\s+[\d.]+\s+[\d.]+\s+([\d.]+)"),
        "bound": num(r"Total instruction cycles:[^\n]*?([A-Z])\s*\n"),
        "regs": num(r"Work registers: (\d+)"),
        "spill": num(r"Spill region: (\d+) bytes"),
        "fp16": num(r"16-bit arithmetic: (\d+)%"),
    }


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2

    shader_dir, root, out_dir = argv[1], argv[2], argv[3]
    malioc = core = None
    if "--malioc" in argv:
        malioc = argv[argv.index("--malioc") + 1]
        core = argv[argv.index("--core") + 1] if "--core" in argv else "Mali-G71"

    os.makedirs(out_dir, exist_ok=True)
    rows = []
    for label, overrides in VARIANTS.items():
        values = dict(DEFAULTS)
        values.update(overrides)
        path = os.path.join(out_dir, f"{label}.frag")
        open(path, "w").write(variant_source(shader_dir, root, values))
        if malioc is not None:
            rows.append((label, cost(malioc, core, path)))
        print(f"wrote {path}")

    if rows:
        print(f"\n{'variant':22s} {'A':>7s} {'LS':>7s} {'T':>7s} {'bound':>6s} {'regs':>5s} {'spill':>6s} {'fp16':>5s}")
        for label, c in rows:
            print(f"{label:22s} {c['A']:>7s} {c['LS']:>7s} {c['T']:>7s} {c['bound']:>6s} "
                  f"{c['regs']:>5s} {c['spill']:>6s} {c['fp16']:>5s}")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
