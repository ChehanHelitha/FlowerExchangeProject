"""
Flower Exchange — Python Bridge Server
======================================
Acts as the middleware between the web Trader Application (frontend)
and the compiled C++ Exchange Engine (backend).

Responsibilities:
  1. Serve the frontend HTML page
  2. Accept orders.csv file uploads
  3. Invoke the C++ binary via subprocess
  4. Return the generated execution_rep.csv as JSON
"""

import os
import csv
import json
import subprocess
import platform
from pathlib import Path
from flask import Flask, request, jsonify, render_template, send_from_directory
from werkzeug.utils import secure_filename

# ── Configuration ─────────────────────────────────────────────────────────────

app = Flask(__name__)

# Directory where uploaded orders.csv and output execution_rep.csv will live.
# Kept in the same folder as the C++ binary for simplicity.
BASE_DIR      = Path(__file__).parent
UPLOAD_FOLDER = BASE_DIR / "exchange_data"
UPLOAD_FOLDER.mkdir(exist_ok=True)

INPUT_FILE  = UPLOAD_FOLDER / "orders.csv"
OUTPUT_FILE = UPLOAD_FOLDER / "execution_rep.csv"

# Path to the compiled C++ binary.
# Adjust this if your build output directory is different.
if platform.system() == "Windows":
    CPP_BINARY = BASE_DIR.parent / "build" / "Release" / "FlowerExchange.exe"
    # Fallback for Debug builds
    if not CPP_BINARY.exists():
        CPP_BINARY = BASE_DIR.parent / "build" / "Debug" / "FlowerExchange.exe"
else:
    CPP_BINARY = BASE_DIR.parent / "build" / "FlowerExchange"

app.config["MAX_CONTENT_LENGTH"] = 16 * 1024 * 1024  # 16 MB max upload

# ── Routes ────────────────────────────────────────────────────────────────────

@app.route("/")
def index():
    """Serve the Trader Application frontend."""
    return render_template("index.html")


@app.route("/api/process", methods=["POST"])
def process_orders():
    """
    Main pipeline endpoint.

    Expects: multipart/form-data with a file field named 'orders_file'.
    Returns: JSON with:
        - orders:    list of dicts (parsed orders.csv rows)
        - reports:   list of dicts (parsed execution_rep.csv rows)
        - engine_log: stdout/stderr from the C++ binary
        - elapsed_ms: processing time reported by the engine
    """
    # 1. Validate uploaded file ────────────────────────────────────────────────
    if "orders_file" not in request.files:
        return jsonify({"error": "No file uploaded. Field name must be 'orders_file'."}), 400

    file = request.files["orders_file"]
    if file.filename == "":
        return jsonify({"error": "Empty filename received."}), 400

    filename = secure_filename(file.filename)
    if not filename.lower().endswith(".csv"):
        return jsonify({"error": "Only .csv files are accepted."}), 400

    # 2. Save the uploaded file ────────────────────────────────────────────────
    file.save(str(INPUT_FILE))

    # 3. Parse orders for the pre-execution preview ────────────────────────────
    orders_data = _parse_csv(INPUT_FILE)

    # 4. Check the C++ binary exists ───────────────────────────────────────────
    if not CPP_BINARY.exists():
        return jsonify({
            "error": (
                f"C++ binary not found at '{CPP_BINARY}'. "
                "Please compile the project first (see README)."
            ),
            "orders": orders_data,
            "reports": [],
            "engine_log": "",
            "elapsed_ms": 0,
        }), 500

    # 5. Execute the C++ matching engine ───────────────────────────────────────
    try:
        result = subprocess.run(
            [str(CPP_BINARY), str(INPUT_FILE), str(OUTPUT_FILE)],
            capture_output=True,
            text=True,
            timeout=60,         # 60-second timeout for very large files
        )
        engine_log = (result.stdout + result.stderr).strip()

        if result.returncode != 0:
            return jsonify({
                "error": f"C++ engine exited with code {result.returncode}.",
                "orders": orders_data,
                "reports": [],
                "engine_log": engine_log,
                "elapsed_ms": 0,
            }), 500

    except subprocess.TimeoutExpired:
        return jsonify({"error": "C++ engine timed out (> 60s)."}), 500
    except Exception as exc:
        return jsonify({"error": f"Failed to run engine: {exc}"}), 500

    # 6. Parse the generated execution report ──────────────────────────────────
    if not OUTPUT_FILE.exists():
        return jsonify({
            "error": "Engine ran but execution_rep.csv was not produced.",
            "orders": orders_data,
            "reports": [],
            "engine_log": engine_log,
            "elapsed_ms": 0,
        }), 500

    reports_data = _parse_csv(OUTPUT_FILE)

    # Extract elapsed time from engine stdout if present
    elapsed_ms = _extract_elapsed(engine_log)

    return jsonify({
        "orders":      orders_data,
        "reports":     reports_data,
        "engine_log":  engine_log,
        "elapsed_ms":  elapsed_ms,
    })


@app.route("/api/download", methods=["GET"])
def download_report():
    """Serve the most recently generated execution_rep.csv for download."""
    if not OUTPUT_FILE.exists():
        return jsonify({"error": "No execution report available yet."}), 404
    return send_from_directory(
        str(UPLOAD_FOLDER),
        "execution_rep.csv",
        as_attachment=True,
        download_name="execution_rep.csv",
        mimetype="text/csv",
    )


@app.route("/api/status", methods=["GET"])
def status():
    """Health-check endpoint."""
    return jsonify({
        "status":        "online",
        "binary_found":  CPP_BINARY.exists(),
        "binary_path":   str(CPP_BINARY),
    })


# ── Helpers ───────────────────────────────────────────────────────────────────

def _parse_csv(filepath: Path) -> list[dict]:
    """
    Read a CSV file and return a list of row dicts.
    Strips BOM characters and whitespace from all values.
    """
    rows = []
    try:
        with open(filepath, newline="", encoding="utf-8-sig") as f:
            reader = csv.DictReader(f)
            for row in reader:
                # Strip whitespace from keys and values
                clean = {k.strip(): v.strip() for k, v in row.items() if k}
                rows.append(clean)
    except Exception as exc:
        app.logger.error("CSV parse error for %s: %s", filepath, exc)
    return rows


def _extract_elapsed(log: str) -> float:
    """
    Parse the elapsed time (in ms) from the C++ engine's stdout.
    Looks for a line like: '[Exchange] Completed in 2.34 ms'
    """
    for line in log.splitlines():
        if "Completed in" in line and "ms" in line:
            parts = line.split()
            for i, p in enumerate(parts):
                if p == "ms" and i > 0:
                    try:
                        return float(parts[i - 1])
                    except ValueError:
                        pass
    return 0.0


# ── Entry Point ───────────────────────────────────────────────────────────────

if __name__ == "__main__":
    print("=" * 60)
    print("  Flower Exchange — Trader Application Server")
    print("=" * 60)
    print(f"  C++ Binary : {CPP_BINARY}")
    print(f"  Binary exists: {CPP_BINARY.exists()}")
    print(f"  Data dir   : {UPLOAD_FOLDER}")
    print(f"  Open       : http://127.0.0.1:5000")
    print("=" * 60)
    app.run(debug=True, host="0.0.0.0", port=5000)
