#!/usr/bin/env python3
"""Build ONE continuous MP3 mapping-drill from LEADERSHIP_MAPPING_DRILL.md.

Voices:
  INTERVIEWER -> Daniel (en_GB)
  COACH       -> Karen  (en_AU)

After each COACH cue there is a long silence for YOU to answer out loud.
No model answers are spoken. Targets ~20-25 minutes across 50 questions.
"""
import os
import re
import subprocess
import sys
import tempfile

SRC = os.path.join(os.path.dirname(__file__), "LEADERSHIP_MAPPING_DRILL.md")
OUT_MP3 = os.path.join(os.path.dirname(__file__), "audio", "leadership-mapping-drill-20min.mp3")

VOICES = {"INTERVIEWER": "Daniel", "COACH": "Karen"}
RATE = "180"

# Silence (seconds) inserted AFTER a turn of the given role.
GAP_AFTER = {
    "INTERVIEWER": 0.4,   # brief beat before the coach names the story
    "COACH": 11.0,        # long pause for YOU to answer out loud
}

os.makedirs(os.path.dirname(OUT_MP3), exist_ok=True)


def clean(text: str) -> str:
    text = re.sub(r"\*\*(.*?)\*\*", r"\1", text)
    text = re.sub(r"\*(.*?)\*", r"\1", text)
    text = re.sub(r"_(.*?)_", r"\1", text)
    text = text.replace("`", "").replace("→", " then ")
    return re.sub(r"\s+", " ", text).strip()


def parse():
    turns = []
    with open(SRC, encoding="utf-8") as fh:
        for raw in fh:
            m = re.match(r"^\*\*(INTERVIEWER|COACH):\*\*\s*(.*)", raw.rstrip("\n"))
            if m:
                text = clean(m.group(2))
                if text:
                    turns.append((m.group(1), text))
    return turns


def make_silence(seconds: float, path: str):
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-f", "lavfi",
         "-i", "anullsrc=r=22050:cl=mono", "-t", f"{seconds}",
         "-c:a", "pcm_s16le", path],
        check=True,
    )


def main():
    turns = parse()
    if not turns:
        print("No turns parsed.", file=sys.stderr)
        sys.exit(1)
    print(f"Parsed {len(turns)} spoken turns.")

    with tempfile.TemporaryDirectory() as tmp:
        silence = {}
        for sec in set(GAP_AFTER.values()):
            sp = os.path.join(tmp, f"sil_{sec}.aiff")
            make_silence(sec, sp)
            silence[sec] = sp

        parts = []
        for i, (role, text) in enumerate(turns):
            aiff = os.path.join(tmp, f"turn_{i:03d}.aiff")
            subprocess.run(["say", "-v", VOICES[role], "-r", RATE, "-o", aiff, text], check=True)
            parts.append(aiff)
            parts.append(silence[GAP_AFTER[role]])

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
