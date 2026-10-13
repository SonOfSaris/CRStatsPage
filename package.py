# Builds dist/release/StatsPage.zip (entries under crmods/StatsCompletionTracker/) from out/statscompletiontracker.dll and the package
# files in this folder. usage: python package.py
import hashlib, os, zipfile
src = os.path.dirname(os.path.abspath(__file__))
dst = os.path.join(src, '..', 'dist', 'release', 'StatsPage.zip')
files = [('out/statscompletiontracker.dll', 'statscompletiontracker.dll'), ('statscompletiontracker_config.ini',) * 2, ('statscompletiontracker.menu.json',) * 2,
         ('README-StatsCompletionTracker.txt',) * 2]
with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for f, name in files: z.write(os.path.join(src, f), 'crmods/StatsCompletionTracker/' + name)
data = open(dst, 'rb').read()
print(os.path.normpath(dst), len(data), 'bytes sha256', hashlib.sha256(data).hexdigest()[:16])
for i in zipfile.ZipFile(dst).infolist(): print('  ', i.filename, i.file_size)
