import shutil
from pathlib import Path


def pytest_runtest_setup(item):
    """Remove a test's ``output/`` build directory before each run so the host
    unit tests always regenerate their extracted headers from the current
    sources rather than reusing stale artifacts."""
    output_dir = Path(item.fspath).parent / "output"
    if output_dir.exists():
        shutil.rmtree(output_dir)
