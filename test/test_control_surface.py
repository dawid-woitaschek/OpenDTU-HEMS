#!/usr/bin/env python3
"""Guard the public semantic-only surface and removal of obsolete authorization dependencies."""
from pathlib import Path
import re
root = Path(__file__).resolve().parent.parent
paths = list((root / 'include/hems').rglob('*.h')) + list((root / 'src/hems').rglob('*.cpp'))
paths += [root / 'src/WebApi_hems.cpp', root / 'webapp/src/components/HemsControlPanel.vue', root / 'webapp/src/views/HemsAdminView.vue']
for path in paths:
    text = path.read_text()
    for obsolete in ('Validation::', 'getValidation', 'validation_session_inactive', 'recovery_automatic_unsupported', 'prepareOwnership', 'independently_released'):
        assert obsolete not in text, (path, obsolete)
api = (root / 'src/WebApi_hems.cpp').read_text()
assert 'server.on("/api/hems/command", HTTP_POST' in api
for path in (root / 'src').glob('WebApi*.cpp'):
    for route in re.findall(r'server.on\("(/api/hems[^\"]*)", HTTP_POST', path.read_text()):
        assert not any(word in route.lower() for word in ('register', 'raw', 'validation', 'recovery')), route
handler = api[api.index('void WebApiHemsClass::onCommand('):]
assert handler.index('checkCredentials(request)') < handler.index('parseRequestData(')
assert 'submitManual(command, id)' in handler
assert not re.search(r'data\["(address|register|registers|payload)"\]', handler)
print('Semantic-only admin API and removed authorization dependencies passed')
