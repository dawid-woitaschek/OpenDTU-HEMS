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
assert 'legacy WARP mapping' not in (root / 'src/hems/warp/Provider.cpp').read_text()
assert 'if (!this.evseBinding.profile)' in devices_ui

# Enable/disable must durably discard the old EVSE permission before changing the endpoint state.
config_handler = api[api.index('void WebApiHemsClass::onConfigPost('):api.index('void WebApiHemsClass::onCommand(')]
assert 'warpConfig.Enabled != Configuration.get().Hems.Warp.Enabled' in config_handler
assert config_handler.index('BindingSettings.clear()') < config_handler.index('config.Hems.Warp = warpConfig')

print('Semantic-only admin API, removed authorization dependencies and symmetric EVSE binding passed')

# Shadow planning cannot acquire ownership or enter any semantic actuator path.
for path in list((root / 'src/hems/optimizer').glob('*.cpp')) + list((root / 'include/hems/optimizer').glob('*.h')) + [root / 'src/hems/policies/StorageEconomic.cpp', root / 'include/hems/policies/StorageEconomic.h']:
    source = path.read_text()
    for forbidden in ('submitManual(', 'submitActuation(', 'submitCommand(', 'cancelPendingActuation(', 'ProfileSession', '#include <hems/actuation/', '#include <hems/modbus/'):
        assert forbidden not in source, (path, forbidden)
for path in (root / 'src/hems/energy').glob('*.cpp'):
    assert 'setInsecure(' not in path.read_text(), path
energy_api = (root / 'src/WebApi_hems_energy.cpp').read_text()
optimizer_api = energy_api[energy_api.index('void WebApiHemsClass::onOptimizer('):]
assert optimizer_api.index('checkCredentials(request)') < optimizer_api.index('ShadowOptimizer.serialize(')
print('Shadow no-write architecture, authenticated optimizer API and verified provider TLS surface passed')

# The economic runtime is exclusively a semantic storage producer. No new
# transport/register implementation or EVSE economic producer may appear here.
for path in list((root / 'src/hems/economic').glob('*.cpp')) + list((root / 'include/hems/economic').glob('*.h')):
    source = path.read_text()
    for forbidden in ('writeHoldingRegisters', 'RegisterIo', 'Modbus::', 'ControlTarget::Evse', 'setCurrent'):
        assert forbidden not in source, (path, forbidden)
controller = (root / 'src/hems/Controller.cpp').read_text()
dispatch = controller[controller.index('void Controller::applyEconomic()'):controller.index('void Controller::serializeEconomic(')]
assert 'submitActuation(decision.command, id)' in dispatch
assert '_storage.releaseEconomic()' in dispatch
assert '_warp.' not in dispatch
assert '/api/hems/optimizer/resume' in energy_api
assert optimizer_api.index('checkCredentials(request)') < optimizer_api.index('ShadowOptimizer.resume()')
print('Economic semantic-only storage boundary and authenticated resume passed')
