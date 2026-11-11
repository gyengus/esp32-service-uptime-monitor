Import("env")
import os
import sys

project_dir = env.get("PROJECT_DIR", ".")
scripts_dir = os.path.join(project_dir, "scripts")
if scripts_dir not in sys.path:
    sys.path.insert(0, scripts_dir)

import openapi_generator

openapi_generator.ensure_openapi_spec(project_dir)
