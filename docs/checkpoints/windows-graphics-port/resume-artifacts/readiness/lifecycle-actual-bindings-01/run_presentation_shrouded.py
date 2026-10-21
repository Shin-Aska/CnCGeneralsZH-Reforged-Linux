# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
# How to run: pinned cached Python -B run_presentation_shrouded.py --go after root GO.
import sys
from lifecycle_launch import Case, run

if __name__ == "__main__":
    run(Case.SHROUDED, go=sys.argv[1:] == ["--go"])
