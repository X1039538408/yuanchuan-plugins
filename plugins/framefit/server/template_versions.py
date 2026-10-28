"""Compare template dates without discarding distinct layouts or languages."""
from datetime import date, datetime
from pathlib import Path
import re

DATE = re.compile(r'(?<!\d)(20\d{2})[年._-]?(0?[1-9]|1[0-2])[月._-]?(0?[1-9]|[12]\d|3[01])日?(?!\d)')
MONTH_DAY = re.compile(r'(?<!\d)(0?[1-9]|1[0-2])[月.](0?[1-9]|[12]\d|3[01])日?(?!\d)')
VERSION = re.compile(r'(?:^|[\s._-])v\d+(?=$|[\s._-])',re.I)


def _date_evidence(value,year):
    matches=[]
    for match in DATE.finditer(value):
        try:
            matches.append(date(*map(int,match.groups())).isoformat())
        except ValueError:
            pass
    if not matches:
        for match in MONTH_DAY.finditer(value):
            try:
                matches.append(date(year,*map(int,match.groups())).isoformat())
            except ValueError:
                pass
    return max(matches,default='')


def rank_versions(files,year=None):
    year=year or date.today().year
    groups={}
    for source in files:
        item=dict(source)
        path=Path(item['path'])
        relative=path
        if item.get('templateRoot'):
            try:
                relative=path.relative_to(item['templateRoot'])
            except ValueError:
                pass
        item['nameDate']=_date_evidence(str(relative),year)
        item['modifiedDate']=datetime.fromtimestamp(item.get('modifiedAt',0)).strftime('%Y-%m-%d %H:%M')
        # Date-only folders are versions. Named folders remain separate styles.
        family=MONTH_DAY.sub('',DATE.sub('',str(relative.with_suffix(''))))
        family=VERSION.sub('',family)
        family=re.sub(r'[\W_]+','',family.casefold())
        key=(family,item.get('width'),item.get('height'))
        groups.setdefault(key,[]).append(item)
    chosen,conflicts=[],[]
    for group in groups.values():
        if len(group)==1:
            chosen.extend(group)
            continue
        time_latest=max(group,key=lambda item:item.get('modifiedAt',0))
        dated=[item for item in group if item['nameDate']]
        named_latest=max(dated,key=lambda item:item['nameDate']) if dated else None
        if named_latest and named_latest['path']!=time_latest['path'] and named_latest['nameDate']>time_latest['nameDate']:
            paths=[item['path'] for item in group]
            conflicts.append(paths)
            for item in group:
                item['dateConflict']=True
            chosen.extend(group)
        else:
            winner=dict(time_latest)
            winner['olderVersions']=[item['path'] for item in group if item['path']!=winner['path']]
            chosen.append(winner)
    return {'candidates':sorted(chosen,key=lambda item:(item['name'],item['path'])), 'conflicts':conflicts}
