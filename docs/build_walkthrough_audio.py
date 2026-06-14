#!/usr/bin/env python3
"""Generate per-segment MP3 audio from the spoken walkthrough.

Interviewer lines -> Daniel (en_GB). Candidate lines -> Samantha (en_US).
Each "## SEGMENT" (plus the mental-model intro and rapid-fire close)
becomes one MP3 so you can loop through them during revision.
"""
import os
import re
import subprocess
import sys
import tempfile

SRC = os.path.join(os.path.dirname(__file__), "EXECUTION_WALKTHROUGH_AUDIO.md")
OUT_DIR = os.path.join(os.path.dirname(__file__), "audio")

INTERVIEWER_VOICE = "Daniel"
CANDIDATE_VOICE = "Samantha"
RATE = "185"  # words per minute

os.makedirs(OUT_DIR, exist_ok=True)


def clean(text: str) -> str:
    """Strip markdown so TTS reads naturally."""
    text = re.sub(r"\*\*(.*?)\*\*", r"\1", text)   # bold
    text = re.sub(r"\*(.*?)\*", r"\1", text)        # italics
    text = re.sub(r"_(.*?)_", r"\1", text)          # underscore emphasis
    text = text.replace("`", "")
    text = text.replace("→", " then ")
    text = re.sub(r"\s+", " ", text).strip()
    return text


def parse():
    """Return list of (segment_title, [(voice, text), ...])."""
    segments = []
    title = None
    lines = []

    def flush():
        if title and lines:
            segments.append((title, list(lines)))

    with open(SRC, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.rstrip("\n")

            m = re.match(r"^##\s+(.*)", line)
            if m:
                heading = m.group(1).strip()
                # Start a new audio chunk only for real spoken sections.
                if heading.startswith("SEGMENT") or "Mental Model" in heading or "Rapid-Fire" in heading:
                    flush()
                    title = heading
                    lines = []
                else:
                    flush()
                    title = None
                    lines = []
                continue

            if title is None:
                continue

            mi = re.match(r"^\*\*INTERVIEWER:\*\*\s*(.*)", line)
            if mi:
                t = clean(mi.group(1))
                if t:
                    lines.append((INTERVIEWER_VOICE, t))
                continue

            my = re.match(r"^\*\*YOU(?:\s*\(map\))?:\*\*\s*(.*)", line)
            if my:
                t = clean(my.group(1))
                if t:
                    lines.append((CANDIDATE_VOICE, t))
                continue

    flush()
    return segments


def slugify(title: str, idx: int) -> str:
    base = re.sub(r"[^a-zA-Z0-9]+", "-", title.lower()).strip("-")
    base = re.sub(r"-+", "-", base)[:48]
    return f"{idx:02d}-{base}"


def synth_line(voice: str, text: str, aiff_path: str):
    subprocess.run(
        ["say", "-v", voice, "-r", RATE, "-o", aiff_path, text],
        check=True,
    )


def main():
    segments = parse()
    if not segments:
        print("No spoken segments found.", file=sys.stderr)
        sys.exit(1)

    for idx, (title, dialogue) in enumerate(segments, start=1):
        slug = slugify(title, idx)
        out_mp3 = os.path.join(OUT_DIR, f"{slug}.mp3")
        print(f"[{idx}/{len(segments)}] {title} -> {os.path.basename(out_mp3)} ({len(dialogue)} lines)")

        with tempfile.TemporaryDirectory() as tmp:
            part_files = []
            for i, (voice, text) in enumerate(dialogue):
                aiff = os.path.join(tmp, f"part_{i:03d}.aiff")
                synth_line(voice, text, aiff)
                part_files.append(aiff)

            # Concatenate AIFFs, then encode to MP3.
            list_file = os.path.join(tmp, "list.txt")
            with open(list_file, "w", encoding="utf-8") as lf:
                for p in part_files:
                    lf.write(f"file '{p}'\n")

            subprocess.run(
                [
                    "ffmpeg", "-y", "-loglevel", "error",
                    "-f", "concat", "-safe", "0", "-i", list_file,
                    "-codec:a", "libmp3lame", "-q:a", "4",
                    out_mp3,
                ],
                check=True,
            )

    print(f"\nDone. {len(segments)} MP3 files in {OUT_DIR}")


if __name__ == "__main__":
    main()
