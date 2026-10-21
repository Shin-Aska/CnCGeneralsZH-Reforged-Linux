# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
# How to run: pinned cached Python -B run_destroy_replacement.py --go after root GO.
import sys
from lifecycle_launch import Case, run

if __name__ == "__main__":
    run(Case.REPLACE, go=sys.argv[1:] == ["--go"])
