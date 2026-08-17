import json
import base64
import io
import struct
import sys
import os

os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Redirect print to log file
_LOG = open("merge_orm_log.txt", "w", encoding="utf-8")
_original_print = print
def print(*args, **kwargs):
    kwargs["file"] = _LOG
    _original_print(*args, **kwargs)
    _LOG.flush()

GLTF_PATH = r"SandBox\models\helmat\models\DamagedHelmet\glTF-Embedded\DamagedHelmet.gltf"
OUT_PATH   = r"SandBox\models\helmat\models\DamagedHelmet\glTF-Embedded\DamagedHelmet.gltf.backup"

print("Loading glTF...")
with open(GLTF_PATH, "r", encoding="utf-8") as f:
    data = json.load(f)

images  = data.get("images", [])
textures = data.get("textures", [])
materials = data.get("materials", [])

print(f"Images: {len(images)}, Textures: {len(textures)}, Materials: {len(materials)}")

# --- Decode a data-URI image to raw RGBA pixels ---
def decode_uri(uri):
    """Decode data: URI to raw RGBA bytes using stb_image-like approach via PNG decode."""
    if not uri.startswith("data:"):
        return None, 0, 0
    header, b64 = uri.split(",", 1)
    raw = base64.b64decode(b64)
    return decode_png(raw)

def decode_png(raw):
    """Minimal PNG decoder - handles 8-bit RGBA/RGB PNG files."""
    # Check PNG signature
    if raw[:8] != b'\x89PNG\r\n\x1a\n':
        return None, 0, 0
    
    import zlib
    width = height = 0
    bit_depth = color_type = 0
    pixels = None
    pos = 8
    
    while pos < len(raw):
        length = struct.unpack(">I", raw[pos:pos+4])[0]
        chunk_type = raw[pos+4:pos+8].decode("ascii", errors="ignore")
        chunk_data = raw[pos+8:pos+8+length]
        
        if chunk_type == "IHDR":
            width = struct.unpack(">I", chunk_data[0:4])[0]
            height = struct.unpack(">I", chunk_data[4:8])[0]
            bit_depth = chunk_data[8]
            color_type = chunk_data[9]
            print(f"  PNG: {width}x{height}, bit_depth={bit_depth}, color_type={color_type}")
            
        elif chunk_type == "IDAT":
            if pixels is None:
                # Decompress all IDAT chunks
                pixels = zlib.decompress(chunk_data)
            else:
                pixels += zlib.decompress(chunk_data)
                
        elif chunk_type == "IEND":
            break
            
        pos += 12 + length
    
    if pixels is None:
        return None, 0, 0
    
    # Unfilter and convert to RGBA
    bpp = 3 if color_type == 2 else 4  # RGB or RGBA
    stride = width * bpp
    raw_rows = []
    
    # Reconstruct rows (PNG filter)
    row_size = stride + 1  # +1 for filter byte
    for y in range(height):
        row_start = y * row_size
        if row_start + row_size > len(pixels):
            # Try without filter byte (uncompressed)
            row_start = y * stride
            row = pixels[row_start:row_start+stride]
        else:
            row = pixels[row_start:row_start+row_size]
        raw_rows.append(row)
    
    # Apply PNG filters
    decoded = bytearray()
    for y, row in enumerate(raw_rows):
        filter_type = row[0]
        scanline = bytearray(row[1:])
        
        if filter_type == 1:  # Sub
            for x in range(bpp, len(scanline)):
                scanline[x] = (scanline[x] + scanline[x - bpp]) & 0xFF
        elif filter_type == 2:  # Up
            if y > 0:
                prev = raw_rows[y-1][1:]
                for x in range(len(scanline)):
                    scanline[x] = (scanline[x] + prev[x]) & 0xFF
        elif filter_type == 3:  # Average
            for x in range(len(scanline)):
                a = scanline[x - bpp] if x >= bpp else 0
                b = prev[x] if y > 0 else 0
                scanline[x] = (scanline[x] + ((a + b) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for x in range(len(scanline)):
                a = scanline[x - bpp] if x >= bpp else 0
                b = prev[x] if y > 0 else 0
                c = prev[x - bpp] if y > 0 and x >= bpp else 0
                p = a + b - c
                pa = abs(p - a)
                pb = abs(p - b)
                pc = abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                scanline[x] = (scanline[x] + pr) & 0xFF
        
        decoded.extend(scanline)
    
    # Convert to RGBA
    if bpp == 3:
        rgba = bytearray(width * height * 4)
        for i in range(width * height):
            rgba[i*4+0] = decoded[i*3+0]
            rgba[i*4+1] = decoded[i*3+1]
            rgba[i*4+2] = decoded[i*3+2]
            rgba[i*4+3] = 255
        return bytes(rgba), width, height
    else:
        return bytes(decoded), width, height


# --- Encode RGBA to PNG (base64 data URI) ---
def encode_png(rgba_bytes, width, height):
    """Encode RGBA raw pixels to PNG base64 data URI."""
    import zlib
    
    def write_chunk(chunk_type, data):
        chunk = chunk_type + data
        crc = struct.pack(">I", zlib.crc32(chunk) & 0xFFFFFFFF)
        return struct.pack(">I", len(data)) + chunk + crc
    
    # IHDR
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8-bit RGBA
    
    # IDAT - raw pixels with filter byte 0 for each row
    raw = b""
    for y in range(height):
        raw += b"\x00"  # filter: None
        raw += rgba_bytes[y*width*4:(y+1)*width*4]
    
    compressed = zlib.compress(raw)
    
    png = b'\x89PNG\r\n\x1a\n'
    png += write_chunk(b"IHDR", ihdr)
    png += write_chunk(b"IDAT", compressed)
    png += write_chunk(b"IEND", b"")
    
    b64 = base64.b64encode(png).decode("ascii")
    return f"data:image/png;base64,{b64}"


# Redirect all output to a log file
LOG_PATH = r"merge_orm_log.txt"
import sys as _sys
_original_stdout = _sys.stdout
_log = open(LOG_PATH, "w", encoding="utf-8")
_sys.stdout = _log

def tee(msg):
    _log.write(msg + "\n")
    _log.flush()

# ============================================================
# MAIN LOGIC
# ============================================================

mat = materials[0]
print(f"\nMaterial: {mat.get('name', 'unnamed')}")

# Find which image indices correspond to AO and MR
ao_tex_idx  = mat.get("occlusionTexture", {}).get("index", -1)
mr_tex_idx  = mat["pbrMetallicRoughness"].get("metallicRoughnessTexture", {}).get("index", -1)
emissive_tex_idx = mat.get("emissiveTexture", {}).get("index", -1)
normal_tex_idx   = mat.get("normalTexture", {}).get("index", -1)
base_color_tex_idx = mat["pbrMetallicRoughness"].get("baseColorTexture", {}).get("index", -1)

print(f"  baseColorTexture: {base_color_tex_idx}")
print(f"  metallicRoughnessTexture: {mr_tex_idx}")
print(f"  emissiveTexture: {emissive_tex_idx}")
print(f"  normalTexture: {normal_tex_idx}")
print(f"  occlusionTexture: {ao_tex_idx}")

if ao_tex_idx < 0 or mr_tex_idx < 0:
    print("ERROR: Need both AO and MR textures to merge!")
    sys.exit(1)

ao_image_idx = textures[ao_tex_idx]["source"]
mr_image_idx = textures[mr_tex_idx]["source"]
print(f"\nAO texture[{ao_tex_idx}] → image[{ao_image_idx}]")
print(f"MR texture[{mr_tex_idx}] → image[{mr_image_idx}]")

# Decode both images
print("\nDecoding AO image...")
ao_rgba, ao_w, ao_h = decode_uri(images[ao_image_idx]["uri"])
print(f"  AO: {ao_w}x{ao_h}, {len(ao_rgba)} bytes RGBA")

print("Decoding MR image...")
mr_rgba, mr_w, mr_h = decode_uri(images[mr_image_idx]["uri"])
print(f"  MR: {mr_w}x{mr_h}, {len(mr_rgba)} bytes RGBA")

if ao_rgba is None or mr_rgba is None:
    print("ERROR: Failed to decode images!")
    sys.exit(1)

# Merge: use larger dimensions
out_w = max(ao_w, mr_w)
out_h = max(ao_h, mr_h)
merged = bytearray(out_w * out_h * 4)

for y in range(out_h):
    for x in range(out_w):
        ao_idx = ((y * ao_h // out_h) * ao_w + (x * ao_w // out_w)) * 4
        mr_idx = ((y * mr_h // out_h) * mr_w + (x * mr_w // out_w)) * 4
        out_idx = (y * out_w + x) * 4
        
        merged[out_idx + 0] = ao_rgba[ao_idx + 0]   # R = AO
        merged[out_idx + 1] = mr_rgba[mr_idx + 1]   # G = Roughness
        merged[out_idx + 2] = mr_rgba[mr_idx + 2]   # B = Metallic
        merged[out_idx + 3] = 255                    # A = 1.0

# Encode merged image
print(f"\nEncoding merged ORM ({out_w}x{out_h})...")
orm_uri = encode_png(bytes(merged), out_w, out_h)
print(f"  Encoded URI length: {len(orm_uri)}")

# Back up original
print(f"\nBacking up original to: {OUT_PATH}")
with open(OUT_PATH, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=4)

# --- Modify glTF data ---

# Replace MR image with merged ORM
images[mr_image_idx]["uri"] = orm_uri

# Remove AO texture and image (careful with indices)
# Since we're removing texture[ao_tex_idx] and image[ao_image_idx],
# we need to adjust all references

# First, remove the AO texture
texture_to_remove = ao_tex_idx
image_to_remove = ao_image_idx

del textures[texture_to_remove]
del images[image_to_remove]

# Now adjust all texture references in material
# texture indices: old → new mapping
def remap_tex(old_idx):
    if old_idx < 0:
        return old_idx
    if old_idx == texture_to_remove:
        return -1  # shouldn't happen
    if old_idx > texture_to_remove:
        return old_idx - 1
    return old_idx

# Update material
if "occlusionTexture" in mat:
    del mat["occlusionTexture"]
    print("  Removed occlusionTexture from material")

mat["emissiveTexture"]["index"] = remap_tex(emissive_tex_idx)
mat["normalTexture"]["index"] = remap_tex(normal_tex_idx)
mat["pbrMetallicRoughness"]["baseColorTexture"]["index"] = remap_tex(base_color_tex_idx)
mat["pbrMetallicRoughness"]["metallicRoughnessTexture"]["index"] = remap_tex(mr_tex_idx)

print(f"\nFinal texture references:")
print(f"  baseColorTexture: {mat['pbrMetallicRoughness']['baseColorTexture']['index']}")
print(f"  metallicRoughnessTexture: {mat['pbrMetallicRoughness']['metallicRoughnessTexture']['index']}")
print(f"  emissiveTexture: {mat['emissiveTexture']['index']}")
print(f"  normalTexture: {mat['normalTexture']['index']}")
print(f"  occlusionTexture: REMOVED")
print(f"  Remaining textures: {len(textures)}, images: {len(images)}")

# Save
print(f"\nSaving to: {GLTF_PATH}")
with open(GLTF_PATH, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=4)

print("Done!")
