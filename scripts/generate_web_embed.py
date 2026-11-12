Import("env")
import os
import sys

project_dir = env.get("PROJECT_DIR", ".")
scripts_dir = os.path.join(project_dir, "scripts")
if scripts_dir not in sys.path:
    sys.path.insert(0, scripts_dir)

import web_packer
web_packer.ensure_embedded_web_archive(project_dir, env=env)
