#!/usr/bin/env python3
"""Build ONE continuous MP3 teacher tutorial from CRAFTSMANSHIP_TUTORIAL.md.

A single warm narrator (Samantha, en_US) speaks every **TEACHER:** line, like a
mentor talking you through the round. Short natural breaths between paragraphs,
slightly longer pauses at "## " section breaks. Targets ~15-18 minutes.
"""
import os
import re
import subprocess
import sys
import tempfile

SRC = os.path.join(os.path.dirname(__file__), "CRAFTSMANSHIP_TUTORIAL.md")
OUT_MP3 = os.path.join(os.path.dirname(__file__), "audio", "craftsmanship-tutorial-15min.mp3")

TEACHER_VOICE = "Samantha"
RATE = "160"  # words per minute — calm, teacherly pace

PARA_GAP = 0.9      # breath between teacher paragraphs
SECTION_GAP = 2.0   # longer pause at a new section

os.makedirs(os.path.dirname(OUT_MP3), exist_ok=True)


def clean(text: str) -> str:
    text = re.sub(r"\*\*(.*?)\*\*", r"\1", text)
    text = re.sub(r"\*(.*?)\*", r"\1", text)
    text = re.sub(r"_(.*?)_", r"\1", text)
    text = text.replace("`", "").replace("→", " then ")
    return re.sub(r"\s+", " ", text).strip()


def parse():
    """Return list of (kind, text). kind is 'speak' or 'section'."""
    items = []
    with open(SRC, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            if re.match(r"^##\s+", line):
                items.append(("section", ""))
                continue
            m = re.match(r"^\*\*TEACHER:\*\*\s*(.*)", line)
            if m:
                text = clean(m.group(1))
                if text:
                    items.append(("speak", text))
    return items


def make_silence(seconds: float, path: str):
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-f", "lavfi",
         "-i", "anullsrc=r=22050:cl=mono", "-t", f"{seconds}",
         "-c:a", "pcm_s16le", path],
        check=True,
    )


def main():
    items = parse()
    spoken = [i for i in items if i[0] == "speak"]
    if not spoken:
        print("No TEACHER lines parsed.", file=sys.stderr)
        sys.exit(1)
    print(f"Parsed {len(spoken)} spoken paragraphs.")

    with tempfile.TemporaryDirectory() as tmp:
        para_sil = os.path.join(tmp, "para.aiff")
        sec_sil = os.path.join(tmp, "sec.aiff")
        make_silence(PARA_GAP, para_sil)
        make_silence(SECTION_GAP, sec_sil)

        parts = []
        idx = 0
        for kind, text in items:
            if kind == "section":
                parts.append(sec_sil)
                continue
            aiff = os.path.join(tmp, f"para_{idx:03d}.aiff")
            subprocess.run(["say", "-v", TEACHER_VOICE, "-r", RATE, "-o", aiff, text], check=True)
            parts.append(aiff)
            parts.append(para_sil)
            idx += 1

        list_file = os.path.join(tmp, "list.txt")
        with open(list_file, "w", encoding="utf-8") as lf:
            for p in parts:
                lf.write(f"file '{p}'\n")

        subprocess.run(
            ["ffmpeg", "-y", "-loglevel", "error",
             "-f", "concat", "-safe", "0", "-i", list_file,
             "-codec:a", "libmp3lame", "-q:a", "4", OUT_MP3],
            check=True,
        )

    print(f"Done -> {OUT_MP3}")


if __name__ == "__main__":
    main()
