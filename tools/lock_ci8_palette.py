from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent

FILES = [
    ROOT / "carpet_border8.ci8.png",
    ROOT / "carpet_border8-mip1.ci8.png",
    ROOT / "carpet_border8-mip2.ci8.png",
]


def load_rgb(path: Path) -> Image.Image:
    image = Image.open(path).convert("RGBA")

    if image.getchannel("A").getextrema() != (255, 255):
        raise RuntimeError(f"{path.name} contains transparency")

    return image.convert("RGB")


def verify_indexed_png(path: Path) -> list[int]:
    image = Image.open(path)
    image.load()

    if image.mode != "P":
        raise RuntimeError(f"{path.name} is not an indexed PNG")

    # Confirm PNG IHDR: bit depth 8, color type 3 (indexed).
    header = path.read_bytes()[:26]

    if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise RuntimeError(f"{path.name} is not a valid PNG")

    bit_depth = header[24]
    color_type = header[25]

    if bit_depth != 8 or color_type != 3:
        raise RuntimeError(
            f"{path.name}: expected indexed 8-bit PNG, "
            f"got bit depth {bit_depth}, color type {color_type}"
        )

    palette = image.getpalette()

    if palette is None or len(palette) < 768:
        raise RuntimeError(f"{path.name} does not contain a full CI8 palette")

    return palette[:768]


for source in FILES:
    if not source.exists():
        raise FileNotFoundError(source)

# Create the shared 256-color palette from the full-resolution texture.
master = load_rgb(FILES[0]).quantize(
    colors=256,
    method=Image.Quantize.MEDIANCUT,
    dither=Image.Dither.NONE,
)

master_palette = master.getpalette()[:768]
temporary_files = []

try:
    # Convert all three textures using the exact same palette and index order.
    for source in FILES:
        indexed = load_rgb(source).quantize(
            palette=master,
            dither=Image.Dither.NONE,
        )

        indexed.putpalette(master_palette)

        temporary = source.with_suffix(".tmp.png")
        indexed.save(
            temporary,
            format="PNG",
            bits=8,
            optimize=False,
        )

        temporary_files.append(temporary)

    # Validate the actual files before replacing anything.
    palettes = [verify_indexed_png(path) for path in temporary_files]

    if any(palette != palettes[0] for palette in palettes[1:]):
        raise RuntimeError("Generated PNG palettes do not match")

    # Replace the originals only after all three pass validation.
    for temporary, original in zip(temporary_files, FILES):
        temporary.replace(original)

finally:
    for temporary in temporary_files:
        temporary.unlink(missing_ok=True)

print("PASS: replaced all three originals")
print("All textures are indexed 8-bit PNGs with an identical palette")
