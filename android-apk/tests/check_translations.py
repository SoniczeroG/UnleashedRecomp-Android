"""Validate Android locale coverage, array shape and indexed format arguments."""
from collections import Counter
from pathlib import Path
import re
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
resources = root / 'android-apk/app/src/main/res'


def read(path):
    nodes = list(ET.parse(path).getroot())
    names = [node.attrib['name'] for node in nodes]
    assert len(names) == len(set(names)), f'{path}: duplicate names'
    return {node.attrib['name']: node for node in nodes}


def arguments(node):
    return Counter(re.findall(r'%[0-9]+\$[sdf]', ''.join(node.itertext())))


reference = read(resources / 'values/strings.xml')
paths = sorted(resources.glob('values-*/strings.xml'))
for path in paths:
    actual = read(path)
    assert actual.keys() == reference.keys(), f'{path}: missing {reference.keys() - actual.keys()}, extra {actual.keys() - reference.keys()}'
    for key, expected in reference.items():
        translated = actual[key]
        assert translated.tag == expected.tag, f'{path}: type mismatch for {key}'
        assert ''.join(translated.itertext()).strip(), f'{path}: empty {key}'
        assert arguments(translated) == arguments(expected), f'{path}: format arguments differ for {key}'
        if expected.tag == 'string-array':
            assert len(translated) == len(expected), f'{path}: array length differs for {key}'
            for original, localized in zip(expected, translated):
                assert arguments(original) == arguments(localized), f'{path}: array item arguments differ for {key}'

# Keys passed through the Java-only helpers and the native JNI bridge must exist.
java = root / 'android-apk/app/src/main/java/org/libsdl/app'
native = root / 'UnleashedRecomp'
for path in list(java.glob('*.java')) + [native / 'main.cpp', native / 'os/android/storage_android.cpp']:
    source = path.read_text(encoding='utf-8')
    keys = re.findall(r'(?:new LocalizedIOException\(|LocaliseMessage\(|fail\(|errorKey\s*=\s*)"([a-z_]+)"', source)
    for key in keys:
        assert key in reference, f'{path}: unknown localized error {key}'

print(f'PASS: {len(paths) + 1} locales, {len(reference)} resource entries each; no missing strings, duplicate keys, array or format mismatches.')
