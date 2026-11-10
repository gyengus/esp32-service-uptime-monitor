import subprocess
import datetime

commit_hash = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).decode('ascii').strip()
build_date = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

Import("env")
env.Append(CPPDEFINES=[
    ("COMMIT_HASH", f'\\"{commit_hash}\\"'),
    ("BUILD_TIMESTAMP", f'\\"{build_date}\\"')
])
