"""Keep portable JSX self-contained while maintaining its C# worker as editable source."""
from pathlib import Path
import argparse,json
ROOT=Path(__file__).resolve().parent
JSX=ROOT.parents[1]/'zexp_image_split_portable.jsx'
CS=ROOT/'worker.cs'
parser=argparse.ArgumentParser();parser.add_argument('--extract',action='store_true');parser.add_argument('--check',action='store_true');args=parser.parse_args()
s=JSX.read_text(encoding='utf-8-sig')
start=s.index('        var WORKER = [');end=s.index('        ].join("\\n");',start)+len('        ].join("\\n");')
block=s[start:end].split('[',1)[1].rsplit('].join',1)[0]
worker='\n'.join(json.loads(line.strip().rstrip(',')) for line in block.splitlines() if line.strip())
prefix,rest=worker.split("$code = @'\n",1);oldcode,suffix=rest.split("\n'@",1)
if args.extract:
    if CS.exists():raise RuntimeError('Refusing to overwrite existing canonical C#')
    CS.write_text(oldcode+'\n',encoding='utf-8')
    print('Extracted',CS)
else:
    code=CS.read_text(encoding='utf-8-sig').rstrip('\n')
    for helper_name in ('glyph_templates.cs',):
        helper=ROOT/helper_name
        if helper.exists():
            helper_code=helper.read_text(encoding='utf-8-sig').rstrip('\n')
            helper_code='\n'.join(line for line in helper_code.splitlines() if not line.startswith('using '))
            code+='\n'+helper_code
    regenerated=prefix+"$code = @'\n"+code+"\n'@"+suffix
    lines=regenerated.splitlines()
    replacement='        var WORKER = [\n'+',\n'.join('            '+json.dumps(line,ensure_ascii=False) for line in lines)+'\n        ].join("\\n");'
    result=s[:start]+replacement+s[end:]
    native=ROOT/'native_text.jsxinc'
    if native.exists():
        begin_native='    // BEGIN_NATIVE_TEXT\n';end_native='    // END_NATIVE_TEXT'
        ns=result.index(begin_native);ne=result.index(end_native,ns)+len(end_native)
        result=result[:ns]+begin_native+native.read_text(encoding='utf-8-sig').rstrip('\n')+'\n'+end_native+result[ne:]
    runtime=ROOT/'host_runtime.jsxinc'
    if runtime.exists():
        begin='        // BEGIN_BATCH_HOST\n';finish='    // END_BATCH_HOST'
        if begin in result:
            host_start=result.index(begin);host_end=result.index(finish,host_start)+len(finish)
        else:
            host_start=result.index('        var foot=src.source,orig=')
            host_end=result.index('\n\n    } catch (fatal)',host_start)
        result=result[:host_start]+begin+runtime.read_text(encoding='utf-8-sig').rstrip('\n')+'\n'+finish+result[host_end:]
    if args.check:
        assert result==s,'Embedded worker/host differs from canonical sources'
        print('PASS canonical and embedded C#/native text/batch host match')
    else:
        JSX.write_text(result,encoding='utf-8')
        print('Rebuilt',JSX)
