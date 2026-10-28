"""Safe output-name expressions. No eval, filesystem lookup or user code."""
import math
import re
from datetime import date as calendar_date
from pathlib import Path

TOKENS = ('原名','源文件名','项目','游戏','套版','宽','高','尺寸','比例','语言','日期','序号','扩展名')
DEFAULT_PATTERN = '[原名] [尺寸] [语言]'
TOKEN = re.compile(r'\[([^\[\]]+)\]')


def validate_pattern(pattern):
    if not isinstance(pattern,str) or not 1 <= len(pattern) <= 300:
        raise ValueError('命名规则需要 1～300 个字符。')
    def check(match):
        name=match.group(1)
        if name not in TOKENS and not re.fullmatch(r'序号:0?[1-6]',name):
            raise ValueError(f'未知命名字段：[{name}]')
        return ''
    literal=TOKEN.sub(check,pattern)
    if any(char in literal for char in '<>:"/\\|?*[]') or any(ord(c)<32 for c in literal):
        raise ValueError('命名规则只能生成文件名，不能含路径、换行或非法字符。')
    return pattern


def render_name(pattern,filename,size,language='',template='',project='',game='',date='',index=1):
    validate_pattern(pattern)
    # Import lazily so both the packaged worker and package-based tests work.
    if __package__:
        from .framefit_workflow import parse_video_name
    else:
        from framefit_workflow import parse_video_name
    info=parse_video_name(filename)
    width,height=size
    divisor=math.gcd(width,height)
    fields=dict(zip(TOKENS,(info['baseName'],Path(filename).stem,project or info['projectFolder'],game,
        Path(template).stem,str(width),str(height),f'{width}X{height}',f'{width//divisor}-{height//divisor}',
        language or '',date or calendar_date.today().isoformat(),str(index),'mp4')))
    def substitute(match):
        token=match.group(1)
        if token.startswith('序号:'):
            return str(index).zfill(int(token.split(':')[1]))
        return fields[token]
    name=re.sub(r'\s+',' ',TOKEN.sub(substitute,pattern)).strip(' ._')
    if not name or any(c in name for c in '<>:"/\\|?*') or any(ord(c)<32 for c in name):
        raise ValueError('命名结果为空或包含非法文件名字符，请修改规则。')
    return name if name.lower().endswith('.mp4') else name+'.mp4'
