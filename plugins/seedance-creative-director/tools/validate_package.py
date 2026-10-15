"""Validate portable package invariants, not language-model behavior."""
import json
import re
import sys
from pathlib import Path
from xml.etree import ElementTree

import yaml


def validate(root):
    root = root.resolve()
    errors = []
    checks = []

    def check(condition, message):
        (checks if condition else errors).append(message)

    manifest = json.loads((root / 'plugin.json').read_text(encoding='utf-8'))
    check(manifest['name'] == root.name, 'manifest matches package directory')
    check(bool(re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', manifest['name'])), 'valid package name')
    check(len(manifest['name']) <= 64, 'package name length')
    check(bool(re.fullmatch(r'\d+\.\d+\.\d+', manifest['version'])), 'semantic release version')
    check(not ({'skills', 'mcpServers', 'apps', 'interface'} & manifest.keys()), 'portable manifest discovery fields')
    interface = manifest['extensions']['com.openai']['interface']
    check(len(interface['shortDescription']) <= 30, 'listing subtitle length')
    prompts = interface['defaultPrompt']
    check(isinstance(prompts, str) or (isinstance(prompts, list) and 1 <= len(prompts) <= 3 and all(isinstance(p, str) for p in prompts)), 'default prompt contract')
    for name in ('logo', 'composerIcon'):
        icon = (root / interface[name]).resolve()
        check(icon.is_relative_to(root) and icon.is_file(), f'{name} exists inside package')
        svg = ElementTree.parse(icon).getroot()
        check(int(svg.attrib['width']) >= 48 and int(svg.attrib['height']) >= 48, f'{name} minimum dimensions')
        check(icon.stat().st_size <= 5 * 1024 * 1024, f'{name} file size')

    skills = list((root / 'skills').glob('*/SKILL.md'))
    check(len(skills) == 1, 'single focused skill')
    for skill in skills:
        text = skill.read_text(encoding='utf-8')
        front = re.match(r'^---\n(.*?)\n---', text, re.S)
        check(front is not None, 'skill frontmatter exists')
        if not front:
            continue
        meta = yaml.safe_load(front.group(1))
        check(meta['name'] == skill.parent.name, 'skill name matches directory')
        check(bool(meta.get('description')), 'skill trigger description exists')
        ui = yaml.safe_load((skill.parent / 'agents/openai.yaml').read_text(encoding='utf-8'))
        check('$' + meta['name'] in ui['interface']['default_prompt'], 'skill default invocation')
        check(25 <= len(ui['interface']['short_description']) <= 64, 'skill subtitle length')
        registry = (skill.parent / 'references/modules.md').read_text(encoding='utf-8')
        ids = []
        for line in registry.splitlines():
            if re.match(r'^\| [a-z][a-z-]* \|', line):
                parts = [part.strip() for part in line.strip('|').split('|')]
                ids.append(parts[0])
                check(parts[2] in ('必读', '条件', '停用'), 'module state: ' + parts[0])
        check(len(ids) == len(set(ids)) and len(ids) >= 10, 'unique registered modules')

    for path in root.rglob('*'):
        check(not path.is_symlink(), 'no symlink: ' + str(path.relative_to(root)))
        if not path.is_file():
            continue
        check(path.name not in ('.env', '.DS_Store') and '__pycache__' not in path.parts, 'no unrelated runtime artifacts: ' + path.name)
        if path.suffix not in ('.md', '.json', '.yaml', '.svg', '.py'):
            continue
        text = path.read_text(encoding='utf-8')
        check('\ufffd' not in text, 'valid text: ' + path.name)
        if path.suffix != '.md':
            continue
        check(not re.search(r'(?i)\bTODO\b|\bTBD\b', text), 'no unfinished scaffold: ' + path.name)
        for target in re.findall(r'\[[^\]\n]+\]\(([^)]+)\)', text):
            if re.match(r'^[a-z]+://', target) or target.startswith('#'):
                continue
            dest = (path.parent / target.split('#')[0]).resolve()
            check(dest.is_relative_to(root) and dest.is_file(), f'local link {path.name}: {target}')
        check(sum(1 for line in text.splitlines() if line.startswith('```')) % 2 == 0, 'balanced code fences: ' + path.name)

    check((root / 'LICENSE').is_file(), 'upstream license preserved')
    provenance = json.loads((root / 'source-provenance.json').read_text(encoding='utf-8'))
    check(len(provenance['user_sources']) == 6, 'six source fingerprints')
    check(all(re.fullmatch(r'[0-9a-f]{64}', item['sha256']) for item in provenance['user_sources']), 'source hashes valid')
    return {'status': 'passed' if not errors else 'failed', 'passed_checks': len(checks), 'errors': errors, 'behavioral_evaluation': 'not_run', 'image_video_acceptance': 'not_run'}


if __name__ == '__main__':
    result = validate(Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1])
    print(json.dumps(result, ensure_ascii=False, indent=2))
    sys.exit(bool(result['errors']))
