"""Shared fixtures and markers for system tests."""

import platform
import pytest
from tests.system.harness import find_client_bin

# ---------------------------------------------------------------------------
# Platform markers
# ---------------------------------------------------------------------------

# Apply @pytest.mark.linux_only / windows_only / posix_only to any test that
# only makes sense on a specific OS.  The test is automatically skipped on
# non-matching platforms.

def pytest_configure(config):
    config.addinivalue_line("markers", "linux_only: run only on Linux")
    config.addinivalue_line("markers", "windows_only: run only on Windows")
    config.addinivalue_line("markers", "posix_only: run only on POSIX (Linux/macOS)")
    config.addinivalue_line("markers", "requires_binary: skip if mag_client not built")


def pytest_runtest_setup(item):
    for mark in item.iter_markers("linux_only"):
        if platform.system() != "Linux":
            pytest.skip("Linux only")
    for mark in item.iter_markers("windows_only"):
        if platform.system() != "Windows":
            pytest.skip("Windows only")
    for mark in item.iter_markers("posix_only"):
        if platform.system() == "Windows":
            pytest.skip("POSIX only")
    for mark in item.iter_markers("requires_binary"):
        if find_client_bin() is None:
            pytest.skip("mag_client binary not built (run: make build)")
