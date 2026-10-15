// Captures of what a test drew, saved as PNG files for run_tests.py to compare with the images a real
// Vita gives. A capture goes to TEST_OUTPUT_DIR/<test name>/<capture name>.png, and prints the line
// "capture <capture name>: <result>", which tells run_tests.py which captures the test made.
#pragma once

// Saves width x height A8B8G8R8 pixels, rows stride pixels apart, as the capture name. Prints and
// returns 0, or the error of the write.
int test_capture(const char *name, const void *pixels, int width, int height, int stride);

// Saves what the display shows, its framebuffer from sceDisplayGetFrameBuf, as the capture name.
// Prints and returns 0, or an error.
int test_capture_display(const char *name);
