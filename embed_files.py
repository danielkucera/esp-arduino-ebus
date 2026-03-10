#!/usr/bin/env python3
"""
PlatformIO pre-build script to embed HTML files into C++ headers
"""
import os

def embed_file(source_path, output_path, variable_name):
    """Embed a file into a C++ header with PROGMEM"""
    
    if not os.path.exists(source_path):
        raise Exception(f"Source file not found: {source_path}")
    
    with open(source_path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Escape backslashes and quotes for C++ string
    escaped = content.replace('\\', '\\\\').replace('"', '\\"')
    
    # Build the header
    header_content = f"""// Auto-generated - DO NOT EDIT
#ifndef EMBEDDED_{variable_name.upper()}_H
#define EMBEDDED_{variable_name.upper()}_H

static const char {variable_name}[] PROGMEM = "{escaped}";

#endif
"""
    
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(header_content)

def pre_build(ctx):
    """PlatformIO pre-build hook"""
    src_dir = os.path.join(ctx.project_dir, "src")
    include_dir = os.path.join(ctx.project_dir, "include")
    
    root_html = os.path.join(src_dir, "root.html")
    root_header = os.path.join(include_dir, "embedded_root.h")
    
    if os.path.exists(root_html):
        embed_file(root_html, root_header, "root_page")
        print(f"[embed_files] Generated {root_header}")
