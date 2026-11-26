"""Render the 50-pixel RGBA8888 badges consumed by Ultrahand v2.5.4."""
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

DIRECTORY = Path(__file__).resolve().parent
OUTPUT = DIRECTORY.parent / "config/ultrahand/assets/notifications"

# A common drive silhouette, with a second glyph so events are distinct in monochrome.
BADGES = {
    "ams-usb": ("#5664fd", "#a44cff", '<path d="M34 32V22m0 0-3 3m3-3 3 3"/>'),
    "ams-usb-connected": ("#007ac5", "#0ab79e", '<path d="M34 21v11m-5.5-5.5h11"/>'),
    "ams-usb-safe": ("#5a42dd", "#b44ee6", '<path d="m28.5 27 4 4 7-9"/>'),
    "ams-usb-disconnected": ("#c45b09", "#e29420", '<path d="M29 26.5h10"/>'),
}

def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, (start, end, glyph) in BADGES.items():
        svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="200" height="200" viewBox="0 0 50 50">
        <defs><linearGradient id="g" x2="1" y2="1" gradientUnits="objectBoundingBox">
        <stop stop-color="{start}"/><stop offset="1" stop-color="{end}"/></linearGradient></defs>
        <rect x="2" y="2" width="46" height="46" rx="11" fill="#10172f"/>
        <rect x="3" y="3" width="44" height="44" rx="10" fill="none" stroke="url(#g)" stroke-width="2"/>
        <path d="M10 25l3-9h18l3 9v8H10z" fill="#182848" stroke="#e0edff" stroke-linejoin="round" stroke-width="2"/>
        <path d="M11 25h20" fill="none" stroke="#b6cffe" stroke-width="1.5"/>
        <circle cx="15" cy="29" r="1.5" fill="#8cddff"/>
        <circle cx="34" cy="26.5" r="10.7" fill="#111931"/>
        <circle cx="34" cy="26.5" r="9.4" fill="url(#g)"/>
        <g fill="none" stroke="white" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round">{glyph}</g></svg>'''
        with TemporaryDirectory() as directory:
            source = Path(directory) / "badge.svg"
            source.write_text(svg)
            data = subprocess.run(["ffmpeg", "-v", "error", "-i", str(source),
                                   "-vf", "scale=50:50:flags=lanczos", "-frames:v", "1",
                                   "-f", "rawvideo", "-pix_fmt", "rgba", "pipe:1"],
                                  stdout=subprocess.PIPE, check=True).stdout
        if len(data) != 50*50*4:
            raise ValueError(f"Invalid icon dimensions for {name}")
        (OUTPUT / (name + ".rgba")).write_bytes(data)

if __name__ == "__main__":
    main()
