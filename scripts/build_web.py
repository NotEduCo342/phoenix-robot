#!/usr/bin/env python3
"""
Bundle web assets (HTML, CSS, JS) from components/alarm_clock/web/
into components/alarm_clock/src/web_assets.c for direct zero-copy embedding.
"""
import os

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB_DIR = os.path.join(PROJECT_ROOT, "components", "alarm_clock", "web")
OUT_FILE = os.path.join(PROJECT_ROOT, "components", "alarm_clock", "src", "web_assets.c")

def embed_file(var_name, filename):
    path = os.path.join(WEB_DIR, filename)
    with open(path, "rb") as f:
        data = f.read()
    hex_bytes = ", ".join(f"0x{b:02x}" for b in data)
    return (
        f"// {filename} ({len(data)} bytes)\n"
        f"const unsigned char {var_name}[] = {{{hex_bytes}, 0x00}};\n"
        f"const size_t {var_name}_len = {len(data)};\n\n"
    )

def main():
    out = "/* Auto-generated from components/alarm_clock/web/ - Do NOT edit manually */\n"
    out += "#include <stddef.h>\n\n"
    out += embed_file("index_html", "index.html")
    out += embed_file("style_css", "style.css")
    out += embed_file("app_js", "app.js")

    with open(OUT_FILE, "w") as f:
        f.write(out)
    print(f"Successfully generated {OUT_FILE}")

if __name__ == "__main__":
    main()

