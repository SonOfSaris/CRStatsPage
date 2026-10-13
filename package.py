# Builds dist/release/StatsPage.zip (entries under crmods/StatsPage/) from out/statspage.dll and the package
# files in this folder. usage: python package.py
import hashlib, os, zipfile
src = os.path.dirname(os.path.abspath(__file__))
dst = os.path.join(src, '..', 'dist', 'release', 'StatsPage.zip')
files = [('out/statspage.dll', 'statspage.dll'), ('statspage_config.ini',) * 2, ('statspage.menu.json',) * 2, ('README-StatsPage.txt',) * 2]
with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for f, name in files: z.write(os.path.join(src, f), 'crmods/StatsPage/' + name)
data = open(dst, 'rb').read()
print(os.path.normpath(dst), len(data), 'bytes sha256', hashlib.sha256(data).hexdigest()[:16])
for i in zipfile.ZipFile(dst).infolist(): print('  ', i.filename, i.file_size)
