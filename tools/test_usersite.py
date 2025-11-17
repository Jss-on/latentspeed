#!/usr/bin/env python3
import site
import sys

print("Python executable:", sys.executable)
print("User site:", site.USER_SITE)
print("User site enabled:", site.ENABLE_USER_SITE)
print("User site exists:", __import__('os').path.exists(site.USER_SITE))
