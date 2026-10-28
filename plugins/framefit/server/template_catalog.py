"""Local image catalogs and stable project-aware browsing order."""
from datetime import datetime
import os
from pathlib import Path

from PIL import Image

EXTENSIONS={'.png','.jpg','.jpeg','.bmp','.webp'}


def identity(path):
    return os.path.normcase(str(Path(path).resolve()))


def inspect_image(path):
    path=Path(path)
    with Image.open(path) as image:
        width,height=image.size
    modified=path.stat().st_mtime
    if __package__:
        from .template_versions import _date_evidence
    else:
        from template_versions import _date_evidence
    return dict(path=str(path),name=path.name,width=width,height=height,modifiedAt=modified,
                nameDate=_date_evidence(str(path),datetime.now().year),
                modifiedDate=datetime.fromtimestamp(modified).strftime('%Y-%m-%d %H:%M'))


def scan_folder(folder,recursive=True):
    folder=Path(folder).expanduser()
    if not folder.is_dir():
        raise ValueError('套版目录不存在或当前无法访问。')
    files=[]
    for current,dirs,names in os.walk(folder):
        dirs[:]=sorted(name for name in dirs if not (Path(current)/name).is_symlink()) if recursive else []
        for name in names:
            path=Path(current)/name
            if path.suffix.lower() in EXTENSIONS:
                try:
                    files.append(inspect_image(path))
                except (OSError,ValueError):
                    continue
    return files


def sort_catalog(items,order='最新日期',previous=(),selected_at=0):
    if __package__:
        from .framefit_workflow import template_language
    else:
        from framefit_workflow import template_language
    previous={identity(path) for path in previous}
    unique={identity(item['path']):dict(item) for item in items}
    for key,item in unique.items():
        item['previouslySelected']=key in previous
        item['newVersion']=bool(selected_at and key not in previous and item.get('modifiedAt',0)>selected_at)
    def key(item):
        priority=0 if item['newVersion'] else 1 if item['previouslySelected'] else 2
        latest=(-int((item.get('nameDate') or '0000-00-00').replace('-','')),
                -item.get('modifiedAt',0),item.get('name','').casefold())
        if order=='名称': detail=(item.get('name','').casefold(),)
        elif order=='尺寸':detail=(item.get('width',0)*item.get('height',0),item.get('width',0),*latest)
        elif order=='语言':detail=(template_language(item['path']) or '',*latest)
        else:detail=latest
        return (priority,*detail)
    return sorted(unique.values(),key=key)
