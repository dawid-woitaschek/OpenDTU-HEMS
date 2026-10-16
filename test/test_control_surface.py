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

# Disabled consumers must not retain hidden runtime-profile ownership. Storage
# already had this behavior; keep EVSE symmetric and never expose the historical
# hard-coded WARP telemetry fallback as a selectable product mode.
evse_store = (root / 'src/hems/evse/SettingsStore.cpp').read_text()
devices_ui = (root / 'webapp/src/views/HemsDevicesView.vue').read_text()
assert 'return enabled ? _config : BindingConfig {};' in evse_store
assert 'effectiveConfig(candidate)' in evse_store
assert 'if (enabled && !config.Profile[0]) { return false; }' in evse_store
assert "this.evseBinding.profile = '';" in devices_ui
assert "this.evseBinding.control_mode = 'monitor';" in devices_ui
assert 'WARP (legacy)' not in devices_ui
assert 'if (!this.evseBinding.profile)' in devices_ui

# Enable/disable must durably discard the old EVSE permission before changing the endpoint state.
config_handler = api[api.index('void WebApiHemsClass::onConfigPost('):api.index('void WebApiHemsClass::onCommand(')]
assert 'warpConfig.Enabled != Configuration.get().Hems.Warp.Enabled' in config_handler
assert config_handler.index('BindingSettings.clear()') < config_handler.index('config.Hems.Warp = warpConfig')

print('Semantic-only admin API, removed authorization dependencies and symmetric EVSE binding passed')
