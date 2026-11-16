import os
import glob
import re
import shutil
import subprocess
import sys

def run_native_tests(project_dir):
    pio_cmd = shutil.which("pio")
    if pio_cmd:
        cmd = [pio_cmd, "test", "-e", "native"]
    else:
        cmd = [sys.executable, "-m", "platformio", "test", "-e", "native"]
    return subprocess.run(cmd, cwd=project_dir)

def generate_coverage_report(project_dir=None):
    if not project_dir:
        project_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

    build_dir = os.path.join(project_dir, ".pio", "build", "native")
    if not os.path.isdir(build_dir):
        print("[COVERAGE] .pio/build/native not found. Running tests first: pio test -e native...")
        res = run_native_tests(project_dir)
        if res.returncode != 0:
            print("[COVERAGE] Unit tests failed. Cannot generate coverage report.")
            return False

    gcda_files = glob.glob(os.path.join(build_dir, "**", "*.gcda"), recursive=True)
    if not gcda_files:
        print("[COVERAGE] No .gcda coverage data found. Running tests first: pio test -e native...")
        res = run_native_tests(project_dir)
        if res.returncode != 0:
            print("[COVERAGE] Unit tests failed. Cannot generate coverage report.")
            return False
        gcda_files = glob.glob(os.path.join(build_dir, "**", "*.gcda"), recursive=True)
        if not gcda_files:
            print("[COVERAGE] Still no .gcda files found after running tests.")
            return False

    src_files = []
    # Collect application source files to report on
    for pattern in ["src/*.cpp", "lib/NetworkContext/src/*.cpp"]:
        src_files.extend(glob.glob(os.path.join(project_dir, pattern)))

    # Filter out main.cpp (firmware entry point, not linked in unit test) and auto-generated embed archive
    src_files = [
        f for f in sorted(src_files)
        if not f.endswith("main.cpp") and not f.endswith("EmbeddedWebArchive.cpp")
    ]

    results = []
    total_exec = 0
    total_lines = 0

    lcov_entries = []

    for sf in src_files:
        base = os.path.basename(sf)
        obj_gcda = None
        for g in gcda_files:
            if os.path.basename(g) == base.replace(".cpp", ".gcda"):
                obj_gcda = g
                break

        if not obj_gcda:
            continue

        obj_dir = os.path.dirname(obj_gcda)
        res = subprocess.run(
            ["gcov", "-o", obj_dir, sf],
            cwd=project_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )

        rel_path = os.path.relpath(sf, project_dir)
        m = re.search(r"File '" + re.escape(rel_path) + r"'\s+Lines executed:([\d\.]+)% of (\d+)", res.stdout)
        if m:
            pct = float(m.group(1))
            lines = int(m.group(2))
            exec_lines = int(round(pct * lines / 100.0))
            total_exec += exec_lines
            total_lines += lines
            results.append((rel_path, exec_lines, lines, pct))

    # Clean up stray .gcov files created in project root by gcov
    for gcov_file in glob.glob(os.path.join(project_dir, "*.gcov")):
        try:
            os.remove(gcov_file)
        except Exception:
            pass

    if not results:
        print("[COVERAGE] No coverage results could be parsed.")
        return False

    # Print summary table
    print("\n" + "=" * 65)
    print(f"{'Source File':<42} {'Lines':>8} {'Covered':>9} {'Coverage':>8}")
    print("-" * 65)
    for path, exec_l, total_l, pct in results:
        print(f"{path:<42} {total_l:>8} {exec_l:>9} {pct:>7.1f}%")
    print("=" * 65)
    overall_pct = (total_exec * 100.0 / total_lines) if total_lines > 0 else 0.0
    print(f"{'OVERALL TOTAL':<42} {total_lines:>8} {total_exec:>9} {overall_pct:>7.1f}%")
    print("=" * 65 + "\n")

    report_file = os.path.join(project_dir, "coverage.txt")
    try:
        with open(report_file, "w", encoding="utf-8") as f:
            f.write("=" * 65 + "\n")
            f.write(f"{'Source File':<42} {'Lines':>8} {'Covered':>9} {'Coverage':>8}\n")
            f.write("-" * 65 + "\n")
            for path, exec_l, total_l, pct in results:
                f.write(f"{path:<42} {total_l:>8} {exec_l:>9} {pct:>7.1f}%\n")
            f.write("=" * 65 + "\n")
            f.write(f"{'OVERALL TOTAL':<42} {total_lines:>8} {total_exec:>9} {overall_pct:>7.1f}%\n")
            f.write("=" * 65 + "\n")
    except Exception:
        pass

    return True

try:
    Import("env")
    def coverage_action(source, target, env):
        project_dir = env.get("PROJECT_DIR", ".")
        generate_coverage_report(project_dir)

    env.AddCustomTarget(
        name="coverage",
        dependencies=[],
        actions=[coverage_action],
        title="Generate Coverage Report",
        description="Run native tests and display code coverage report"
    )
except Exception:
    pass

if __name__ == "__main__":
    generate_coverage_report()
